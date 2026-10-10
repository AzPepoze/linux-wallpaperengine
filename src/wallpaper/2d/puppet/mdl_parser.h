#ifndef WALLPAPER_ENGINE_MDL_PARSER_H
#define WALLPAPER_ENGINE_MDL_PARSER_H

#include <stddef.h>
#include <stdint.h>

#include <string>
#include <vector>

namespace wallpaper_engine {

// Wallpaper Engine .mdl puppet model; byte layout is in docs/wallpaper-engine-knowledge.md.

struct MdlVertex {
    float position[3] = {0, 0, 0};
    float normal[3] = {0, 0, 0};
    float tangent[3] = {0, 0, 0};
    float tangent_w = 0.0f;
    uint32_t bone_indices[4] = {0, 0, 0, 0};
    float bone_weights[4] = {0, 0, 0, 0};
    float uv[2] = {0, 0};
};

struct MdlTriangle {
    uint16_t a = 0;
    uint16_t b = 0;
    uint16_t c = 0;
};

struct MdlBone {
    uint32_t parent = 0xFFFFFFFFu;
    // Row-major 4x4 bind transform, translation in row 3.
    float bind_matrix[16] = {};
    std::string name;
    uint32_t ik_depth = 0;
};

struct MdlKeyframe {
    float translation[3] = {0, 0, 0};
    float rotation[3] = {0, 0, 0};
    float scale[3] = {1, 1, 1};
};

struct MdlAnimationClip {
    uint32_t id = 0;
    std::string name;
    std::string loop_mode;
    float fps = 0.0f;
    uint32_t frame_count = 0;
    // One track per bone, in bone order; each holds frame_count + 1 samples.
    std::vector<std::vector<MdlKeyframe>> tracks;
};

struct MdlAttachment {
    uint16_t bone_index = 0;
    std::string name;
    // Column-major local transform from attachment space to the named bone.
    float matrix[16] = {};
};

struct MdlController {
    uint32_t bone_index = 0;
    bool pole = false;
};

struct MdlModel {
    std::string version;
    std::string material;
    std::vector<MdlVertex> vertices;
    std::vector<MdlTriangle> triangles;
    std::vector<MdlBone> bones;
    std::vector<MdlAttachment> attachments;
    // Extra animation tracks follow these records in skeleton order.
    std::vector<MdlController> controllers;
    // Authored assembled local pose, distinct from the sprite-sheet bind pose.
    std::vector<MdlKeyframe> reference_pose;
    std::vector<MdlAnimationClip> clips;
};

bool parseMdl(const uint8_t* data, size_t size, MdlModel& out);

}  // namespace wallpaper_engine

#endif  // WALLPAPER_ENGINE_MDL_PARSER_H
