#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>

#define SOKOL_ARGS_IMPL
#include "render/diagnostics/diagnostic_config.h"
#include "render/diagnostics/image_stats.h"
#include "render/diagnostics/render_graph.h"
#include "render/diagnostics/uniform_provenance.h"
#include "render/shader/shader_processor.h"
#include "sokol_args.h"
#include "ui/sandbox_catalog.h"
#include "wallpaper/scene/2d/parser/effect_parser.h"
#include "wallpaper/scene/2d/parser/image_parser.h"
#include "wallpaper/scene/2d/parser/particle_parser.h"

static int g_test_passed = 0;
static int g_test_failed = 0;

#define TEST_ASSERT(cond, msg)                                                                       \
    do {                                                                                             \
        if (!(cond)) {                                                                               \
            std::cerr << "FAIL: " << msg << " (" << __FILE__ << ":" << __LINE__ << ")" << std::endl; \
            g_test_failed++;                                                                         \
        } else {                                                                                     \
            g_test_passed++;                                                                         \
        }                                                                                            \
    } while (0)

void test_uniform_provenance_json() {
    PassUniformProvenance prov;
    prov.effect_file = "effects/workshop/test/effect.json";
    prov.pass_index = 2;
    prov.shader_name = "test_shader";

    UniformProvenanceEntry entry;
    entry.shader_name = "u_lightFactor";
    entry.authored_name = "Highlight factor (gamma)";
    entry.resolved_name = "u_lightFactor";
    entry.type = "float";

    UniformResolutionStep s1;
    s1.source = ProvenanceSource::ShaderMetadataDefault;
    s1.present = true;
    s1.values = {2.2f};
    entry.resolution.push_back(s1);

    UniformResolutionStep s2;
    s2.source = ProvenanceSource::InstanceOverride;
    s2.present = true;
    s2.applied = true;
    s2.values = {3.5f};
    entry.resolution.push_back(s2);

    entry.final_source = ProvenanceSource::InstanceOverride;
    entry.final_value = {3.5f};
    prov.uniforms["u_lightFactor"] = entry;

    cJSON* json = prov.toJson();
    TEST_ASSERT(json != nullptr, "PassUniformProvenance::toJson should produce valid cJSON");

    cJSON* u_obj = cJSON_GetObjectItemCaseSensitive(json, "uniforms");
    TEST_ASSERT(u_obj != nullptr, "Uniforms object should be present");
    cJSON* item = cJSON_GetObjectItemCaseSensitive(u_obj, "u_lightFactor");
    TEST_ASSERT(item != nullptr, "u_lightFactor should be present");
    cJSON* final_src = cJSON_GetObjectItemCaseSensitive(item, "final_source");
    TEST_ASSERT(final_src != nullptr && strcmp(final_src->valuestring, "instance_override") == 0,
                "final_source should be instance_override");

    cJSON_Delete(json);
}

void test_image_stats() {
    // Create a 2x2 synthetic RGBA8 image:
    // Pixel 0: (255, 0, 0, 255) -> Red
    // Pixel 1: (0, 255, 0, 255) -> Green
    // Pixel 2: (0, 0, 255, 255) -> Blue
    // Pixel 3: (255, 255, 255, 255) -> White
    std::vector<uint8_t> pixels = {
        255, 0,   0,   255,  // P0
        0,   255, 0,   255,  // P1
        0,   0,   255, 255,  // P2
        255, 255, 255, 255   // P3
    };

    ImageStats stats = ImageStats::compute(pixels.data(), 2, 2);

    // Expected mean R: (1 + 0 + 0 + 1)/4 = 0.5
    // Expected mean G: (0 + 1 + 0 + 1)/4 = 0.5
    // Expected mean B: (0 + 0 + 1 + 1)/4 = 0.5
    // Expected mean A: (1 + 1 + 1 + 1)/4 = 1.0
    TEST_ASSERT(std::abs(stats.mean_rgba[0] - 0.5f) < 0.01f, "Mean R should be 0.5");
    TEST_ASSERT(std::abs(stats.mean_rgba[1] - 0.5f) < 0.01f, "Mean G should be 0.5");
    TEST_ASSERT(std::abs(stats.mean_rgba[2] - 0.5f) < 0.01f, "Mean B should be 0.5");
    TEST_ASSERT(std::abs(stats.mean_rgba[3] - 1.0f) < 0.01f, "Mean A should be 1.0");

    TEST_ASSERT(stats.min_rgba[0] == 0.0f, "Min R should be 0.0");
    TEST_ASSERT(stats.max_rgba[0] == 1.0f, "Max R should be 1.0");

    cJSON* json = stats.toJson();
    TEST_ASSERT(json != nullptr, "ImageStats::toJson should produce valid cJSON");
    cJSON_Delete(json);

    // Delta with itself should be zero
    ImageDeltaStats self_delta = ImageDeltaStats::compute(pixels.data(), pixels.data(), 2, 2);
    TEST_ASSERT(self_delta.mean_abs_delta_rgb == 0.0f, "Self delta should have 0 error");
    TEST_ASSERT(self_delta.max_abs_delta_rgb == 0.0f, "Self max delta should be 0");
    TEST_ASSERT(self_delta.psnr_rgb > 100.0f, "Lossless PSNR should be infinite/high");

    cJSON* delta_json = self_delta.toJson();
    TEST_ASSERT(delta_json != nullptr, "ImageDeltaStats::toJson should produce valid cJSON");
    cJSON_Delete(delta_json);
}

void test_render_graph_validation() {
    RenderGraph graph;

    PassTraceEntry pass0;
    pass0.effect_index = 0;
    pass0.pass_index = 0;
    pass0.shader_name = "downsample";
    pass0.render_target_name = "_rt_downscaled1";
    pass0.target_image_id = 10;
    pass0.target_width = 960;
    pass0.target_height = 540;
    TextureBindingTrace in0;
    in0.slot = 0;
    in0.semantic_source = "previous";
    in0.image_id = 5;
    pass0.inputs.push_back(in0);
    graph.addPass(pass0);

    PassTraceEntry pass1;
    pass1.effect_index = 0;
    pass1.pass_index = 1;
    pass1.shader_name = "bokeh_blur";
    pass1.render_target_name = "_rt_downscaled2";
    pass1.target_image_id = 20;
    pass1.target_width = 960;
    pass1.target_height = 540;
    TextureBindingTrace in1_0;
    in1_0.slot = 0;
    in1_0.semantic_source = "_rt_downscaled1";
    in1_0.is_render_target = true;
    in1_0.image_id = 10;
    pass1.inputs.push_back(in1_0);
    graph.addPass(pass1);

    graph.validate();
    // pass0 writes _rt_downscaled1 and pass1 consumes it, but _rt_downscaled2 is never consumed
    bool found_unconsumed = false;
    for (const auto& w : graph.warnings) {
        if (w.message.find("_rt_downscaled2") != std::string::npos) {
            found_unconsumed = true;
        }
    }
    TEST_ASSERT(found_unconsumed, "Validation should detect unconsumed render target _rt_downscaled2");

    // Test feedback hazard detection
    RenderGraph hazard_graph;
    PassTraceEntry hazard_pass;
    hazard_pass.effect_index = 0;
    hazard_pass.pass_index = 0;
    hazard_pass.shader_name = "feedback_hazard";
    hazard_pass.target_image_id = 15;
    hazard_pass.target_width = 100;
    hazard_pass.target_height = 100;
    TextureBindingTrace hazard_in;
    hazard_in.slot = 0;
    hazard_in.image_id = 15;  // Same as target!
    hazard_pass.inputs.push_back(hazard_in);
    hazard_graph.addPass(hazard_pass);
    hazard_graph.validate();

    bool found_hazard = false;
    for (const auto& w : hazard_graph.warnings) {
        if (w.level == "error" && w.message.find("Hazard") != std::string::npos) {
            found_hazard = true;
        }
    }
    TEST_ASSERT(found_hazard, "Validation should detect same image feedback hazard");

    // Test Mermaid generation
    std::string mermaid = graph.toMermaid();
    TEST_ASSERT(!mermaid.empty(), "Mermaid markdown should not be empty");
    TEST_ASSERT(mermaid.find("graph TD") != std::string::npos, "Mermaid should use vertical layout (graph TD)");
}

void test_cli_parsing() {
    // 1. Test equals-separated
    {
        const char* fake_argv[] = {"linux_wallpaperengine", "--diagnose-effects",
                                   "--diagnostic-frame=42", "--diagnostic-output=/tmp/test_diag",
                                   "--diagnostic-time=5.5", "--diagnostic-pass=1"};
        sargs_desc desc = {};
        desc.argc = 6;
        desc.argv = (char**)fake_argv;
        sargs_setup(&desc);

        DiagnosticConfig cfg;
        cfg.parseFromArgs();

        TEST_ASSERT(cfg.enabled == true, "Diagnostic mode should be enabled from --diagnose-effects");
        TEST_ASSERT(cfg.target_frame == 42, "Target frame should be parsed as 42");
        TEST_ASSERT(cfg.output_dir == "/tmp/test_diag", "Output dir should be /tmp/test_diag");
        TEST_ASSERT(cfg.has_deterministic_time == true, "Deterministic time flag should be set");
        TEST_ASSERT(std::abs(cfg.deterministic_time - 5.5f) < 0.01f, "Deterministic time should be 5.5");
        TEST_ASSERT(cfg.isolate_pass_index == 1, "Isolate pass index should be 1");

        sargs_shutdown();
    }

    // 2. Test space-separated
    {
        const char* fake_argv[] = {"linux_wallpaperengine",
                                   "--diagnose-effects",
                                   "--diagnostic-frame",
                                   "99",
                                   "--diagnostic-output",
                                   "/tmp/test_diag_space",
                                   "--diagnostic-time",
                                   "12.3",
                                   "--diagnostic-stop-after-pass",
                                   "3"};
        sargs_desc desc = {};
        desc.argc = sizeof(fake_argv) / sizeof(fake_argv[0]);
        desc.argv = (char**)fake_argv;
        desc.max_args = 64;
        sargs_setup(&desc);

        DiagnosticConfig cfg;
        cfg.parseFromArgs();

        TEST_ASSERT(cfg.enabled == true, "Diagnostic mode should be enabled");
        TEST_ASSERT(cfg.target_frame == 99, "Target frame should be parsed as 99");
        TEST_ASSERT(cfg.output_dir == "/tmp/test_diag_space", "Output dir should be /tmp/test_diag_space");
        TEST_ASSERT(cfg.has_deterministic_time == true, "Deterministic time flag should be set");
        TEST_ASSERT(std::abs(cfg.deterministic_time - 12.3f) < 0.01f, "Deterministic time should be 12.3");
        TEST_ASSERT(cfg.stop_after_pass_index == 3, "Stop after pass should be 3");

        sargs_shutdown();
    }
}

void test_shader_source_metadata() {
    const char* source =
        "// [COMBO] {\"combo\":\"MASK\",\"default\":1}\n"
        "// [COMBO] {\"combo\":\"MASK\",\"default\":0}\n"
        "uniform sampler2D g_Texture1; // {\"label\":\"ui_editor_properties_specular\"}\n"
        "uniform sampler2D g_Texture2; // [ui_editor_properties_opacity_mask]\n";
    const std::string combos = ShaderSourceProcessor::extractCombos(source);
    TEST_ASSERT(combos == "#define MASK 1\n", "First combo declaration should define its default exactly once");

    const auto labels = ShaderSourceProcessor::extractTextureLabels(source);
    TEST_ASSERT(labels.at(1) == "Specular", "Known texture metadata should use its readable label");
    TEST_ASSERT(labels.at(2) == "Opacity Mask", "Legacy texture metadata should be normalized");
    TEST_ASSERT(ShaderSourceProcessor::buildShaderPrefix().find("#define float3 vec3") != std::string::npos,
                "Shader prefix should retain HLSL vector aliases");
}

void test_effect_configuration_precedence() {
    cJSON* material = cJSON_Parse("{\"constantshadervalues\":{\"g_Value\":1,\"g_Color\":[1,2]}}");
    cJSON* pass = cJSON_Parse("{\"constantshadervalues\":{\"g_Value\":2}}");
    cJSON* instance = cJSON_Parse("{\"constantshadervalues\":{\"g_Value\":3}}");
    cJSON* merged = nullptr;
    EffectConfiguration::mergeObject(merged, cJSON_GetObjectItemCaseSensitive(material, "constantshadervalues"));
    EffectConfiguration::mergeObject(merged, cJSON_GetObjectItemCaseSensitive(pass, "constantshadervalues"));
    EffectConfiguration::mergeObject(merged, cJSON_GetObjectItemCaseSensitive(instance, "constantshadervalues"));
    std::map<std::string, std::vector<float>> values;
    EffectConfiguration::readValuesObject(merged, values);
    TEST_ASSERT(values["g_Value"] == std::vector<float>({3.0f}),
                "Instance constants should override pass and material constants");
    TEST_ASSERT(values["g_Color"] == std::vector<float>({1.0f, 2.0f}),
                "Non-overridden material constants should be retained");
    cJSON_Delete(material);
    cJSON_Delete(pass);
    cJSON_Delete(instance);
    cJSON_Delete(merged);
}

void test_particle_parser() {
    cJSON* vector_value = cJSON_CreateString("1.5 2.5 3.5");
    vec3 parsed = {};
    ParticleParser::readVec3(vector_value, parsed);
    TEST_ASSERT(parsed[0] == 1.5f && parsed[1] == 2.5f && parsed[2] == 3.5f,
                "Particle parser should read string vec3 values");
    cJSON_Delete(vector_value);

    cJSON* scalar_value = cJSON_CreateString("2.25");
    TEST_ASSERT(std::abs(ParticleParser::readFloat(scalar_value) - 2.25f) < 0.001f,
                "Particle parser should read numeric strings");
    cJSON_Delete(scalar_value);
}

void test_image_parser() {
    wallpaper_engine::SceneObjectDocument document;
    document.name = "Model layer";
    document.image.model = "models/card.json";
    document.image.size = {320.0f, 180.0f};
    const ImageObjectConfig config = ImageParser::parse(document);
    TEST_ASSERT(config.name == "Model layer" && config.asset_path == "models/card.json" && config.is_model,
                "Image parser should choose model assets when no image asset exists");
    TEST_ASSERT(config.width == 320.0f && config.height == 180.0f, "Image parser should retain document dimensions");
}

void test_sandbox_catalog() {
    namespace fs = std::filesystem;

    const fs::path root = fs::temp_directory_path() / "linux-wallpaperengine-sandbox-catalog-test";
    std::error_code error;
    fs::remove_all(root, error);
    fs::create_directories(root / "assets/effects/opacity/preview", error);
    fs::create_directories(root / "assets/effects/opacity/materials/effects", error);
    fs::create_directories(root / "assets/effects/no-preview/materials", error);
    std::ofstream(root / "assets/effects/opacity/preview/scene.json") << "{}";
    std::ofstream(root / "assets/effects/opacity/materials/effects/opacity.json") << "{}";
    std::ofstream(root / "assets/effects/no-preview/materials/no-preview.json") << "{}";

    SandboxCatalog catalog;
    catalog.scan(root.string());

    TEST_ASSERT(catalog.effects().size() == 2, "Sandbox should list effects with and without preview projects");
    TEST_ASSERT(catalog.effects()[0].name == "no-preview", "Effect entries should have a stable alphabetical order");
    TEST_ASSERT(!catalog.effects()[0].available, "Effects without preview/scene.json must be unavailable");
    TEST_ASSERT(catalog.effects()[1].name == "opacity" && catalog.effects()[1].available,
                "Effects with preview/scene.json must be available");
    TEST_ASSERT(catalog.materials().size() == 2, "Sandbox should include installed material definitions");
    TEST_ASSERT(!catalog.materials()[0].available,
                "Materials whose effect has no preview project must stay visible but unavailable");
    TEST_ASSERT(catalog.materials()[1].available,
                "Materials should use the containing effect's supplied preview project");

    fs::remove_all(root, error);
}

int main(int argc, char* argv[]) {
    (void)argc;
    (void)argv;
    std::cout << "Running diagnostic subsystem unit tests...\n";

    test_cli_parsing();
    test_shader_source_metadata();
    test_effect_configuration_precedence();
    test_particle_parser();
    test_image_parser();
    test_sandbox_catalog();
    test_uniform_provenance_json();
    test_image_stats();
    test_render_graph_validation();

    std::cout << "\nTest Results: " << g_test_passed << " passed, " << g_test_failed << " failed.\n";
    return (g_test_failed == 0) ? 0 : 1;
}
