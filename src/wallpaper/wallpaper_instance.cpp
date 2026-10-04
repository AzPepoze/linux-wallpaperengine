#include "wallpaper/wallpaper_instance.h"

void activateInstance(EngineContext& ctx, WallpaperInstance& instance, WallpaperInstance*& current) {
    if (current == &instance) return;
    if (current) stashInstanceState(ctx, current->state);
    activateInstanceState(ctx, instance.state);
    instance.assets.setAudioGroup(instance.audio_group);
    ctx.asset_mgr = &instance.assets;
    ctx.audio_group = instance.audio_group;
    current = &instance;
}

void deactivateInstance(EngineContext& ctx, WallpaperInstance*& current) {
    if (!current) return;
    stashInstanceState(ctx, current->state);
    ctx.asset_mgr = nullptr;
    ctx.audio_group = AudioEngine::kDefaultGroup;
    current = nullptr;
}
