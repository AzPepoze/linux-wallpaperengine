#include "render_diagnostics.h"

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

#include "core/engine_context.h"
#include "core/logger.h"
#include "render/backend/gpu_debug_labels.h"
#include "render/backend/gpu_readback.h"

namespace fs = std::filesystem;

namespace {
void ensureDir(const std::string& path) {
    std::error_code ec;
    fs::create_directories(path, ec);
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
    while (!clean.empty() && clean.front() == '_') clean.erase(clean.begin());
    while (!clean.empty() && clean.back() == '_') clean.pop_back();
    return clean.empty() ? "wallpaper" : clean;
}

std::string resolveWallpaperName(const EngineContext& ctx) {
    const char* roots[] = {ctx.wallpaper_path, ctx.asset_root};
    for (const char* root : roots) {
        if (!root || root[0] == '\0') continue;
        std::string project_json_path = std::string(root) + "/project.json";
        std::ifstream file(project_json_path);
        if (file.is_open()) {
            std::stringstream buffer;
            buffer << file.rdbuf();
            std::string content = buffer.str();
            cJSON* json = cJSON_Parse(content.c_str());
            if (json) {
                cJSON* title_item = cJSON_GetObjectItemCaseSensitive(json, "title");
                if (cJSON_IsString(title_item) && title_item->valuestring && title_item->valuestring[0] != '\0') {
                    std::string sanitized = sanitizeFilename(title_item->valuestring);
                    cJSON_Delete(json);
                    return sanitized;
                }
                cJSON_Delete(json);
            }
        }
    }

    if (ctx.wallpaper_path[0] != '\0') {
        fs::path p(ctx.wallpaper_path);
        while (p.has_filename() && p.filename().empty()) p = p.parent_path();
        std::string stem = p.stem().string();
        if (!stem.empty() && stem != "extracted") {
            return sanitizeFilename(stem);
        }
    }

    if (ctx.asset_root[0] != '\0') {
        fs::path p(ctx.asset_root);
        while (p.has_filename() && p.filename().empty()) p = p.parent_path();
        std::string stem = p.stem().string();
        if (!stem.empty() && stem != "extracted") {
            return sanitizeFilename(stem);
        }
    }

    return "wallpaper";
}

std::string cleanEffectName(const std::string& effect_file, const std::string& shader_name) {
    fs::path p(effect_file.empty() ? shader_name : effect_file);
    if (p.filename() == "effect.json") p = p.parent_path();
    std::string stem = p.stem().string();
    return sanitizeFilename(stem.empty() ? "effect" : stem);
}

void exportBundleAsync(DiagnosticExportPayload payload) {
    ensureDir(payload.output_dir);

    char frame_dir_buf[64];
    snprintf(frame_dir_buf, sizeof(frame_dir_buf), "frame-%04llu", (unsigned long long)payload.frame_index);
    std::string frame_dir = payload.output_dir + "/" + frame_dir_buf;
    ensureDir(frame_dir);

    std::vector<std::string> generated_files;

    // 1. Source image
    ImageStats source_stats;
    bool has_source_stats = false;
    if (payload.has_source_image && !payload.source_rgba.empty()) {
        source_stats = ImageStats::compute(payload.source_rgba.data(), payload.source_w, payload.source_h);
        has_source_stats = true;
        std::string src_path = frame_dir + "/source.png";
        if (RenderDiagnostics::writePng(src_path, payload.source_w, payload.source_h, payload.source_rgba.data())) {
            generated_files.push_back(std::string(frame_dir_buf) + "/source.png");
        }
    }

    // 2. Final layer image
    if (payload.has_final_image && !payload.final_rgba.empty()) {
        std::string final_path = frame_dir + "/layer-final.png";
        if (RenderDiagnostics::writePng(final_path, payload.final_w, payload.final_h, payload.final_rgba.data())) {
            generated_files.push_back(std::string(frame_dir_buf) + "/layer-final.png");
        }
    }

    // 3. Scene stages
    if (!payload.stage_images.empty()) {
        std::string stage_dir = frame_dir + "/scene-stages";
        ensureDir(stage_dir);
        for (const auto& stage : payload.stage_images) {
            char file_buf[256];
            std::string clean_stage = sanitizeFilename(stage.name);
            snprintf(file_buf, sizeof(file_buf), "%03d-%s.png", stage.stage_index, clean_stage.c_str());
            std::string path = stage_dir + "/" + file_buf;
            if (RenderDiagnostics::writePng(path, stage.width, stage.height, stage.rgba_data.data())) {
                generated_files.push_back(std::string(frame_dir_buf) + "/scene-stages/" + file_buf);
            }
        }
    }

    // 4. Pass images, stats, and deltas
    ImageStats previous_stats = source_stats;
    bool has_previous_stats = has_source_stats;
    const std::vector<uint8_t>* prev_rgba = payload.has_source_image ? &payload.source_rgba : nullptr;
    int prev_w = payload.source_w;
    int prev_h = payload.source_h;

    for (auto& item : payload.pass_images) {
        if (!item.rgba_data.empty() && item.width > 0 && item.height > 0) {
            item.trace.image_stats = ImageStats::compute(item.rgba_data.data(), item.width, item.height);
            item.trace.has_image_stats = true;

            if (prev_rgba && prev_w == item.width && prev_h == item.height) {
                item.trace.delta_from_previous =
                    ImageDeltaStats::compute(item.rgba_data.data(), prev_rgba->data(), item.width, item.height);
                item.trace.has_delta_from_previous = true;
            } else if (has_previous_stats) {
                item.trace.delta_from_previous =
                    ImageDeltaStats::computeFromStats(item.trace.image_stats, previous_stats);
                item.trace.has_delta_from_previous = true;
            }

            if (payload.has_source_image && payload.source_w == item.width && payload.source_h == item.height) {
                item.trace.delta_from_source = ImageDeltaStats::compute(
                    item.rgba_data.data(), payload.source_rgba.data(), item.width, item.height);
                item.trace.has_delta_from_source = true;
            } else if (has_source_stats) {
                item.trace.delta_from_source = ImageDeltaStats::computeFromStats(item.trace.image_stats, source_stats);
                item.trace.has_delta_from_source = true;
            }

            std::string layer_clean = sanitizeFilename(item.trace.layer_name.empty() ? "layer" : item.trace.layer_name);
            std::string effect_clean = cleanEffectName(item.trace.effect_file, item.trace.shader_name);
            std::string shader_clean =
                sanitizeFilename(item.trace.shader_name.empty() ? "shader" : item.trace.shader_name);

            char eff_dir_buf[256];
            snprintf(eff_dir_buf, sizeof(eff_dir_buf), "%02d_%s_%s", item.trace.draw_order, layer_clean.c_str(),
                     effect_clean.c_str());
            std::string pass_dir = frame_dir + "/" + eff_dir_buf;
            ensureDir(pass_dir);

            char pass_file_buf[256];
            snprintf(pass_file_buf, sizeof(pass_file_buf), "pass-%02d-%s.png", item.trace.pass_index,
                     shader_clean.c_str());
            std::string pass_png_path = pass_dir + "/" + pass_file_buf;

            if (RenderDiagnostics::writePng(pass_png_path, item.width, item.height, item.rgba_data.data())) {
                item.trace.captured_image_filename =
                    std::string(frame_dir_buf) + "/" + eff_dir_buf + "/" + pass_file_buf;
                generated_files.push_back(item.trace.captured_image_filename);
            }

            if (item.trace.render_target_name.empty()) {
                prev_w = item.width;
                prev_h = item.height;
                previous_stats = item.trace.image_stats;
                has_previous_stats = true;
                prev_rgba = &item.rgba_data;
            }
        }

        payload.render_graph.addPass(item.trace);
    }

    payload.render_graph.validate();

    // 5. JSON & Markdown Manifests
    cJSON* rg_json = payload.render_graph.toJson();
    std::string rg_json_path = payload.output_dir + "/rendergraph.json";
    RenderDiagnostics::writeJsonToFile(rg_json_path, rg_json);
    cJSON_Delete(rg_json);
    generated_files.push_back("rendergraph.json");

    std::string rg_md = payload.render_graph.toMarkdown();
    std::string rg_md_path = payload.output_dir + "/rendergraph.md";
    RenderDiagnostics::writeStringToFile(rg_md_path, rg_md);
    generated_files.push_back("rendergraph.md");

    cJSON* passes_arr = cJSON_CreateArray();
    for (const auto& p : payload.render_graph.passes) {
        cJSON_AddItemToArray(passes_arr, p.toJson());
    }
    cJSON* passes_root = cJSON_CreateObject();
    cJSON_AddItemToObject(passes_root, "passes", passes_arr);
    std::string passes_path = payload.output_dir + "/passes.json";
    RenderDiagnostics::writeJsonToFile(passes_path, passes_root);
    cJSON_Delete(passes_root);
    generated_files.push_back("passes.json");

    cJSON* uniforms_root = cJSON_CreateArray();
    for (const auto& prov : payload.provenance_list) {
        cJSON_AddItemToArray(uniforms_root, prov.toJson());
    }
    std::string uniforms_path = payload.output_dir + "/uniforms.json";
    RenderDiagnostics::writeJsonToFile(uniforms_path, uniforms_root);
    cJSON_Delete(uniforms_root);
    generated_files.push_back("uniforms.json");

    cJSON* combos_root = cJSON_CreateArray();
    for (const auto& prov : payload.provenance_list) {
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
    std::string combos_path = payload.output_dir + "/combos.json";
    RenderDiagnostics::writeJsonToFile(combos_path, combos_root);
    cJSON_Delete(combos_root);
    generated_files.push_back("combos.json");

    std::string shaders_base_dir = payload.output_dir + "/shaders";
    ensureDir(shaders_base_dir);
    for (const auto& dump : payload.shader_dumps) {
        char pass_tag[256];
        std::string s_effect = cleanEffectName(dump.effect_file, dump.shader_name);
        snprintf(pass_tag, sizeof(pass_tag), "%02d_%s_pass-%d", dump.effect_index, s_effect.c_str(), dump.pass_index);
        std::string sdir = shaders_base_dir + "/" + pass_tag;
        ensureDir(sdir);

        RenderDiagnostics::writeStringToFile(sdir + "/original.vert", dump.original_vs);
        RenderDiagnostics::writeStringToFile(sdir + "/original.frag", dump.original_fs);
        RenderDiagnostics::writeStringToFile(sdir + "/processed.vert", dump.processed_vs);
        RenderDiagnostics::writeStringToFile(sdir + "/processed.frag", dump.processed_fs);
        RenderDiagnostics::writeStringToFile(sdir + "/final.vert", dump.final_vs);
        RenderDiagnostics::writeStringToFile(sdir + "/final.frag", dump.final_fs);

        cJSON* c_obj = cJSON_CreateObject();
        for (const auto& [k, v] : dump.combos) cJSON_AddNumberToObject(c_obj, k.c_str(), v);
        RenderDiagnostics::writeJsonToFile(sdir + "/combos.json", c_obj);
        cJSON_Delete(c_obj);

        cJSON* u_obj = cJSON_CreateObject();
        for (const auto& [k, v] : dump.uniforms) {
            cJSON* arr = cJSON_CreateArray();
            for (float f : v) cJSON_AddItemToArray(arr, cJSON_CreateNumber(f));
            cJSON_AddItemToObject(u_obj, k.c_str(), arr);
        }
        RenderDiagnostics::writeJsonToFile(sdir + "/uniforms.json", u_obj);
        cJSON_Delete(u_obj);

        generated_files.push_back(std::string("shaders/") + pass_tag + "/...");
    }

    cJSON* env_root = cJSON_CreateObject();
    cJSON_AddStringToObject(env_root, "backend", "Vulkan (Sokol GFX)");
    cJSON_AddStringToObject(env_root, "renderer", "Sokol Generic 2D/Effect Pipeline");
    cJSON* res_arr = cJSON_CreateArray();
    cJSON_AddItemToArray(res_arr, cJSON_CreateNumber(payload.scene_w));
    cJSON_AddItemToArray(res_arr, cJSON_CreateNumber(payload.scene_h));
    cJSON_AddItemToObject(env_root, "design_resolution", res_arr);
    cJSON_AddNumberToObject(env_root, "render_scale", payload.render_scale);
    cJSON_AddStringToObject(env_root, "wallpaper_path", payload.wallpaper_path.c_str());
    cJSON_AddStringToObject(env_root, "engine_path", payload.engine_path.c_str());
    std::string env_path = payload.output_dir + "/environment.json";
    RenderDiagnostics::writeJsonToFile(env_path, env_root);
    cJSON_Delete(env_root);
    generated_files.push_back("environment.json");

    cJSON* manifest = cJSON_CreateObject();
    cJSON_AddStringToObject(manifest, "format_version", "1.0.0");
    cJSON_AddNumberToObject(manifest, "target_frame", (double)payload.frame_index);
    cJSON_AddNumberToObject(manifest, "target_time", (double)payload.time);
    cJSON_AddStringToObject(manifest, "wallpaper_path", payload.wallpaper_path.c_str());

    cJSON* det_obj = cJSON_CreateObject();
    cJSON_AddBoolToObject(det_obj, "time_frozen", payload.has_deterministic_time);
    cJSON_AddBoolToObject(det_obj, "mouse_frozen", payload.has_deterministic_time);
    cJSON_AddBoolToObject(det_obj, "particles_prng_deterministic", false);
    cJSON_AddBoolToObject(det_obj, "audio_deterministic", false);
    cJSON_AddStringToObject(det_obj, "notes",
                            "Time and pointer are frozen. Particle PRNG and audio streams are non-deterministic.");
    cJSON_AddItemToObject(manifest, "deterministic_systems", det_obj);

    cJSON* files_arr = cJSON_CreateArray();
    for (const auto& f : generated_files) {
        cJSON_AddItemToArray(files_arr, cJSON_CreateString(f.c_str()));
    }
    cJSON_AddItemToObject(manifest, "generated_files", files_arr);

    std::string manifest_path = payload.output_dir + "/manifest.json";
    RenderDiagnostics::writeJsonToFile(manifest_path, manifest);
    cJSON_Delete(manifest);

    effect_log.info("Effect diagnostic capture complete (written to: %s/)", payload.output_dir.c_str());
    printf("\n=======================================================\n");
    printf("Effect diagnostic capture written to:\n%s/\n", payload.output_dir.c_str());
    printf("=======================================================\n\n");
}
}  // namespace

RenderDiagnostics& RenderDiagnostics::instance() {
    static RenderDiagnostics s_inst;
    return s_inst;
}

RenderDiagnostics::~RenderDiagnostics() {
    if (worker_thread_.joinable()) {
        worker_thread_.join();
    }
}

void RenderDiagnostics::init(bool enabled) {
    config.enabled = enabled;
    config.target_frame = 100;
    config.capture_pass_images = true;
    if (enabled) {
        effect_log.info("Effect diagnostic mode ENABLED (auto-run on frame: %llu)",
                        (unsigned long long)config.target_frame);
    }
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

        // Replace old diagnostic directory if it already exists
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
    payload.scene_w = ctx.scene_w;
    payload.scene_h = ctx.scene_h;
    payload.render_scale = ctx.render_scale;
    payload.wallpaper_path = ctx.wallpaper_path;
    payload.engine_path = ctx.engine_path;
    payload.has_deterministic_time = config.has_deterministic_time;

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

    // Join any previous worker thread before launching new one
    if (worker_thread_.joinable()) {
        worker_thread_.join();
    }
    worker_thread_ = std::thread(exportBundleAsync, std::move(payload));
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
