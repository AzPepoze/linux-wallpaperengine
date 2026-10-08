// HLSL -> SPIR-V path for Wallpaper Engine's DX11 fallbacks; these need no source rewriting.
#include <slang-com-ptr.h>

#include <cstring>
#include <string>
#include <vector>

#include "shader_backend_internal.h"
#include "shared/core/logger.h"

using Slang::ComPtr;

namespace {
bool compile_hlsl_stage(SlangStage stage, const std::string& source, const char* source_name,
                        std::vector<uint32_t>& output) {
    SlangGlobalSessionDesc global_desc = {};
    // Slang's HLSL prelude imports `glsl`, which fails (E38201) without GLSL support.
    global_desc.enableGLSL = true;
    ComPtr<slang::IGlobalSession> global;
    if (slang_createGlobalSession2(&global_desc, global.writeRef()) != SLANG_OK || !global) {
        return false;
    }

    slang::TargetDesc target = {};
    target.format = SLANG_SPIRV;
    target.profile = global->findProfile("sm_5_0");
    if (!target.profile) return false;

    slang::SessionDesc session_desc = {};
    session_desc.targetCount = 1;
    session_desc.targets = &target;
    session_desc.allowGLSLSyntax = false;
    ComPtr<slang::ISession> session;
    if (global->createSession(session_desc, session.writeRef()) != SLANG_OK || !session) return false;

    const std::string module_name = std::string("lwe_hlsl_") + source_name;
    Slang::ComPtr<slang::IBlob> diagnostics;
    slang::IModule* module = session->loadModuleFromSourceString(
        module_name.c_str(), (module_name + "/" + source_name).c_str(), source.c_str(), diagnostics.writeRef());
    if (!module) {
        if (diagnostics && diagnostics->getBufferSize())
            core_log.error("Slang HLSL diagnostics for %s: %s", source_name,
                           (const char*)diagnostics->getBufferPointer());
        return false;
    }

    ComPtr<slang::IEntryPoint> entry_point;
    diagnostics.setNull();
    if (module->findAndCheckEntryPoint("main", stage, entry_point.writeRef(), diagnostics.writeRef()) != SLANG_OK ||
        !entry_point) {
        if (diagnostics && diagnostics->getBufferSize())
            core_log.error("Slang HLSL entry diagnostics for %s: %s", source_name,
                           (const char*)diagnostics->getBufferPointer());
        return false;
    }

    slang::IComponentType* components[2] = {module, entry_point.get()};
    ComPtr<slang::IComponentType> composed;
    diagnostics.setNull();
    if (session->createCompositeComponentType(components, 2, composed.writeRef(), diagnostics.writeRef()) != SLANG_OK ||
        !composed) {
        return false;
    }

    ComPtr<slang::IComponentType> linked;
    diagnostics.setNull();
    if (composed->link(linked.writeRef(), diagnostics.writeRef()) != SLANG_OK || !linked) {
        if (diagnostics && diagnostics->getBufferSize())
            core_log.error("Slang HLSL link diagnostics for %s: %s", source_name,
                           (const char*)diagnostics->getBufferPointer());
        return false;
    }

    ComPtr<slang::IBlob> code;
    diagnostics.setNull();
    if (linked->getEntryPointCode(0, 0, code.writeRef(), diagnostics.writeRef()) != SLANG_OK || !code) {
        if (diagnostics && diagnostics->getBufferSize())
            core_log.error("Slang HLSL codegen diagnostics for %s: %s", source_name,
                           (const char*)diagnostics->getBufferPointer());
        return false;
    }

    const size_t byte_length = code->getBufferSize();
    if (byte_length == 0 || (byte_length % sizeof(uint32_t)) != 0) return false;
    output.resize(byte_length / sizeof(uint32_t));
    std::memcpy(output.data(), code->getBufferPointer(), byte_length);
    return true;
}
}  // namespace

namespace shader_backend_internal {
bool get_or_compile_spirv_hlsl(SlangStage stage, const std::string& source, const char* source_name,
                               const char* stage_str, std::vector<uint32_t>& output) {
    const uint64_t hash = ShaderDiskCache::computeHash(stage, source);
    if (shader_cache().tryGet(hash, stage_str, output)) return true;
    if (!compile_hlsl_stage(stage, source, source_name, output)) return false;
    shader_cache().put(hash, stage_str, output);
    return true;
}
}  // namespace shader_backend_internal
