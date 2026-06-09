#ifndef SHADER_COMPILER_H
#define SHADER_COMPILER_H

#include <string>
#include <map>
#include <vector>
#include "../core/gfx_resource.h"
#include "../../libs/sokol/sokol_gfx.h"

struct CompiledShader {
    GfxShader shader;
    GfxPipeline pipeline;
};

class ShaderCompiler {
public:
    static CompiledShader compile(const std::string& shader_name, const std::string& vertSource, const std::string& fragSource, const std::map<std::string, std::vector<float>>& uniforms, int textureCount);
    static std::string applyDebugMode(const std::string& fsSource, int debug_mode);
    static std::string applyDebugStep(const std::string& fsSource, int debug_step);
};

#endif // SHADER_COMPILER_H
