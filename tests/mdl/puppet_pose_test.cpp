// Synthetic checks for puppet pose skinning. Part of the mdl_tests target.
#include "wallpaper/2d/puppet/puppet_pose.h"

#include <math.h>
#include <stdio.h>

using namespace wallpaper_engine;

namespace {

int g_failures = 0;

void check(bool condition, const char* what) {
    if (!condition) {
        fprintf(stderr, "FAIL: %s\n", what);
        ++g_failures;
    }
}

MdlModel makeModel() {
    MdlModel model;
    MdlBone bone;
    bone.bind_matrix[0] = bone.bind_matrix[5] = bone.bind_matrix[10] = bone.bind_matrix[15] = 1.0f;
    bone.bind_matrix[12] = 100.0f;
    model.bones.push_back(bone);

    MdlVertex vertex;
    vertex.position[0] = 110.0f;
    vertex.bone_weights[0] = 1.0f;
    model.vertices.push_back(vertex);

    MdlAnimationClip clip;
    clip.id = 7;
    clip.loop_mode = "loop";
    clip.fps = 10.0f;
    clip.frame_count = 10;
    clip.tracks.resize(1);
    for (uint32_t i = 0; i <= clip.frame_count; ++i) {
        MdlKeyframe key;
        key.translation[0] = 100.0f + (float)i * 10.0f;
        clip.tracks[0].push_back(key);
    }
    model.clips.push_back(clip);
    return model;
}

}  // namespace

void runPuppetPoseTests() {
    const MdlModel model = makeModel();
    PuppetPose pose;
    pose.init(model);
    std::vector<float> out;

    std::vector<PuppetAnimationLayer> layers(1);
    layers[0].animation_id = 7;
    pose.skin(model, layers, out);
    check(out.size() == 3 && fabsf(out[0] - 110.0f) < 1e-3f, "first frame keeps the bind position");

    pose.advance(layers, 0.5f);
    pose.skin(model, layers, out);
    check(fabsf(out[0] - 160.0f) < 1e-3f, "half a second moves the bone by 50 units");

    layers[0].visible = false;
    pose.skin(model, layers, out);
    check(fabsf(out[0] - 110.0f) < 1e-3f, "hidden layer leaves the rest pose");

    if (g_failures != 0) fprintf(stderr, "%d pose check(s) failed\n", g_failures);
}

int puppetPoseFailures() {
    return g_failures;
}
