#define SOKOL_VULKAN
#include <cjson/cJSON.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "assets/unpack.h"
#include "core/build_config.h"
#include "core/config.h"
#include "core/context.h"
#include "core/gpu_device_manager.h"
#include "core/logger.h"
#include "core/utils.h"
#include "sokol_app.h"
#include "sokol_args.h"
#include "sokol_gfx.h"
#include "sokol_glue.h"
#include "sokol_log.h"
#include "sokol_time.h"
#include "wallpaper/scene/2d/parallax.h"
#include "wallpaper/scene/2d/scene_2d.h"
#include "wallpaper/scene/2d/scene_builder.h"

#if DEBUG_BUILD
#include "render/diagnostics/render_diagnostics.h"
#include "ui/debugger.h"
#include "util/sokol_imgui.h"
#endif

namespace {

Scene2DRuntime* scene_engine = nullptr;

bool isVideoFile(const char* path) {
    if (!path) return false;
    const char* ext = strrchr(path, '.');
    if (!ext) return false;
    return (strcasecmp(ext, ".mp4") == 0 || strcasecmp(ext, ".webm") == 0 || strcasecmp(ext, ".mkv") == 0 ||
            strcasecmp(ext, ".avi") == 0 || strcasecmp(ext, ".mov") == 0 || strcasecmp(ext, ".wmv") == 0);
}

}  // namespace

static EngineContext ctx;

static bool applyParsedScene(ParsedScene parsed) {
    if (parsed.layers.empty() && !parsed.scene_tree) return false;

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
    scene_engine->updateViewport();
    return true;
}

static bool loadSceneDirectory(const char* scene_directory) {
    if (!scene_engine || !scene_directory || scene_directory[0] == '\0') return false;

    // 1. Direct video file
    if (isVideoFile(scene_directory) && access(scene_directory, F_OK) == 0) {
        scene_engine->clearScene();
        strncpy(ctx.asset_root, scene_directory, sizeof(ctx.asset_root) - 1);
        ctx.asset_root[sizeof(ctx.asset_root) - 1] = '\0';
        ctx.asset_mgr.init(ctx.engine_path, ctx.asset_root);
        return applyParsedScene(SceneBuilder::buildVideoScene(scene_directory, ctx));
    }

    char scene_path[1024] = {};
    snprintf(scene_path, sizeof(scene_path), "%s/scene.json", scene_directory);
    if (access(scene_path, F_OK) == 0) {
        scene_engine->clearScene();
        strncpy(ctx.asset_root, scene_directory, sizeof(ctx.asset_root) - 1);
        ctx.asset_root[sizeof(ctx.asset_root) - 1] = '\0';
        ctx.asset_mgr.init(ctx.engine_path, ctx.asset_root);
        if (!applyParsedScene(SceneBuilder::load(scene_path, ctx))) {
            LOG_TAG_W("SANDBOX", "Could not parse scene: %s", scene_path);
            return false;
        }
        return true;
    }

    // 2. Check project.json
    char project_path[1024] = {};
    snprintf(project_path, sizeof(project_path), "%s/project.json", scene_directory);
    if (access(project_path, F_OK) == 0) {
        char* json_str = read_file_to_string(project_path);
        if (json_str) {
            cJSON* root = cJSON_Parse(json_str);
            free(json_str);
            if (root) {
                cJSON* file_item = cJSON_GetObjectItemCaseSensitive(root, "file");
                if (cJSON_IsString(file_item) && file_item->valuestring && file_item->valuestring[0] != '\0') {
                    char video_path[1024] = {};
                    snprintf(video_path, sizeof(video_path), "%s/%s", scene_directory, file_item->valuestring);
                    if (access(video_path, F_OK) == 0) {
                        cJSON_Delete(root);
                        scene_engine->clearScene();
                        strncpy(ctx.asset_root, scene_directory, sizeof(ctx.asset_root) - 1);
                        ctx.asset_root[sizeof(ctx.asset_root) - 1] = '\0';
                        ctx.asset_mgr.init(ctx.engine_path, ctx.asset_root);
                        return applyParsedScene(SceneBuilder::buildVideoScene(video_path, ctx));
                    }
                }
                cJSON_Delete(root);
            }
        }
    }

    // 3. Scan directory for video files
    DIR* dir = opendir(scene_directory);
    if (dir) {
        struct dirent* entry = nullptr;
        while ((entry = readdir(dir)) != nullptr) {
            if (isVideoFile(entry->d_name)) {
                char video_path[1024] = {};
                snprintf(video_path, sizeof(video_path), "%s/%s", scene_directory, entry->d_name);
                closedir(dir);
                scene_engine->clearScene();
                strncpy(ctx.asset_root, scene_directory, sizeof(ctx.asset_root) - 1);
                ctx.asset_root[sizeof(ctx.asset_root) - 1] = '\0';
                ctx.asset_mgr.init(ctx.engine_path, ctx.asset_root);
                return applyParsedScene(SceneBuilder::buildVideoScene(video_path, ctx));
            }
        }
        closedir(dir);
    }

    LOG_TAG_W("SANDBOX", "Preview scene or video not found in directory: %s", scene_directory);
    return false;
}

#if DEBUG_BUILD
static bool loadSandboxPreviewScene(const char* scene_path) {
    if (!scene_path) return false;

    char scene_directory[1024] = {};
    strncpy(scene_directory, scene_path, sizeof(scene_directory) - 1);
    char* separator = strrchr(scene_directory, '/');
    if (!separator) return false;
    *separator = '\0';
    return loadSceneDirectory(scene_directory);
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

    scene_engine = new Scene2DRuntime(ctx);
    scene_engine->init();

#if DEBUG_BUILD
    if (ctx.runtime_mode == RuntimeMode::Sandbox) {
        Debugger::startSandbox(ctx, loadSandboxPreviewScene);
        LOG_I("Wallpaper Engine sandbox initialized");
        return;
    }
#endif

    if (ctx.wallpaper_path[0] != '\0') {
        if (isVideoFile(ctx.wallpaper_path)) {
            loadSceneDirectory(ctx.wallpaper_path);
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
                else
                    strncpy(ctx.asset_root, ctx.wallpaper_path, sizeof(ctx.asset_root) - 1);
            }
            loadSceneDirectory(ctx.asset_root);
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
#if DEBUG_BUILD
    if (ctx.runtime_mode == RuntimeMode::Sandbox) {
        const SandboxPreviewRect preview_rect = Debugger::sandboxPreviewRect();
        if (preview_rect.width > 0 && preview_rect.height > 0) {
            scene_engine->setOutputViewport(preview_rect.x, preview_rect.y, preview_rect.width, preview_rect.height);
        }
    } else {
        scene_engine->resetOutputViewport();
    }
#endif
    scene_engine->updateViewport();
    // Inspector edits are intentionally runtime-only.  Rebuild the clear pass
    // every frame so direct and offscreen composition see the same live state.
    ctx.pass_action.colors[0].load_action = ctx.general.clear_enabled ? SG_LOADACTION_CLEAR : SG_LOADACTION_DONTCARE;
    ctx.pass_action.colors[0].clear_value = {ctx.general.clear_color[0], ctx.general.clear_color[1],
                                             ctx.general.clear_color[2], ctx.general.clear_color[3]};
    float dt = (float)sapp_frame_duration();
    ctx.time += dt;

    ctx.asset_mgr.updateVideoTextures(dt, ctx.layers);
    parallax_update(ctx, dt, sapp_width(), sapp_height());
    scene_engine->update(dt);
#if DEBUG_BUILD
    ctx.profiler.update_ms = stm_ms(stm_since(update_start));
    const uint64_t render_start = stm_now();
#endif

    const bool offscreen_composition = scene_engine->requiresOffscreenComposition();
    if (offscreen_composition) scene_engine->draw();

    sg_pass pass = {};
    pass.action = ctx.pass_action;
    pass.swapchain = sglue_swapchain();
    sg_begin_pass(&pass);

    if (offscreen_composition)
        scene_engine->present();
    else
        scene_engine->draw();

    scene_engine->drawParticleDiagnostics();

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
#endif
}

static void event(const sapp_event* e) {
    if (e->type == SAPP_EVENTTYPE_MOUSE_MOVE) {
        ctx.mouse_x = e->mouse_x;
        ctx.mouse_y = e->mouse_y;
        ctx.mouse_position_valid = true;
    }

#if DEBUG_BUILD
    if (e->type == SAPP_EVENTTYPE_KEY_DOWN && e->key_code == SAPP_KEYCODE_F8) {
        ctx.show_ui = !ctx.show_ui;
        return;
    }
    if (simgui_handle_event(e)) return;
#endif
}

static void cleanup(void) {
    if (scene_engine) {
        scene_engine->cleanup();
        delete scene_engine;
        scene_engine = nullptr;
    }
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
