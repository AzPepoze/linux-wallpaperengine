#include "app/frame_loop.h"

#include "shared/audio/audio_engine.h"
#include "shared/core/build_config.h"
#include "shared/core/logger.h"
#include "shared/graphics/backend/surface.h"
#include "sokol_gfx.h"
#include "sokol_glue.h"
#include "sokol_time.h"
#include "wallpaper/2d/camera/parallax.h"
#include "wallpaper/2d/input/pointer_input.h"
#include "wallpaper/2d/scene_2d_wallpaper.h"
#include "wallpaper/2d/script/media_events.h"
#include "wallpaper/2d/script/script_engine.h"

#if DEBUG_BUILD
#include "shared/graphics/diagnostics/render_diagnostics.h"
#include "ui/debugger.h"
#include "util/sokol_imgui.h"
#endif

static Scene2DRuntime* activeRuntime(WallpaperManager& mgr) {
    auto* s2d = dynamic_cast<Scene2DWallpaper*>(mgr.getActiveWallpaper());
    return s2d ? s2d->getRuntime() : nullptr;
}

static void updateFrame(EngineContext& ctx, WallpaperManager& mgr, Scene2DRuntime* runtime) {
#if DEBUG_BUILD
    if (ctx.runtime_mode == RuntimeMode::Sandbox && runtime) {
        const SandboxPreviewRect preview_rect = Debugger::sandboxPreviewRect();
        if (preview_rect.width > 0 && preview_rect.height > 0) {
            runtime->setOutputViewport(preview_rect.x, preview_rect.y, preview_rect.width, preview_rect.height);
        } else {
            runtime->resetOutputViewport();
        }
    }
#endif
    if (runtime) runtime->updateViewport();

    if (runtime && ctx.input.mouse_position_valid) {
        const WorldPoint world = screenToWorld(ctx.input.mouse_x, ctx.input.mouse_y, ctx.scene.offset_x,
                                               ctx.scene.offset_y, ctx.scene.render_scale, ctx.scene.scene_h);
        ctx.input.mouse_world_x = world.x;
        ctx.input.mouse_world_y = world.y;
    }

    // Inspector edits are intentionally runtime-only. Rebuild the clear pass
    // every frame so direct and offscreen composition see the same live state.
    ctx.pass_action.colors[0].load_action =
        ctx.scene.general.clear_enabled ? SG_LOADACTION_CLEAR : SG_LOADACTION_DONTCARE;
    ctx.pass_action.colors[0].clear_value = {ctx.scene.general.clear_color[0], ctx.scene.general.clear_color[1],
                                             ctx.scene.general.clear_color[2], ctx.scene.general.clear_color[3]};
    float dt = (float)surface::frameDuration();
#if DEBUG_BUILD
    if (RenderDiagnostics::instance().getConfig().fixed_step) dt = 1.0f / 60.0f;
#endif
    ctx.time += dt;
    AudioEngine::instance().update(dt);
    {
        const AudioEngine::Spectrum& spectrum = AudioEngine::instance().spectrum();
        ScriptEngine& scripts = ScriptEngine::instance();
        scripts.setActiveScope(ctx.scene.scripts);
        scripts.setAudioBands(16, spectrum.bands16_left, spectrum.bands16_right);
        scripts.setAudioBands(32, spectrum.bands32_left, spectrum.bands32_right);
        scripts.setAudioBands(64, spectrum.bands64_left, spectrum.bands64_right);
        scripts.beginFrame(dt, ctx.time, ctx.scene.scene_w, ctx.scene.scene_h, (float)surface::width(),
                           (float)surface::height());
    }

    static wallpaper_engine::MediaScriptBridge media_bridge;
    media_bridge.update();

    ctx.asset_mgr->updateVideoTextures(dt, ctx.scene.layers);
    parallax_update(ctx, dt, surface::width(), surface::height());
    mgr.update(dt, ctx);
}

#if DEBUG_BUILD
static void recordFrameProfile(EngineContext& ctx, uint64_t frame_start) {
    ctx.profiler.frame_ms = stm_ms(stm_since(frame_start));
    ctx.profiler.draw_calls = ctx.renderer.draw_calls;
    ctx.profiler.frame_index++;

    if (ctx.profiler.frame_index == 1) {
        ctx.profiler.frame_avg_ms = ctx.profiler.frame_ms;
    } else {
        ctx.profiler.frame_avg_ms += (ctx.profiler.frame_ms - ctx.profiler.frame_avg_ms) * 0.05;
    }
    if (ctx.profiler.frame_ms > ctx.profiler.frame_peak_ms) ctx.profiler.frame_peak_ms = ctx.profiler.frame_ms;

    ctx.profiler.sample_timer += ctx.profiler.frame_ms * 0.001;
    if (ctx.profiler.sample_timer >= ctx.profiler.sample_interval) {
        ctx.profiler.sample_timer = 0.0;
        ctx.profiler.frame_history[ctx.profiler.history_offset] = static_cast<float>(ctx.profiler.frame_ms);
        ctx.profiler.update_history[ctx.profiler.history_offset] = static_cast<float>(ctx.profiler.update_ms);
        ctx.profiler.render_history[ctx.profiler.history_offset] = static_cast<float>(ctx.profiler.render_ms);
        ctx.profiler.ui_history[ctx.profiler.history_offset] = static_cast<float>(ctx.profiler.ui_ms);
        ctx.profiler.history_offset = (ctx.profiler.history_offset + 1) % profiler_stats_t::HISTORY_SIZE;
    }
}
#endif

void runFrame(EngineContext& ctx, WallpaperManager& mgr) {
#if DEBUG_BUILD
    const uint64_t frame_start = stm_now();
#endif
    ctx.renderer.draw_calls = 0;

    // Runtime switch requests are applied before this frame's scene work so the
    // new wallpaper is the one updated and rendered.
    mgr.pollControl(ctx);
    mgr.beginPendingSwitch(ctx);

#if DEBUG_BUILD
    RenderDiagnostics::instance().onFrameStart(ctx.profiler.frame_index, ctx);
    const uint64_t update_start = stm_now();
#endif

    Scene2DRuntime* runtime = activeRuntime(mgr);
    updateFrame(ctx, mgr, runtime);
    mgr.updateTransition((float)surface::frameDuration());
    // continue mode: step/render the outgoing instance and feed the live source
    // before the incoming instance draws.
    if (mgr.isTransitioning()) mgr.stepOutgoingForTransition(ctx, (float)surface::frameDuration());
    if (runtime && mgr.isTransitioning())
        runtime->setForceOffscreen(true);
    else if (runtime)
        runtime->setForceOffscreen(false);

#if DEBUG_BUILD
    ctx.profiler.update_ms = stm_ms(stm_since(update_start));
    const uint64_t render_start = stm_now();
#endif

    const bool offscreen_composition =
        runtime ? (runtime->requiresOffscreenComposition() || mgr.isTransitioning()) : false;
    if (offscreen_composition && runtime) runtime->draw();

#if DEBUG_BUILD
    // Capture the transition overlay into the diagnostics bundle before the
    // swapchain pass starts (a pass cannot be nested).
    if (mgr.isTransitioning() && RenderDiagnostics::instance().isCapturingFrame()) mgr.captureTransitionStage(ctx);
#endif

    sg_pass pass = {};
    pass.action = ctx.pass_action;
    pass.swapchain = surface::acquireSwapchain();
    sg_begin_pass(&pass);

    if (offscreen_composition && runtime)
        runtime->present();
    else if (runtime)
        runtime->draw();

    if (mgr.isTransitioning()) mgr.compositeTransition(ctx);

    if (runtime) runtime->drawParticleDiagnostics();

#if DEBUG_BUILD
    ctx.profiler.render_ms = stm_ms(stm_since(render_start));

    const uint64_t ui_start = stm_now();
    Debugger::draw(ctx);
    ctx.profiler.ui_ms = stm_ms(stm_since(ui_start));
#endif

    sg_end_pass();
    sg_commit();

#if DEBUG_BUILD
    RenderDiagnostics::instance().onFrameEnd(ctx.profiler.frame_index, ctx);
    recordFrameProfile(ctx, frame_start);
#endif
}

void handleAppEvent(const sapp_event* e, EngineContext& ctx, WallpaperManager& mgr) {
    if (e->type == SAPP_EVENTTYPE_MOUSE_MOVE) {
        ctx.input.mouse_x = e->mouse_x;
        ctx.input.mouse_y = e->mouse_y;
        ctx.input.mouse_position_valid = true;
    } else if (e->type == SAPP_EVENTTYPE_MOUSE_DOWN || e->type == SAPP_EVENTTYPE_MOUSE_UP) {
        if (e->mouse_button <= SAPP_MOUSEBUTTON_MIDDLE) {
            const uint8_t bit = static_cast<uint8_t>(1u << e->mouse_button);
            if (e->type == SAPP_EVENTTYPE_MOUSE_DOWN)
                ctx.input.buttons |= bit;
            else
                ctx.input.buttons &= static_cast<uint8_t>(~bit);
        }
    } else if (e->type == SAPP_EVENTTYPE_MOUSE_LEAVE) {
        ctx.input.mouse_position_valid = false;
    } else if (e->type == SAPP_EVENTTYPE_QUIT_REQUESTED) {
        LOG_I("[APP] Received quit request from window/system");
    } else if (e->type == SAPP_EVENTTYPE_RESIZED) {
        LOG_I("[APP] Window resized: window=%dx%d, framebuffer=%dx%d", e->window_width, e->window_height,
              e->framebuffer_width, e->framebuffer_height);
    } else if (e->type == SAPP_EVENTTYPE_SUSPENDED || e->type == SAPP_EVENTTYPE_ICONIFIED) {
        LOG_I("[APP] Application %s", e->type == SAPP_EVENTTYPE_SUSPENDED ? "suspended" : "iconified");
        mgr.pause();
    } else if (e->type == SAPP_EVENTTYPE_RESUMED || e->type == SAPP_EVENTTYPE_RESTORED) {
        LOG_I("[APP] Application %s", e->type == SAPP_EVENTTYPE_RESUMED ? "resumed" : "restored");
        mgr.resume();
    }

    mgr.handleInput(e, ctx);

#if DEBUG_BUILD
    if (e->type == SAPP_EVENTTYPE_KEY_DOWN && e->key_code == SAPP_KEYCODE_F8) {
        ctx.debug.show_ui = !ctx.debug.show_ui;
        return;
    }
    if (simgui_handle_event(e)) return;
#endif
}
