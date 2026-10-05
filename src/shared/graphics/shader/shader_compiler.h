#ifndef SHADER_COMPILER_H
#define SHADER_COMPILER_H

#include <map>
#include <string>
#include <vector>

#include "shared/graphics/gfx_resource.h"
#include "sokol_gfx.h"

enum class ShaderVertexLayout {
    Sprite2D,
    ParticleSprite,
    ParticleRope,
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

struct CompiledAudioSpectrumMember {
    std::string name;
    int count = 0;
};

struct CompiledAudioSpectrumBlock {
    int slot = -1;
    sg_shader_stage stage = SG_SHADERSTAGE_NONE;
    uint32_t size_bytes = 0;
    std::vector<CompiledAudioSpectrumMember> members;
};

struct CompiledShader {
    GfxShader shader;
    GfxPipeline pipeline;
    ShaderVertexLayout vertex_layout = ShaderVertexLayout::Sprite2D;
    std::vector<CompiledUniformBlock> custom_uniform_blocks;
    std::vector<CompiledAudioSpectrumBlock> audio_spectrum_blocks;
};

class ShaderCompiler {
   public:
    static CompiledShader compile(const std::string& shader_name, const std::string& vertSource,
                                  const std::string& fragSource,
                                  const std::map<std::string, std::vector<float>>& uniforms, int textureCount);
    static void prewarm(const std::string& shader_name, const std::string& vertSource, const std::string& fragSource,
                        const std::map<std::string, std::vector<float>>& uniforms, int textureCount);
    static GfxPipeline makePipeline(sg_shader shader, ShaderVertexLayout layout, ShaderBlendMode blend_mode);
    static std::string applyDebugMode(const std::string& fsSource, int debug_mode);
    static std::string applyDebugStep(const std::string& shader_name, const std::string& fsSource, int debug_step);

   private:
    static CompiledShader build(const std::string& shader_name, const std::string& vertSource,
                                const std::string& fragSource,
                                const std::map<std::string, std::vector<float>>& uniforms, int textureCount,
                                bool create_gpu_objects);
};

#endif  // SHADER_COMPILER_H
