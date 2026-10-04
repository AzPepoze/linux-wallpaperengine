#include <malloc.h>
#include <slang-com-ptr.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <future>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "shader_backend_internal.h"
#include "shared/core/logger.h"

namespace {
struct SlangGlobalContext {
    Slang::ComPtr<slang::IGlobalSession> global_session;
    std::mutex init_mutex;

    void reset() {
        std::lock_guard<std::mutex> lock(init_mutex);
        global_session = nullptr;
    }

    slang::IGlobalSession* get() {
        std::lock_guard<std::mutex> lock(init_mutex);
        if (!global_session) {
            SlangGlobalSessionDesc global_desc = {};
            global_desc.enableGLSL = true;
            if (slang_createGlobalSession2(&global_desc, global_session.writeRef()) != SLANG_OK || !global_session) {
                core_log.error("Failed to initialize Slang global session");
                return nullptr;
            }
        }
        return global_session.get();
    }
};

SlangGlobalContext& slang_global() {
    static SlangGlobalContext ctx;
    return ctx;
}

// Slang sessions are not thread safe but are cheap to reuse from one thread at a time. Every module a session loads
// stays resident under its own name, so a session is retired after a few compiles instead of growing without bound.
class SessionPool {
   public:
    struct Lease {
        Slang::ComPtr<slang::ISession> session;
        int uses = 0;
    };

    Lease acquire() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!idle_.empty()) {
                Lease lease = std::move(idle_.back());
                idle_.pop_back();
                return lease;
            }
        }
        return create();
    }

    void release(Lease lease) {
        if (!lease.session) return;
        if (++lease.uses >= kMaxUsesPerSession) return;
        std::lock_guard<std::mutex> lock(mutex_);
        idle_.push_back(std::move(lease));
    }

    void clear() {
        std::lock_guard<std::mutex> lock(mutex_);
        idle_.clear();
    }

   private:
    static constexpr int kMaxUsesPerSession = 8;

    Lease create() {
        Lease lease;
        slang::IGlobalSession* global = slang_global().get();
        if (!global) return lease;

        slang::TargetDesc target_desc = {};
        target_desc.format = SLANG_SPIRV;
        target_desc.profile = global->findProfile("glsl_450");

        slang::SessionDesc session_desc = {};
        session_desc.targetCount = 1;
        session_desc.targets = &target_desc;
        session_desc.allowGLSLSyntax = true;
        if (global->createSession(session_desc, lease.session.writeRef()) != SLANG_OK || !lease.session) {
            core_log.error("Failed to initialize Slang session");
            lease.session = nullptr;
        }
        return lease;
    }

    std::mutex mutex_;
    std::vector<Lease> idle_;
};

std::atomic<int> g_active_compiles{0};
std::atomic<int64_t> g_last_use_ms{0};

int64_t now_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

// Slang shares mutable state between sessions (concurrent module loads trip a dictionary assertion), so only one
// compile runs at a time even though sessions are pooled.
std::mutex g_compile_mutex;

void release_if_idle(int idle_seconds);

// Frees the compiler (hundreds of MB resident) once nothing has compiled for a while. It watches the clock on its
// own thread so it works whether or not frames are being rendered.
class CompilerJanitor {
   public:
    CompilerJanitor() : thread_([this] { run(); }) {}
    ~CompilerJanitor() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            stop_ = true;
        }
        wake_.notify_all();
        thread_.join();
    }
    CompilerJanitor(const CompilerJanitor&) = delete;
    CompilerJanitor& operator=(const CompilerJanitor&) = delete;

   private:
    static constexpr int kIdleSeconds = 20;

    void run() {
        std::unique_lock<std::mutex> lock(mutex_);
        while (!stop_) {
            wake_.wait_for(lock, std::chrono::seconds(5), [this] { return stop_; });
            if (stop_) break;
            lock.unlock();
            release_if_idle(kIdleSeconds);
            lock.lock();
        }
    }

    std::mutex mutex_;
    std::condition_variable wake_;
    bool stop_ = false;
    std::thread thread_;
};

SessionPool& session_pool() {
    static SessionPool pool;
    return pool;
}

std::atomic<uint64_t> slang_module_counter{0};

void log_slang_diagnostics(const char* source_name, slang::IBlob* diagnostics) {
    if (!diagnostics || !diagnostics->getBufferPointer() || diagnostics->getBufferSize() == 0) return;
    std::string text(static_cast<const char*>(diagnostics->getBufferPointer()), diagnostics->getBufferSize());
    core_log.error("Slang diagnostics for %s: %s", source_name, text.c_str());
}

void release_if_idle(int idle_seconds) {
    std::lock_guard<std::mutex> compile_lock(g_compile_mutex);
    const int64_t last_use = g_last_use_ms.load();
    if (last_use == 0 || now_ms() - last_use < (int64_t)idle_seconds * 1000) return;
    session_pool().clear();
    slang_global().reset();
    // Slang allocates heavily while compiling; hand the freed pages back to the OS.
    malloc_trim(0);
    g_last_use_ms.store(0);
    core_log.info("Released the idle shader compiler");
}
}  // namespace

namespace shader_backend_internal {
bool compile_spirv_impl(SlangStage stage, const std::string& source, const char* source_name,
                        std::vector<uint32_t>& output) {
    std::lock_guard<std::mutex> compile_lock(g_compile_mutex);
    static CompilerJanitor janitor;
    SessionPool::Lease lease = session_pool().acquire();
    g_active_compiles.fetch_add(1);
    struct Returner {
        SessionPool::Lease& lease;
        ~Returner() {
            session_pool().release(std::move(lease));
            g_last_use_ms.store(now_ms());
            g_active_compiles.fetch_sub(1);
        }
    } returner{lease};
    try {
        slang::ISession* session = lease.session.get();
        if (!session) return false;

        const uint64_t module_id = slang_module_counter.fetch_add(1, std::memory_order_relaxed);
        const std::string module_name = "lwe_runtime_shader_" + std::to_string(module_id);
        const std::string virtual_source_name = module_name + "/" + source_name;

        Slang::ComPtr<slang::IBlob> diagnostics;
        slang::IModule* module = session->loadModuleFromSourceString(module_name.c_str(), virtual_source_name.c_str(),
                                                                     source.c_str(), diagnostics.writeRef());
        if (!module) {
            log_slang_diagnostics(source_name, diagnostics.get());
            core_log.error("Slang failed to load GLSL module for %s", source_name);
            return false;
        }

        Slang::ComPtr<slang::IEntryPoint> entry_point;
        diagnostics.setNull();
        if (module->findAndCheckEntryPoint("main", stage, entry_point.writeRef(), diagnostics.writeRef()) != SLANG_OK ||
            !entry_point) {
            log_slang_diagnostics(source_name, diagnostics.get());
            core_log.error("Slang failed to resolve main() for %s", source_name);
            return false;
        }

        slang::IComponentType* components[2] = {module, entry_point.get()};
        Slang::ComPtr<slang::IComponentType> composed_program;
        diagnostics.setNull();
        if (session->createCompositeComponentType(components, 2, composed_program.writeRef(), diagnostics.writeRef()) !=
                SLANG_OK ||
            !composed_program) {
            log_slang_diagnostics(source_name, diagnostics.get());
            core_log.error("Slang failed to compose shader program for %s", source_name);
            return false;
        }

        Slang::ComPtr<slang::IComponentType> linked_program;
        diagnostics.setNull();
        if (composed_program->link(linked_program.writeRef(), diagnostics.writeRef()) != SLANG_OK || !linked_program) {
            log_slang_diagnostics(source_name, diagnostics.get());
            core_log.error("Slang failed to link shader program for %s", source_name);
            return false;
        }

        Slang::ComPtr<slang::IBlob> code;
        diagnostics.setNull();
        if (linked_program->getEntryPointCode(0, 0, code.writeRef(), diagnostics.writeRef()) != SLANG_OK || !code) {
            log_slang_diagnostics(source_name, diagnostics.get());
            core_log.error("Slang failed to emit SPIR-V for %s", source_name);
            return false;
        }

        const size_t byte_length = code->getBufferSize();
        if (byte_length == 0 || (byte_length % sizeof(uint32_t)) != 0) {
            core_log.error("Slang produced invalid SPIR-V length for %s", source_name);
            return false;
        }

        output.resize(byte_length / sizeof(uint32_t));
        std::memcpy(output.data(), code->getBufferPointer(), byte_length);
        return true;
    } catch (const std::exception& ex) {
        core_log.error("Slang compilation threw exception for %s: %s", source_name, ex.what());
        return false;
    } catch (...) {
        core_log.error("Slang compilation threw unknown internal error for %s", source_name);
        return false;
    }
}

bool get_or_compile_spirv(SlangStage stage, const std::string& source, const char* source_name, const char* stage_str,
                          std::vector<uint32_t>& output) {
    const uint64_t hash = ShaderDiskCache::computeHash(stage, source);
    if (shader_cache().tryGet(hash, stage_str, output)) {
        return true;
    }

    if (!compile_spirv_impl(stage, source, source_name, output)) {
        return false;
    }

    shader_cache().put(hash, stage_str, output);
    return true;
}
}  // namespace shader_backend_internal
