#ifndef PARTICLE_ROPE_H
#define PARTICLE_ROPE_H

#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

// Native genericropeparticle THICKFORMAT layout, including the two endpoint colors.
struct ParticleRopeVertex {
    float position[4];
    float endpoint[4];
    float color[4];
    float control_start[4];
    float control_end[4];
    float color_end[4];
    float uv[2];
};
static_assert(sizeof(ParticleRopeVertex) == 26 * sizeof(float));

struct ParticleRopePoint {
    std::array<float, 3> position;
    std::array<float, 4> color;
    float size;
};

inline void appendParticleRope(const std::vector<ParticleRopePoint>& points, std::vector<ParticleRopeVertex>& vertices,
                               std::vector<uint32_t>& indices) {
    for (size_t i = 1; i < points.size(); ++i) {
        const auto& a = points[i - 1];
        const auto& b = points[i];
        const float dx = b.position[0] - a.position[0], dy = b.position[1] - a.position[1];
        if (!std::isfinite(dx) || !std::isfinite(dy) || dx * dx + dy * dy < 1e-8f || !std::isfinite(a.size) ||
            !std::isfinite(b.size) || a.size <= 0 || b.size <= 0)
            continue;
        bool finite = true;
        for (int axis = 0; axis < 3; ++axis)
            finite = finite && std::isfinite(a.position[axis]) && std::isfinite(b.position[axis]);
        for (int channel = 0; channel < 4; ++channel)
            finite = finite && std::isfinite(a.color[channel]) && std::isfinite(b.color[channel]);
        if (!finite) continue;
        const uint32_t base = (uint32_t)vertices.size();
        for (const std::array<float, 2> uv : {std::array<float, 2>{0, 0}, {1, 0}, {1, 1}, {0, 1}}) {
            ParticleRopeVertex v = {};
            for (int axis = 0; axis < 3; ++axis) {
                v.position[axis] = v.control_start[axis] = a.position[axis];
                v.endpoint[axis] = v.control_end[axis] = b.position[axis];
            }
            v.position[3] = a.size * 0.5f;
            v.control_end[3] = b.size * 0.5f;
            v.endpoint[3] = (float)points.size();
            v.control_start[3] = (float)(i - 1);
            for (int channel = 0; channel < 4; ++channel) {
                v.color[channel] = a.color[channel];
                v.color_end[channel] = b.color[channel];
            }
            v.uv[0] = uv[0];
            v.uv[1] = uv[1];
            vertices.push_back(v);
        }
        for (uint32_t offset : {0u, 1u, 2u, 2u, 3u, 0u}) indices.push_back(base + offset);
    }
}

#endif
