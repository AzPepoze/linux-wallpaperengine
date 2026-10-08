#pragma once

#include <string>

#include "sokol_gfx.h"

namespace shader_preparation {
template <typename T>
void appendValue(std::string& key, const T& value) {
    key.append(reinterpret_cast<const char*>(&value), sizeof(value));
}

inline void appendString(std::string& key, const char* value) {
    if (!value) {
        key.push_back('\0');
        return;
    }
    key.append(value);
    key.push_back('\0');
}

inline std::string preparedKey(const sg_shader_desc& desc, const std::string& vertex_source,
                               const std::string& fragment_source) {
    std::string key;
    key.reserve(vertex_source.size() + fragment_source.size() + 8192);
    key.append(vertex_source);
    key.push_back('\0');
    key.append(fragment_source);
    key.push_back('\0');
    for (int slot = 0; slot < SG_MAX_VERTEX_ATTRIBUTES; ++slot) appendString(key, desc.attrs[slot].glsl_name);
    for (int slot = 0; slot < SG_MAX_UNIFORMBLOCK_BINDSLOTS; ++slot) {
        const auto& block = desc.uniform_blocks[slot];
        appendValue(key, block.stage);
        appendValue(key, block.size);
        for (int member = 0; member < SG_MAX_UNIFORMBLOCK_MEMBERS; ++member) {
            const auto& uniform = block.glsl_uniforms[member];
            appendString(key, uniform.glsl_name);
            appendValue(key, uniform.type);
            appendValue(key, uniform.array_count);
        }
    }
    for (int slot = 0; slot < SG_MAX_VIEW_BINDSLOTS; ++slot) {
        const auto& texture = desc.views[slot].texture;
        appendValue(key, texture.stage);
        appendValue(key, texture.image_type);
    }
    for (int slot = 0; slot < SG_MAX_SAMPLER_BINDSLOTS; ++slot) {
        const auto& sampler = desc.samplers[slot];
        appendValue(key, sampler.stage);
        appendValue(key, sampler.sampler_type);
    }
    for (const auto& pair : desc.texture_sampler_pairs) {
        appendValue(key, pair.stage);
        appendString(key, pair.glsl_name);
        appendValue(key, pair.view_slot);
        appendValue(key, pair.sampler_slot);
    }
    return key;
}

}  // namespace shader_preparation
