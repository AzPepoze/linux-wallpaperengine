#ifndef SHADER_COMPILER_H
#define SHADER_COMPILER_H

#include <map>
#include <string>
#include <vector>

#include "core/gfx_resource.h"
#include "sokol_gfx.h"

struct CompiledUniformBlock {
    int slot = -1;
    std::vector<std::string> uniform_names;
};

struct CompiledShader {
    GfxShader shader;
    GfxPipeline pipeline;
    std::vector<CompiledUniformBlock> custom_uniform_blocks;
};

class ShaderCompiler {
   public:
    static CompiledShader compile(const std::string& shader_name, const std::string& vertSource,
                                  const std::string& fragSource,
                                  const std::map<std::string, std::vector<float>>& uniforms, int textureCount);
    static std::string applyDebugMode(const std::string& fsSource, int debug_mode);
    static std::string applyDebugStep(const std::string& shader_name, const std::string& fsSource, int debug_step);
};

#endif  // SHADER_COMPILER_H
