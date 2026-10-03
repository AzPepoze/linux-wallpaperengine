#include <slang-com-ptr.h>

#include <cstring>
#include <future>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

#include "shader_backend_internal.h"
#include "shared/core/logger.h"

namespace {
struct SlangGlobalContext {
    Slang::ComPtr<slang::IGlobalSession> global_session;
    std::mutex init_mutex;

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

slang::ISession* get_thread_slang_session() {
    thread_local Slang::ComPtr<slang::ISession> thread_session;
    if (thread_session) return thread_session.get();

    slang::IGlobalSession* global = slang_global().get();
    if (!global) return nullptr;

    slang::TargetDesc target_desc = {};
    target_desc.format = SLANG_SPIRV;
    target_desc.profile = global->findProfile("glsl_450");

    slang::SessionDesc session_desc = {};
    session_desc.targetCount = 1;
    session_desc.targets = &target_desc;
    session_desc.allowGLSLSyntax = true;
    if (global->createSession(session_desc, thread_session.writeRef()) != SLANG_OK || !thread_session) {
        core_log.error("Failed to initialize thread-local Slang session");
        return nullptr;
    }
    return thread_session.get();
}

std::atomic<uint64_t> slang_module_counter{0};

void log_slang_diagnostics(const char* source_name, slang::IBlob* diagnostics) {
    if (!diagnostics || !diagnostics->getBufferPointer() || diagnostics->getBufferSize() == 0) return;
    std::string text(static_cast<const char*>(diagnostics->getBufferPointer()), diagnostics->getBufferSize());
    core_log.error("Slang diagnostics for %s: %s", source_name, text.c_str());
}

static std::mutex g_slang_compile_mutex;

}  // namespace

namespace shader_backend_internal {
bool compile_spirv_impl(SlangStage stage, const std::string& source, const char* source_name,
                        std::vector<uint32_t>& output) {
    std::lock_guard<std::mutex> compile_lock(g_slang_compile_mutex);
    try {
        slang::ISession* session = get_thread_slang_session();
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
