#define SOKOL_VULKAN
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>

#include "app/cli_options.h"
#include "app/frame_loop.h"
#include "app/package_extractor.h"
#include "app/signals.h"
#include "shared/audio/audio_engine.h"
#include "shared/core/build_config.h"
#include "shared/core/context.h"
#include "shared/core/logger.h"
#include "shared/core/utils.h"
#include "shared/graphics/backend/gpu_device_manager.h"
#include "shared/graphics/backend/sokol/sokol_sync.h"
#include "sokol_app.h"
#include "sokol_gfx.h"
#include "sokol_glue.h"
#include "sokol_log.h"
#include "sokol_time.h"
#include "wallpaper/2d/camera/parallax.h"
#include "wallpaper/2d/scene_2d_wallpaper.h"
#include "wallpaper/project_info.h"
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
    s_desc.environment = sglue_environment();
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

    ctx.show_ui = DEBUG_BUILD && !cli.no_ui;
    ctx.selected_object = -1;
    ctx.scaling_mode = cli.cover ? SCALING_COVER : SCALING_FIT;
    ctx.particle_debug_bounds = cli.particle_debug_bounds;
    ctx.particle_debug_velocity = cli.particle_debug_velocity;
    if (cli.particle_debug_velocity_scale > 0.0f) ctx.particle_debug_velocity_scale = cli.particle_debug_velocity_scale;
    if (cli.particle_debug_max_particles > 0) ctx.particle_debug_max_particles = cli.particle_debug_max_particles;
}

static void loadInitialWallpaper() {
    if (isVideoFile(ctx.wallpaper_path)) {
        wallpaper_mgr.load(ctx.wallpaper_path, ctx);
        return;
    }
    strcpy(ctx.asset_root, "extracted");
    ctx.asset_mgr.init(ctx.engine_path, ctx.wallpaper_path);
    const std::string asset_root = prepareAssetRoot(wallpaper_source);
    strncpy(ctx.asset_root, asset_root.c_str(), sizeof(ctx.asset_root) - 1);
    wallpaper_mgr.load(ctx.asset_root, ctx);
}

static void init(void) {
    logger_init(LOG_LEVEL_DEBUG);
#if DEBUG_BUILD
    // Distinguish this test binary in kernel GPU-fault logs ("comm" field)
    // from other concurrently running wallpaper engine instances.
    prctl(PR_SET_NAME, "lwe-debug-repo", 0, 0, 0);
#endif
    installSignalHandlers();
    stm_setup();
    GpuDeviceManager::instance().init();
    logActiveGpu();

    if (!detect_engine_path(ctx.engine_path, sizeof(ctx.engine_path))) {
        LOG_E("A Wallpaper Engine installation with its original assets is required");
        exit(EXIT_FAILURE);
    }
    ctx.asset_mgr.init(ctx.engine_path, ctx.wallpaper_path[0] ? ctx.wallpaper_path : "extracted");

    initAudio();
#if DEBUG_BUILD
    RenderDiagnostics::instance().init(cli.diagnose);
#endif
    initGraphics();
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

    if (ctx.wallpaper_path[0] != '\0') loadInitialWallpaper();
    LOG_I("Linux Wallpaper Engine Initialized");
}

static void frame(void) {
    runFrame(ctx, wallpaper_mgr);
}

static void event(const sapp_event* e) {
    handleAppEvent(e, ctx, wallpaper_mgr);
}

static void cleanup(void) {
    LOG_I("[APP] Shutting down");
    lwe_vk_wait_idle();

#if DEBUG_BUILD
    RenderDiagnostics::instance().shutdown(terminationRequested());
#endif

    wallpaper_mgr.clear();
    ctx.asset_mgr.clearVideoTextures();
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
    if (!cli.gpu.empty()) GpuDeviceManager::instance().selectGpu(cli.gpu.c_str());
    if (cli.list_gpus) {
        GpuDeviceManager::instance().printGpuList();
        exit(0);
    }
}

extern "C" sapp_desc lwe_app_descriptor(int argc, char* argv[]) {
    logger_init(LOG_LEVEL_DEBUG);
    cli = CliOptions::parse(argc, argv);
    selectRequestedGpu();

    if (cli.sandbox) ctx.runtime_mode = RuntimeMode::Sandbox;
    wallpaper_source = resolveWallpaperSource(cli);
    strncpy(ctx.wallpaper_path, wallpaper_source.path.c_str(), sizeof(ctx.wallpaper_path) - 1);
    ctx.is_pkg = wallpaper_source.is_pkg;

    if (cli.extract_only && !wallpaper_source.path.empty()) exit(runExtractOnly(wallpaper_source, cli));

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
