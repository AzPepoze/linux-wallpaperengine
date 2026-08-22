#ifndef SHADER_COMPILER_H
#define SHADER_COMPILER_H

#include <map>
#include <string>
#include <vector>

#include "shared/core/build_config.h"
#include "shared/graphics/gfx_resource.h"
#include "sokol_gfx.h"

enum class ShaderVertexLayout {
    Sprite2D,
    ParticleSprite,
};

enum class ShaderBlendMode {
    Disabled,
    Alpha,
    Additive,
};

struct CompiledUniformBlock {
    int slot = -1;
    std::vector<std::string> uniform_names;
};

struct CompiledShader {
    GfxShader shader;
    GfxPipeline pipeline;
    ShaderVertexLayout vertex_layout = ShaderVertexLayout::Sprite2D;
    std::vector<CompiledUniformBlock> custom_uniform_blocks;
};

class ShaderCompiler {
   public:
    static CompiledShader compile(const std::string& shader_name, const std::string& vertSource,
                                  const std::string& fragSource,
                                  const std::map<std::string, std::vector<float>>& uniforms, int textureCount);
    static GfxPipeline makePipeline(sg_shader shader, ShaderVertexLayout layout, ShaderBlendMode blend_mode);
#if DEBUG_BUILD
    static std::string applyDebugMode(const std::string& fsSource, int debug_mode);
    static std::string applyDebugStep(const std::string& shader_name, const std::string& fsSource, int debug_step);
#endif
};

#endif  // SHADER_COMPILER_H
