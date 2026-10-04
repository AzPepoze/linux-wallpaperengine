#include <sokol_app.h>
#include <sokol_args.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
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

namespace {
struct ExportContext {
    DiagnosticExportPayload& payload;
    const std::atomic<bool>* cancel;
    std::string frame_dir;
    std::string frame_dir_name;
    std::vector<std::string> generated_files;

    bool cancelled() const {
        return cancel && cancel->load();
    }
    std::string frameRelative(const std::string& path) const {
        return frame_dir_name + "/" + path;
    }
};

std::string childPath(const ExportContext& export_context, const char* name) {
    return export_context.payload.output_dir + "/" + name;
}

bool writeSourceAndFinalImages(ExportContext& export_context, ImageStats& source_stats) {
    const DiagnosticExportPayload& payload = export_context.payload;
    bool has_source_stats = false;
    if (payload.has_source_image && !payload.source_rgba.empty()) {
        source_stats = ImageStats::compute(payload.source_rgba.data(), payload.source_w, payload.source_h);
        has_source_stats = true;
        if (!payload.final_only &&
            RenderDiagnostics::writePng(export_context.frame_dir + "/source.png", payload.source_w, payload.source_h,
                                        payload.source_rgba.data())) {
            export_context.generated_files.push_back(export_context.frameRelative("source.png"));
        }
    }
    if (!payload.final_only && payload.has_final_image && !payload.final_rgba.empty() &&
        RenderDiagnostics::writePng(export_context.frame_dir + "/layer-final.png", payload.final_w, payload.final_h,
                                    payload.final_rgba.data())) {
        export_context.generated_files.push_back(export_context.frameRelative("layer-final.png"));
    }
    return has_source_stats;
}

bool writeStageImages(ExportContext& export_context) {
    const DiagnosticExportPayload& payload = export_context.payload;
    if (payload.stage_images.empty()) return true;

    const std::string stage_dir = export_context.frame_dir + "/scene-stages";
    bool stage_dir_ready = false;
    const size_t batch_size = std::max(1u, std::thread::hardware_concurrency());
    std::vector<std::pair<std::future<bool>, std::string>> batch;
    auto flush_batch = [&]() {
        for (auto& [done, file] : batch) {
            if (done.get())
                export_context.generated_files.push_back(export_context.frameRelative("scene-stages/" + file));
        }
        batch.clear();
    };
    for (const auto& stage : payload.stage_images) {
        if (export_context.cancelled()) {
            flush_batch();
            return false;
        }
        if (payload.final_only && stage.name != "post-bloom-final") continue;
        if (!stage_dir_ready) {
            ensureDir(stage_dir);
            stage_dir_ready = true;
        }
        char file_buf[256];
        snprintf(file_buf, sizeof(file_buf), "%03d-%s.png", stage.stage_index, sanitizeFilename(stage.name).c_str());
        const std::string path = stage_dir + "/" + file_buf;
        batch.emplace_back(std::async(std::launch::async,
                                      [&stage, path]() {
                                          return RenderDiagnostics::writePng(path, stage.width, stage.height,
                                                                             stage.rgba_data.data());
                                      }),
                           file_buf);
        if (batch.size() >= batch_size) flush_batch();
    }
    flush_batch();
    return true;
}

bool analyzePassImages(ExportContext& export_context, const ImageStats& source_stats, bool has_source_stats) {
    DiagnosticExportPayload& payload = export_context.payload;
    // PNG encoding is independent per image, so it runs on worker threads while statistics are computed here.
    const size_t batch_size = std::max(1u, std::thread::hardware_concurrency());
    std::vector<std::pair<std::future<bool>, CapturedPassImage*>> writes;
    std::vector<std::string> written_names;
    auto flushWrites = [&]() {
        for (size_t i = 0; i < writes.size(); ++i) {
            if (!writes[i].first.get()) continue;
            writes[i].second->trace.captured_image_filename = written_names[i];
            export_context.generated_files.push_back(written_names[i]);
        }
        writes.clear();
        written_names.clear();
    };

    ImageStats previous_stats = source_stats;
    bool has_previous_stats = has_source_stats;
    const std::vector<uint8_t>* prev_rgba = payload.has_source_image ? &payload.source_rgba : nullptr;
    int prev_w = payload.source_w;
    int prev_h = payload.source_h;

    for (auto& item : payload.pass_images) {
        if (export_context.cancelled()) return false;
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
                const std::string layer_clean =
                    sanitizeFilename(item.trace.layer_name.empty() ? "layer" : item.trace.layer_name);
                const std::string effect_clean = cleanEffectName(item.trace.effect_file, item.trace.shader_name);
                const std::string shader_clean =
                    sanitizeFilename(item.trace.shader_name.empty() ? "shader" : item.trace.shader_name);

                char pass_dir_name[256];
                snprintf(pass_dir_name, sizeof(pass_dir_name), "%02d_%s_%s", item.trace.draw_order, layer_clean.c_str(),
                         effect_clean.c_str());
                const std::string pass_dir = export_context.frame_dir + "/" + pass_dir_name;
                ensureDir(pass_dir);

                char pass_file_name[256];
                snprintf(pass_file_name, sizeof(pass_file_name), "pass-%02d-%s.png", item.trace.pass_index,
                         shader_clean.c_str());
                const std::string relative =
                    export_context.frameRelative(std::string(pass_dir_name) + "/" + pass_file_name);
                const std::string png_path = pass_dir + "/" + pass_file_name;
                const int png_width = item.width;
                const int png_height = item.height;
                const uint8_t* png_data = item.rgba_data.data();
                writes.emplace_back(std::async(std::launch::async,
                                               [png_path, png_width, png_height, png_data]() {
                                                   return RenderDiagnostics::writePng(png_path, png_width, png_height,
                                                                                      png_data);
                                               }),
                                    &item);
                item.trace.captured_image_filename.clear();
                written_names.push_back(relative);
                if (writes.size() >= batch_size) flushWrites();
            }

            if (item.trace.render_target_name.empty()) {
                prev_w = item.width;
                prev_h = item.height;
                previous_stats = item.trace.image_stats;
                has_previous_stats = true;
                prev_rgba = &item.rgba_data;
            }
        }
    }
    flushWrites();
    for (auto& item : payload.pass_images) payload.render_graph.addPass(item.trace);
    return true;
}

void writeRenderGraphFiles(ExportContext& export_context) {
    DiagnosticExportPayload& payload = export_context.payload;
    payload.render_graph.validate();

    cJSON* render_graph_json = payload.render_graph.toJson();
    RenderDiagnostics::writeJsonToFile(childPath(export_context, "rendergraph.json"), render_graph_json);
    cJSON_Delete(render_graph_json);
    export_context.generated_files.push_back("rendergraph.json");

    RenderDiagnostics::writeStringToFile(childPath(export_context, "rendergraph.md"),
                                         payload.render_graph.toMarkdown());
    export_context.generated_files.push_back("rendergraph.md");

    cJSON* passes_array = cJSON_CreateArray();
    for (const auto& pass : payload.render_graph.passes) cJSON_AddItemToArray(passes_array, pass.toJson());
    cJSON* passes_root = cJSON_CreateObject();
    cJSON_AddItemToObject(passes_root, "passes", passes_array);
    RenderDiagnostics::writeJsonToFile(childPath(export_context, "passes.json"), passes_root);
    cJSON_Delete(passes_root);
    export_context.generated_files.push_back("passes.json");
}

void writeProvenanceFiles(ExportContext& export_context) {
    const DiagnosticExportPayload& payload = export_context.payload;

    cJSON* uniforms_root = cJSON_CreateArray();
    for (const auto& provenance : payload.provenance_list) cJSON_AddItemToArray(uniforms_root, provenance.toJson());
    RenderDiagnostics::writeJsonToFile(childPath(export_context, "uniforms.json"), uniforms_root);
    cJSON_Delete(uniforms_root);
    export_context.generated_files.push_back("uniforms.json");

    cJSON* combos_root = cJSON_CreateArray();
    for (const auto& provenance : payload.provenance_list) {
        cJSON* item = cJSON_CreateObject();
        cJSON_AddStringToObject(item, "effect", provenance.effect_file.c_str());
        cJSON_AddNumberToObject(item, "pass_index", provenance.pass_index);
        cJSON_AddStringToObject(item, "shader", provenance.shader_name.c_str());
        cJSON* combo_map = cJSON_CreateObject();
        for (const auto& [name, entry] : provenance.combos) {
            cJSON_AddItemToObject(combo_map, name.c_str(), entry.toJson());
        }
        cJSON_AddItemToObject(item, "combos", combo_map);
        cJSON_AddItemToArray(combos_root, item);
    }
    RenderDiagnostics::writeJsonToFile(childPath(export_context, "combos.json"), combos_root);
    cJSON_Delete(combos_root);
    export_context.generated_files.push_back("combos.json");
}

bool writeShaderDumps(ExportContext& export_context) {
    const std::string shaders_base_dir = childPath(export_context, "shaders");
    ensureDir(shaders_base_dir);
    for (const auto& dump : export_context.payload.shader_dumps) {
        if (export_context.cancelled()) return false;
        char pass_tag[256];
        snprintf(pass_tag, sizeof(pass_tag), "%02d_%s_pass-%d", dump.effect_index,
                 cleanEffectName(dump.effect_file, dump.shader_name).c_str(), dump.pass_index);
        const std::string dir = shaders_base_dir + "/" + pass_tag;
        ensureDir(dir);

        RenderDiagnostics::writeStringToFile(dir + "/original.vert", dump.original_vs);
        RenderDiagnostics::writeStringToFile(dir + "/original.frag", dump.original_fs);
        RenderDiagnostics::writeStringToFile(dir + "/processed.vert", dump.processed_vs);
        RenderDiagnostics::writeStringToFile(dir + "/processed.frag", dump.processed_fs);
        RenderDiagnostics::writeStringToFile(dir + "/final.vert", dump.final_vs);
        RenderDiagnostics::writeStringToFile(dir + "/final.frag", dump.final_fs);

        cJSON* combos = cJSON_CreateObject();
        for (const auto& [name, value] : dump.combos) cJSON_AddNumberToObject(combos, name.c_str(), value);
        RenderDiagnostics::writeJsonToFile(dir + "/combos.json", combos);
        cJSON_Delete(combos);

        cJSON* uniforms = cJSON_CreateObject();
        for (const auto& [name, values] : dump.uniforms) {
            cJSON* array = cJSON_CreateArray();
            for (float value : values) cJSON_AddItemToArray(array, cJSON_CreateNumber(value));
            cJSON_AddItemToObject(uniforms, name.c_str(), array);
        }
        RenderDiagnostics::writeJsonToFile(dir + "/uniforms.json", uniforms);
        cJSON_Delete(uniforms);

        export_context.generated_files.push_back(std::string("shaders/") + pass_tag + "/...");
    }
    return true;
}

void writeEnvironment(ExportContext& export_context) {
    const DiagnosticExportPayload& payload = export_context.payload;
    cJSON* environment = cJSON_CreateObject();
    cJSON_AddStringToObject(environment, "backend", "Vulkan (Sokol GFX)");
    cJSON_AddStringToObject(environment, "renderer", "Sokol Generic 2D/Effect Pipeline");
    cJSON* resolution = cJSON_CreateArray();
    cJSON_AddItemToArray(resolution, cJSON_CreateNumber(payload.scene_w));
    cJSON_AddItemToArray(resolution, cJSON_CreateNumber(payload.scene_h));
    cJSON_AddItemToObject(environment, "design_resolution", resolution);
    cJSON_AddNumberToObject(environment, "render_scale", payload.render_scale);
    cJSON_AddStringToObject(environment, "wallpaper_path", payload.wallpaper_path.c_str());
    cJSON_AddStringToObject(environment, "engine_path", payload.engine_path.c_str());
    RenderDiagnostics::writeJsonToFile(childPath(export_context, "environment.json"), environment);
    cJSON_Delete(environment);
    export_context.generated_files.push_back("environment.json");
}

void writeManifest(const ExportContext& export_context) {
    const DiagnosticExportPayload& payload = export_context.payload;
    cJSON* manifest = cJSON_CreateObject();
    cJSON_AddStringToObject(manifest, "format_version", "1.0.0");
    cJSON_AddNumberToObject(manifest, "target_frame", (double)payload.frame_index);
    cJSON_AddNumberToObject(manifest, "target_time", (double)payload.time);
    cJSON_AddStringToObject(manifest, "wallpaper_path", payload.wallpaper_path.c_str());

    cJSON* deterministic = cJSON_CreateObject();
    cJSON_AddBoolToObject(deterministic, "time_frozen", payload.has_deterministic_time);
    cJSON_AddBoolToObject(deterministic, "mouse_frozen", payload.has_deterministic_time);
    cJSON_AddBoolToObject(deterministic, "particles_prng_deterministic", false);
    cJSON_AddBoolToObject(deterministic, "audio_deterministic", false);
    cJSON_AddStringToObject(deterministic, "notes",
                            "Time and pointer are frozen. Particle PRNG and audio streams are non-deterministic.");
    cJSON_AddItemToObject(manifest, "deterministic_systems", deterministic);

    cJSON* files = cJSON_CreateArray();
    for (const auto& file : export_context.generated_files)
        cJSON_AddItemToArray(files, cJSON_CreateString(file.c_str()));
    cJSON_AddItemToObject(manifest, "generated_files", files);

    RenderDiagnostics::writeJsonToFile(childPath(export_context, "manifest.json"), manifest);
    cJSON_Delete(manifest);
}
}  // namespace

void exportBundleAsync(DiagnosticExportPayload payload, const std::atomic<bool>* cancel) {
    const auto export_start = std::chrono::steady_clock::now();
    ensureDir(payload.output_dir);

    char frame_dir_name[64];
    snprintf(frame_dir_name, sizeof(frame_dir_name), "frame-%04llu", (unsigned long long)payload.frame_index);
    ExportContext export_context{payload, cancel, payload.output_dir + "/" + frame_dir_name, frame_dir_name, {}};
    ensureDir(export_context.frame_dir);

    ImageStats source_stats;
    const bool has_source_stats = writeSourceAndFinalImages(export_context, source_stats);
    if (!writeStageImages(export_context)) return;
    if (!analyzePassImages(export_context, source_stats, has_source_stats)) return;

    if (export_context.cancelled()) return;
    writeRenderGraphFiles(export_context);
    writeProvenanceFiles(export_context);
    if (!writeShaderDumps(export_context)) return;
    writeEnvironment(export_context);
    writeManifest(export_context);

    const double export_ms =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - export_start).count();
    effect_log.info("Effect diagnostic capture complete in %.0f ms (written to: %s/)", export_ms,
                    payload.output_dir.c_str());
    printf("\n=======================================================\n");
    printf("Effect diagnostic capture written to:\n%s/\n", payload.output_dir.c_str());
    printf("=======================================================\n\n");
}
}  // namespace render_diagnostics_internal
