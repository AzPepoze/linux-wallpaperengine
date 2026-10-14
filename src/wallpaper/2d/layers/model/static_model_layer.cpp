#include "static_model_layer.h"

#include <stdlib.h>
#include <string.h>

#include "model_material.h"
#include "shared/assets/asset_manager.h"
#include "shared/core/engine_context.h"
#include "shared/core/logger.h"
#include "shared/core/utils.h"
#include "shared/core/vfs.h"
#include "shared/graphics/render.h"
#include "wallpaper/2d/camera/perspective_camera.h"

namespace {

constexpr int kTextureSlots = 12;
constexpr const char* kReflectionTarget = "_rt_Reflection";

// Uniform blocks 0-3 as the shader compiler declares them for every shader.
struct MeshUniforms {
    builtin_uniforms_t builtins;
    struct {
        uint8_t builtins[sizeof(builtin_uniforms_t) - sizeof(mat4x4)];
        float tint[4];
    } fragment;
    particle_builtin_uniforms_t object;
};

cJSON* firstPass(cJSON* material) {
    cJSON* passes = cJSON_GetObjectItemCaseSensitive(material, "passes");
    if (cJSON_IsArray(passes) && cJSON_GetArraySize(passes) > 0) return cJSON_GetArrayItem(passes, 0);
    return material;
}

GfxBuffer makeImmutableBuffer(const void* data, size_t bytes, bool index) {
    sg_buffer_desc desc = {};
    desc.data = {data, bytes};
    desc.usage.vertex_buffer = !index;
    desc.usage.index_buffer = index;
    return GfxBuffer(sg_make_buffer(&desc));
}

sg_view viewOr(const GfxView& view, sg_view fallback) {
    return view.id != SG_INVALID_ID ? (sg_view)view : fallback;
}

// g_Texture1 and up; a render target such as _rt_Reflection is not loaded and gets the fallback until bound.
sg_view extraTextureView(const PassTextures& textures, size_t index, sg_view fallback) {
    if (index >= textures.cached_views.size()) return fallback;
    return viewOr(textures.cached_views[index], fallback);
}

float viewAspect(const EngineContext& ctx) {
    if (ctx.scene.physical_view_width <= 0.0f || ctx.scene.physical_view_height <= 0.0f) return 1.0f;
    return ctx.scene.physical_view_width / ctx.scene.physical_view_height;
}

void fillFrameBuiltins(const EngineContext& ctx, builtin_uniforms_t& out) {
    out = {};
    for (vec4& resolution : out.texture_resolutions) resolution[0] = resolution[1] = resolution[2] = resolution[3] = 1;
    out.time = ctx.time;
    out.frametime = ctx.frametime;
    out.screen_res[0] = ctx.scene.physical_view_width;
    out.screen_res[1] = ctx.scene.physical_view_height;
    if (ctx.scene.physical_view_width > 0.0f) out.texel_size[0] = 1.0f / ctx.scene.physical_view_width;
    if (ctx.scene.physical_view_height > 0.0f) out.texel_size[1] = 1.0f / ctx.scene.physical_view_height;
    out.pointer_position[0] = 0.5f;
    out.pointer_position[1] = 0.5f;
    mat4x4_identity(out.effect_texture_projection);
    mat4x4_identity(out.effect_texture_projection_inverse);
    for (int channel = 0; channel < 3; ++channel) {
        out.light_ambient_color[channel] = ctx.scene.general.ambient_color[channel];
        out.light_skylight_color[channel] = ctx.scene.general.skylight_color[channel];
    }
    out.light_ambient_color[3] = 1.0f;
    out.light_skylight_color[3] = 1.0f;
}

void sceneCamera(const EngineContext& ctx, PerspectiveCamera& out) {
    buildPerspectiveCamera(ctx.scene.camera, ctx.scene.general, viewAspect(ctx), out);
}

void fillMeshUniforms(const EngineContext& ctx, const PerspectiveCamera& camera, const mat4x4 model,
                      MeshUniforms& out) {
    fillFrameBuiltins(ctx, out.builtins);
    mat4x4_mul(out.builtins.mvp, camera.view_projection, model);
    mat4x4_invert(out.builtins.mvp_inverse, out.builtins.mvp);

    memcpy(out.fragment.builtins, reinterpret_cast<const uint8_t*>(&out.builtins) + sizeof(mat4x4),
           sizeof(out.fragment.builtins));
    out.fragment.tint[0] = out.fragment.tint[1] = out.fragment.tint[2] = out.fragment.tint[3] = 1.0f;

    out.object = {};
    mat4x4_dup(out.object.model_matrix, model);
    mat4x4_invert(out.object.model_matrix_inverse, model);
    mat4x4_dup(out.object.view_projection_matrix, camera.view_projection);
    out.object.eye_position[0] = camera.eye[0];
    out.object.eye_position[1] = camera.eye[1];
    out.object.eye_position[2] = camera.eye[2];
}

void applyMeshUniforms(const MeshUniforms& uniforms) {
    const sg_range mvp = SG_RANGE(uniforms.builtins.mvp);
    sg_apply_uniforms(0, &mvp);
    const sg_range vertex_builtins = {reinterpret_cast<const uint8_t*>(&uniforms.builtins) + sizeof(mat4x4),
                                      sizeof(builtin_uniforms_t) - sizeof(mat4x4)};
    sg_apply_uniforms(1, &vertex_builtins);
    const sg_range fragment_builtins = SG_RANGE(uniforms.fragment);
    sg_apply_uniforms(2, &fragment_builtins);
    const sg_range object = SG_RANGE(uniforms.object);
    sg_apply_uniforms(3, &object);
}

}  // namespace

StaticModelLayer* StaticModelLayer::createFromDocument(const wallpaper_engine::SceneObjectDocument& doc,
                                                       EngineContext& ctx) {
    auto* layer = new StaticModelLayer(doc.name.empty() ? "Model" : doc.name.c_str());
    layer->initFromDocument(doc, ctx, false);
    layer->reflected_ = doc.model.reflected;
    buildModelMatrix(layer->model_matrix_, doc.node.origin, doc.node.angles, doc.node.scale);
    if (!layer->loadMesh(doc.model.model.c_str(), ctx)) {
        delete layer;
        return nullptr;
    }
    return layer;
}

bool StaticModelLayer::loadMesh(const char* mdl_path, EngineContext& ctx) {
    char absolute_path[1024];
    std::vector<uint8_t> bytes;
    if (!ctx.asset_mgr->resolvePath(mdl_path, absolute_path, sizeof(absolute_path)) ||
        !vfs::readAll(absolute_path, bytes)) {
        LOG_TAG_W("MODEL", "Model file not found: %s", mdl_path);
        return false;
    }
    wallpaper_engine::MdlMesh mesh;
    if (!wallpaper_engine::parseMdlMesh(bytes.data(), bytes.size(), mesh)) {
        LOG_TAG_W("MODEL", "Not a static model mesh: %s", mdl_path);
        return false;
    }

    std::vector<bool> translucent;
    for (const wallpaper_engine::MdlSubmesh& submesh : mesh.submeshes) {
        if (submesh.materials.empty() || !submeshDrawable(submesh, mesh.indices.size())) continue;
        const int material = materialFor(submesh.materials.front(), ctx);
        if (material < 0) continue;
        draws_.push_back({submesh.first_index, submesh.index_count, (size_t)material});
    }
    for (const Material& material : materials_) translucent.push_back(material.translucent);
    orderMeshDraws(draws_, translucent);
    if (draws_.empty()) {
        LOG_TAG_W("MODEL", "Model has no drawable submesh: %s", mdl_path);
        return false;
    }

    const std::vector<mesh_vertex_t> vertices = packMeshVertices(mesh);
    vertex_buffer_ = makeImmutableBuffer(vertices.data(), vertices.size() * sizeof(mesh_vertex_t), false);
    index_buffer_ = makeImmutableBuffer(mesh.indices.data(), mesh.indices.size() * sizeof(uint32_t), true);
    for (Material& material : materials_) bindMaterial(ctx, material);
    LOG_TAG_I("MODEL", "Loaded model %s (%zu vertices, %zu triangles, %zu draws)", mdl_path, vertices.size(),
              mesh.indices.size() / 3, draws_.size());
    return vertex_buffer_.id != SG_INVALID_ID && index_buffer_.id != SG_INVALID_ID;
}

int StaticModelLayer::materialFor(const std::string& path, EngineContext& ctx) {
    for (size_t index = 0; index < materials_.size(); ++index) {
        if (materials_[index].path == path) return materials_[index].pipeline.id != SG_INVALID_ID ? (int)index : -1;
    }
    Material material;
    material.path = path;
    if (!loadMaterial(path, ctx, material)) LOG_TAG_W("MODEL", "Model material is not drawable: %s", path.c_str());
    materials_.push_back(std::move(material));
    return materials_.back().pipeline.id != SG_INVALID_ID ? (int)materials_.size() - 1 : -1;
}

bool StaticModelLayer::loadMaterial(const std::string& path, EngineContext& ctx, Material& out) const {
    char absolute_path[1024];
    if (!ctx.asset_mgr->resolvePath(path.c_str(), absolute_path, sizeof(absolute_path))) return false;
    char* text = read_file_to_string(absolute_path);
    if (!text) return false;
    // Material files may carry // comments, which cJSON_Parse rejects.
    cJSON_Minify(text);
    cJSON* document = cJSON_Parse(text);
    free(text);
    if (!document) return false;

    cJSON* pass = firstPass(document);
    const MeshPipelineState state = parseMeshPipelineState(pass);
    out.reflection_slots = textureSlotsNamed(pass, kReflectionTarget);
    out.pass = std::make_unique<ShaderPass>(pass, nullptr, ctx);
    out.pass->mesh_geometry = true;
    out.pass->combos = upperCaseCombos(out.pass->combos);
    for (auto& [name, values] : resolveUserShaderValues(pass, ctx.user_properties)) out.pass->uniforms[name] = values;
    cJSON_Delete(document);

    out.pass->init(ctx);
    out.pipeline = ShaderCompiler::makeMeshPipeline(out.pass->compiled.shader, state);
    out.translucent = state.blend != ShaderBlendMode::Disabled;
    return out.pipeline.id != SG_INVALID_ID;
}

void StaticModelLayer::bindMaterial(EngineContext& ctx, Material& material) const {
    if (!material.pass) return;
    const PassTextures& textures = material.pass->pass_textures;
    sg_bindings& bindings = material.bindings;
    bindings.vertex_buffers[0] = vertex_buffer_;
    bindings.index_buffer = index_buffer_;
    bindings.views[0] = viewOr(textures.texture0_view, ctx.renderer.white_view);
    for (int slot = 1; slot < kTextureSlots; ++slot)
        bindings.views[slot] = extraTextureView(textures, (size_t)slot - 1, ctx.renderer.black_view);
    for (int slot = 0; slot < kTextureSlots; ++slot) bindings.samplers[slot] = ctx.renderer.smp_repeat;
}

bool StaticModelLayer::samplesReflection() const {
    for (const Material& material : materials_)
        if (material.reflection_slots != 0) return true;
    return false;
}

void StaticModelLayer::setReflectionView(sg_view view, EngineContext& ctx) {
    const sg_view bound = view.id != SG_INVALID_ID ? view : (sg_view)ctx.renderer.black_view;
    for (Material& material : materials_) {
        for (int slot = 0; slot < kTextureSlots; ++slot) {
            if ((material.reflection_slots & (1u << slot)) == 0) continue;
            material.bindings.views[slot] = bound;
            material.bindings.samplers[slot] = ctx.renderer.smp_clamp;
        }
    }
}

void StaticModelLayer::draw(EngineContext& ctx) {
    PerspectiveCamera camera;
    sceneCamera(ctx, camera);
    drawWithCamera(ctx, camera);
}

void StaticModelLayer::drawReflection(EngineContext& ctx, float plane_y) {
    PerspectiveCamera camera;
    sceneCamera(ctx, camera);
    PerspectiveCamera mirrored;
    buildReflectedCamera(camera, plane_y, mirrored);
    drawWithCamera(ctx, mirrored);
}

void StaticModelLayer::drawWithCamera(EngineContext& ctx, const PerspectiveCamera& camera) {
    MeshUniforms uniforms;
    fillMeshUniforms(ctx, camera, model_matrix_, uniforms);
    for (const MeshDraw& draw : draws_) {
        Material& material = materials_[draw.material];
        pipeline_variants::apply(material.pipeline);
        sg_apply_bindings(&material.bindings);
        applyMeshUniforms(uniforms);
        material.pass->applyCompiledUniforms();
        sg_draw((int)draw.first_index, (int)draw.index_count, 1);
        ctx.renderer.draw_calls++;
    }
}
