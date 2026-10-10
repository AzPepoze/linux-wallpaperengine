#include "wallpaper/2d/puppet/mdl_parser.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <vector>

#include "../test_util.h"
using test::check;

void runPuppetPoseTests();
void runSceneAttachmentTests();

namespace {

void appendU32(std::vector<uint8_t>& out, uint32_t value) {
    out.push_back((uint8_t)(value & 0xff));
    out.push_back((uint8_t)((value >> 8) & 0xff));
    out.push_back((uint8_t)((value >> 16) & 0xff));
    out.push_back((uint8_t)((value >> 24) & 0xff));
}

void patchU32(std::vector<uint8_t>& out, size_t offset, uint32_t value) {
    for (int i = 0; i < 4; ++i) out[offset + (size_t)i] = (uint8_t)((value >> (i * 8)) & 0xff);
}

void appendU16(std::vector<uint8_t>& out, uint16_t value) {
    out.push_back((uint8_t)(value & 0xff));
    out.push_back((uint8_t)(value >> 8));
}

void appendF32(std::vector<uint8_t>& out, float value) {
    uint32_t bits;
    memcpy(&bits, &value, 4);
    appendU32(out, bits);
}

void appendU8(std::vector<uint8_t>& out, uint8_t value) {
    out.push_back(value);
}

void appendText(std::vector<uint8_t>& out, const char* text) {
    out.insert(out.end(), text, text + strlen(text));
}

void appendVertex(std::vector<uint8_t>& out, float x, float y, float u, float v, uint32_t b0, uint32_t b1, float w0,
                  float w1) {
    appendF32(out, x);
    appendF32(out, y);
    appendF32(out, 0.0f);
    appendF32(out, 0.0f);
    appendF32(out, 0.0f);
    appendF32(out, 1.0f);
    appendF32(out, 1.0f);
    appendF32(out, 0.0f);
    appendF32(out, 0.0f);
    appendF32(out, 1.0f);
    appendU32(out, b0);
    appendU32(out, b1);
    appendU32(out, 0);
    appendU32(out, 0);
    appendF32(out, w0);
    appendF32(out, w1);
    appendF32(out, 0.0f);
    appendF32(out, 0.0f);
    appendF32(out, u);
    appendF32(out, v);
}

std::vector<uint8_t> makeMdl(bool controllers = false) {
    std::vector<uint8_t> out;
    appendText(out, "MDLV0023");
    appendU8(out, 0);
    appendU32(out, 0x01800009);
    appendU16(out, 1);
    appendU16(out, 0);
    appendU32(out, 0);
    appendText(out, "materials/test.json");
    appendU8(out, 0);
    for (int i = 0; i < 28; ++i) appendU8(out, 0);

    std::vector<uint8_t> vertices;
    appendVertex(vertices, 10.0f, 20.0f, 0.25f, 0.5f, 0, 0, 1.0f, 0.0f);
    appendVertex(vertices, -10.0f, -20.0f, 0.75f, 0.9f, 0, 1, 0.5f, 0.5f);
    appendU8(out, 0x0f);
    appendU8(out, 0x00);
    appendU8(out, 0x80);
    appendU8(out, 0x01);
    appendU32(out, (uint32_t)vertices.size());
    out.insert(out.end(), vertices.begin(), vertices.end());
    appendU32(out, 6);
    appendU16(out, 0);
    appendU16(out, 1);
    appendU16(out, 0);

    // MDLS: one root bone with an identity bind matrix.
    appendText(out, "MDLS0004");
    appendU8(out, 0);
    const size_t skeleton_end_offset_pos = out.size();
    appendU32(out, 0);
    appendU32(out, 1);
    appendU8(out, 0);
    appendU32(out, 0);
    appendU32(out, 0xFFFFFFFFu);
    appendU32(out, 64);
    for (int i = 0; i < 16; ++i) appendF32(out, i % 5 == 0 ? 1.0f : 0.0f);
    if (controllers) appendText(out, "{\"ik\":true,\"ikd\":1}");
    appendU8(out, 0);
    appendText(out, "root");
    appendU8(out, 0);

    if (controllers) {
        appendU8(out, 0);
        appendU32(out, 0);
        appendU32(out, 0);  // endpoint, rather than pole
        for (int i = 0; i < 16; ++i) appendF32(out, i % 5 == 0 ? 1.0f : 0.0f);
        appendU8(out, 1);
        for (int i = 0; i < 16; ++i) appendF32(out, i == 12 ? 25.0f : (i % 5 == 0 ? 1.0f : 0.0f));
    }
    patchU32(out, skeleton_end_offset_pos, (uint32_t)out.size());

    // MDAT: one attachment on the root bone, translated by (3, 4, 0).
    appendText(out, "MDAT0001");
    appendU8(out, 0);
    const size_t attachment_end_offset_pos = out.size();
    appendU32(out, 0);
    appendU16(out, 1);
    appendU16(out, 0);
    appendText(out, "socket");
    appendU8(out, 0);
    for (int i = 0; i < 16; ++i) {
        float value = i % 5 == 0 ? 1.0f : 0.0f;
        if (i == 12) value = 3.0f;
        if (i == 13) value = 4.0f;
        appendF32(out, value);
    }
    patchU32(out, attachment_end_offset_pos, (uint32_t)out.size());

    // MDLA: one clip, one bone track, keyframes for frame_count=1.
    appendText(out, "MDLA0006");
    appendU8(out, 0);
    appendU32(out, 0);
    appendU32(out, 1);
    appendU32(out, 77);
    appendU32(out, 0);
    appendText(out, "clip");
    appendU8(out, 0);
    appendText(out, "loop");
    appendU8(out, 0);
    appendF32(out, 30.0f);
    appendU32(out, 1);
    appendU32(out, 0);
    appendU32(out, 1);
    appendU32(out, 0);
    appendU32(out, 72);  // (frame_count + 1) * 36
    for (int frame = 0; frame < 2; ++frame) {
        appendF32(out, (float)frame);
        appendF32(out, 0.0f);
        appendF32(out, 0.0f);
        appendF32(out, 0.0f);
        appendF32(out, 0.0f);
        appendF32(out, (float)frame * 0.5f);
        appendF32(out, 1.0f);
        appendF32(out, 1.0f);
        appendF32(out, 1.0f);
    }
    if (controllers) {
        appendU32(out, 1);
        appendU32(out, 72);
        for (int frame = 0; frame < 2; ++frame) {
            for (int axis = 0; axis < 3; ++axis) appendF32(out, axis == 0 ? 25.0f + frame : 0.0f);
            for (int axis = 0; axis < 3; ++axis) appendF32(out, 0.0f);
            for (int axis = 0; axis < 3; ++axis) appendF32(out, 1.0f);
        }
    }
    return out;
}

}  // namespace

int main() {
    const std::vector<uint8_t> bytes = makeMdl();
    wallpaper_engine::MdlModel model;
    check(wallpaper_engine::parseMdl(bytes.data(), bytes.size(), model), "synthetic MDLV parses");

    check(model.version == "MDLV0023", "version string");
    check(model.material == "materials/test.json", "material path");
    check(model.vertices.size() == 2, "two vertices");
    check(model.triangles.size() == 1, "one triangle");
    if (model.vertices.size() == 2) {
        check(model.vertices[0].position[0] == 10.0f && model.vertices[0].position[1] == 20.0f, "vertex 0 position");
        check(model.vertices[1].uv[0] == 0.75f && model.vertices[1].uv[1] == 0.9f, "vertex 1 uv");
        check(model.vertices[1].bone_indices[1] == 1 && model.vertices[1].bone_weights[0] == 0.5f, "vertex skin data");
    }
    if (model.triangles.size() == 1) {
        check(model.triangles[0].a == 0 && model.triangles[0].b == 1 && model.triangles[0].c == 0, "triangle indices");
    }

    check(model.bones.size() == 1, "one bone");
    if (model.bones.size() == 1) {
        check(model.bones[0].parent == 0xFFFFFFFFu, "root bone parent");
        check(model.bones[0].name == "root", "bone name");
        check(model.bones[0].bind_matrix[0] == 1.0f && model.bones[0].bind_matrix[1] == 0.0f, "bind matrix");
    }

    check(model.attachments.size() == 1, "one attachment");
    if (model.attachments.size() == 1) {
        check(model.attachments[0].bone_index == 0 && model.attachments[0].name == "socket",
              "attachment bone index and name");
        check(model.attachments[0].matrix[12] == 3.0f && model.attachments[0].matrix[13] == 4.0f,
              "attachment local matrix translation");
    }

    check(model.clips.size() == 1, "one clip");
    if (model.clips.size() == 1) {
        const wallpaper_engine::MdlAnimationClip& clip = model.clips[0];
        check(clip.id == 77, "clip id");
        check(clip.name == "clip" && clip.loop_mode == "loop", "clip name and loop");
        check(clip.fps == 30.0f && clip.frame_count == 1, "clip timing");
        check(clip.tracks.size() == 1 && clip.tracks[0].size() == 2, "clip track sampling");
        if (clip.tracks.size() == 1 && clip.tracks[0].size() == 2) {
            check(clip.tracks[0][1].rotation[2] == 0.5f, "second keyframe rotation");
        }
    }

    wallpaper_engine::MdlModel rejected;
    const uint8_t junk[4] = {1, 2, 3, 4};
    check(!wallpaper_engine::parseMdl(junk, sizeof(junk), rejected), "short buffer rejected");
    check(!wallpaper_engine::parseMdl(nullptr, 0, rejected), "null buffer rejected");

    // The 60 legacy vertices also divide by the 48-byte stride; both layouts accept every index.
    auto legacy_bytes = makeMdl();
    memcpy(legacy_bytes.data(), "MDLV0016", 8);
    const size_t mesh = 21 + strlen("materials/test.json") + 1 + 28;
    std::vector<uint8_t> legacy_vertices;
    for (int i = 0; i < 60; ++i) {
        std::vector<uint8_t> vertex;
        appendVertex(vertex, (float)i, 20, 0.25f, 0.5f, 0, 0, 1, 0);
        legacy_vertices.insert(legacy_vertices.end(), vertex.begin(), vertex.begin() + 12);
        legacy_vertices.insert(legacy_vertices.end(), vertex.begin() + 40, vertex.end());
    }
    legacy_bytes.erase(legacy_bytes.begin() + mesh + 8, legacy_bytes.begin() + mesh + 8 + 160);
    legacy_bytes.insert(legacy_bytes.begin() + mesh + 8, legacy_vertices.begin(), legacy_vertices.end());
    patchU32(legacy_bytes, mesh + 4, (uint32_t)legacy_vertices.size());
    legacy_bytes[mesh + 8 + legacy_vertices.size() + 4 + 2] = 59;
    wallpaper_engine::MdlModel legacy;
    check(wallpaper_engine::parseMdl(legacy_bytes.data(), legacy_bytes.size(), legacy), "ambiguous legacy mesh parses");
    check(legacy.vertices.size() == 60, "legacy version prefers 52-byte skinned vertices over 48-byte vertices");
    if (legacy.vertices.size() == 60) {
        check(legacy.vertices[59].position[0] == 59 && legacy.vertices[59].uv[0] == 0.25f,
              "legacy stride retains final vertex position and UV");
        check(legacy.vertices[59].bone_weights[0] == 1, "legacy stride retains skinning weights");
    }

    const auto ik_bytes = makeMdl(true);
    wallpaper_engine::MdlModel ik_model;
    check(wallpaper_engine::parseMdl(ik_bytes.data(), ik_bytes.size(), ik_model), "IK skeleton parses");
    check(ik_model.bones.size() == 1 && ik_model.bones[0].ik_depth == 1, "IK chain depth is retained");
    check(ik_model.controllers.size() == 1 && !ik_model.controllers[0].pole,
          "endpoint controller is retained in track order");
    check(ik_model.reference_pose.size() == 1 && ik_model.reference_pose[0].translation[0] == 25,
          "assembled reference pose differs from sheet bind pose");
    check(ik_model.clips.size() == 1 && ik_model.clips[0].tracks.size() == 2 &&
              ik_model.clips[0].tracks[1][1].translation[0] == 26,
          "extra controller tracks retain animated samples");

    runPuppetPoseTests();
    runSceneAttachmentTests();
    return test::finish("mdl tests");
}
