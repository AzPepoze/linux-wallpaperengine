#include "wallpaper/2d/scene_2d_wallpaper.h"

#include "shared/core/logger.h"
#include "wallpaper/2d/scene_builder.h"

Scene2DWallpaper::Scene2DWallpaper(EngineContext& ctx) {
    runtime_ = std::make_unique<Scene2DRuntime>(ctx);
    runtime_->init();
}

Scene2DWallpaper::~Scene2DWallpaper() {
    clear();
}

bool Scene2DWallpaper::applyParsedScene(ParsedScene parsed, EngineContext& ctx) {
    if (parsed.layers.empty() && !parsed.scene_tree) {
        return false;
    }

    ctx.camera = parsed.camera;
    ctx.general = parsed.general;
    ctx.layers = std::move(parsed.layers);
    ctx.scene_tree = parsed.scene_tree;
    ctx.scene_w = parsed.design_width;
    ctx.scene_h = parsed.design_height;
    ctx.camera_parallax_enabled = parsed.general.camera_parallax_enabled;
    ctx.camera_parallax_amount = parsed.general.camera_parallax_amount;
    ctx.camera_parallax_delay = parsed.general.camera_parallax_delay;
    ctx.camera_parallax_mouse_influence = parsed.general.camera_parallax_mouse_influence;
    ctx.camera_shake_enabled = parsed.general.camera_shake_enabled;
    ctx.camera_shake_amplitude = parsed.general.camera_shake_amplitude;
    ctx.camera_shake_speed = parsed.general.camera_shake_speed;
    ctx.camera_shake_roughness = parsed.general.camera_shake_roughness;
    ctx.perspective_override_fov = parsed.general.perspective_override_fov;

    if (parsed.general.has_clear_color && parsed.general.clear_enabled) {
        ctx.pass_action.colors[0].load_action = SG_LOADACTION_CLEAR;
        ctx.pass_action.colors[0].clear_value = {parsed.general.clear_color[0], parsed.general.clear_color[1],
                                                 parsed.general.clear_color[2], parsed.general.clear_color[3]};
    }

    runtime_->updateViewport();
    return true;
}

bool Scene2DWallpaper::load(const std::string& path, EngineContext& ctx) {
    clear();

    ParsedScene parsed = SceneBuilder::load(path.c_str(), ctx);
    if (!applyParsedScene(std::move(parsed), ctx)) {
        LOG_TAG_E("SCENE_2D", "Failed to parse 2D scene: %s", path.c_str());
        return false;
    }
    return true;
}

void Scene2DWallpaper::update(float dt, EngineContext& ctx) {
    (void)ctx;
    if (runtime_) {
        runtime_->update(dt);
    }
}

void Scene2DWallpaper::render(EngineContext& ctx) {
    (void)ctx;
    if (runtime_) {
        runtime_->draw();
        runtime_->present();
    }
}

void Scene2DWallpaper::onResize(float width, float height) {
    (void)width;
    (void)height;
    if (runtime_) {
        runtime_->updateViewport();
    }
}

void Scene2DWallpaper::handleInput(const sapp_event* event, EngineContext& ctx) {
    (void)event;
    (void)ctx;
}

void Scene2DWallpaper::clear() {
    if (runtime_) {
        runtime_->clearScene();
    }
}
