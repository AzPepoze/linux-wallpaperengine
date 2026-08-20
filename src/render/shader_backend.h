#ifndef SHADER_BACKEND_H
#define SHADER_BACKEND_H

#include <string>

#include "../../libs/sokol/sokol_gfx.h"

sg_shader create_backend_shader(sg_shader_desc* desc, const std::string& vertex_source,
                                const std::string& fragment_source, const char* label = nullptr);

#endif  // SHADER_BACKEND_H
