#include "smaa_pass.h"

#include <string.h>

#include <initializer_list>
#include <string>

#include "AreaTex.h"
#include "SearchTex.h"
#include "linmath.h"
#include "shader/shader_backend.h"

namespace {
#include "smaa_source.inc"

// The vertex stage has no samplers, so the shader compiler emits no sampler type for it. The shared SMAA helpers still
// take sampler parameters and must compile there, so the vertex library declares the same type and helpers itself.
const std::string kVertexSamplerTypes =
    "struct LweCombinedSampler2D { Texture2D t; SamplerState s; };\n"
    "vec4 texture(LweCombinedSampler2D sampler, vec2 uv) { return sampler.t.Sample(sampler.s, uv); }\n"
    "vec4 textureLod(LweCombinedSampler2D sampler, vec2 uv, float lod) { return sampler.t.SampleLevel(sampler.s, uv, lod); }\n";

// The SMAA library, with only the stage's entry points compiled.
std::string smaaLibrary(bool vertex_stage) {
    return "#version 330\n"
           "#define SMAA_GLSL_3 1\n"
           "#define SMAA_PRESET_HIGH 1\n"
           "#define SMAA_RT_METRICS smaa_metrics\n" +
           std::string(vertex_stage ? "#define SMAA_INCLUDE_PS 0\n" : "#define SMAA_INCLUDE_VS 0\n") +
           (vertex_stage ? kVertexSamplerTypes : std::string()) +
           "uniform vec4 smaa_metrics;\n" + std::string(kSmaaHlsl);
}

const std::string kEdgeVertex = R"GLSL(
layout(location = 0) in vec2 position;
layout(location = 1) in vec2 texcoord0;
uniform mat4 mvp;
out vec2 uv;
out vec4 offset[3];
void main() {
  gl_Position = mvp * vec4(position, 0.0, 1.0);
  uv = texcoord0;
  SMAAEdgeDetectionVS(uv, offset);
}
)GLSL";

const std::string kEdgeFragment = R"GLSL(
in vec2 uv;
in vec4 offset[3];
uniform sampler2D smaa_color;
out vec4 frag_color;
void main() {
  frag_color = vec4(SMAALumaEdgeDetectionPS(uv, offset, smaa_color), 0.0, 1.0);
}
)GLSL";

const std::string kBlendVertex = R"GLSL(
layout(location = 0) in vec2 position;
layout(location = 1) in vec2 texcoord0;
uniform mat4 mvp;
out vec2 uv;
out vec2 pixcoord;
out vec4 offset[3];
void main() {
  gl_Position = mvp * vec4(position, 0.0, 1.0);
  uv = texcoord0;
  SMAABlendingWeightCalculationVS(uv, pixcoord, offset);
}
)GLSL";

const std::string kBlendFragment = R"GLSL(
in vec2 uv;
in vec2 pixcoord;
in vec4 offset[3];
uniform sampler2D smaa_edges;
uniform sampler2D smaa_area;
uniform sampler2D smaa_search;
out vec4 frag_color;
void main() {
  frag_color = SMAABlendingWeightCalculationPS(uv, pixcoord, offset, smaa_edges, smaa_area, smaa_search, vec4(0.0));
}
)GLSL";

const std::string kNeighborhoodVertex = R"GLSL(
layout(location = 0) in vec2 position;
layout(location = 1) in vec2 texcoord0;
uniform mat4 mvp;
out vec2 uv;
out vec4 offset;
void main() {
  gl_Position = mvp * vec4(position, 0.0, 1.0);
  uv = texcoord0;
  SMAANeighborhoodBlendingVS(uv, offset);
}
)GLSL";

const std::string kNeighborhoodFragment = R"GLSL(
in vec2 uv;
in vec4 offset;
uniform sampler2D smaa_color;
uniform sampler2D smaa_blend;
out vec4 frag_color;
void main() {
  frag_color = SMAANeighborhoodBlendingPS(uv, offset, smaa_color, smaa_blend);
}
)GLSL";

// Uniform layout shared by the passes: slot 0 is mvp + metrics (vertex), slot 1 is metrics (fragment).
constexpr size_t kVertexBlockBytes = sizeof(mat4x4) + 4 * sizeof(float);
constexpr size_t kFragmentBlockBytes = 4 * sizeof(float);

sg_shader makeShader(const std::string& vertex, const std::string& fragment, const char* label,
                     std::initializer_list<const char*> textures) {
    sg_shader_desc desc = {};
    desc.uniform_blocks[0].stage = SG_SHADERSTAGE_VERTEX;
    desc.uniform_blocks[0].size = kVertexBlockBytes;
    desc.uniform_blocks[0].glsl_uniforms[0].glsl_name = "mvp";
    desc.uniform_blocks[0].glsl_uniforms[0].type = SG_UNIFORMTYPE_MAT4;
    desc.uniform_blocks[0].glsl_uniforms[1].glsl_name = "smaa_metrics";
    desc.uniform_blocks[0].glsl_uniforms[1].type = SG_UNIFORMTYPE_FLOAT4;

    desc.uniform_blocks[1].stage = SG_SHADERSTAGE_FRAGMENT;
    desc.uniform_blocks[1].size = kFragmentBlockBytes;
    desc.uniform_blocks[1].glsl_uniforms[0].glsl_name = "smaa_metrics";
    desc.uniform_blocks[1].glsl_uniforms[0].type = SG_UNIFORMTYPE_FLOAT4;

    int slot = 0;
    for (const char* name : textures) {
        desc.views[slot].texture.stage = SG_SHADERSTAGE_FRAGMENT;
        desc.views[slot].texture.image_type = SG_IMAGETYPE_2D;
        desc.samplers[slot].stage = SG_SHADERSTAGE_FRAGMENT;
        desc.samplers[slot].sampler_type = SG_SAMPLERTYPE_FILTERING;
        desc.texture_sampler_pairs[slot].stage = SG_SHADERSTAGE_FRAGMENT;
        desc.texture_sampler_pairs[slot].glsl_name = name;
        desc.texture_sampler_pairs[slot].view_slot = slot;
        desc.texture_sampler_pairs[slot].sampler_slot = slot;
        ++slot;
    }
    return create_backend_shader(&desc, vertex, fragment, label);
}

sg_pipeline makePipeline(sg_shader shader) {
    sg_pipeline_desc desc = {};
    desc.shader = shader;
    desc.layout.attrs[0].format = SG_VERTEXFORMAT_FLOAT2;
    desc.layout.attrs[1].format = SG_VERTEXFORMAT_FLOAT2;
    desc.index_type = SG_INDEXTYPE_UINT16;
    // The SMAA targets have no depth buffer.
    desc.depth.pixel_format = SG_PIXELFORMAT_NONE;
    return sg_make_pipeline(&desc);
}

GfxView makeTextureView(sg_image image) {
    sg_view_desc desc = {};
    desc.texture.image = image;
    return sg_make_view(&desc);
}

GfxView makeAttachmentView(sg_image image) {
    sg_view_desc desc = {};
    desc.color_attachment.image = image;
    return sg_make_view(&desc);
}
}  // namespace

sg_view SmaaPass::apply(renderer_t* r, sg_view source, int width, int height) {
    if (width <= 0 || height <= 0) return {SG_INVALID_ID};
    if (!ensureLookupTables() || !ensurePipelines() || !ensureTargets(width, height)) return {SG_INVALID_ID};

    // Pass 1 finds edges, pass 2 turns them into blend weights, pass 3 blends the neighbourhood.
    draw(r, edges_attachment_, edge_pipeline_, &source, 1, width, height);
    const sg_view blend_inputs[3] = {edges_view_, area_view_, search_view_};
    draw(r, blend_attachment_, blend_pipeline_, blend_inputs, 3, width, height);
    const sg_view neighborhood_inputs[2] = {source, blend_view_};
    draw(r, output_attachment_, neighborhood_pipeline_, neighborhood_inputs, 2, width, height);
    return output_view_;
}

bool SmaaPass::ensureLookupTables() {
    if (area_image_.id != SG_INVALID_ID) return true;

    sg_image_desc area = {};
    area.width = AREATEX_WIDTH;
    area.height = AREATEX_HEIGHT;
    area.pixel_format = SG_PIXELFORMAT_RG8;
    area.data.mip_levels[0] = {areaTexBytes, sizeof(areaTexBytes)};
    area_image_ = sg_make_image(&area);

    sg_image_desc search = {};
    search.width = SEARCHTEX_WIDTH;
    search.height = SEARCHTEX_HEIGHT;
    search.pixel_format = SG_PIXELFORMAT_R8;
    search.data.mip_levels[0] = {searchTexBytes, sizeof(searchTexBytes)};
    search_image_ = sg_make_image(&search);

    if (area_image_.id == SG_INVALID_ID || search_image_.id == SG_INVALID_ID) return false;
    area_view_ = makeTextureView(area_image_);
    search_view_ = makeTextureView(search_image_);
    return area_view_.id != SG_INVALID_ID && search_view_.id != SG_INVALID_ID;
}

bool SmaaPass::ensurePipelines() {
    if (edge_pipeline_.id != SG_INVALID_ID) return true;

    const std::string vertex_library = smaaLibrary(true);
    const std::string fragment_library = smaaLibrary(false);
    edge_pipeline_ = makePipeline(
        makeShader(vertex_library + kEdgeVertex, fragment_library + kEdgeFragment, "smaa-edge", {"smaa_color"}));
    blend_pipeline_ = makePipeline(makeShader(vertex_library + kBlendVertex, fragment_library + kBlendFragment, "smaa-blend",
                                              {"smaa_edges", "smaa_area", "smaa_search"}));
    neighborhood_pipeline_ = makePipeline(makeShader(vertex_library + kNeighborhoodVertex,
                                                     fragment_library + kNeighborhoodFragment, "smaa-neighborhood",
                                                     {"smaa_color", "smaa_blend"}));
    return edge_pipeline_.id != SG_INVALID_ID && blend_pipeline_.id != SG_INVALID_ID &&
           neighborhood_pipeline_.id != SG_INVALID_ID;
}

bool SmaaPass::ensureTargets(int width, int height) {
    if (edges_image_.id != SG_INVALID_ID && width == target_width_ && height == target_height_) return true;

    const auto makeTarget = [&](GfxImage& image, GfxView& texture, GfxView& attachment) {
        sg_image_desc desc = {};
        desc.usage.color_attachment = true;
        desc.width = width;
        desc.height = height;
        desc.pixel_format = SG_PIXELFORMAT_RGBA8;
        image = sg_make_image(&desc);
        if (image.id == SG_INVALID_ID) return false;
        texture = makeTextureView(image);
        attachment = makeAttachmentView(image);
        return texture.id != SG_INVALID_ID && attachment.id != SG_INVALID_ID;
    };
    if (!makeTarget(edges_image_, edges_view_, edges_attachment_)) return false;
    if (!makeTarget(blend_image_, blend_view_, blend_attachment_)) return false;
    if (!makeTarget(output_image_, output_view_, output_attachment_)) return false;
    target_width_ = width;
    target_height_ = height;
    return true;
}

void SmaaPass::draw(renderer_t* r, sg_view target, sg_pipeline pipeline, const sg_view* inputs, int input_count,
                    int width, int height) {
    mat4x4 proj, model, mvp;
    mat4x4_ortho(proj, 0, (float)width, (float)height, 0, -1.0f, 1.0f);
    mat4x4_identity(model);
    mat4x4_scale_aniso(model, model, (float)width, (float)height, 1.0f);
    mat4x4_mul(mvp, proj, model);

    // Every SMAA pass renders at the output size, so one metrics vector serves all of them.
    const float metrics[4] = {1.0f / width, 1.0f / height, (float)width, (float)height};
    float vertex_block[kVertexBlockBytes / sizeof(float)];
    memcpy(vertex_block, mvp, sizeof(mat4x4));
    memcpy(vertex_block + sizeof(mat4x4) / sizeof(float), metrics, sizeof(metrics));

    sg_pass pass = {};
    pass.action.colors[0].load_action = SG_LOADACTION_CLEAR;
    pass.action.colors[0].clear_value = {0.0f, 0.0f, 0.0f, 0.0f};
    pass.attachments.colors[0] = target;
    sg_begin_pass(&pass);
    sg_apply_viewport(0, 0, width, height, true);
    sg_apply_scissor_rect(0, 0, width, height, true);
    sg_apply_pipeline(pipeline);

    sg_range vertex_range = {vertex_block, kVertexBlockBytes};
    sg_apply_uniforms(0, &vertex_range);
    sg_range fragment_range = {metrics, kFragmentBlockBytes};
    sg_apply_uniforms(1, &fragment_range);

    sg_bindings bind = {};
    bind.vertex_buffers[0] = r->vertex_buffer;
    bind.index_buffer = r->index_buffer;
    for (int i = 0; i < input_count; ++i) {
        bind.views[i] = inputs[i];
        bind.samplers[i] = r->smp_clamp;
    }
    sg_apply_bindings(&bind);
    sg_draw(0, 6, 1);
    sg_end_pass();
    r->draw_calls++;
}
