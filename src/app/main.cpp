#define SOKOL_VULKAN
#include <malloc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>

#include "app/cli_options.h"
#include "app/control/control_client.h"
#include "app/control/control_endpoint.h"
#include "app/control/control_server.h"
#include "app/frame_limiter.h"
#include "app/frame_loop.h"
#if LWE_LAYER_SHELL
#include "app/platform/wayland_layer/layer_app.h"
#endif
#include "app/package_extractor.h"
#include "app/signals.h"
#include "shared/assets/media/media_source.h"
#include "shared/assets/shared_assets.h"
#include "shared/audio/audio_engine.h"
#include "shared/core/build_config.h"
#include "shared/core/engine_context.h"
#include "shared/core/logger.h"
#include "shared/core/phase_timer.h"
#include "shared/core/utils.h"
#include "shared/graphics/backend/gpu_device_manager.h"
#include "shared/graphics/backend/sokol/sokol_sync.h"
#include "shared/graphics/backend/surface.h"
#include "sokol_app.h"
#include "sokol_gfx.h"
#include "sokol_glue.h"
#include "sokol_log.h"
#include "sokol_time.h"
#include "wallpaper/2d/script/script_engine.h"
#include "wallpaper/2d/camera/parallax.h"
#include "wallpaper/2d/scene_2d_wallpaper.h"
#include "wallpaper/project_info.h"
#include "wallpaper/transition/transition_catalog.h"
#include "wallpaper/wallpaper_manager.h"

#if DEBUG_BUILD
#include "shared/graphics/diagnostics/render_diagnostics.h"
#include "ui/debugger.h"
#include "util/sokol_imgui.h"
#endif

namespace {
WallpaperManager wallpaper_mgr;
CliOptions cli;
WallpaperSource wallpaper_source;
ControlServer control_server;
}  // namespace

static EngineContext ctx;
static bool layer_active = false;

// Process-wide shared assets and the active wallpaper's asset manager. The
// manager becomes per-instance in the next task; for now it is a single owner.
static AssetManager asset_manager;
static SharedAssets shared_assets;

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

static void logActiveGpu() {
    const auto& active_gpu = GpuDeviceManager::instance().getSelectedGpu();
    LOG_I("[GPU] Active GPU [%u]: %s (%s, PCI: %s, DRM: %s, VA-API: %s)", active_gpu.index, active_gpu.name.c_str(),
          active_gpu.device_type.c_str(), active_gpu.pci_bus_id.empty() ? "N/A" : active_gpu.pci_bus_id.c_str(),
          active_gpu.drm_render_node.empty() ? "N/A" : active_gpu.drm_render_node.c_str(),
          active_gpu.vaapi_supported ? "Supported" : "N/A");
}

static void initAudio() {
    if (cli.no_audio) {
        LOG_TAG_I("AUDIO", "audio disabled");
        AudioEngine::instance().setAudioDisabled(true);
    } else {
        AudioEngine::instance().init();
    }
}

static void initGraphics() {
    sg_desc s_desc = {};
    s_desc.environment = surface::environment();
    s_desc.logger.func = slog_func;
    s_desc.buffer_pool_size = 2048;
    s_desc.image_pool_size = 2048;
    s_desc.view_pool_size = 4096;
    s_desc.shader_pool_size = 1024;
    s_desc.pipeline_pool_size = 1024;
    s_desc.uniform_buffer_size = 64 * 1024 * 1024;
    s_desc.vulkan.descriptor_buffer_size = 64 * 1024 * 1024;
    s_desc.vulkan.stream_staging_buffer_size = 64 * 1024 * 1024;
    sg_setup(&s_desc);
}

static void applyCliToContext() {
    ctx.pass_action.colors[0].load_action = SG_LOADACTION_CLEAR;
    ctx.pass_action.colors[0].clear_value = {0.0f, 0.0f, 0.0f, 1.0f};

    ctx.debug.show_ui = DEBUG_BUILD && !cli.no_ui;
    ctx.debug.selected_object = -1;
    ctx.scene.scaling_mode = cli.cover || cli.scaling == "fill" ? SCALING_COVER : SCALING_FIT;
    if (cli.scaling == "stretch") LOG_W("--scaling stretch is not supported yet; using fit");
    if (layer_active) ctx.debug.show_ui = false;
    ctx.debug.particle_debug_bounds = cli.particle_debug_bounds;
    ctx.debug.particle_debug_velocity = cli.particle_debug_velocity;
    if (cli.particle_debug_velocity_scale > 0.0f)
        ctx.debug.particle_debug_velocity_scale = cli.particle_debug_velocity_scale;
    if (cli.particle_debug_max_particles > 0) ctx.debug.particle_debug_max_particles = cli.particle_debug_max_particles;

    int transition = 0;
    int transition_duration = cli.transition_duration_ms;
    std::string transition_error;
    if (!lwe::transition::resolveTransitionSetting(cli.transition, transition, transition_duration, transition_error))
        LOG_W("[CONTROL] %s; using fade", transition_error.c_str());
    TransitionConfig transition_config;
    transition_config.selection = transition;
    transition_config.duration_ms = transition_duration;
    bool continue_previous = false;
    std::string transition_mode_error;
    if (!lwe::transition::resolveTransitionModeSetting(cli.transition_mode, continue_previous, transition_mode_error))
        LOG_W("[CONTROL] %s; using freeze", transition_mode_error.c_str());
    transition_config.continue_previous = continue_previous;
    wallpaper_mgr.setTransitionConfig(transition_config);
}

static void loadInitialWallpaper() {
    if (isVideoFile(ctx.wallpaper_path)) {
        wallpaper_mgr.load(ctx.wallpaper_path, ctx);
        return;
    }
    strcpy(ctx.asset_root, "extracted");
    std::string asset_root;
    {
        PhaseTimer timer("package mount / extract");
        asset_root = prepareAssetRoot(wallpaper_source);
    }
    strncpy(ctx.asset_root, asset_root.c_str(), sizeof(ctx.asset_root) - 1);
    wallpaper_mgr.load(ctx.asset_root, ctx);
}

static void init(void) {
    logger_init(DEBUG_BUILD ? LOG_LEVEL_DEBUG : LOG_LEVEL_INFO);
#if DEBUG_BUILD
    // Distinguish this test binary in kernel GPU-fault logs ("comm" field)
    // from other concurrently running wallpaper engine instances.
    prctl(PR_SET_NAME, "lwe-debug-repo", 0, 0, 0);
#endif
    installSignalHandlers();
    stm_setup();
    {
        PhaseTimer timer("GPU device selection");
        GpuDeviceManager::instance().init();
    }
    logActiveGpu();

    const bool engine_from_cli =
        !cli.assets_dir.empty() &&
        engine_path_from_assets_dir(cli.assets_dir.c_str(), ctx.engine_path, sizeof(ctx.engine_path));
    if (engine_from_cli) {
        LOG_I("Using Wallpaper Engine assets from --assets-dir: %s", ctx.engine_path);
    } else if (!detect_engine_path(ctx.engine_path, sizeof(ctx.engine_path))) {
        LOG_E("A Wallpaper Engine installation with its original assets is required");
        exit(EXIT_FAILURE);
    }
    ctx.asset_mgr = &asset_manager;
    shared_assets.engine_path = ctx.engine_path;
    shared_assets.engine_provider = std::make_unique<EngineAssetProvider>(ctx.engine_path);
    shared_assets.internal_provider = std::make_unique<InternalAssetProvider>();
    shared_assets.decode_cache = std::make_unique<TextureDecodeCache>();
    ctx.asset_mgr->attachShared(&shared_assets);
    wallpaper_mgr.setSharedAssets(&shared_assets);

    wallpaper_engine::setVideoLoadInRam(cli.video_ram);
    ScriptEngine::instance().setProfiling(cli.script_profile);
    {
        PhaseTimer timer("audio init");
        initAudio();
    }
#if DEBUG_BUILD
    RenderDiagnostics::instance().init(cli.diagnostics);
#endif
    {
        PhaseTimer timer("graphics init");
        initGraphics();
    }
#if DEBUG_BUILD
    Debugger::init();
#endif
    applyCliToContext();

#if DEBUG_BUILD
    if (ctx.runtime_mode == RuntimeMode::Sandbox) {
        Debugger::startSandbox(ctx, loadSandboxPreviewScene);
        LOG_I("Wallpaper Engine sandbox initialized");
        return;
    }
#endif

    if (ctx.wallpaper_path[0] != '\0') {
        PhaseTimer timer("wallpaper load (total)");
        loadInitialWallpaper();
    }
    LOG_I("Linux Wallpaper Engine Initialized");
}

static void frame(void) {
    runFrame(ctx, wallpaper_mgr);
    limitFrameRate(cli.fps_limit);
}

static void event(const sapp_event* e) {
    handleAppEvent(e, ctx, wallpaper_mgr);
}

static void cleanup(void) {
    LOG_I("[APP] Shutting down");
    control_server.close();
    lwe_vk_wait_idle();

#if DEBUG_BUILD
    RenderDiagnostics::instance().shutdown(terminationRequested());
#endif

    wallpaper_mgr.clear(ctx);
    wallpaper_mgr.shutdownTransition();
    AudioEngine::instance().shutdown();
    renderer_cleanup(&ctx.renderer);

#if DEBUG_BUILD
    simgui_shutdown();
#endif

    lwe_vk_wait_idle();
    sg_shutdown();

    LOG_I("[APP] Shutdown complete");
}

static void selectRequestedGpu() {
    GpuDeviceManager::instance().init();
    if (!cli.gpu.empty() && !GpuDeviceManager::instance().selectGpu(cli.gpu.c_str())) exit(1);
    if (cli.list_gpus) {
        GpuDeviceManager::instance().printGpuList();
        exit(0);
    }
}

static bool wantsDesktopLayer() {
    return !cli.screen_root.empty() || !cli.layer.empty();
}

#if LWE_LAYER_SHELL
static void runDesktopLayerIfPossible() {
    if (!wantsDesktopLayer() || cli.sandbox) return;
    std::unique_ptr<LayerApp> app = LayerApp::create(cli);
    if (!app) {
        LOG_W("[LAYER] desktop layer unavailable; running in a window");
        return;
    }
    layer_active = true;
    const int code = app->run({init, frame, event, cleanup});
    app.reset();
    exit(code);
}
#else
static void runDesktopLayerIfPossible() {
    if (wantsDesktopLayer()) LOG_W("--screen-root/--layer need a build with --layer_shell=y; running in a window");
}
#endif

extern "C" sapp_desc lwe_app_descriptor(int argc, char* argv[]) {
    // Worker threads each get their own malloc arena by default, and memory freed on one rarely goes back to the OS;
    // two arenas keep loading parallel without leaving tens of MB resident.
    mallopt(M_ARENA_MAX, 2);
    logger_init(LOG_LEVEL_DEBUG);
    cli = CliOptions::parse(argc, argv);
    selectRequestedGpu();

    if (cli.sandbox) ctx.runtime_mode = RuntimeMode::Sandbox;
    ctx.cli_properties = cli.set_properties;
    wallpaper_source = resolveWallpaperSource(cli);
    strncpy(ctx.wallpaper_path, wallpaper_source.path.c_str(), sizeof(ctx.wallpaper_path) - 1);
    ctx.is_pkg = wallpaper_source.is_pkg;

    if (cli.extract_only && !wallpaper_source.path.empty()) exit(runExtractOnly(wallpaper_source, cli));

    // Hand a switch to an existing instance on this display, or become that
    // instance. Must run before any GPU work so a handoff process stays cheap.
    if (!cli.no_control && !cli.sandbox) {
        const std::string key = controlKey(cli.screen_root, cli.layer);
        if (!wallpaper_source.path.empty()) {
            int transition = 0;
            int duration = cli.transition_duration_ms;
            std::string resolve_error;
            lwe::transition::resolveTransitionSetting(cli.transition, transition, duration, resolve_error);

            SwitchRequest request;
            request.path = wallpaper_source.path;
            request.is_pkg = wallpaper_source.is_pkg;
            request.properties = cli.set_properties;
            request.transition = transition;
            request.transition_time_ms = duration;

            bool continue_previous = false;
            std::string mode_error;
            if (!lwe::transition::resolveTransitionModeSetting(cli.transition_mode, continue_previous, mode_error))
                LOG_W("[CONTROL] %s; using freeze", mode_error.c_str());
            request.continue_previous = continue_previous;

            std::string handoff_error;
            if (ControlClient::tryHandoff(key, request, &handoff_error)) {
                LOG_I("[CONTROL] handed off wallpaper switch for %s: %s", key.c_str(), request.path.c_str());
                exit(0);
            }
            LOG_D("[CONTROL] no live instance on %s (%s)", key.c_str(), handoff_error.c_str());
        }

        if (control_server.bind(key)) {
            wallpaper_mgr.setControlServer(&control_server);
        } else {
            LOG_W("[CONTROL] another instance owns %s; running without runtime control", key.c_str());
        }
    }

    runDesktopLayerIfPossible();

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
    desc.enable_clipboard = true;
    return desc;
}

#if DEBUG_BUILD
extern "C" sapp_desc sokol_main(int argc, char* argv[]) {
    return lwe_app_descriptor(argc, argv);
}
#endif
