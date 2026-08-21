#ifndef RENDER_DIAGNOSTICS_H
#define RENDER_DIAGNOSTICS_H

#include <cjson/cJSON.h>
#include <stdint.h>

#include <map>
#include <memory>
#include <string>
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

class EngineContext;

class RenderDiagnostics {
   public:
    static RenderDiagnostics& instance();

    DiagnosticConfig config;
    bool is_capturing_frame = false;

    void init();
    void onFrameStart(uint64_t frame_index, EngineContext& ctx);
    void onFrameEnd(uint64_t frame_index, EngineContext& ctx);

    // Provenance and shader registration
    void registerShaderDump(const ShaderDump& dump);
    void registerUniformProvenance(const PassUniformProvenance& prov);

    // Per-pass and image tracing
    void onSourceImage(int effect_index, sg_image img, int width, int height);
    void recordPass(PassTraceEntry trace, sg_image out_img);
    void onLayerFinalImage(int effect_index, sg_image img, int width, int height);

    // Pass isolation helpers
    bool isEffectIsolated(int effect_index, const std::string& effect_path) const;
    bool isPassDisabled(int pass_index) const;
    bool shouldStopAfterPass(int pass_index) const;
    int getForcedOutputSlot() const;

    const std::vector<ShaderDump>& getShaderDumps() const {
        return shader_dumps_;
    }
    const RenderGraph& getRenderGraph() const {
        return render_graph_;
    }
    const std::vector<PassUniformProvenance>& getProvenanceList() const {
        return provenance_list_;
    }

   private:
    RenderDiagnostics() = default;

    RenderGraph render_graph_;
    std::vector<ShaderDump> shader_dumps_;
    std::vector<PassUniformProvenance> provenance_list_;

    // Frame capture caches
    bool has_source_image_ = false;
    std::vector<uint8_t> source_rgba_;
    int source_w_ = 0;
    int source_h_ = 0;
    ImageStats source_stats_;

    bool has_previous_image_ = false;
    std::vector<uint8_t> previous_rgba_;
    int previous_w_ = 0;
    int previous_h_ = 0;
    ImageStats previous_stats_;

    std::vector<std::string> generated_files_;

    void writeBundle(uint64_t frame_index, const EngineContext& ctx);
    void writeJsonToFile(const std::string& path, cJSON* json);
    void writeStringToFile(const std::string& path, const std::string& content);
    bool writePng(const std::string& path, int w, int h, const uint8_t* rgba);
};

#endif  // RENDER_DIAGNOSTICS_H
