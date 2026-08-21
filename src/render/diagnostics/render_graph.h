#ifndef RENDER_GRAPH_H
#define RENDER_GRAPH_H

#include <cjson/cJSON.h>
#include <stdint.h>

#include <map>
#include <set>
#include <string>
#include <vector>

#include "image_stats.h"

struct TextureBindingTrace {
    int slot = 0;
    std::string semantic_source;  // "previous", "_rt_coc", "material", "fallback", etc.
    uint32_t image_id = 0;
    uint32_t view_id = 0;
    int width = 0;
    int height = 0;
    std::string pixel_format = "RGBA8";
    bool is_render_target = false;
    std::string sampler_mode = "clamp";

    cJSON* toJson() const;
};

struct PassTraceEntry {
    uint64_t frame_number = 0;
    int effect_index = 0;
    std::string effect_file;
    int pass_index = 0;
    std::string shader_name;
    bool enabled = true;
    bool visible = true;
    int draw_order = 0;

    std::string render_target_name;  // empty means ping-pong target
    uint32_t target_image_id = 0;
    uint32_t target_view_id = 0;
    int target_width = 0;
    int target_height = 0;
    std::string target_pixel_format = "RGBA8";
    float render_scale = 1.0f;
    bool is_fullscreen_quad = false;

    std::vector<TextureBindingTrace> inputs;

    // Output capture info & stats if captured
    std::string captured_image_filename;
    bool has_image_stats = false;
    ImageStats image_stats;
    bool has_delta_from_previous = false;
    ImageDeltaStats delta_from_previous;
    bool has_delta_from_source = false;
    ImageDeltaStats delta_from_source;

    cJSON* toJson() const;
};

struct RenderGraphWarning {
    std::string level;  // "warning", "error", "info"
    std::string pass_identifier;
    std::string message;

    cJSON* toJson() const;
};

class RenderGraph {
   public:
    std::vector<PassTraceEntry> passes;
    std::vector<RenderGraphWarning> warnings;

    void addPass(const PassTraceEntry& pass);
    void validate();
    cJSON* toJson() const;
    std::string toMermaid() const;
    std::string toMarkdown() const;
};

#endif  // RENDER_GRAPH_H
