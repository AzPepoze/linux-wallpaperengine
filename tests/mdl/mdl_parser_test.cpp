// Synthetic checks for the puppet MDLV parser. Not part of the default build.
#include "wallpaper/2d/puppet/mdl_parser.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <vector>

namespace {

int g_failures = 0;

void check(bool condition, const char* what) {
    if (!condition) {
        fprintf(stderr, "FAIL: %s\n", what);
        ++g_failures;
    }
}

void appendU32(std::vector<uint8_t>& out, uint32_t value) {
    out.push_back((uint8_t)(value & 0xff));
    out.push_back((uint8_t)((value >> 8) & 0xff));
    out.push_back((uint8_t)((value >> 16) & 0xff));
    out.push_back((uint8_t)((value >> 24) & 0xff));
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
    appendF32(out, 0.0f);  // normal
    appendF32(out, 0.0f);
    appendF32(out, 1.0f);
    appendF32(out, 1.0f);  // tangent
    appendF32(out, 0.0f);
    appendF32(out, 0.0f);
    appendF32(out, 1.0f);  // tangent handedness
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

std::vector<uint8_t> makeMdl() {
    std::vector<uint8_t> out;
    appendText(out, "MDLV0023");
    appendU8(out, 0);            // reserved
    appendU32(out, 0x01800009);  // type word
    appendU16(out, 1);           // sub-version
    appendU16(out, 0);           // flags
    appendU32(out, 0);           // reserved
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
    appendU32(out, 0);  // next offset (patched below)
    appendU32(out, 1);  // bone count
    appendU8(out, 0);   // reserved
    appendU32(out, 0);  // type
    appendU32(out, 0xFFFFFFFFu);
    appendU32(out, 64);
    for (int i = 0; i < 16; ++i) appendF32(out, i % 5 == 0 ? 1.0f : 0.0f);
    appendU8(out, 0);         // empty info JSON
    appendText(out, "root");  // bone name
    appendU8(out, 0);

    // MDLA: one clip, one bone track, keyframes for frame_count=1.
    appendText(out, "MDLA0006");
    appendU8(out, 0);
    appendU32(out, 0);   // end offset
    appendU32(out, 1);   // animation count
    appendU32(out, 77);  // first clip id
    appendU32(out, 0);
    appendText(out, "clip");
    appendU8(out, 0);
    appendText(out, "loop");
    appendU8(out, 0);
    appendF32(out, 30.0f);
    appendU32(out, 1);  // frame count
    appendU32(out, 0);
    appendU32(out, 1);   // track count
    appendU32(out, 0);   // track reserved
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

    if (g_failures != 0) {
        fprintf(stderr, "%d check(s) failed\n", g_failures);
        return 1;
    }
    printf("mdl parser checks passed\n");
    return 0;
}
