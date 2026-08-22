#ifndef SHADER_UNIFORM_LAYOUT_H
#define SHADER_UNIFORM_LAYOUT_H

#include <map>
#include <string>
#include <vector>

#include "shader_compiler.h"
#include "sokol_gfx.h"

// Packs authored uniforms into the vec4 blocks the runtime backend expects.
void configureCustomUniformBlocks(const std::map<std::string, std::vector<float>>& uniforms, std::string& vertex_source,
                                  std::string& fragment_source, sg_shader_desc& shader_desc, CompiledShader& result,
                                  int& next_uniform_slot,
                                  std::string names[SG_MAX_UNIFORMBLOCK_BINDSLOTS][SG_MAX_UNIFORMBLOCK_MEMBERS]);

#endif  // SHADER_UNIFORM_LAYOUT_H
