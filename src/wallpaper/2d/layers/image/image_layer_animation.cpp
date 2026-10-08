#include <algorithm>
#include <cmath>
#include <cstring>

#include "image_layer.h"
#include "shared/core/engine_context.h"
#include "shared/graphics/render.h"
#include "wallpaper/2d/tree/scene_tree.h"

void ImageLayer::updateAnimatedFrame(EngineContext& ctx) {
    const float clock = sprite_clock.joined ? ctx.time : (float)sprite_clock.time;
    const auto* frame = wallpaper_engine::textureFrameAtTime(texture_metadata, clock);
    if (!frame || frame == current_texture_frame) return;
    if (cached_view.id == SG_INVALID_ID) updateCachedView();

    sg_image page = img;
    sg_view page_view = cached_view;
    if (frame->image_index != 0) {
        if (animation_page.id == SG_INVALID_ID || animation_page_index != frame->image_index) {
            // Keep only the active page; uploading every page can take hundreds of MB.
            animation_page_view = {};
            animation_page = ctx.asset_mgr->resolveTexture(path.c_str(), nullptr, (int)frame->image_index);
            animation_page_index = frame->image_index;
            if (animation_page.id != SG_INVALID_ID) {
                sg_view_desc view_desc = {};
                view_desc.texture.image = animation_page;
                animation_page_view = sg_make_view(&view_desc);
            }
        }
        page = animation_page;
        page_view = animation_page_view;
    }
    if (page.id == SG_INVALID_ID || page_view.id == SG_INVALID_ID) return;

    const int width = std::max(1, (int)std::lround(frame->width));
    const int height = std::max(1, (int)std::lround(frame->height));
    if (animated_frame.width != width || animated_frame.height != height) {
        if (!animated_frame.create(width, height)) return;
    }
    const float saved_width = ctx.renderer.view_width;
    const float saved_height = ctx.renderer.view_height;
    sg_pass pass = {};
    pass.attachments.colors[0] = animated_frame.attachment_view;
    pass.action.colors[0].load_action = SG_LOADACTION_CLEAR;
    pass.action.colors[0].store_action = SG_STOREACTION_STORE;
    pass.action.colors[0].clear_value = {0.0f, 0.0f, 0.0f, 0.0f};
    sg_begin_pass(&pass);
    renderer_update_viewport(&ctx.renderer, (float)width, (float)height);
    float white[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    // The viewport clips the atlas to the authored frame; replace blending keeps straight alpha.
    renderer_draw_sprite(ctx, &ctx.renderer, page, page_view, -frame->x, -frame->y, (float)texture_metadata.width,
                         (float)texture_metadata.height, 0.0f, white, false, nullptr, true);
    sg_end_pass();
    renderer_update_viewport(&ctx.renderer, saved_width, saved_height);
    current_texture_frame = frame;
}

namespace {
double spriteDuration(const wallpaper_engine::TextureMetadata& metadata) {
    double duration = 0.0;
    for (const auto& frame : metadata.animation_frames) duration += frame.duration;
    return duration;
}

double spriteTimeOfFrame(const wallpaper_engine::TextureMetadata& metadata, double frame_index) {
    double time = 0.0;
    const size_t whole = (size_t)std::max(0.0, std::floor(frame_index));
    for (size_t i = 0; i < whole && i < metadata.animation_frames.size(); ++i)
        time += metadata.animation_frames[i].duration;
    return time;
}

int spriteFrameIndexAt(const wallpaper_engine::TextureMetadata& metadata, double seconds) {
    const double duration = spriteDuration(metadata);
    if (duration <= 0.0) return 0;
    double phase = std::fmod(seconds, duration);
    if (phase < 0.0) phase += duration;
    for (size_t i = 0; i < metadata.animation_frames.size(); ++i) {
        if (phase < metadata.animation_frames[i].duration) return (int)i;
        phase -= metadata.animation_frames[i].duration;
    }
    return (int)metadata.animation_frames.size() - 1;
}
}  // namespace

bool ImageLayer::spriteGet(const std::string& field, double now, double& out) const {
    if (!hasSpriteAnimation()) return false;
    const double duration = spriteDuration(texture_metadata);
    const double time = sprite_clock.joined ? now : sprite_clock.time;
    if (field == "rate") {
        out = sprite_clock.rate;
    } else if (field == "frameCount") {
        out = (double)texture_metadata.animation_frames.size();
    } else if (field == "duration") {
        out = duration;
    } else if (field == "fps") {
        out = duration > 0.0 ? (double)texture_metadata.animation_frames.size() / duration : 0.0;
    } else if (field == "frame") {
        out = spriteFrameIndexAt(texture_metadata, time);
    } else if (field == "playing") {
        out = sprite_clock.joined || sprite_clock.playing ? 1.0 : 0.0;
    } else {
        return false;
    }
    return true;
}

bool ImageLayer::spriteSet(const std::string& field, double value, double now) {
    if (!hasSpriteAnimation()) return false;
    if (field != "rate" && field != "frame") return false;
    if (sprite_clock.joined) {  // taking control keeps the current position
        sprite_clock.joined = false;
        sprite_clock.time = now;
    }
    if (field == "rate")
        sprite_clock.rate = value;
    else
        sprite_clock.time = spriteTimeOfFrame(texture_metadata, value);
    return true;
}

bool ImageLayer::spriteCommand(const std::string& command, double now) {
    if (!hasSpriteAnimation()) return false;
    if (command == "join") {
        sprite_clock.joined = true;
        return true;
    }
    if (command != "play" && command != "pause" && command != "stop") return false;
    if (sprite_clock.joined) {
        sprite_clock.joined = false;
        sprite_clock.time = now;
    }
    if (command == "play") {
        sprite_clock.playing = true;
    } else {
        sprite_clock.playing = false;
        if (command == "stop") sprite_clock.time = 0.0;
    }
    return true;
}

int ImageLayer::puppetLayerIndex(const std::string& name) const {
    for (size_t i = 0; i < puppet_layers.size(); ++i)
        if (puppet_layers[i].name == name) return (int)i;
    return -1;
}

bool ImageLayer::puppetLayerGet(size_t index, const std::string& field, double& out) const {
    if (index >= puppet_layers.size()) return false;
    const wallpaper_engine::PuppetAnimationLayer& layer = puppet_layers[index];
    const wallpaper_engine::MdlAnimationClip* clip = nullptr;
    for (const auto& candidate : puppet.clips)
        if (candidate.id == layer.animation_id) clip = &candidate;
    if (field == "rate") {
        out = layer.rate;
    } else if (field == "blend") {
        out = layer.blend;
    } else if (field == "visible") {
        out = layer.visible ? 1.0 : 0.0;
    } else if (field == "playing") {
        out = layer.playing ? 1.0 : 0.0;
    } else if (clip && field == "fps") {
        out = clip->fps;
    } else if (clip && field == "frameCount") {
        out = clip->frame_count;
    } else if (clip && field == "duration") {
        out = clip->fps > 0.0f ? (double)clip->frame_count / clip->fps : 0.0;
    } else if (clip && field == "frame") {
        out = layer.time * clip->fps;
    } else {
        return false;
    }
    return true;
}

bool ImageLayer::puppetLayerGetString(size_t index, const std::string& field, std::string& out) const {
    if (index >= puppet_layers.size() || field != "name") return false;
    out = puppet_layers[index].name;
    return true;
}

bool ImageLayer::puppetLayerSet(size_t index, const std::string& field, double value) {
    if (index >= puppet_layers.size()) return false;
    wallpaper_engine::PuppetAnimationLayer& layer = puppet_layers[index];
    if (field == "rate") {
        layer.rate = (float)value;
    } else if (field == "blend") {
        layer.blend = (float)value;
    } else if (field == "visible") {
        layer.visible = value != 0.0;
    } else if (field == "frame") {
        float fps = 0.0f;
        for (const auto& candidate : puppet.clips)
            if (candidate.id == layer.animation_id) fps = candidate.fps;
        if (fps <= 0.0f) return false;
        layer.time = (float)std::max(0.0, value / fps);
        layer.ended = false;
    } else {
        return false;
    }
    return true;
}

bool ImageLayer::puppetLayerCommand(size_t index, const std::string& command) {
    if (index >= puppet_layers.size()) return false;
    wallpaper_engine::PuppetAnimationLayer& layer = puppet_layers[index];
    if (command == "play") {
        // A finished one-shot clip starts over.
        if (layer.ended || !layer.playing) {
            for (const auto& clip : puppet.clips) {
                if (clip.id == layer.animation_id && clip.loop_mode == "single" &&
                    layer.time * clip.fps >= (float)clip.frame_count)
                    layer.time = 0.0f;
            }
        }
        layer.ended = false;
        layer.playing = true;
    } else if (command == "pause") {
        layer.playing = false;
    } else if (command == "stop") {
        layer.playing = false;
        layer.time = 0.0f;
        layer.ended = false;
    } else {
        return false;
    }
    return true;
}

bool ImageLayer::puppetLayerTakeEnded(size_t index) {
    if (index >= puppet_layers.size() || !puppet_layers[index].ended) return false;
    puppet_layers[index].ended = false;
    return true;
}

void ImageLayer::clearEffectTargets(const std::vector<std::string>& names) {
    for (const std::string& name : names) named_effect_targets.erase(name);
}

int ImageLayer::puppetLayerCreate(const std::string& animation, double rate, double blend, bool additive, bool once,
                                  bool remove_when_done, const std::string& name) {
    if (!has_puppet_mesh) return -1;
    const wallpaper_engine::MdlAnimationClip* found = nullptr;
    const bool numeric = !animation.empty() && std::all_of(animation.begin(), animation.end(), ::isdigit);
    for (const wallpaper_engine::MdlAnimationClip& clip : puppet.clips) {
        if (clip.name == animation || (numeric && clip.id == (uint32_t)std::stoul(animation))) {
            found = &clip;
            break;
        }
    }
    if (!found) return -1;

    wallpaper_engine::PuppetAnimationLayer layer;
    layer.animation_id = found->id;
    layer.name = name.empty() ? found->name : name;
    layer.rate = (float)rate;
    layer.blend = (float)blend;
    layer.additive = additive;
    layer.once = once;
    layer.remove_when_done = remove_when_done;
    puppet_layers.push_back(layer);
    return (int)puppet_layers.size() - 1;
}

bool ImageLayer::puppetLayerDestroy(size_t index) {
    if (index >= puppet_layers.size()) return false;
    puppet_layers.erase(puppet_layers.begin() + (std::ptrdiff_t)index);
    return true;
}

void ImageLayer::dropFinishedPuppetLayers() {
    for (size_t i = 0; i < puppet_layers.size();) {
        wallpaper_engine::PuppetAnimationLayer& layer = puppet_layers[i];
        if (layer.remove_when_done && !layer.playing && layer.ended) ++layer.frames_since_end;
        // A finished layer waits a few frames so scripts can still hear that it ended.
        const bool finished = layer.remove_when_done && !layer.playing && (!layer.ended || layer.frames_since_end > 3);
        if (finished)
            puppet_layers.erase(puppet_layers.begin() + (std::ptrdiff_t)i);
        else
            ++i;
    }
}

int ImageLayer::boneIndex(const std::string& name) const {
    for (size_t i = 0; i < puppet.bones.size(); ++i)
        if (puppet.bones[i].name == name) return (int)i;
    return -1;
}

std::string ImageLayer::boneName(size_t bone) const {
    return bone < puppet.bones.size() ? puppet.bones[bone].name : "";
}

int ImageLayer::boneParent(size_t bone) const {
    if (bone >= puppet.bones.size() || puppet.bones[bone].parent >= puppet.bones.size()) return -1;
    return (int)puppet.bones[bone].parent;
}

bool ImageLayer::boneGet(size_t bone, const std::string& field, std::vector<double>& out) const {
    if (bone >= puppet.bones.size()) return false;
    if (field == "matrix" || field == "localmatrix") {
        std::vector<wallpaper_engine::PuppetMatrix> matrices;
        if (field == "matrix")
            puppet_pose.worldMatrices(puppet, puppet_layers, matrices);
        else
            puppet_pose.localMatrices(puppet, puppet_layers, matrices);
        out.assign(matrices[bone].m, matrices[bone].m + 16);
        return true;
    }
    std::vector<wallpaper_engine::MdlKeyframe> pose;
    if (!puppet_pose.localPose(puppet, puppet_layers, pose)) return false;
    const wallpaper_engine::MdlKeyframe& now = pose[bone];
    constexpr double kRadToDeg = 180.0 / M_PI;
    if (field == "origin") {
        out.assign(now.translation, now.translation + 3);
    } else if (field == "scale") {
        out.assign(now.scale, now.scale + 3);
    } else if (field == "angles") {
        out = {now.rotation[0] * kRadToDeg, now.rotation[1] * kRadToDeg, now.rotation[2] * kRadToDeg};
    } else {
        return false;
    }
    return true;
}

bool ImageLayer::boneSet(size_t bone, const std::string& field, const std::vector<double>& value) {
    if (bone >= puppet.bones.size()) return false;
    std::vector<wallpaper_engine::MdlKeyframe> pose;
    if (!puppet_pose.localPose(puppet, puppet_layers, pose)) return false;
    wallpaper_engine::MdlKeyframe next = pose[bone];
    constexpr double kDegToRad = M_PI / 180.0;

    if (field == "matrix" || field == "localmatrix") {
        if (value.size() < 16) return false;
        mat4x4 wanted;
        for (int i = 0; i < 16; ++i) wanted[i / 4][i % 4] = (float)value[(size_t)i];
        if (field == "matrix") {
            // A model-space transform becomes relative to the parent bone.
            const uint32_t parent = puppet.bones[bone].parent;
            if (parent < puppet.bones.size()) {
                std::vector<wallpaper_engine::PuppetMatrix> world;
                puppet_pose.worldMatrices(puppet, puppet_layers, world);
                mat4x4 parent_world, inverse, local;
                for (int i = 0; i < 16; ++i) parent_world[i / 4][i % 4] = world[parent].m[i];
                mat4x4_invert(inverse, parent_world);
                mat4x4_mul(local, inverse, wanted);
                memcpy(wanted, local, sizeof(mat4x4));
            }
        }
        SceneTreeNode split;
        SceneTree::decompose(wanted, split);
        for (size_t i = 0; i < 3; ++i) {
            next.translation[i] = split.origin[i];
            next.scale[i] = split.scale[i];
            next.rotation[i] = (float)(split.angles[i] * kDegToRad);
        }
        puppet_pose.setBoneOverride(bone, next);
        return true;
    }
    if (value.size() < 3) return false;
    for (size_t i = 0; i < 3; ++i) {
        if (field == "origin")
            next.translation[i] = (float)value[i];
        else if (field == "scale")
            next.scale[i] = (float)value[i];
        else if (field == "angles")
            next.rotation[i] = (float)(value[i] * kDegToRad);
        else
            return false;
    }
    puppet_pose.setBoneOverride(bone, next);
    return true;
}

void ImageLayer::boneReset(size_t bone) {
    puppet_pose.clearBoneOverride(bone);
}
