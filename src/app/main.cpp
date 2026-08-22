#define SOKOL_VULKAN
#include <cjson/cJSON.h>
#include <dirent.h>
#include <libgen.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "shared/assets/unpack.h"
#include "shared/core/build_config.h"
#include "shared/core/config.h"
#include "shared/core/context.h"
#include "shared/core/logger.h"
#include "shared/core/utils.h"
#include "shared/graphics/backend/gpu_device_manager.h"
#include "sokol_app.h"
#include "sokol_args.h"
#include "sokol_gfx.h"
#include "sokol_glue.h"
#include "sokol_log.h"
#include "sokol_time.h"
#include "wallpaper/2d/camera/parallax.h"
#include "wallpaper/2d/scene_2d_wallpaper.h"
#include "wallpaper/video/video_wallpaper.h"
#include "wallpaper/wallpaper_manager.h"

#if DEBUG_BUILD
#include "shared/graphics/diagnostics/render_diagnostics.h"
#include "ui/debugger.h"
#include "util/sokol_imgui.h"
#endif

namespace {
WallpaperManager wallpaper_mgr;
}  // namespace

static EngineContext ctx;

#if DEBUG_BUILD
static bool loadSandboxPreviewScene(const char* scene_path) {
    if (!scene_path) return false;

    char scene_directory[1024] = {};
    strncpy(scene_directory, scene_path, sizeof(scene_directory) - 1);
    char* separator = strrchr(scene_directory, '/');
    if (!separator) return false;
    *separator = '\0';
    return wallpaper_mgr.load(scene_directory, ctx);
}
#endif

static void init(void) {
    stm_setup();
    GpuDeviceManager::instance().init();
    const auto& active_gpu = GpuDeviceManager::instance().getSelectedGpu();
    LOG_I("[GPU] Active GPU [%u]: %s (%s, PCI: %s, DRM: %s, VA-API: %s)", active_gpu.index, active_gpu.name.c_str(),
          active_gpu.device_type.c_str(), active_gpu.pci_bus_id.empty() ? "N/A" : active_gpu.pci_bus_id.c_str(),
          active_gpu.drm_render_node.empty() ? "N/A" : active_gpu.drm_render_node.c_str(),
          active_gpu.vaapi_supported ? "Supported" : "N/A");

    if (!detect_engine_path(ctx.engine_path, sizeof(ctx.engine_path))) {
        LOG_E("A Wallpaper Engine installation with its original assets is required");
        exit(EXIT_FAILURE);
    }
    ctx.asset_mgr.init(ctx.engine_path, ctx.wallpaper_path[0] ? ctx.wallpaper_path : "extracted");
#if DEBUG_BUILD
    const bool enable_diagnostics = sargs_exists("diagnose") || sargs_exists("--diagnose") ||
                                    sargs_exists("diagnostics") || sargs_exists("--diagnostics");
    RenderDiagnostics::instance().init(enable_diagnostics);
#endif

    sg_desc s_desc = {};
    s_desc.environment = sglue_environment();
    s_desc.logger.func = slog_func;
    s_desc.image_pool_size = 512;
    s_desc.shader_pool_size = 128;
    s_desc.pipeline_pool_size = 256;
    // A single 4K RGBA video frame needs about 32 MiB. Sokol's default Vulkan
    // streaming staging buffer is 16 MiB, which corrupts per-frame video uploads.
    s_desc.vulkan.stream_staging_buffer_size = 64 * 1024 * 1024;
    sg_setup(&s_desc);

#if DEBUG_BUILD
    Debugger::init();
#endif

    ctx.pass_action.colors[0].load_action = SG_LOADACTION_CLEAR;
    ctx.pass_action.colors[0].clear_value = {0.0f, 0.0f, 0.0f, 1.0f};

    ctx.show_ui = DEBUG_BUILD;
    ctx.selected_object = -1;
    ctx.scaling_mode = sargs_exists("cover") ? SCALING_COVER : SCALING_FIT;
    ctx.particle_debug_bounds = sargs_exists("particle-debug-bounds") || sargs_exists("particle-debug");
    ctx.particle_debug_velocity = sargs_exists("particle-debug-velocity") || sargs_exists("particle-debug");
    if (sargs_exists("particle-debug-velocity-scale")) {
        const float value = (float)atof(sargs_value("particle-debug-velocity-scale"));
        if (value > 0.0f) ctx.particle_debug_velocity_scale = value;
    }
    if (sargs_exists("particle-debug-max-particles")) {
        const int value = atoi(sargs_value("particle-debug-max-particles"));
        if (value > 0) ctx.particle_debug_max_particles = value;
    }

#if DEBUG_BUILD
    if (ctx.runtime_mode == RuntimeMode::Sandbox) {
        Debugger::startSandbox(ctx, loadSandboxPreviewScene);
        LOG_I("Wallpaper Engine sandbox initialized");
        return;
    }
#endif

    if (ctx.wallpaper_path[0] != '\0') {
        if (WallpaperManager::isVideoFile(ctx.wallpaper_path)) {
            wallpaper_mgr.load(ctx.wallpaper_path, ctx);
        } else {
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
                else {
                    strncpy(ctx.asset_root, ctx.wallpaper_path, sizeof(ctx.asset_root) - 1);
                    ctx.asset_root[sizeof(ctx.asset_root) - 1] = '\0';
                }
            }
            wallpaper_mgr.load(ctx.asset_root, ctx);
        }
    }
    LOG_I("Linux Wallpaper Engine Initialized");
}

static void frame(void) {
#if DEBUG_BUILD
    const uint64_t frame_start = stm_now();
#endif
    ctx.renderer.draw_calls = 0;

#if DEBUG_BUILD
    RenderDiagnostics::instance().onFrameStart(ctx.profiler.frame_index, ctx);
    const uint64_t update_start = stm_now();
#endif

    Scene2DRuntime* runtime = nullptr;
    if (auto* s2d = dynamic_cast<Scene2DWallpaper*>(wallpaper_mgr.getActiveWallpaper())) {
        runtime = s2d->getRuntime();
    }

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
    if (runtime) {
        runtime->updateViewport();
    }

    // Inspector edits are intentionally runtime-only. Rebuild the clear pass
    // every frame so direct and offscreen composition see the same live state.
    ctx.pass_action.colors[0].load_action = ctx.general.clear_enabled ? SG_LOADACTION_CLEAR : SG_LOADACTION_DONTCARE;
    ctx.pass_action.colors[0].clear_value = {ctx.general.clear_color[0], ctx.general.clear_color[1],
                                             ctx.general.clear_color[2], ctx.general.clear_color[3]};
    float dt = (float)sapp_frame_duration();
    ctx.time += dt;

    ctx.asset_mgr.updateVideoTextures(dt, ctx.layers);
    parallax_update(ctx, dt, sapp_width(), sapp_height());
    wallpaper_mgr.update(dt, ctx);

#if DEBUG_BUILD
    ctx.profiler.update_ms = stm_ms(stm_since(update_start));
    const uint64_t render_start = stm_now();
#endif

    const bool offscreen_composition = runtime ? runtime->requiresOffscreenComposition() : false;
    if (offscreen_composition && runtime) runtime->draw();

    sg_pass pass = {};
    pass.action = ctx.pass_action;
    pass.swapchain = sglue_swapchain();
    sg_begin_pass(&pass);

    if (offscreen_composition && runtime)
        runtime->present();
    else if (runtime)
        runtime->draw();

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
#endif
}

static void event(const sapp_event* e) {
    if (e->type == SAPP_EVENTTYPE_MOUSE_MOVE) {
        ctx.mouse_x = e->mouse_x;
        ctx.mouse_y = e->mouse_y;
        ctx.mouse_position_valid = true;
    }

    wallpaper_mgr.handleInput(e, ctx);

#if DEBUG_BUILD
    if (e->type == SAPP_EVENTTYPE_KEY_DOWN && e->key_code == SAPP_KEYCODE_F8) {
        ctx.show_ui = !ctx.show_ui;
        return;
    }
    if (simgui_handle_event(e)) return;
#endif
}

static void cleanup(void) {
    wallpaper_mgr.clear();
    ctx.asset_mgr.clearVideoTextures();
#if DEBUG_BUILD
    simgui_shutdown();
#endif
    sargs_shutdown();
    sg_shutdown();
}

extern "C" sapp_desc lwe_app_descriptor(int argc, char* argv[]) {
    sargs_desc a_desc = {};
    a_desc.argc = argc;
    a_desc.argv = argv;
    a_desc.max_args = 64;
    sargs_setup(&a_desc);

    GpuDeviceManager::instance().init();
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--gpu") == 0 || strcmp(argv[i], "-gpu") == 0) {
            if (i + 1 < argc) GpuDeviceManager::instance().selectGpu(argv[i + 1]);
        } else if (strncmp(argv[i], "--gpu=", 6) == 0) {
            GpuDeviceManager::instance().selectGpu(argv[i] + 6);
        }
    }

    if (sargs_exists("gpu") && sargs_value("gpu")) {
        GpuDeviceManager::instance().selectGpu(sargs_value("gpu"));
    }

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--list-gpus") == 0 || strcmp(argv[i], "-list-gpus") == 0) {
            GpuDeviceManager::instance().printGpuList();
            exit(0);
        }
    }
    if (sargs_exists("list-gpus") || sargs_exists("--list-gpus")) {
        GpuDeviceManager::instance().printGpuList();
        exit(0);
    }

#if DEBUG_BUILD
    if (sargs_exists("sandbox") || sargs_exists("--sandbox")) ctx.runtime_mode = RuntimeMode::Sandbox;
#endif

    if (ctx.runtime_mode != RuntimeMode::Sandbox && sargs_exists("pkg")) {
        strncpy(ctx.wallpaper_path, sargs_value("pkg"), sizeof(ctx.wallpaper_path) - 1);
        ctx.is_pkg = true;
    } else if (ctx.runtime_mode != RuntimeMode::Sandbox && argc > 1 && argv[argc - 1][0] != '-') {
        strncpy(ctx.wallpaper_path, argv[argc - 1], sizeof(ctx.wallpaper_path) - 1);
    } else if (ctx.runtime_mode != RuntimeMode::Sandbox) {
        detect_default_wallpaper(ctx.wallpaper_path, sizeof(ctx.wallpaper_path));
    }

    if (ctx.wallpaper_path[0] != '\0' && !ctx.is_pkg) {
        size_t len = strlen(ctx.wallpaper_path);
        if (len >= 4 && strcmp(ctx.wallpaper_path + len - 4, ".pkg") == 0) ctx.is_pkg = true;
    }

    const bool extract_only = sargs_exists("extract-only") || sargs_exists("--extract-only");
    if (extract_only && ctx.wallpaper_path[0] != '\0') {
        char out_dir[1024] = {};
        if (sargs_exists("extract-dir")) {
            strncpy(out_dir, sargs_value("extract-dir"), sizeof(out_dir) - 1);
        } else {
            char wp_copy[1024];
            strncpy(wp_copy, ctx.wallpaper_path, sizeof(wp_copy) - 1);
            const char* wp_name = basename(wp_copy);
            snprintf(out_dir, sizeof(out_dir), "extracted/%s", wp_name);
        }
        mkdir("extracted", 0755);
        mkdir(out_dir, 0755);

        char pkg_file[1024];
        if (ctx.is_pkg) {
            strncpy(pkg_file, ctx.wallpaper_path, sizeof(pkg_file) - 1);
        } else {
            snprintf(pkg_file, sizeof(pkg_file), "%s/scene.pkg", ctx.wallpaper_path);
        }

        if (access(pkg_file, F_OK) == 0) {
            const bool ok = extract_pkg(pkg_file, out_dir);
            exit(ok ? EXIT_SUCCESS : EXIT_FAILURE);
        } else {
            fprintf(stderr, "extract-only: no scene.pkg found at %s\n", pkg_file);
            exit(EXIT_FAILURE);
        }
    }

    sapp_desc desc = {};
    desc.init_cb = init;
    desc.frame_cb = frame;
    desc.event_cb = event;
    desc.cleanup_cb = cleanup;
    desc.width = 1280;
    desc.height = 720;
    desc.window_title =
        ctx.runtime_mode == RuntimeMode::Sandbox ? "Linux Wallpaper Engine Sandbox" : "Linux Wallpaper Engine";
    desc.icon.sokol_default = true;
    desc.logger.func = slog_func;
    return desc;
}

#if DEBUG_BUILD
extern "C" sapp_desc sokol_main(int argc, char* argv[]) {
    return lwe_app_descriptor(argc, argv);
}
#endif
