#include "parallax.h"

#include <algorithm>
#include <vector>

#include "../../libs/linmath.h"

namespace {

float clamp01(float value) {
    return std::max(0.0f, std::min(1.0f, value));
}

const scene_parallax_node_t* find_node(const EngineContext& ctx, uint32_t id) {
    if (id == 0) return nullptr;
    for (const auto& node : ctx.parallax_nodes) {
        if (node.id == id) return &node;
    }
    return nullptr;
}

void local_transform(const scene_parallax_node_t& node, mat4x4 out) {
    // OWE SceneNode::GetLocalTrans(): T * Rz * Ry * Rx * S.
    mat4x4_identity(out);
    mat4x4_translate_in_place(out, node.origin[0], node.origin[1], node.origin[2]);
    mat4x4_rotate_Z(out, out, node.angles[2]);
    mat4x4_rotate_Y(out, out, node.angles[1]);
    mat4x4_rotate_X(out, out, node.angles[0]);
    mat4x4_scale_aniso(out, out, node.scale[0], node.scale[1], node.scale[2]);
}

bool world_position(const EngineContext& ctx, const scene_parallax_node_t& node, float out[3]) {
    std::vector<const scene_parallax_node_t*> chain;
    chain.reserve(8);

    const scene_parallax_node_t* current = &node;
    for (size_t steps = 0; current && steps <= ctx.parallax_nodes.size(); ++steps) {
        chain.push_back(current);
        if (current->parent_id == 0) break;
        current = find_node(ctx, current->parent_id);
    }

    if (chain.empty()) return false;

    mat4x4 world;
    mat4x4_identity(world);
    for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
        mat4x4 local;
        local_transform(**it, local);
        mat4x4_mul(world, world, local);
    }

    out[0] = world[3][0];
    out[1] = world[3][1];
    out[2] = world[3][2];
    return true;
}

const scene_parallax_node_t* resolve_parallax_node(const EngineContext& ctx,
                                                    const scene_parallax_node_t& node) {
    // Match UniformSceneState::ResolveParallaxState(): a parent only takes over
    // when that parent explicitly allows propagation to its children. Continue
    // walking upward while each selected ancestor propagates.
    const scene_parallax_node_t* resolved = &node;
    uint32_t parent_id = node.parent_id;

    for (size_t steps = 0; parent_id != 0 && steps <= ctx.parallax_nodes.size(); ++steps) {
        const scene_parallax_node_t* candidate = find_node(ctx, parent_id);
        if (!candidate) break;
        if (!candidate->propagate_to_children) break;
        resolved = candidate;
        parent_id = candidate->parent_id;
    }

    return resolved;
}

}  // namespace

void parallax_update(EngineContext& ctx, float dt, int viewport_width, int viewport_height) {
    float target_x = 0.5f;
    float target_y = 0.5f;

    if (ctx.camera_parallax_enabled && ctx.mouse_position_valid && viewport_width > 0 && viewport_height > 0) {
        target_x = clamp01(ctx.mouse_x / (float)viewport_width);
        target_y = clamp01(ctx.mouse_y / (float)viewport_height);
    }

    // OWE's pointer-delay runtime advances the current pointer toward the input
    // by approximately frame_delta / cameraparallaxdelay every frame. Do the
    // equivalent here instead of using an exponential time constant.
    float response = 1.0f;
    if (ctx.camera_parallax_delay > 0.0f) {
        response = dt > 0.0f ? clamp01(dt / ctx.camera_parallax_delay) : 0.0f;
    }

    ctx.parallax_pointer_x += (target_x - ctx.parallax_pointer_x) * response;
    ctx.parallax_pointer_y += (target_y - ctx.parallax_pointer_y) * response;

    const parallax_position_t shader_position = parallax_shader_position(ctx);
    ctx.parallax_smooth_x = (shader_position.x - 0.5f) * 2.0f;
    ctx.parallax_smooth_y = (shader_position.y - 0.5f) * 2.0f;
}

parallax_offset_t parallax_layer_offset(const EngineContext& ctx, uint32_t scene_object_id,
                                        const float fallback_origin[3], const float fallback_depth[2]) {
    parallax_offset_t result = {};
    if (!ctx.camera_parallax_enabled || ctx.camera_parallax_amount == 0.0f) return result;
    if (ctx.scene_w <= 0.0f || ctx.scene_h <= 0.0f) return result;

    float node_position[3] = {fallback_origin[0], fallback_origin[1], fallback_origin[2]};
    const float* depth = fallback_depth;

    if (const scene_parallax_node_t* node = find_node(ctx, scene_object_id)) {
        const scene_parallax_node_t* resolved = resolve_parallax_node(ctx, *node);
        if (resolved) {
            depth = resolved->depth;
            world_position(ctx, *resolved, node_position);
        }
    }

    if (depth[0] == 0.0f && depth[1] == 0.0f) return result;

    // The default orthographic camera is attached at half the authored scene extent.
    const float camera_x = ctx.scene_w * 0.5f;
    const float camera_y = ctx.scene_h * 0.5f;

    // Exact OWE TransformUniformSource convention:
    //   pointer_offset = Scaling(1,-1) * (vec2(0.5) - pointer)
    // so X is (0.5 - px), while Y is (py - 0.5).
    const float mouse_x = (0.5f - ctx.parallax_pointer_x) * ctx.scene_w * ctx.camera_parallax_mouse_influence;
    const float mouse_y = (ctx.parallax_pointer_y - 0.5f) * ctx.scene_h * ctx.camera_parallax_mouse_influence;

    result.x = (node_position[0] - camera_x + mouse_x) * depth[0] * ctx.camera_parallax_amount;
    result.y = (node_position[1] - camera_y + mouse_y) * depth[1] * ctx.camera_parallax_amount;
    return result;
}

parallax_position_t parallax_shader_position(const EngineContext& ctx) {
    parallax_position_t result = {};
    if (!ctx.camera_parallax_enabled) return result;

    const float centered_x = ctx.parallax_pointer_x - 0.5f;
    const float centered_y = ctx.parallax_pointer_y - 0.5f;

    // g_ParallaxPosition is a normalized shader input and intentionally does not
    // include camera_parallax_amount. OWE flips Y only for this shader builtin.
    result.x = 0.5f + centered_x * ctx.camera_parallax_mouse_influence;
    result.y = 0.5f - centered_y * ctx.camera_parallax_mouse_influence;
    return result;
}