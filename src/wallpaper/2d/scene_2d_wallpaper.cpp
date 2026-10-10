#include "wallpaper/2d/scene_2d_wallpaper.h"

#include "shared/core/config.h"
#include "shared/core/logger.h"
#include "shared/core/vfs.h"
#include "shared/graphics/backend/surface.h"
#include "wallpaper/2d/camera/parallax.h"
#include "wallpaper/2d/layers/image/image_layer.h"
#include "wallpaper/2d/scene_builder.h"
#include "wallpaper/2d/script/script_engine.h"

Scene2DWallpaper::Scene2DWallpaper(EngineContext& ctx) {
    runtime_ = std::make_unique<Scene2DRuntime>(ctx);
    runtime_->init();
}

Scene2DWallpaper::~Scene2DWallpaper() {
    clear();
}

bool Scene2DWallpaper::applyParsedScene(ParsedScene parsed, EngineContext& ctx, bool precompile_blends) {
    if (parsed.layers.empty() && !parsed.scene_tree) {
        return false;
    }

    ctx.scene.elapsed_time = 0.0f;
    ctx.scene.camera = parsed.camera;
    ctx.scene.general = parsed.general;
    ctx.scene_type = parsed.type;
    ctx.scene.layers = std::move(parsed.layers);
    ctx.scene.scene_tree = parsed.scene_tree;
    ctx.scene.scripts = parsed.scripts;
    ctx.scene.scene_w = parsed.design_width;
    ctx.scene.scene_h = parsed.design_height;
    applyParallaxScene(ctx, parsed.general);
    ctx.shake.enabled = parsed.general.camera_shake_enabled;
    ctx.shake.amplitude = parsed.general.camera_shake_amplitude;
    ctx.shake.speed = parsed.general.camera_shake_speed;
    ctx.shake.roughness = parsed.general.camera_shake_roughness;
    ctx.scene.perspective_override_fov = parsed.general.perspective_override_fov;

    if (parsed.general.has_clear_color && parsed.general.clear_enabled) {
        ctx.pass_action.colors[0].load_action = SG_LOADACTION_CLEAR;
        ctx.pass_action.colors[0].clear_value = {parsed.general.clear_color[0], parsed.general.clear_color[1],
                                                 parsed.general.clear_color[2], parsed.general.clear_color[3]};
    }

    if (precompile_blends) runtime_->precompileBlendModes();
    runtime_->updateViewport();
    return true;
}

bool Scene2DWallpaper::load(const std::string& path, EngineContext& ctx) {
    build_job_.reset();
    load_complete_ = false;
    load_failed_ = false;
    load_applied_ = false;
    blend_precompile_done_ = true;
    blend_precompile_job_.reset();
    clear();

    ParsedScene parsed = SceneBuilder::load(path.c_str(), ctx);
    if (!applyParsedScene(std::move(parsed), ctx)) {
        LOG_TAG_E("SCENE_2D", "Failed to parse 2D scene: %s", path.c_str());
        return false;
    }
    return true;
}

void Scene2DWallpaper::beginLoadDocument(wallpaper_engine::SceneDocument document, EngineContext& ctx) {
    build_job_.reset();
    clear();
    load_complete_ = false;
    load_failed_ = false;
    cleanup_done_ = false;
    load_applied_ = false;
    blend_precompile_done_ = true;
    blend_precompile_job_.reset();
    build_job_ = SceneBuilder::beginIncremental(std::move(document), ctx);
}

void Scene2DWallpaper::beginPrewarmLoaded(EngineContext& ctx) {
    load_complete_ = false;
    load_failed_ = false;
    load_applied_ = true;
    blend_precompile_done_ = true;
    blend_precompile_job_.reset();
    startPrewarm(ctx);
}

void Scene2DWallpaper::startPrewarm(EngineContext& ctx) {
    runtime_->beginPrewarm();
    prewarm_width_ = surface::width();
    prewarm_height_ = surface::height();
    std::vector<int> blend_modes;
    for (const Layer* layer : ctx.scene.layers) {
        if (const auto* image = dynamic_cast<const ImageLayer*>(layer)) blend_modes.push_back(image->color_blend_mode);
    }
    if (!blend_modes.empty() && ctx.asset_mgr) {
        blend_precompile_job_ =
            renderer_begin_blend_pipeline_precompile(*ctx.asset_mgr, vfs::currentPackage(), &ctx.renderer, blend_modes);
        blend_precompile_done_ = !blend_precompile_job_;
    }
}

bool Scene2DWallpaper::stepLoad(std::chrono::milliseconds budget, EngineContext& ctx) {
    ScriptEngine::instance().setActiveScope(nullptr);
    if (load_complete_) return true;
    const auto deadline = std::chrono::steady_clock::now() + budget;
    if (load_failed_) return false;
    if (load_applied_ && (prewarm_width_ != surface::width() || prewarm_height_ != surface::height())) {
        runtime_->updateViewport();
        runtime_->beginPrewarm();
        prewarm_width_ = surface::width();
        prewarm_height_ = surface::height();
    }
    if (!load_applied_) {
        if (!build_job_ || !build_job_->step(budget)) return false;
        ParsedScene parsed = build_job_->takeResult();
        build_job_.reset();
        if (!applyParsedScene(std::move(parsed), ctx, false)) {
            load_failed_ = true;
            return false;
        }
        load_applied_ = true;
        startPrewarm(ctx);
    }
    const auto now = std::chrono::steady_clock::now();
    if (now >= deadline) return false;
    const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now);
    if (!runtime_->stepPrewarm(remaining)) return false;
    if (!blend_precompile_done_ && blend_precompile_job_) {
        blend_precompile_done_ =
            renderer_poll_blend_pipeline_precompile(blend_precompile_job_, &ctx.renderer, deadline);
        if (!blend_precompile_done_) return false;
        blend_precompile_job_.reset();
    }
    load_complete_ = true;
    return true;
}

bool Scene2DWallpaper::stepCleanup(std::chrono::milliseconds budget) {
    ScriptEngine::instance().setActiveScope(nullptr);
    if (cleanup_done_) return true;
    blend_precompile_job_.reset();
    if (build_job_) {
        build_job_->cancel();
        if (!build_job_->step(budget)) return false;
        build_job_.reset();
    }
    if (runtime_ && !runtime_->stepCleanup()) return false;
    cleanup_done_ = true;
    load_complete_ = false;
    return true;
}

void Scene2DWallpaper::update(float dt, EngineContext& ctx) {
    ctx.web_frame_transport = "none";
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
    build_job_.reset();
    blend_precompile_job_.reset();
    cleanup_done_ = false;
    if (runtime_) {
        LOG_TAG_I("SCENE_2D", "Clearing 2D wallpaper runtime...");
        runtime_->cleanup();
    }
}
