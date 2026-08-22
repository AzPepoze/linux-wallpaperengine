#ifndef SHADER_UNIFORM_LAYOUT_H
#define SHADER_UNIFORM_LAYOUT_H

#include <map>
#include <string>
#include <vector>

#include "shader_compiler.h"
#include "sokol_gfx.h"

// Packs authored uniforms into vec4 blocks accepted by the runtime backend.
// Keeping this transformation separate makes shader compilation focus on
// describing built-ins and textures.
void configureCustomUniformBlocks(const std::map<std::string, std::vector<float>>& uniforms, std::string& vertex_source,
                                  std::string& fragment_source, sg_shader_desc& shader_desc, CompiledShader& result,
                                  int& next_uniform_slot,
                                  std::string names[SG_MAX_UNIFORMBLOCK_BINDSLOTS][SG_MAX_UNIFORMBLOCK_MEMBERS]);

#endif  // SHADER_UNIFORM_LAYOUT_H
