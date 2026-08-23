#ifndef EFFECT_GEOMETRY_H
#define EFFECT_GEOMETRY_H

#include <string>

bool effectShaderUsesClipSpaceGeometry(const std::string& vertex_source, const char* shader_name);

#endif  // EFFECT_GEOMETRY_H
