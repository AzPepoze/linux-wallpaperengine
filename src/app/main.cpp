#define SOKOL_VULKAN
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "assets/unpack.h"
#include "core/config.h"
#include "core/context.h"
#include "core/logger.h"
#include "core/utils.h"
#include "imgui.h"
#include "render/diagnostics/render_diagnostics.h"
#include "sokol_app.h"
#include "sokol_args.h"
#include "sokol_gfx.h"
#include "sokol_glue.h"
#include "sokol_imgui.h"
#include "sokol_log.h"
#include "sokol_time.h"
#include "ui/debugger.h"
#include "wallpaper/scene/2d/parallax.h"
#include "wallpaper/scene/2d/scene_2d.h"
#include "wallpaper/scene/2d/scene_builder.h"

static EngineContext ctx;
static Scene2DRuntime* scene_engine = nullptr;

static void init(void) {
    stm_setup();
    detect_engine_path(ctx.engine_path, sizeof(ctx.engine_path));
    RenderDiagnostics::instance().init();

    sg_desc s_desc = {};
    s_desc.environment = sglue_environment();
    s_desc.logger.func = slog_func;
    s_desc.image_pool_size = 512;
    s_desc.view_pool_size = 1024;
    s_desc.shader_pool_size = 128;
    s_desc.pipeline_pool_size = 256;
    sg_setup(&s_desc);

    Debugger::init();

    ctx.pass_action.colors[0].load_action = SG_LOADACTION_CLEAR;
    ctx.pass_action.colors[0].clear_value = {0.0f, 0.0f, 0.0f, 1.0f};

    ctx.show_ui = true;
    ctx.selected_object = -1;
    ctx.scaling_mode = sargs_exists("cover") ? SCALING_COVER : SCALING_FIT;

    scene_engine = new Scene2DRuntime(ctx);
    scene_engine->init();

    if (ctx.wallpaper_path[0] != '\0') {
        mkdir("extracted", 0755);
        strcpy(ctx.asset_root, "extracted");
        ctx.asset_mgr.init(ctx.engine_path, ctx.wallpaper_path);

        if (ctx.is_pkg)
            extract_pkg(ctx.wallpaper_path, "extracted");
        else {
            char pkg_file[1024];
            snprintf(pkg_file, sizeof(pkg_file), "%s/scene.pkg", ctx.wallpaper_path);
            if (access(pkg_file, F_OK) == 0)
                extract_pkg(pkg_file, "extracted");
            else
                strncpy(ctx.asset_root, ctx.wallpaper_path, sizeof(ctx.asset_root) - 1);
        }
        ctx.asset_mgr.init(ctx.engine_path, ctx.asset_root);

        char scene_path[1024];
        snprintf(scene_path, sizeof(scene_path), "%s/scene.json", ctx.asset_root);

        ParsedScene parsed = SceneBuilder::load(scene_path, ctx);
        ctx.layers = std::move(parsed.layers);
        ctx.scene_graph = parsed.scene_graph;
        ctx.scene_w = parsed.design_width;
        ctx.scene_h = parsed.design_height;
        ctx.camera_parallax_enabled = parsed.camera_parallax_enabled;
        ctx.camera_parallax_amount = parsed.camera_parallax_amount;
        ctx.camera_parallax_delay = parsed.camera_parallax_delay;
        ctx.camera_parallax_mouse_influence = parsed.camera_parallax_mouse_influence;
        if (parsed.has_clear_color) {
            ctx.pass_action.colors[0].load_action = SG_LOADACTION_CLEAR;
            ctx.pass_action.colors[0].clear_value = {parsed.clear_color[0], parsed.clear_color[1],
                                                     parsed.clear_color[2], parsed.clear_color[3]};
        }
        scene_engine->updateViewport();
    }
    LOG_I("Linux Wallpaper Engine Initialized");
}

static void frame(void) {
    const uint64_t frame_start = stm_now();
    ctx.renderer.draw_calls = 0;

    RenderDiagnostics::instance().onFrameStart(ctx.profiler.frame_index, ctx);

    const uint64_t update_start = stm_now();
    scene_engine->updateViewport();
    float dt = (float)sapp_frame_duration();
    ctx.time += dt;

    parallax_update(ctx, dt, sapp_width(), sapp_height());
    scene_engine->update(dt);
    ctx.profiler.update_ms = stm_ms(stm_since(update_start));

    sg_pass pass = {};
    pass.action = ctx.pass_action;
    pass.swapchain = sglue_swapchain();
    sg_begin_pass(&pass);

    const uint64_t render_start = stm_now();
    scene_engine->draw();
    ctx.profiler.render_ms = stm_ms(stm_since(render_start));

    const uint64_t ui_start = stm_now();
    Debugger::draw(ctx);
    ctx.profiler.ui_ms = stm_ms(stm_since(ui_start));

    sg_end_pass();
    sg_commit();

    RenderDiagnostics::instance().onFrameEnd(ctx.profiler.frame_index, ctx);

    ctx.profiler.frame_ms = stm_ms(stm_since(frame_start));
    ctx.profiler.draw_calls = ctx.renderer.draw_calls;
    ctx.profiler.frame_index++;

    if (ctx.profiler.frame_index == 1) {
        ctx.profiler.frame_avg_ms = ctx.profiler.frame_ms;
    } else {
        ctx.profiler.frame_avg_ms += (ctx.profiler.frame_ms - ctx.profiler.frame_avg_ms) * 0.05;
    }
    if (ctx.profiler.frame_ms > ctx.profiler.frame_peak_ms) ctx.profiler.frame_peak_ms = ctx.profiler.frame_ms;
}

static void event(const sapp_event* e) {
    if (e->type == SAPP_EVENTTYPE_MOUSE_MOVE) {
        ctx.mouse_x = e->mouse_x;
        ctx.mouse_y = e->mouse_y;
        ctx.mouse_position_valid = true;
    }

    if (simgui_handle_event(e)) return;
    if (e->type == SAPP_EVENTTYPE_KEY_DOWN && e->key_code == SAPP_KEYCODE_F8) ctx.show_ui = !ctx.show_ui;
}

static void cleanup(void) {
    if (scene_engine) {
        scene_engine->cleanup();
        delete scene_engine;
        scene_engine = nullptr;
    }
    delete ctx.scene_graph;
    ctx.scene_graph = nullptr;
    simgui_shutdown();
    sargs_shutdown();
    sg_shutdown();
}

extern "C" sapp_desc sokol_main(int argc, char* argv[]) {
    sargs_desc a_desc = {};
    a_desc.argc = argc;
    a_desc.argv = argv;
    a_desc.max_args = 64;
    sargs_setup(&a_desc);

    if (sargs_exists("pkg")) {
        strncpy(ctx.wallpaper_path, sargs_value("pkg"), sizeof(ctx.wallpaper_path) - 1);
        ctx.is_pkg = true;
    } else if (argc > 1 && argv[argc - 1][0] != '-') {
        strncpy(ctx.wallpaper_path, argv[argc - 1], sizeof(ctx.wallpaper_path) - 1);
    } else {
        detect_default_wallpaper(ctx.wallpaper_path, sizeof(ctx.wallpaper_path));
    }

    if (ctx.wallpaper_path[0] != '\0' && !ctx.is_pkg) {
        size_t len = strlen(ctx.wallpaper_path);
        if (len >= 4 && strcmp(ctx.wallpaper_path + len - 4, ".pkg") == 0) ctx.is_pkg = true;
    }

    sapp_desc desc = {};
    desc.init_cb = init;
    desc.frame_cb = frame;
    desc.event_cb = event;
    desc.cleanup_cb = cleanup;
    desc.width = 1280;
    desc.height = 720;
    desc.window_title = "Linux Wallpaper Engine";
    desc.icon.sokol_default = true;
    desc.logger.func = slog_func;
    return desc;
}
