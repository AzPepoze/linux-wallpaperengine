#define SOKOL_GLCORE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "../libs/cJSON.h"
#include "../libs/sokol/sokol_app.h"
#include "../libs/sokol/sokol_args.h"
#include "../libs/sokol/sokol_gfx.h"
#include "../libs/sokol/sokol_glue.h"
#include "../libs/sokol/sokol_imgui.h"
#include "../libs/sokol/sokol_log.h"
#include "asset/scene_loader.h"
#include "core/context.h"
#include "core/logger.h"
#include "core/utils.h"
#include "imgui.h"
#include "ui/debugger.h"

static void init(void) {
    detect_engine_path(state.engine_path, sizeof(state.engine_path));

    sg_desc s_desc = {};
    s_desc.environment = sglue_environment();
    s_desc.logger.func = slog_func;
    sg_setup(&s_desc);

    Debugger::init();

    state.pass_action.colors[0].load_action = SG_LOADACTION_CLEAR;
    state.pass_action.colors[0].clear_value = {0.0f, 0.0f, 0.0f, 1.0f};

    scene_loader_init();

    state.show_ui = true;
    state.selected_object = -1;
    state.scaling_mode = sargs_exists("cover") ? SCALING_COVER : SCALING_FIT;

    if (state.wallpaper_path[0] != '\0') {
        scene_loader_load(state.wallpaper_path);
    }
    LOG_I("Linux Wallpaper Engine (C Port) Initialized");
}

static void frame(void) {
    scene_loader_update_viewport();

    float target_px = (state.mouse_x / (float)sapp_width() - 0.5f) * 2.0f;
    float target_py = (state.mouse_y / (float)sapp_height() - 0.5f) * 2.0f;
    state.parallax_smooth_x += (target_px - state.parallax_smooth_x) * 0.1f;
    state.parallax_smooth_y += (target_py - state.parallax_smooth_y) * 0.1f;

    sg_pass pass = {};
    pass.action = state.pass_action;
    pass.swapchain = sglue_swapchain();
    sg_begin_pass(&pass);

    scene_loader_draw();
    Debugger::draw();

    sg_end_pass();
    sg_commit();
}

static void event(const sapp_event* e) {
    if (simgui_handle_event(e)) return;
    if (e->type == SAPP_EVENTTYPE_KEY_DOWN) {
        if (e->key_code == SAPP_KEYCODE_F8) state.show_ui = !state.show_ui;
    }
    if (e->type == SAPP_EVENTTYPE_MOUSE_MOVE) {
        state.mouse_x = e->mouse_x;
        state.mouse_y = e->mouse_y;
    }
}

static void cleanup(void) {
    scene_loader_cleanup();
    simgui_shutdown();
    sargs_shutdown();
    sg_shutdown();
}

extern "C" sapp_desc sokol_main(int argc, char* argv[]) {
    sargs_desc a_desc = {};
    a_desc.argc = argc;
    a_desc.argv = argv;
    sargs_setup(&a_desc);

    if (sargs_exists("pkg"))
        strncpy(state.wallpaper_path, sargs_value("pkg"), sizeof(state.wallpaper_path) - 1), state.is_pkg = true;
    else if (argc > 1 && argv[argc - 1][0] != '-')
        strncpy(state.wallpaper_path, argv[argc - 1], sizeof(state.wallpaper_path) - 1);

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
