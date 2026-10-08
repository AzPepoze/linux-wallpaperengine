#include "puppet_pose.h"

#include <math.h>

#include <algorithm>

namespace wallpaper_engine {
namespace {

PuppetMatrix multiply(const PuppetMatrix& a, const PuppetMatrix& b) {
    PuppetMatrix out;
    for (int col = 0; col < 4; ++col) {
        for (int row = 0; row < 4; ++row) {
            float sum = 0.0f;
            for (int k = 0; k < 4; ++k) sum += a.m[k * 4 + row] * b.m[col * 4 + k];
            out.m[col * 4 + row] = sum;
        }
    }
    return out;
}

PuppetMatrix inverseAffine(const PuppetMatrix& in) {
    const float* m = in.m;
    const float a = m[0], b = m[4], c = m[8], d = m[1], e = m[5], f = m[9], g = m[2], h = m[6], i = m[10];
    const float det = a * (e * i - f * h) - b * (d * i - f * g) + c * (d * h - e * g);
    PuppetMatrix out;
    if (fabsf(det) < 1e-12f) return out;
    const float inv = 1.0f / det;
    const float r[9] = {(e * i - f * h) * inv, (c * h - b * i) * inv, (b * f - c * e) * inv,
                        (f * g - d * i) * inv, (a * i - c * g) * inv, (c * d - a * f) * inv,
                        (d * h - e * g) * inv, (b * g - a * h) * inv, (a * e - b * d) * inv};
    for (int row = 0; row < 3; ++row) {
        for (int col = 0; col < 3; ++col) out.m[col * 4 + row] = r[row * 3 + col];
    }
    for (int row = 0; row < 3; ++row) {
        out.m[12 + row] = -(out.m[row] * m[12] + out.m[4 + row] * m[13] + out.m[8 + row] * m[14]);
    }
    return out;
}

PuppetMatrix composeLocal(const MdlKeyframe& pose) {
    const float cx = cosf(pose.rotation[0]), sx = sinf(pose.rotation[0]);
    const float cy = cosf(pose.rotation[1]), sy = sinf(pose.rotation[1]);
    const float cz = cosf(pose.rotation[2]), sz = sinf(pose.rotation[2]);
    // R = Rz * Ry * Rx
    const float r00 = cz * cy, r01 = cz * sy * sx - sz * cx, r02 = cz * sy * cx + sz * sx;
    const float r10 = sz * cy, r11 = sz * sy * sx + cz * cx, r12 = sz * sy * cx - cz * sx;
    const float r20 = -sy, r21 = cy * sx, r22 = cy * cx;
    PuppetMatrix out;
    const float* s = pose.scale;
    const float cols[9] = {r00 * s[0], r10 * s[0], r20 * s[0], r01 * s[1], r11 * s[1],
                           r21 * s[1], r02 * s[2], r12 * s[2], r22 * s[2]};
    for (int col = 0; col < 3; ++col) {
        for (int row = 0; row < 3; ++row) out.m[col * 4 + row] = cols[col * 3 + row];
        out.m[12 + col] = pose.translation[col];
    }
    return out;
}

PuppetMatrix bindLocal(const MdlBone& bone) {
    PuppetMatrix out;
    for (int i = 0; i < 16; ++i) out.m[i] = bone.bind_matrix[i];
    return out;
}

float wrapFrame(float frame, uint32_t frame_count, const std::string& mode) {
    const float length = (float)frame_count;
    if (mode == "single") return std::clamp(frame, 0.0f, length);
    if (mode == "mirror") {
        const float phase = fmodf(frame, length * 2.0f);
        return phase <= length ? phase : length * 2.0f - phase;
    }
    return fmodf(frame, length);
}

MdlKeyframe sample(const std::vector<MdlKeyframe>& track, float frame) {
    const size_t lower = std::min((size_t)frame, track.size() - 1);
    const size_t upper = std::min(lower + 1, track.size() - 1);
    const float t = frame - (float)lower;
    MdlKeyframe out;
    for (int i = 0; i < 3; ++i) {
        out.translation[i] =
            track[lower].translation[i] + (track[upper].translation[i] - track[lower].translation[i]) * t;
        out.rotation[i] = track[lower].rotation[i] + (track[upper].rotation[i] - track[lower].rotation[i]) * t;
        out.scale[i] = track[lower].scale[i] + (track[upper].scale[i] - track[lower].scale[i]) * t;
    }
    return out;
}

const MdlAnimationClip* findClip(const MdlModel& model, uint32_t id) {
    for (const MdlAnimationClip& clip : model.clips) {
        if (clip.id == id) return &clip;
    }
    return nullptr;
}

// Each layer adds motion relative to its own first frame, on top of the assembled pose.
void accumulateLayer(const MdlModel& model, const MdlAnimationClip& clip, const PuppetAnimationLayer& layer,
                     bool root_motion, std::vector<MdlKeyframe>& pose) {
    const float frame = wrapFrame(layer.time * clip.fps, clip.frame_count, layer.once ? "single" : clip.loop_mode);
    const size_t bones = std::min(pose.size(), clip.tracks.size());
    for (size_t b = 0; b < bones; ++b) {
        const std::vector<MdlKeyframe>& track = clip.tracks[b];
        if (track.empty()) continue;
        const MdlKeyframe now = sample(track, frame);
        const bool is_root = b < model.bones.size() && model.bones[b].parent >= model.bones.size();
        for (int i = 0; i < 3; ++i) {
            if (root_motion || !is_root)
                pose[b].translation[i] += layer.blend * (now.translation[i] - track[0].translation[i]);
            pose[b].rotation[i] += layer.blend * (now.rotation[i] - track[0].rotation[i]);
            pose[b].scale[i] += layer.blend * (now.scale[i] - track[0].scale[i]);
        }
    }
}

void solveControllers(const MdlModel& model, const std::vector<PuppetAnimationLayer>& layers,
                      std::vector<MdlKeyframe>& pose) {
    if (model.controllers.empty()) return;
    std::vector<MdlKeyframe> controls(model.controllers.size());
    std::vector<bool> available(model.controllers.size(), false);
    bool seeded = false;
    for (const auto& layer : layers) {
        const auto* clip = layer.visible ? findClip(model, layer.animation_id) : nullptr;
        if (!clip) continue;
        const float frame =
            wrapFrame(layer.time * clip->fps, clip->frame_count, layer.once ? "single" : clip->loop_mode);
        for (size_t c = 0; c < controls.size(); ++c) {
            const size_t track_index = model.bones.size() + c;
            if (track_index >= clip->tracks.size() || clip->tracks[track_index].empty()) continue;
            const auto& track = clip->tracks[track_index];
            if (!available[c]) controls[c] = track[0];
            available[c] = true;
            const auto now = sample(track, frame);
            for (int axis = 0; axis < 3; ++axis) {
                controls[c].translation[axis] += layer.blend * (now.translation[axis] - track[0].translation[axis]);
                controls[c].rotation[axis] += layer.blend * (now.rotation[axis] - track[0].rotation[axis]);
                controls[c].scale[axis] += layer.blend * (now.scale[axis] - track[0].scale[axis]);
            }
        }
        seeded = true;
    }
    if (!seeded) return;
    std::vector<PuppetMatrix> world(pose.size());
    auto update_world = [&] {
        for (size_t b = 0; b < pose.size(); ++b) {
            const auto local = composeLocal(pose[b]);
            const auto parent = model.bones[b].parent;
            world[b] = parent < b ? multiply(world[parent], local) : local;
        }
    };
    for (size_t c = 0; c < controls.size(); ++c) {
        const auto& controller = model.controllers[c];
        if (!available[c] || controller.bone_index >= pose.size()) continue;
        if (controller.pole) continue;  // Pole controls select the limb's bend direction.
        const uint32_t end = controller.bone_index;
        if (model.bones[end].ik_depth == 0 || model.bones[end].ik_depth > 2) continue;
        const uint32_t mid = model.bones[end].parent;
        if (mid >= pose.size()) continue;
        const uint32_t base = model.bones[end].ik_depth == 2 ? model.bones[mid].parent : mid;
        if (base >= pose.size()) continue;
        update_world();
        const auto reference = world[base];
        const auto target = composeLocal(controls[c]);
        const float target_angle = atan2f(target.m[1], target.m[0]);
        if (base != mid) {
            const float l1 = hypotf(pose[mid].translation[0], pose[mid].translation[1]);
            const float l2 = hypotf(pose[end].translation[0], pose[end].translation[1]);
            const float dx = target.m[12] - reference.m[12];
            const float dy = target.m[13] - reference.m[13];
            const float distance = hypotf(dx, dy);
            if (l1 > 1e-5f && l2 > 1e-5f && distance > 1e-5f) {
                float side = 1.0f;
                for (size_t pole = 0; pole < controls.size(); ++pole) {
                    if (!available[pole] || !model.controllers[pole].pole || model.controllers[pole].bone_index != end)
                        continue;
                    const auto pole_world = composeLocal(controls[pole]);
                    const float px = pole_world.m[12] - reference.m[12];
                    const float py = pole_world.m[13] - reference.m[13];
                    side = dx * py - dy * px < 0.0f ? -1.0f : 1.0f;
                    break;
                }
                const float reach = std::clamp(distance, fabsf(l1 - l2) + 1e-5f, l1 + l2);
                const float cosine = std::clamp((l1 * l1 + reach * reach - l2 * l2) / (2.0f * l1 * reach), -1.0f, 1.0f);
                const float direction = atan2f(dy, dx) + side * acosf(cosine);
                const uint32_t parent = model.bones[base].parent;
                const float parent_angle =
                    parent < world.size() ? atan2f(world[parent].m[1], world[parent].m[0]) : 0.0f;
                pose[base].rotation[2] =
                    direction - parent_angle - atan2f(pose[mid].translation[1], pose[mid].translation[0]);
                update_world();
                const float endpoint_x = reference.m[12] + dx * reach / distance;
                const float endpoint_y = reference.m[13] + dy * reach / distance;
                const float second_direction = atan2f(endpoint_y - world[mid].m[13], endpoint_x - world[mid].m[12]);
                const float base_angle = atan2f(world[base].m[1], world[base].m[0]);
                pose[mid].rotation[2] =
                    second_direction - base_angle - atan2f(pose[end].translation[1], pose[end].translation[0]);
            }
        } else {
            const float dx = target.m[12] - world[mid].m[12];
            const float dy = target.m[13] - world[mid].m[13];
            const uint32_t parent = model.bones[mid].parent;
            const float parent_angle = parent < world.size() ? atan2f(world[parent].m[1], world[parent].m[0]) : 0.0f;
            pose[mid].rotation[2] =
                atan2f(dy, dx) - parent_angle - atan2f(pose[end].translation[1], pose[end].translation[0]);
        }
        update_world();
        pose[end].rotation[2] = target_angle - atan2f(world[mid].m[1], world[mid].m[0]);
    }
}

}  // namespace

void PuppetPose::init(const MdlModel& model) {
    const size_t count = model.bones.size();
    inverse_bind_world.assign(count, PuppetMatrix{});
    skin_matrices.assign(count, PuppetMatrix{});
    std::vector<PuppetMatrix> world(count);
    for (size_t i = 0; i < count; ++i) {
        const PuppetMatrix local = bindLocal(model.bones[i]);
        const uint32_t parent = model.bones[i].parent;
        world[i] = parent < i ? multiply(world[parent], local) : local;
        inverse_bind_world[i] = inverseAffine(world[i]);
    }
}

void PuppetPose::setBoneOverride(size_t bone, const MdlKeyframe& pose) {
    if (overrides_.size() <= bone) overrides_.resize(bone + 1);
    overrides_[bone].active = true;
    overrides_[bone].pose = pose;
}

void PuppetPose::clearBoneOverride(size_t bone) {
    if (bone < overrides_.size()) overrides_[bone].active = false;
}

bool PuppetPose::localPose(const MdlModel& model, const std::vector<PuppetAnimationLayer>& layers,
                           std::vector<MdlKeyframe>& pose) const {
    pose.assign(model.bones.size(), MdlKeyframe{});
    bool seeded = false;
    for (const PuppetAnimationLayer& layer : layers) {
        const MdlAnimationClip* clip = layer.visible ? findClip(model, layer.animation_id) : nullptr;
        if (!clip) continue;
        if (!seeded) {
            for (size_t b = 0; b < std::min(pose.size(), clip->tracks.size()); ++b) {
                if (b < model.reference_pose.size())
                    pose[b] = model.reference_pose[b];
                else if (!clip->tracks[b].empty())
                    pose[b] = clip->tracks[b][0];
            }
            seeded = true;
        }
        accumulateLayer(model, *clip, layer, root_motion, pose);
    }
    if (seeded) {
        solveControllers(model, layers, pose);
        for (size_t b = 0; b < std::min(pose.size(), overrides_.size()); ++b)
            if (overrides_[b].active) pose[b] = overrides_[b].pose;
    }
    return seeded;
}

void PuppetPose::localMatrices(const MdlModel& model, const std::vector<PuppetAnimationLayer>& layers,
                               std::vector<PuppetMatrix>& out) const {
    std::vector<MdlKeyframe> pose;
    const bool seeded = localPose(model, layers, pose);
    out.assign(model.bones.size(), PuppetMatrix{});
    for (size_t i = 0; i < model.bones.size(); ++i) out[i] = seeded ? composeLocal(pose[i]) : bindLocal(model.bones[i]);
}

void PuppetPose::worldMatrices(const MdlModel& model, const std::vector<PuppetAnimationLayer>& layers,
                               std::vector<PuppetMatrix>& out) const {
    std::vector<MdlKeyframe> pose;
    const bool seeded = localPose(model, layers, pose);
    out.assign(model.bones.size(), PuppetMatrix{});
    for (size_t i = 0; i < model.bones.size(); ++i) {
        const PuppetMatrix local = seeded ? composeLocal(pose[i]) : bindLocal(model.bones[i]);
        const uint32_t parent = model.bones[i].parent;
        out[i] = parent < i ? multiply(out[parent], local) : local;
    }
}

void PuppetPose::advance(const MdlModel& model, std::vector<PuppetAnimationLayer>& layers, float dt) const {
    for (PuppetAnimationLayer& layer : layers) {
        if (!layer.visible || !layer.playing) continue;
        layer.time = std::max(0.0f, layer.time + dt * layer.rate);
        const MdlAnimationClip* clip = findClip(model, layer.animation_id);
        if (clip && (clip->loop_mode == "single" || layer.once) && clip->fps > 0.0f &&
            layer.time * clip->fps >= (float)clip->frame_count) {
            layer.time = (float)clip->frame_count / clip->fps;
            layer.playing = false;
            layer.ended = true;
        }
    }
}

void PuppetPose::attachmentTransforms(const MdlModel& model, const std::vector<PuppetAnimationLayer>& layers,
                                      std::unordered_map<std::string, PuppetMatrix>& out) const {
    out.clear();
    if (model.attachments.empty()) return;

    std::vector<MdlKeyframe> pose;
    const bool seeded = localPose(model, layers, pose);

    std::vector<PuppetMatrix> world(model.bones.size());
    for (size_t i = 0; i < model.bones.size(); ++i) {
        const PuppetMatrix local = seeded ? composeLocal(pose[i]) : bindLocal(model.bones[i]);
        const uint32_t parent = model.bones[i].parent;
        world[i] = parent < i ? multiply(world[parent], local) : local;
    }

    for (const MdlAttachment& attachment : model.attachments) {
        if (attachment.bone_index >= world.size() || attachment.name.empty()) continue;
        PuppetMatrix local;
        for (int i = 0; i < 16; ++i) local.m[i] = attachment.matrix[i];
        out[attachment.name] = multiply(world[attachment.bone_index], local);
    }
}

void PuppetPose::computeBoneMatrices(const MdlModel& model, const std::vector<PuppetAnimationLayer>& layers) const {
    const size_t count = model.bones.size();
    std::vector<MdlKeyframe> pose;
    if (!localPose(model, layers, pose)) {
        std::fill(skin_matrices.begin(), skin_matrices.end(), PuppetMatrix{});
        return;
    }

    std::vector<PuppetMatrix> world(count);
    for (size_t i = 0; i < count; ++i) {
        const PuppetMatrix local = composeLocal(pose[i]);
        const uint32_t parent = model.bones[i].parent;
        world[i] = parent < i ? multiply(world[parent], local) : local;
        skin_matrices[i] = multiply(world[i], inverse_bind_world[i]);
    }
}

void PuppetPose::skin(const MdlModel& model, const std::vector<PuppetAnimationLayer>& layers,
                      std::vector<float>& out) const {
    computeBoneMatrices(model, layers);
    out.resize(model.vertices.size() * 3);
    for (size_t v = 0; v < model.vertices.size(); ++v) {
        const MdlVertex& vertex = model.vertices[v];
        float sum[3] = {0, 0, 0};
        float weight_total = 0.0f;
        for (int k = 0; k < 4; ++k) {
            const float weight = vertex.bone_weights[k];
            if (weight <= 0.0f || vertex.bone_indices[k] >= skin_matrices.size()) continue;
            const float* m = skin_matrices[vertex.bone_indices[k]].m;
            for (int i = 0; i < 3; ++i) {
                sum[i] += weight * (m[i] * vertex.position[0] + m[4 + i] * vertex.position[1] +
                                    m[8 + i] * vertex.position[2] + m[12 + i]);
            }
            weight_total += weight;
        }
        for (int i = 0; i < 3; ++i) out[v * 3 + i] = weight_total > 0.0f ? sum[i] / weight_total : vertex.position[i];
    }
}

}  // namespace wallpaper_engine
