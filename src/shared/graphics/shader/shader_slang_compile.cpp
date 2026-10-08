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
// Each lease owns its own Slang session, retired after a few compiles since loaded modules stay resident.
class SessionPool {
   public:
    struct Lease {
        Slang::ComPtr<slang::IGlobalSession> global;
        Slang::ComPtr<slang::ISession> session;
        int uses = 0;
    };

    Lease acquire() {
        {
            std::unique_lock<std::mutex> lock(mutex_);
            slot_free_.wait(lock, [this] { return active_ < kMaxConcurrent; });
            ++active_;
            if (!idle_.empty()) {
                Lease lease = std::move(idle_.back());
                idle_.pop_back();
                return lease;
            }
        }
        return create();
    }

    void release(Lease lease) {
        std::lock_guard<std::mutex> lock(mutex_);
        --active_;
        if (lease.session && ++lease.uses < kMaxUsesPerSession) idle_.push_back(std::move(lease));
        slot_free_.notify_one();
    }

    bool clearIfUnused() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (active_ != 0) return false;
        idle_.clear();
        return true;
    }

   private:
    static constexpr int kMaxUsesPerSession = 8;
    static constexpr int kMaxConcurrent = 4;

    static Lease create() {
        Lease lease;
        SlangGlobalSessionDesc global_desc = {};
        global_desc.enableGLSL = true;
        if (slang_createGlobalSession2(&global_desc, lease.global.writeRef()) != SLANG_OK || !lease.global) {
            core_log.error("Failed to initialize Slang global session");
            return lease;
        }

        slang::TargetDesc target_desc = {};
        target_desc.format = SLANG_SPIRV;
        target_desc.profile = lease.global->findProfile("glsl_450");

        slang::SessionDesc session_desc = {};
        session_desc.targetCount = 1;
        session_desc.targets = &target_desc;
        session_desc.allowGLSLSyntax = true;
        if (lease.global->createSession(session_desc, lease.session.writeRef()) != SLANG_OK || !lease.session) {
            core_log.error("Failed to initialize Slang session");
            lease.session = nullptr;
        }
        return lease;
    }

    std::mutex mutex_;
    std::condition_variable slot_free_;
    std::vector<Lease> idle_;
    int active_ = 0;
};

std::atomic<int64_t> g_last_use_ms{0};

int64_t now_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

void release_if_idle(int idle_seconds);

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
    const int64_t last_use = g_last_use_ms.load();
    if (last_use == 0 || now_ms() - last_use < (int64_t)idle_seconds * 1000) return;
    if (!session_pool().clearIfUnused()) return;
    // Slang allocates heavily while compiling; hand the freed pages back to the OS.
    malloc_trim(0);
    g_last_use_ms.store(0);
    core_log.info("Released the idle shader compiler");
}
}  // namespace

namespace shader_backend_internal {
bool compile_spirv_impl(SlangStage stage, const std::string& source, const char* source_name,
                        std::vector<uint32_t>& output) {
    static CompilerJanitor janitor;
    SessionPool::Lease lease = session_pool().acquire();
    struct Returner {
        SessionPool::Lease& lease;
        ~Returner() {
            g_last_use_ms.store(now_ms());
            session_pool().release(std::move(lease));
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
