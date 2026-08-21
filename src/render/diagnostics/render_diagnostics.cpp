#include "render_diagnostics.h"

#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <iomanip>
#include <sstream>

#include "core/engine_context.h"
#include "core/logger.h"
#include "render/backend/gpu_debug_labels.h"
#include "render/backend/gpu_readback.h"

namespace {
void ensureDir(const std::string& path) {
    size_t pos = 0;
    while ((pos = path.find('/', pos + 1)) != std::string::npos) {
        std::string sub = path.substr(0, pos);
        if (!sub.empty()) mkdir(sub.c_str(), 0755);
    }
    mkdir(path.c_str(), 0755);
}

std::string sanitizeFilename(const std::string& str) {
    std::string clean;
    for (char c : str) {
        if (std::isalnum((unsigned char)c) || c == '_' || c == '-') {
            clean.push_back(c);
        } else {
            clean.push_back('_');
        }
    }
    return clean;
}
}  // namespace

RenderDiagnostics& RenderDiagnostics::instance() {
    static RenderDiagnostics s_inst;
    return s_inst;
}

void RenderDiagnostics::init() {
    config.parseFromArgs();
    if (config.enabled) {
        effect_log.info("Effect diagnostic mode ENABLED (target frame: %llu, output: %s)",
                        (unsigned long long)config.target_frame, config.output_dir.c_str());
    }
}

void RenderDiagnostics::onFrameStart(uint64_t frame_index, EngineContext& ctx) {
    if (!config.enabled || config.capture_complete) return;

    if (config.shouldCaptureFrame(frame_index)) {
        is_capturing_frame = true;
        render_graph_.passes.clear();
        generated_files_.clear();
        has_source_image_ = false;
        has_previous_image_ = false;
        scene_stage_index_ = 0;

        if (config.has_deterministic_time) {
            ctx.time = config.deterministic_time;
        }

        effect_log.info(">>> Beginning diagnostic capture on frame %llu <<<", (unsigned long long)frame_index);
    } else {
        is_capturing_frame = false;
    }
}

void RenderDiagnostics::onFrameEnd(uint64_t frame_index, EngineContext& ctx) {
    if (!config.enabled || !is_capturing_frame) return;

    for (const SceneStageSnapshot& snapshot : scene_stage_snapshots_) {
        if (snapshot.image.id == SG_INVALID_ID) continue;
        GpuImageReadbackResult readback = gpu_readback_image_rgba8(snapshot.image);
        if (readback.success && !readback.rgba_data.empty()) {
            char frame_dir_buf[64];
            snprintf(frame_dir_buf, sizeof(frame_dir_buf), "frame-%04llu", (unsigned long long)frame_index);
            std::string stage_dir = config.output_dir + "/" + frame_dir_buf + "/scene-stages";
            ensureDir(stage_dir);

            char file_buf[256];
            std::string clean_stage = sanitizeFilename(snapshot.name);
            snprintf(file_buf, sizeof(file_buf), "%03d-%s.png", scene_stage_index_++, clean_stage.c_str());
            std::string path = stage_dir + "/" + file_buf;
            if (writePng(path, readback.width, readback.height, readback.rgba_data.data())) {
                generated_files_.push_back(std::string(frame_dir_buf) + "/scene-stages/" + file_buf);
            }
        }
        if (snapshot.texture_view.id != SG_INVALID_ID) sg_destroy_view(snapshot.texture_view);
        if (snapshot.attachment_view.id != SG_INVALID_ID) sg_destroy_view(snapshot.attachment_view);
        if (snapshot.image.id != SG_INVALID_ID) sg_destroy_image(snapshot.image);
    }
    scene_stage_snapshots_.clear();
    writeBundle(frame_index, ctx);
    config.capture_complete = true;
    is_capturing_frame = false;

    char frame_dir_buf[64];
    snprintf(frame_dir_buf, sizeof(frame_dir_buf), "frame-%04llu", (unsigned long long)frame_index);
    std::string target_dir = config.output_dir + "/" + frame_dir_buf;

    printf("\n=======================================================\n");
    printf("Effect diagnostic capture written to:\n%s/\n", target_dir.c_str());
    printf("=======================================================\n\n");
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
        source_stats_ = ImageStats::compute(source_rgba_.data(), source_w_, source_h_);

        has_previous_image_ = true;
        previous_w_ = source_w_;
        previous_h_ = source_h_;
        previous_rgba_ = source_rgba_;
        previous_stats_ = source_stats_;
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
        if (readback.success && !readback.rgba_data.empty()) {
            trace.image_stats = ImageStats::compute(readback.rgba_data.data(), readback.width, readback.height);
            trace.has_image_stats = true;

            if (has_previous_image_ && previous_w_ == readback.width && previous_h_ == readback.height) {
                trace.delta_from_previous = ImageDeltaStats::compute(readback.rgba_data.data(), previous_rgba_.data(),
                                                                     readback.width, readback.height);
                trace.has_delta_from_previous = true;
            } else if (has_previous_image_) {
                trace.delta_from_previous = ImageDeltaStats::computeFromStats(trace.image_stats, previous_stats_);
                trace.has_delta_from_previous = true;
            }

            if (has_source_image_ && source_w_ == readback.width && source_h_ == readback.height) {
                trace.delta_from_source = ImageDeltaStats::compute(readback.rgba_data.data(), source_rgba_.data(),
                                                                   readback.width, readback.height);
                trace.has_delta_from_source = true;
            } else if (has_source_image_) {
                trace.delta_from_source = ImageDeltaStats::computeFromStats(trace.image_stats, source_stats_);
                trace.has_delta_from_source = true;
            }

            char frame_dir_buf[64];
            snprintf(frame_dir_buf, sizeof(frame_dir_buf), "frame-%04llu", (unsigned long long)trace.frame_number);
            char eff_dir_buf[64];
            snprintf(eff_dir_buf, sizeof(eff_dir_buf), "effect-%02d", trace.effect_index);
            std::string pass_dir = config.output_dir + "/" + frame_dir_buf + "/" + eff_dir_buf;
            ensureDir(pass_dir);

            char pass_file_buf[128];
            std::string clean_shader = sanitizeFilename(trace.shader_name);
            snprintf(pass_file_buf, sizeof(pass_file_buf), "pass-%02d-%s.png", trace.pass_index, clean_shader.c_str());
            std::string pass_png_path = pass_dir + "/" + pass_file_buf;

            if (writePng(pass_png_path, readback.width, readback.height, readback.rgba_data.data())) {
                trace.captured_image_filename = std::string(frame_dir_buf) + "/" + eff_dir_buf + "/" + pass_file_buf;
                generated_files_.push_back(trace.captured_image_filename);
            }

            if (trace.render_target_name.empty()) {
                previous_w_ = readback.width;
                previous_h_ = readback.height;
                previous_rgba_ = std::move(readback.rgba_data);
                previous_stats_ = trace.image_stats;
                has_previous_image_ = true;
            }
        }
    }

    render_graph_.addPass(trace);
}

void RenderDiagnostics::onLayerFinalImage(int effect_index, sg_image img, int width, int height) {
    (void)effect_index;
    (void)width;
    (void)height;
    if (!is_capturing_frame || img.id == SG_INVALID_ID) return;

    GpuImageReadbackResult readback = gpu_readback_image_rgba8(img);
    if (readback.success && !readback.rgba_data.empty()) {
        char frame_dir_buf[64];
        snprintf(frame_dir_buf, sizeof(frame_dir_buf), "frame-%04llu", (unsigned long long)config.target_frame);
        std::string frame_dir = config.output_dir + "/" + frame_dir_buf;
        ensureDir(frame_dir);

        std::string final_path = frame_dir + "/layer-final.png";
        if (writePng(final_path, readback.width, readback.height, readback.rgba_data.data())) {
            generated_files_.push_back(std::string(frame_dir_buf) + "/layer-final.png");
        }
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
    scene_stage_snapshots_.push_back({stage_name, img, texture_view, attachment_view});
}

void RenderDiagnostics::writeBundle(uint64_t frame_index, const EngineContext& ctx) {
    ensureDir(config.output_dir);

    char frame_dir_buf[64];
    snprintf(frame_dir_buf, sizeof(frame_dir_buf), "frame-%04llu", (unsigned long long)frame_index);
    std::string frame_dir = config.output_dir + "/" + frame_dir_buf;
    ensureDir(frame_dir);

    if (has_source_image_ && !source_rgba_.empty()) {
        std::string src_path = frame_dir + "/source.png";
        if (writePng(src_path, source_w_, source_h_, source_rgba_.data())) {
            generated_files_.push_back(std::string(frame_dir_buf) + "/source.png");
        }
    }

    render_graph_.validate();

    cJSON* rg_json = render_graph_.toJson();
    std::string rg_json_path = config.output_dir + "/rendergraph.json";
    writeJsonToFile(rg_json_path, rg_json);
    cJSON_Delete(rg_json);
    generated_files_.push_back("rendergraph.json");

    std::string rg_md = render_graph_.toMarkdown();
    std::string rg_md_path = config.output_dir + "/rendergraph.md";
    writeStringToFile(rg_md_path, rg_md);
    generated_files_.push_back("rendergraph.md");

    cJSON* passes_arr = cJSON_CreateArray();
    for (const auto& p : render_graph_.passes) {
        cJSON_AddItemToArray(passes_arr, p.toJson());
    }
    cJSON* passes_root = cJSON_CreateObject();
    cJSON_AddItemToObject(passes_root, "passes", passes_arr);
    std::string passes_path = config.output_dir + "/passes.json";
    writeJsonToFile(passes_path, passes_root);
    cJSON_Delete(passes_root);
    generated_files_.push_back("passes.json");

    cJSON* uniforms_root = cJSON_CreateArray();
    for (const auto& prov : provenance_list_) {
        cJSON_AddItemToArray(uniforms_root, prov.toJson());
    }
    std::string uniforms_path = config.output_dir + "/uniforms.json";
    writeJsonToFile(uniforms_path, uniforms_root);
    cJSON_Delete(uniforms_root);
    generated_files_.push_back("uniforms.json");

    cJSON* combos_root = cJSON_CreateArray();
    for (const auto& prov : provenance_list_) {
        cJSON* c_item = cJSON_CreateObject();
        cJSON_AddStringToObject(c_item, "effect", prov.effect_file.c_str());
        cJSON_AddNumberToObject(c_item, "pass_index", prov.pass_index);
        cJSON_AddStringToObject(c_item, "shader", prov.shader_name.c_str());
        cJSON* c_map = cJSON_CreateObject();
        for (const auto& [name, entry] : prov.combos) {
            cJSON_AddItemToObject(c_map, name.c_str(), entry.toJson());
        }
        cJSON_AddItemToObject(c_item, "combos", c_map);
        cJSON_AddItemToArray(combos_root, c_item);
    }
    std::string combos_path = config.output_dir + "/combos.json";
    writeJsonToFile(combos_path, combos_root);
    cJSON_Delete(combos_root);
    generated_files_.push_back("combos.json");

    std::string shaders_base_dir = config.output_dir + "/shaders";
    ensureDir(shaders_base_dir);
    for (const auto& dump : shader_dumps_) {
        char pass_tag[128];
        snprintf(pass_tag, sizeof(pass_tag), "effect-%d-pass-%d", dump.effect_index, dump.pass_index);
        std::string sdir = shaders_base_dir + "/" + pass_tag;
        ensureDir(sdir);

        writeStringToFile(sdir + "/original.vert", dump.original_vs);
        writeStringToFile(sdir + "/original.frag", dump.original_fs);
        writeStringToFile(sdir + "/processed.vert", dump.processed_vs);
        writeStringToFile(sdir + "/processed.frag", dump.processed_fs);
        writeStringToFile(sdir + "/final.vert", dump.final_vs);
        writeStringToFile(sdir + "/final.frag", dump.final_fs);

        cJSON* c_obj = cJSON_CreateObject();
        for (const auto& [k, v] : dump.combos) cJSON_AddNumberToObject(c_obj, k.c_str(), v);
        writeJsonToFile(sdir + "/combos.json", c_obj);
        cJSON_Delete(c_obj);

        cJSON* u_obj = cJSON_CreateObject();
        for (const auto& [k, v] : dump.uniforms) {
            cJSON* arr = cJSON_CreateArray();
            for (float f : v) cJSON_AddItemToArray(arr, cJSON_CreateNumber(f));
            cJSON_AddItemToObject(u_obj, k.c_str(), arr);
        }
        writeJsonToFile(sdir + "/uniforms.json", u_obj);
        cJSON_Delete(u_obj);

        generated_files_.push_back(std::string("shaders/") + pass_tag + "/...");
    }

    cJSON* env_root = cJSON_CreateObject();
    cJSON_AddStringToObject(env_root, "backend", "Vulkan (Sokol GFX)");
    cJSON_AddStringToObject(env_root, "renderer", "Sokol Generic 2D/Effect Pipeline");
    cJSON* res_arr = cJSON_CreateArray();
    cJSON_AddItemToArray(res_arr, cJSON_CreateNumber(ctx.scene_w));
    cJSON_AddItemToArray(res_arr, cJSON_CreateNumber(ctx.scene_h));
    cJSON_AddItemToObject(env_root, "design_resolution", res_arr);
    cJSON_AddNumberToObject(env_root, "render_scale", ctx.render_scale);
    cJSON_AddStringToObject(env_root, "wallpaper_path", ctx.wallpaper_path);
    cJSON_AddStringToObject(env_root, "engine_path", ctx.engine_path);
    std::string env_path = config.output_dir + "/environment.json";
    writeJsonToFile(env_path, env_root);
    cJSON_Delete(env_root);
    generated_files_.push_back("environment.json");

    cJSON* manifest = cJSON_CreateObject();
    cJSON_AddStringToObject(manifest, "format_version", "1.0.0");
    cJSON_AddNumberToObject(manifest, "target_frame", (double)frame_index);
    cJSON_AddNumberToObject(manifest, "target_time", (double)ctx.time);
    cJSON_AddStringToObject(manifest, "wallpaper_path", ctx.wallpaper_path);

    cJSON* det_obj = cJSON_CreateObject();
    cJSON_AddBoolToObject(det_obj, "time_frozen", config.has_deterministic_time);
    cJSON_AddBoolToObject(det_obj, "mouse_frozen", config.has_deterministic_time);
    cJSON_AddBoolToObject(det_obj, "particles_prng_deterministic", false);
    cJSON_AddBoolToObject(det_obj, "audio_deterministic", false);
    cJSON_AddStringToObject(det_obj, "notes",
                            "Time and pointer are frozen. Particle PRNG and audio streams are non-deterministic.");
    cJSON_AddItemToObject(manifest, "deterministic_systems", det_obj);

    cJSON* files_arr = cJSON_CreateArray();
    for (const auto& f : generated_files_) {
        cJSON_AddItemToArray(files_arr, cJSON_CreateString(f.c_str()));
    }
    cJSON_AddItemToObject(manifest, "generated_files", files_arr);

    std::string manifest_path = config.output_dir + "/manifest.json";
    writeJsonToFile(manifest_path, manifest);
    cJSON_Delete(manifest);
}
