#ifndef WALLPAPER_INSTANCE_H
#define WALLPAPER_INSTANCE_H

#include <cstdio>
#include <memory>
#include <string>
#include <utility>

#include "shared/assets/asset_manager.h"
#include "shared/audio/audio_engine.h"
#include "shared/core/engine_context.h"
#include "shared/core/vfs.h"
#include "wallpaper/user_properties.h"
#include "wallpaper/wallpaper.h"

// The per-wallpaper slice of EngineContext: everything that differs between two
// running wallpapers. Swapped in and out of the shared context (see
// activateInstanceState) so the many `ctx.scene`/`ctx.parallax` call sites keep
// working unchanged.
struct InstanceState {
    std::string asset_root;
    std::string wallpaper_path;
    bool is_pkg = false;
    scene_type_t scene_type = SCENE_TYPE_2D;
    UserProperties user_properties;
    SceneState scene;
    ParallaxState parallax;
    CameraShakeState shake;
};

// Templated on the context type so the swap can be unit tested with a light
// context that has the same per-wallpaper fields (EngineContext drags in sokol).
template <typename Ctx>
void activateInstanceState(Ctx& ctx, InstanceState& state) {
    ctx.scene = std::move(state.scene);
    ctx.parallax = state.parallax;
    ctx.shake = state.shake;
    ctx.user_properties = state.user_properties;
    std::snprintf(ctx.asset_root, sizeof(ctx.asset_root), "%s", state.asset_root.c_str());
    std::snprintf(ctx.wallpaper_path, sizeof(ctx.wallpaper_path), "%s", state.wallpaper_path.c_str());
    ctx.is_pkg = state.is_pkg;
    ctx.scene_type = state.scene_type;
}

template <typename Ctx>
void stashInstanceState(Ctx& ctx, InstanceState& state) {
    state.scene = std::move(ctx.scene);
    state.parallax = ctx.parallax;
    state.shake = ctx.shake;
    state.user_properties = ctx.user_properties;
    state.asset_root = ctx.asset_root;
    state.wallpaper_path = ctx.wallpaper_path;
    state.is_pkg = ctx.is_pkg;
    state.scene_type = ctx.scene_type;
}

struct WallpaperInstance {
    InstanceState state;
    AssetManager assets;
    vfs::PackageHandle package;
    sg_pass_action pass_action = {};
    std::unique_ptr<Wallpaper> wallpaper;
    AudioEngine::GroupId audio_group = AudioEngine::kDefaultGroup;
};

// Moves `instance`'s state into ctx, stashing whatever `current` pointed at back
// into that instance first. Sets ctx.asset_mgr = &instance.assets.
void activateInstance(EngineContext& ctx, WallpaperInstance& instance, WallpaperInstance*& current);
void deactivateInstance(EngineContext& ctx, WallpaperInstance*& current);

#endif  // WALLPAPER_INSTANCE_H
