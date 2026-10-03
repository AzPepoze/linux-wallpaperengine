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

// Every layer contributes its motion relative to its own first frame, on top of
// the first layer's first frame, which already holds the assembled pose.
void accumulateLayer(const MdlAnimationClip& clip, const PuppetAnimationLayer& layer, std::vector<MdlKeyframe>& pose) {
    const float frame = wrapFrame(layer.time * clip.fps * layer.rate, clip.frame_count, clip.loop_mode);
    const size_t bones = std::min(pose.size(), clip.tracks.size());
    for (size_t b = 0; b < bones; ++b) {
        const std::vector<MdlKeyframe>& track = clip.tracks[b];
        if (track.empty()) continue;
        const MdlKeyframe now = sample(track, frame);
        for (int i = 0; i < 3; ++i) {
            pose[b].translation[i] += layer.blend * (now.translation[i] - track[0].translation[i]);
            pose[b].rotation[i] += layer.blend * (now.rotation[i] - track[0].rotation[i]);
            pose[b].scale[i] += layer.blend * (now.scale[i] - track[0].scale[i]);
        }
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

void PuppetPose::advance(std::vector<PuppetAnimationLayer>& layers, float dt) const {
    for (PuppetAnimationLayer& layer : layers) {
        if (layer.visible) layer.time += dt;
    }
}

void PuppetPose::computeBoneMatrices(const MdlModel& model, const std::vector<PuppetAnimationLayer>& layers) const {
    const size_t count = model.bones.size();
    std::vector<MdlKeyframe> pose(count);
    bool seeded = false;
    for (const PuppetAnimationLayer& layer : layers) {
        const MdlAnimationClip* clip = layer.visible ? findClip(model, layer.animation_id) : nullptr;
        if (!clip) continue;
        if (!seeded) {
            for (size_t b = 0; b < std::min(count, clip->tracks.size()); ++b) {
                if (!clip->tracks[b].empty()) pose[b] = clip->tracks[b][0];
            }
            seeded = true;
        }
        accumulateLayer(*clip, layer, pose);
    }
    if (!seeded) {
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
