#include "render_diagnostics.h"

#include <sokol_app.h>
#include <sokol_args.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <future>
#include <iomanip>
#include <sstream>

#include "render_diagnostics_internal.h"
#include "shared/core/engine_context.h"
#include "shared/core/logger.h"
#include "shared/graphics/backend/gpu_debug_labels.h"
#include "shared/graphics/backend/gpu_readback.h"
#include "shared/graphics/shader/shader_compiler.h"

namespace fs = std::filesystem;

using namespace render_diagnostics_internal;

RenderDiagnostics& RenderDiagnostics::instance() {
    static RenderDiagnostics s_inst;
    return s_inst;
}

RenderDiagnostics::~RenderDiagnostics() {
    if (worker_thread_.joinable()) {
        worker_thread_.join();
    }
}

void RenderDiagnostics::shutdown(bool cancel_pending) {
    if (cancel_pending) cancel_export_.store(true);
    if (worker_thread_.joinable()) {
        worker_thread_.join();
    }
}

void RenderDiagnostics::init(const DiagnosticOptions& options) {
    config.enabled = options.enabled;
    config.target_frame = 100;
    config.capture_pass_images = true;
    if (!options.enabled) return;

    size_t start = 0;
    const std::string& list = options.disable_effects;
    while (start < list.size()) {
        const size_t comma = list.find(',', start);
        std::string item = list.substr(start, comma == std::string::npos ? std::string::npos : comma - start);
        const size_t first = item.find_first_not_of(" \t");
        const size_t last = item.find_last_not_of(" \t");
        item = first == std::string::npos ? "" : item.substr(first, last - first + 1);
        if (!item.empty()) {
            config.disable_effect_paths.push_back(item);
            effect_log.info("Bisect: disabling effects matching \"%s\"", item.c_str());
        }
        if (comma == std::string::npos) break;
        start = comma + 1;
    }
    if (options.disable_particles) {
        config.disable_particles = true;
        effect_log.info("Bisect: particle rendering disabled");
    }
    if (options.disable_bloom) {
        config.disable_bloom = true;
        effect_log.info("Bisect: bloom rendering disabled");
    }
    if (options.final_only) {
        config.final_only = true;
        effect_log.info("Diagnostic final-only mode: per-pass/per-stage PNG dumps disabled");
    }
    config.exit_after_diagnose = options.exit_after_diagnose;
    effect_log.info("Effect diagnostic mode ENABLED (auto-run on frame: %llu)",
                    (unsigned long long)config.target_frame);
}

bool RenderDiagnostics::isEffectDisabled(int effect_index, const std::string& effect_path) const {
    (void)effect_index;
    if (config.disable_effect_paths.empty()) return false;
    for (const auto& needle : config.disable_effect_paths) {
        if (needle == "*" || needle == "all") return true;
        if (effect_path.find(needle) != std::string::npos) return true;
    }
    return false;
}

std::string RenderDiagnostics::overrideFragmentSource(const std::string& shader_name, const std::string& fs_source,
                                                      int view_mode, int step) const {
    return ShaderCompiler::applyDebugStep(shader_name, ShaderCompiler::applyDebugMode(fs_source, view_mode), step);
}

void RenderDiagnostics::triggerCapture(uint64_t current_frame) {
    config.enabled = true;
    config.target_frame = current_frame + 1;
    config.capture_complete = false;
    is_capturing_frame = false;
    effect_log.info("Manual diagnostic capture triggered for frame: %llu", (unsigned long long)config.target_frame);
}

void RenderDiagnostics::onFrameStart(uint64_t frame_index, EngineContext& ctx) {
    if (!config.enabled || config.capture_complete) return;

    if (config.shouldCaptureFrame(frame_index)) {
        is_capturing_frame = true;
        render_graph_.passes.clear();
        pass_images_.clear();
        has_source_image_ = false;
        has_final_image_ = false;
        scene_stage_index_ = 0;

        std::string wallpaper_name = resolveWallpaperName(ctx);
        config.output_dir = "./diagnostics/" + wallpaper_name;

        std::error_code ec;
        if (fs::exists(config.output_dir, ec)) {
            fs::remove_all(config.output_dir, ec);
        }
        fs::create_directories(config.output_dir, ec);

        if (config.has_deterministic_time) {
            ctx.time = config.deterministic_time;
        }

        effect_log.info(">>> Beginning diagnostic capture for '%s' on frame %llu <<<", wallpaper_name.c_str(),
                        (unsigned long long)frame_index);
    } else {
        is_capturing_frame = false;
    }
}

void RenderDiagnostics::onFrameEnd(uint64_t frame_index, EngineContext& ctx) {
    if (!config.enabled || !is_capturing_frame) return;

    DiagnosticExportPayload payload;
    payload.output_dir = config.output_dir;
    payload.frame_index = frame_index;
    payload.time = ctx.time;
    payload.scene_w = ctx.scene.scene_w;
    payload.scene_h = ctx.scene.scene_h;
    payload.render_scale = ctx.scene.render_scale;
    payload.wallpaper_path = ctx.wallpaper_path;
    payload.engine_path = ctx.engine_path;
    payload.has_deterministic_time = config.has_deterministic_time;
    payload.final_only = config.final_only;

    payload.has_source_image = has_source_image_;
    payload.source_rgba = std::move(source_rgba_);
    payload.source_w = source_w_;
    payload.source_h = source_h_;

    payload.has_final_image = has_final_image_;
    payload.final_rgba = std::move(final_rgba_);
    payload.final_w = final_w_;
    payload.final_h = final_h_;

    payload.pass_images = std::move(pass_images_);

    for (const SceneStageSnapshot& snapshot : scene_stage_snapshots_) {
        if (snapshot.image.id == SG_INVALID_ID) continue;
        GpuImageReadbackResult readback = gpu_readback_image_rgba8(snapshot.image);
        if (readback.success && !readback.rgba_data.empty()) {
            CapturedStageImage stage;
            stage.name = snapshot.name;
            stage.stage_index = snapshot.stage_index;
            stage.width = readback.width;
            stage.height = readback.height;
            stage.rgba_data = std::move(readback.rgba_data);
            payload.stage_images.push_back(std::move(stage));
        }
        if (snapshot.texture_view.id != SG_INVALID_ID) sg_destroy_view(snapshot.texture_view);
        if (snapshot.attachment_view.id != SG_INVALID_ID) sg_destroy_view(snapshot.attachment_view);
        if (snapshot.image.id != SG_INVALID_ID) sg_destroy_image(snapshot.image);
    }
    scene_stage_snapshots_.clear();

    payload.render_graph = render_graph_;
    payload.shader_dumps = shader_dumps_;
    payload.provenance_list = provenance_list_;

    config.capture_complete = true;
    is_capturing_frame = false;

    if (worker_thread_.joinable()) {
        worker_thread_.join();
    }
    worker_thread_ = std::thread(exportBundleAsync, std::move(payload), &cancel_export_);

    if (config.exit_after_diagnose) {
        effect_log.info("Diagnostic capture finished, requesting clean quit");
        sapp_request_quit();
    }
}

void RenderDiagnostics::registerShaderDump(const ShaderDump& dump) {
    shader_dumps_.push_back(dump);
}

void RenderDiagnostics::registerUniformProvenance(const PassUniformProvenance& prov) {
    provenance_list_.push_back(prov);
}

void RenderDiagnostics::onSourceImage(int effect_index, sg_image img, int width, int height) {
    (void)effect_index;
    if (!is_capturing_frame || img.id == SG_INVALID_ID) return;

    GpuImageReadbackResult readback = gpu_readback_image_rgba8(img);
    if (readback.success && !readback.rgba_data.empty()) {
        has_source_image_ = true;
        source_w_ = readback.width;
        source_h_ = readback.height;
        source_rgba_ = std::move(readback.rgba_data);
    } else {
        source_w_ = width;
        source_h_ = height;
    }
}

void RenderDiagnostics::recordPass(PassTraceEntry trace, sg_image out_img) {
    if (!config.enabled) return;

    if (is_capturing_frame && shouldCapturePassImage(trace.effect_index, trace.pass_index) &&
        out_img.id != SG_INVALID_ID) {
        GpuImageReadbackResult readback = gpu_readback_image_rgba8(out_img);
        CapturedPassImage pass_img;
        pass_img.trace = trace;
        if (readback.success && !readback.rgba_data.empty()) {
            pass_img.width = readback.width;
            pass_img.height = readback.height;
            pass_img.rgba_data = std::move(readback.rgba_data);
        }
        pass_images_.push_back(std::move(pass_img));
    } else if (is_capturing_frame) {
        CapturedPassImage pass_img;
        pass_img.trace = trace;
        pass_images_.push_back(std::move(pass_img));
    }
}

void RenderDiagnostics::onLayerFinalImage(int effect_index, sg_image img, int width, int height) {
    (void)effect_index;
    (void)width;
    (void)height;
    if (!is_capturing_frame || img.id == SG_INVALID_ID) return;

    GpuImageReadbackResult readback = gpu_readback_image_rgba8(img);
    if (readback.success && !readback.rgba_data.empty()) {
        has_final_image_ = true;
        final_w_ = readback.width;
        final_h_ = readback.height;
        final_rgba_ = std::move(readback.rgba_data);
    }
}

bool RenderDiagnostics::isEffectIsolated(int effect_index, const std::string& effect_path) const {
    if (config.isolate_effect_index >= 0 && effect_index != config.isolate_effect_index) return false;
    if (!config.isolate_effect_path.empty() && effect_path.find(config.isolate_effect_path) == std::string::npos)
        return false;
    return true;
}

bool RenderDiagnostics::isPassDisabled(int pass_index) const {
    if (config.isolate_pass_index >= 0 && pass_index != config.isolate_pass_index) return true;
    if (config.disable_pass_index >= 0 && pass_index == config.disable_pass_index) return true;
    return false;
}

bool RenderDiagnostics::shouldStopAfterPass(int pass_index) const {
    if (config.stop_after_pass_index >= 0 && pass_index >= config.stop_after_pass_index) return true;
    return false;
}

int RenderDiagnostics::getForcedOutputSlot() const {
    return config.force_output_texture_slot;
}

bool RenderDiagnostics::shouldCapturePassImage(int effect_index, int pass_index) const {
    if (!config.capture_pass_images) return false;
    if (config.capture_effect_index >= 0 && effect_index != config.capture_effect_index) return false;
    if (config.capture_pass_index >= 0 && pass_index != config.capture_pass_index) return false;
    return true;
}

void RenderDiagnostics::recordSceneStage(const std::string& stage_name, sg_image img, sg_view texture_view,
                                         sg_view attachment_view) {
    if (!is_capturing_frame || img.id == SG_INVALID_ID) return;
    scene_stage_snapshots_.push_back({stage_name, img, texture_view, attachment_view, scene_stage_index_++});
}
