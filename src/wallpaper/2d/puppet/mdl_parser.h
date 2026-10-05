#ifndef WALLPAPER_ENGINE_MDL_PARSER_H
#define WALLPAPER_ENGINE_MDL_PARSER_H

#include <stddef.h>
#include <stdint.h>

#include <string>
#include <vector>

namespace wallpaper_engine {

// Wallpaper Engine puppet model (.mdl). Layout confirmed against real Workshop
// assets and cross-checked by vertexBytes % 80 and indexBytes % 6:
//
//   "MDLV00XX" | u8 pad | u32 type | u16 subversion | u16 flags | u32 reserved
//   | material path (null-terminated) | 28 zero bytes
//   | tag 0x0180000F | u32 vertexBytes | vertices | u32 indexBytes | u16 indices
//   | trailer | optional MDLS skeleton | optional MDAT | optional MDLA | optional MDLE
//
// Every vertex holds position (f32x3 at +0) and uv; the stride and the skin
// offsets depend on the model version and whether it is skinned:
//   stride 80 (v0017+): normal +12, tangent +24/+36, 4 u32 bone indices +40,
//                       4 f32 weights +56, uv +72
//   stride 84 (some v0023): as 80 with one extra word, bones +44, weights +60, uv +76
//   stride 52 (v0013):  4 u32 bone indices +12, 4 f32 weights +28, uv +44
//   stride 48 (unskinned): normal +12, tangent +24/+36, uv +40, no bones
// Weights sum to 1.
//
// MDLS holds one entry per bone: u32 type, u32 parent (0xFFFFFFFF = root),
// u32 payloadBytes (64 = bind matrix), the row-major 4x4 bind matrix, an info
// JSON string and a name string. MDLA holds clips; each clip samples every
// frame: fps, frameCount, then one track per bone of (frameCount + 1) 36-byte
// keyframes (translation xyz, rotation xyz in radians, scale xyz).

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
