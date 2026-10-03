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
#include <thread>

#include "render_diagnostics_internal.h"
#include "shared/core/engine_context.h"
#include "shared/core/logger.h"
#include "shared/graphics/backend/gpu_debug_labels.h"
#include "shared/graphics/backend/gpu_readback.h"
#include "shared/graphics/shader/shader_compiler.h"

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

std::string cleanEffectName(const std::string& effect_file, const std::string& shader_name) {
    fs::path p(effect_file.empty() ? shader_name : effect_file);
    if (p.filename() == "effect.json") p = p.parent_path();
    std::string stem = p.stem().string();
    return sanitizeFilename(stem.empty() ? "effect" : stem);
}

}  // namespace

namespace render_diagnostics_internal {
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

void exportBundleAsync(DiagnosticExportPayload payload, const std::atomic<bool>* cancel) {
    auto cancelled = [cancel]() { return cancel && cancel->load(); };
    ensureDir(payload.output_dir);

    char frame_dir_buf[64];
    snprintf(frame_dir_buf, sizeof(frame_dir_buf), "frame-%04llu", (unsigned long long)payload.frame_index);
    std::string frame_dir = payload.output_dir + "/" + frame_dir_buf;
    ensureDir(frame_dir);

    std::vector<std::string> generated_files;

    ImageStats source_stats;
    bool has_source_stats = false;
    if (payload.has_source_image && !payload.source_rgba.empty()) {
        source_stats = ImageStats::compute(payload.source_rgba.data(), payload.source_w, payload.source_h);
        has_source_stats = true;
        if (!payload.final_only) {
            std::string src_path = frame_dir + "/source.png";
            if (RenderDiagnostics::writePng(src_path, payload.source_w, payload.source_h, payload.source_rgba.data())) {
                generated_files.push_back(std::string(frame_dir_buf) + "/source.png");
            }
        }
    }

    if (!payload.final_only && payload.has_final_image && !payload.final_rgba.empty()) {
        std::string final_path = frame_dir + "/layer-final.png";
        if (RenderDiagnostics::writePng(final_path, payload.final_w, payload.final_h, payload.final_rgba.data())) {
            generated_files.push_back(std::string(frame_dir_buf) + "/layer-final.png");
        }
    }

    if (!payload.stage_images.empty()) {
        std::string stage_dir = frame_dir + "/scene-stages";
        bool stage_dir_ready = false;
        // PNG encoding dominates export time in unoptimised builds, so encode stages concurrently.
        const size_t batch_size = std::max(1u, std::thread::hardware_concurrency());
        std::vector<std::pair<std::future<bool>, std::string>> batch;
        auto flush_batch = [&]() {
            for (auto& [done, file] : batch) {
                if (done.get()) generated_files.push_back(std::string(frame_dir_buf) + "/scene-stages/" + file);
            }
            batch.clear();
        };
        for (const auto& stage : payload.stage_images) {
            if (cancelled()) {
                flush_batch();
                return;
            }
            if (payload.final_only && stage.name != "post-bloom-final") continue;
            if (!stage_dir_ready) {
                ensureDir(stage_dir);
                stage_dir_ready = true;
            }
            char file_buf[256];
            std::string clean_stage = sanitizeFilename(stage.name);
            snprintf(file_buf, sizeof(file_buf), "%03d-%s.png", stage.stage_index, clean_stage.c_str());
            std::string path = stage_dir + "/" + file_buf;
            batch.emplace_back(std::async(std::launch::async,
                                          [&stage, path]() {
                                              return RenderDiagnostics::writePng(path, stage.width, stage.height,
                                                                                 stage.rgba_data.data());
                                          }),
                               file_buf);
            if (batch.size() >= batch_size) flush_batch();
        }
        flush_batch();
    }

    ImageStats previous_stats = source_stats;
    bool has_previous_stats = has_source_stats;
    const std::vector<uint8_t>* prev_rgba = payload.has_source_image ? &payload.source_rgba : nullptr;
    int prev_w = payload.source_w;
    int prev_h = payload.source_h;

    for (auto& item : payload.pass_images) {
        if (cancelled()) return;
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

            if (!payload.final_only) {
                std::string layer_clean =
                    sanitizeFilename(item.trace.layer_name.empty() ? "layer" : item.trace.layer_name);
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

    if (cancelled()) return;
    payload.render_graph.validate();

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
        if (cancelled()) return;
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
}  // namespace render_diagnostics_internal
