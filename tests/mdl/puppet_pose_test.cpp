#include "wallpaper/2d/puppet/puppet_pose.h"

#include <math.h>
#include <stdio.h>

#include <unordered_map>

#include "../test_util.h"
using test::check;

using namespace wallpaper_engine;

namespace {

MdlModel makeModel() {
    MdlModel model;
    MdlBone bone;
    bone.bind_matrix[0] = bone.bind_matrix[5] = bone.bind_matrix[10] = bone.bind_matrix[15] = 1.0f;
    bone.bind_matrix[12] = 100.0f;
    model.bones.push_back(bone);

    MdlAttachment attachment;
    attachment.bone_index = 0;
    attachment.name = "socket";
    attachment.matrix[0] = attachment.matrix[5] = attachment.matrix[10] = attachment.matrix[15] = 1.0f;
    attachment.matrix[12] = 3.0f;
    model.attachments.push_back(attachment);

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

    pose.advance(model, layers, 0.5f);
    pose.skin(model, layers, out);
    check(fabsf(out[0] - 160.0f) < 1e-3f, "half a second moves the bone by 50 units");
    std::unordered_map<std::string, PuppetMatrix> attachments;
    pose.attachmentTransforms(model, layers, attachments);
    check(attachments.count("socket") == 1, "animated attachment is exposed by name");
    if (attachments.count("socket")) {
        check(fabsf(attachments["socket"].m[12] - 153.0f) < 1e-3f,
              "attachment uses animated bone world transform plus its local transform");
    }

    layers[0].visible = false;
    pose.skin(model, layers, out);
    check(fabsf(out[0] - 110.0f) < 1e-3f, "hidden layer leaves the rest pose");
    layers[0].visible = true;

    // Rate scales the clock as it advances, so changing it later speeds up smoothly instead of jumping.
    layers[0].time = 0.0f;
    layers[0].rate = 2.0f;
    pose.advance(model, layers, 0.25f);
    check(fabsf(layers[0].time - 0.5f) < 1e-5f, "rate 2 advances the clock twice as fast");
    layers[0].rate = 1.0f;
    pose.skin(model, layers, out);
    check(fabsf(out[0] - 160.0f) < 1e-3f, "lowering the rate keeps the current pose");

    layers[0].playing = false;
    pose.advance(model, layers, 1.0f);
    check(fabsf(layers[0].time - 0.5f) < 1e-5f, "a paused layer does not advance");

    std::vector<PuppetAnimationLayer> once_layers(1);
    once_layers[0].animation_id = 7;
    once_layers[0].once = true;
    pose.advance(model, once_layers, 5.0f);
    check(!once_layers[0].playing && once_layers[0].ended, "a one-shot layer ends at the clip's last frame");
    check(fabsf(once_layers[0].time - 1.0f) < 1e-5f, "a one-shot layer holds its last frame");
    pose.skin(model, once_layers, out);
    check(fabsf(out[0] - 210.0f) < 1e-3f, "a one-shot layer shows its last frame instead of wrapping");

    pose.root_motion = false;
    pose.skin(model, layers, out);
    check(fabsf(out[0] - 110.0f) < 1e-3f, "root motion off ignores the root bone's animated translation");
    pose.root_motion = true;

    std::vector<MdlKeyframe> local;
    check(pose.localPose(model, layers, local) && fabsf(local[0].translation[0] - 150.0f) < 1e-3f,
          "local pose reports the animated translation");
    MdlKeyframe forced = local[0];
    forced.translation[0] = 400.0f;
    pose.setBoneOverride(0, forced);
    pose.skin(model, layers, out);
    check(fabsf(out[0] - 410.0f) < 1e-3f, "bone override moves the skinned vertex");
    std::vector<PuppetMatrix> world;
    pose.worldMatrices(model, layers, world);
    check(world.size() == 1 && fabsf(world[0].m[12] - 400.0f) < 1e-3f, "world matrix follows the override");
    pose.clearBoneOverride(0);
    pose.skin(model, layers, out);
    check(fabsf(out[0] - 160.0f) < 1e-3f, "clearing the override restores the animation");
    // The IK endpoint and pole are world-space tracks that follow the ordinary bone tracks.
    MdlModel limb;
    limb.bones.resize(3);
    for (auto& bone : limb.bones)
        bone.bind_matrix[0] = bone.bind_matrix[5] = bone.bind_matrix[10] = bone.bind_matrix[15] = 1.0f;
    limb.bones[1].parent = 0;
    limb.bones[1].bind_matrix[12] = 10.0f;
    limb.bones[2].parent = 1;
    limb.bones[2].bind_matrix[12] = 10.0f;
    limb.bones[2].ik_depth = 2;
    limb.controllers = {{2, true}, {2, false}};
    limb.reference_pose.resize(3);
    limb.reference_pose[1].translation[0] = 10.0f;
    limb.reference_pose[2].translation[0] = 10.0f;
    MdlAnimationClip reach;
    reach.id = 9;
    reach.fps = 1;
    reach.frame_count = 1;
    reach.loop_mode = "single";
    reach.tracks.resize(5);
    for (size_t b = 0; b < 3; ++b) reach.tracks[b] = {limb.reference_pose[b], limb.reference_pose[b]};
    MdlKeyframe pole, endpoint;
    pole.translation[1] = 20;
    endpoint.translation[0] = endpoint.translation[1] = 10;
    reach.tracks[3] = {pole, pole};
    reach.tracks[4] = {endpoint, endpoint};
    reach.tracks[4][1].translation[0] = 15;
    reach.tracks[4][1].translation[1] = 0;
    limb.clips.push_back(reach);
    MdlVertex tip;
    tip.position[0] = 20;
    tip.bone_indices[0] = 2;
    tip.bone_weights[0] = 1;
    limb.vertices.push_back(tip);
    PuppetPose limb_pose;
    limb_pose.init(limb);
    std::vector<PuppetAnimationLayer> reach_layers(1);
    reach_layers[0].animation_id = 9;
    limb_pose.worldMatrices(limb, reach_layers, world);
    check(fabsf(world[2].m[12] - 10) < 1e-3f && fabsf(world[2].m[13] - 10) < 1e-3f,
          "two-bone IK reaches its world-space endpoint");
    check(world[1].m[13] > 9.9f, "pole selects the elbow bend side");
    limb_pose.skin(limb, reach_layers, out);
    check(fabsf(out[0] - 10) < 1e-3f && fabsf(out[1] - 10) < 1e-3f, "IK skins the sheet-space vertex");
    limb_pose.advance(limb, reach_layers, 1);
    limb_pose.worldMatrices(limb, reach_layers, world);
    check(fabsf(world[2].m[12] - 15) < 1e-3f && fabsf(world[2].m[13]) < 1e-3f,
          "animated controller moves the endpoint without stretching the chain");
    reach_layers[0].visible = false;
    limb_pose.skin(limb, reach_layers, out);
    check(fabsf(out[0] - 20) < 1e-3f && fabsf(out[1]) < 1e-3f, "hidden IK animation keeps the bind pose");
}
