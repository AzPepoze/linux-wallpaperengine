#ifndef RENDER_DIAGNOSTICS_H
#define RENDER_DIAGNOSTICS_H

#include <cjson/cJSON.h>
#include <stdint.h>

#include <map>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "diagnostic_config.h"
#include "image_stats.h"
#include "render_graph.h"
#include "sokol_gfx.h"
#include "uniform_provenance.h"

struct ShaderDump {
    int effect_index = 0;
    int pass_index = 0;
    std::string shader_name;
    std::string effect_file;

    std::string original_vs;
    std::string original_fs;
    std::string processed_vs;
    std::string processed_fs;
    std::string final_vs;
    std::string final_fs;
    std::map<std::string, int> combos;
    std::map<std::string, std::vector<float>> uniforms;
};

struct CapturedPassImage {
    PassTraceEntry trace;
    std::vector<uint8_t> rgba_data;
    int width = 0;
    int height = 0;
};

struct CapturedStageImage {
    std::string name;
    std::vector<uint8_t> rgba_data;
    int width = 0;
    int height = 0;
    int stage_index = 0;
};

struct DiagnosticExportPayload {
    std::string output_dir;
    uint64_t frame_index = 0;
    float time = 0.0f;
    int scene_w = 0;
    int scene_h = 0;
    float render_scale = 1.0f;
    std::string wallpaper_path;
    std::string engine_path;
    bool has_deterministic_time = false;

    // Raw memory captures
    bool has_source_image = false;
    std::vector<uint8_t> source_rgba;
    int source_w = 0;
    int source_h = 0;

    bool has_final_image = false;
    std::vector<uint8_t> final_rgba;
    int final_w = 0;
    int final_h = 0;

    std::vector<CapturedPassImage> pass_images;
    std::vector<CapturedStageImage> stage_images;
    RenderGraph render_graph;
    std::vector<ShaderDump> shader_dumps;
    std::vector<PassUniformProvenance> provenance_list;
};

class EngineContext;

class RenderDiagnostics {
   public:
    static RenderDiagnostics& instance();

    DiagnosticConfig config;
    bool is_capturing_frame = false;

    void init(bool enabled = false);
    void onFrameStart(uint64_t frame_index, EngineContext& ctx);
    void onFrameEnd(uint64_t frame_index, EngineContext& ctx);

    // Provenance and shader registration
    void registerShaderDump(const ShaderDump& dump);
    void registerUniformProvenance(const PassUniformProvenance& prov);

    // Per-pass and image tracing
    void onSourceImage(int effect_index, sg_image img, int width, int height);
    void recordPass(PassTraceEntry trace, sg_image out_img);
    void onLayerFinalImage(int effect_index, sg_image img, int width, int height);
    void recordSceneStage(const std::string& stage_name, sg_image img, sg_view texture_view, sg_view attachment_view);

    // Pass isolation helpers
    bool isEffectIsolated(int effect_index, const std::string& effect_path) const;
    bool isPassDisabled(int pass_index) const;
    bool shouldStopAfterPass(int pass_index) const;
    int getForcedOutputSlot() const;
    bool shouldCapturePassImage(int effect_index, int pass_index) const;

    const std::vector<ShaderDump>& getShaderDumps() const {
        return shader_dumps_;
    }
    const RenderGraph& getRenderGraph() const {
        return render_graph_;
    }
    const std::vector<PassUniformProvenance>& getProvenanceList() const {
        return provenance_list_;
    }

    static void writeJsonToFile(const std::string& path, cJSON* json);
    static void writeStringToFile(const std::string& path, const std::string& content);
    static bool writePng(const std::string& path, int w, int h, const uint8_t* rgba);

   private:
    RenderDiagnostics() = default;
    ~RenderDiagnostics();

    RenderGraph render_graph_;
    std::vector<ShaderDump> shader_dumps_;
    std::vector<PassUniformProvenance> provenance_list_;

    // Frame capture caches
    bool has_source_image_ = false;
    std::vector<uint8_t> source_rgba_;
    int source_w_ = 0;
    int source_h_ = 0;

    bool has_final_image_ = false;
    std::vector<uint8_t> final_rgba_;
    int final_w_ = 0;
    int final_h_ = 0;

    std::vector<CapturedPassImage> pass_images_;

    int scene_stage_index_ = 0;
    struct SceneStageSnapshot {
        std::string name;
        sg_image image = {SG_INVALID_ID};
        sg_view texture_view = {SG_INVALID_ID};
        sg_view attachment_view = {SG_INVALID_ID};
        int stage_index = 0;
    };
    std::vector<SceneStageSnapshot> scene_stage_snapshots_;

    std::thread worker_thread_;
};

#endif  // RENDER_DIAGNOSTICS_H
