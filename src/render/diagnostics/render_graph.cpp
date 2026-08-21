#include "render_graph.h"

#include <sstream>

cJSON* TextureBindingTrace::toJson() const {
    cJSON* root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "slot", slot);
    cJSON_AddStringToObject(root, "semantic", semantic_source.c_str());
    cJSON_AddNumberToObject(root, "image_id", image_id);
    cJSON_AddNumberToObject(root, "view_id", view_id);

    cJSON* sz = cJSON_CreateArray();
    cJSON_AddItemToArray(sz, cJSON_CreateNumber(width));
    cJSON_AddItemToArray(sz, cJSON_CreateNumber(height));
    cJSON_AddItemToObject(root, "size", sz);

    cJSON_AddStringToObject(root, "pixel_format", pixel_format.c_str());
    cJSON_AddBoolToObject(root, "is_render_target", is_render_target);
    cJSON_AddStringToObject(root, "sampler_mode", sampler_mode.c_str());
    return root;
}

cJSON* PassTraceEntry::toJson() const {
    cJSON* root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "frame_number", (double)frame_number);
    cJSON_AddNumberToObject(root, "effect_index", effect_index);
    cJSON_AddStringToObject(root, "effect", effect_file.c_str());
    cJSON_AddNumberToObject(root, "pass_index", pass_index);
    cJSON_AddStringToObject(root, "shader", shader_name.c_str());
    cJSON_AddBoolToObject(root, "enabled", enabled);
    cJSON_AddBoolToObject(root, "visible", visible);
    cJSON_AddNumberToObject(root, "draw_order", draw_order);

    cJSON_AddStringToObject(root, "target",
                            render_target_name.empty() ? "_pingpong_output" : render_target_name.c_str());
    cJSON_AddNumberToObject(root, "target_image_id", target_image_id);
    cJSON_AddNumberToObject(root, "target_view_id", target_view_id);

    cJSON* target_sz = cJSON_CreateArray();
    cJSON_AddItemToArray(target_sz, cJSON_CreateNumber(target_width));
    cJSON_AddItemToArray(target_sz, cJSON_CreateNumber(target_height));
    cJSON_AddItemToObject(root, "target_size", target_sz);

    cJSON_AddStringToObject(root, "target_pixel_format", target_pixel_format.c_str());
    cJSON_AddNumberToObject(root, "render_scale", render_scale);
    cJSON_AddBoolToObject(root, "is_fullscreen_quad", is_fullscreen_quad);

    cJSON* in_arr = cJSON_CreateArray();
    for (const auto& in : inputs) {
        cJSON_AddItemToArray(in_arr, in.toJson());
    }
    cJSON_AddItemToObject(root, "inputs", in_arr);

    if (!captured_image_filename.empty()) {
        cJSON_AddStringToObject(root, "captured_image", captured_image_filename.c_str());
    }
    if (has_image_stats) {
        cJSON_AddItemToObject(root, "image_stats", image_stats.toJson());
    }
    if (has_delta_from_previous) {
        cJSON_AddItemToObject(root, "delta_from_previous", delta_from_previous.toJson());
    }
    if (has_delta_from_source) {
        cJSON_AddItemToObject(root, "delta_from_source", delta_from_source.toJson());
    }

    return root;
}

cJSON* RenderGraphWarning::toJson() const {
    cJSON* root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "level", level.c_str());
    cJSON_AddStringToObject(root, "pass", pass_identifier.c_str());
    cJSON_AddStringToObject(root, "message", message.c_str());
    return root;
}

void RenderGraph::addPass(const PassTraceEntry& pass) {
    passes.push_back(pass);
}

void RenderGraph::validate() {
    warnings.clear();
    std::set<std::string> produced_targets;
    std::set<std::string> consumed_targets;

    for (size_t i = 0; i < passes.size(); ++i) {
        const auto& p = passes[i];
        std::string pass_id = "Effect " + std::to_string(p.effect_index) + " Pass " + std::to_string(p.pass_index) +
                              " (" + p.shader_name + ")";

        if (p.target_width <= 0 || p.target_height <= 0) {
            warnings.push_back({"error", pass_id,
                                "Invalid render target dimensions: " + std::to_string(p.target_width) + "x" +
                                    std::to_string(p.target_height)});
        }

        if (!p.render_target_name.empty()) {
            produced_targets.insert(p.render_target_name);
        }

        for (const auto& in : p.inputs) {
            if (in.slot < 0 || in.slot > 11) {
                warnings.push_back({"error", pass_id, "Texture slot out of bounds: " + std::to_string(in.slot)});
            }
            if (in.image_id == 0) {
                warnings.push_back({"warning", pass_id,
                                    "Unresolved texture bound to slot " + std::to_string(in.slot) +
                                        " (semantic: " + in.semantic_source + ")"});
            }
            if (in.is_render_target && !in.semantic_source.empty() && in.semantic_source != "previous") {
                consumed_targets.insert(in.semantic_source);
                if (produced_targets.find(in.semantic_source) == produced_targets.end()) {
                    warnings.push_back({"warning", pass_id,
                                        "Samples from named target '" + in.semantic_source +
                                            "' before it was written by any earlier pass"});
                }
            }
            if (in.image_id != 0 && p.target_image_id != 0 && in.image_id == p.target_image_id) {
                warnings.push_back({"error", pass_id,
                                    "Hazard: Same GPU image ID " + std::to_string(in.image_id) +
                                        " is bound as both input slot " + std::to_string(in.slot) +
                                        " and active color attachment!"});
            }
        }
    }

    for (const auto& target : produced_targets) {
        if (consumed_targets.find(target) == consumed_targets.end()) {
            warnings.push_back({"info", "RenderGraph",
                                "Target '" + target + "' was produced but never consumed by subsequent passes"});
        }
    }
}

cJSON* RenderGraph::toJson() const {
    cJSON* root = cJSON_CreateObject();

    cJSON* pass_arr = cJSON_CreateArray();
    for (const auto& p : passes) {
        cJSON_AddItemToArray(pass_arr, p.toJson());
    }
    cJSON_AddItemToObject(root, "passes", pass_arr);

    cJSON* warn_arr = cJSON_CreateArray();
    for (const auto& w : warnings) {
        cJSON_AddItemToArray(warn_arr, w.toJson());
    }
    cJSON_AddItemToObject(root, "validation_warnings", warn_arr);

    return root;
}

std::string RenderGraph::toMermaid() const {
    std::ostringstream ss;
    ss << "graph TD\n";
    ss << "    Source[\"Layer Source Image\"]\n";

    std::string prev_node = "Source";
    for (size_t i = 0; i < passes.size(); ++i) {
        const auto& p = passes[i];
        std::string pass_node = "Pass_" + std::to_string(p.effect_index) + "_" + std::to_string(p.pass_index);
        std::string target_label = p.render_target_name.empty() ? "(ping-pong)" : p.render_target_name;

        ss << "    " << pass_node << "[\"Pass " << p.pass_index << ": " << p.shader_name
           << "<br/>Target: " << target_label << " (" << p.target_width << "x" << p.target_height << ")\"]\n";

        for (const auto& in : p.inputs) {
            if (in.semantic_source == "previous" || in.semantic_source.empty()) {
                ss << "    " << prev_node << " -->|\"Slot " << in.slot << " (previous)\"| " << pass_node << "\n";
            } else if (in.semantic_source.find("_rt_") == 0) {
                std::string rt_node = "RT_" + in.semantic_source.substr(1);
                ss << "    " << rt_node << " -->|\"Slot " << in.slot << "\"| " << pass_node << "\n";
            } else {
                std::string ext_node = "Ext_" + std::to_string(i) + "_" + std::to_string(in.slot);
                ss << "    " << ext_node << "[\"" << in.semantic_source << "\"] -->|\"Slot " << in.slot << "\"| "
                   << pass_node << "\n";
            }
        }

        if (!p.render_target_name.empty()) {
            std::string rt_node = "RT_" + p.render_target_name.substr(1);
            ss << "    " << pass_node << " --> " << rt_node << "[\"" << p.render_target_name << "\"]\n";
        } else {
            prev_node = pass_node;
        }
    }

    ss << "    " << prev_node << " --> Final[\"Layer Final Output\"]\n";
    return ss.str();
}

std::string RenderGraph::toMarkdown() const {
    std::ostringstream ss;
    ss << "# Effect Render Graph\n\n";
    ss << "```mermaid\n";
    ss << toMermaid();
    ss << "```\n\n";

    if (!warnings.empty()) {
        ss << "## Validation Warnings & Issues\n\n";
        ss << "| Level | Pass / Context | Message |\n";
        ss << "|---|---|---|\n";
        for (const auto& w : warnings) {
            ss << "| " << w.level << " | " << w.pass_identifier << " | " << w.message << " |\n";
        }
        ss << "\n";
    }

    return ss.str();
}
