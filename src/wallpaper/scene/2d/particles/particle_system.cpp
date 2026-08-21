#include "particle_system.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>

#include "core/engine_context.h"
#include "core/logger.h"
#include "core/utils.h"
#include "formats/wallpaper_engine/texture/tex_decoder.h"
#include "render/shader/shader_compiler.h"
#include "wallpaper/scene/2d/effects/effect.h"
#include "wallpaper/scene/2d/parser/particle_parser.h"

#define TAG "PARTICLE"

namespace {

bool materialUsesAdditiveBlend(const std::string& material_path, EngineContext& ctx, bool fallback) {
    char absolute_path[1024];
    if (material_path.empty() || !ctx.asset_mgr.resolvePath(material_path.c_str(), absolute_path, sizeof(absolute_path)))
        return fallback;

    char* text = read_file_to_string(absolute_path);
    if (!text) return fallback;
    cJSON* document = cJSON_Parse(text);
    free(text);
    if (!document) return fallback;

    const cJSON* pass = document;
    const cJSON* passes = cJSON_GetObjectItemCaseSensitive(document, "passes");
    if (cJSON_IsArray(passes) && cJSON_GetArraySize(passes) > 0) pass = cJSON_GetArrayItem(passes, 0);
    const cJSON* blending = cJSON_GetObjectItemCaseSensitive(pass, "blending");
    const bool has_blending = cJSON_IsString(blending) && blending->valuestring;
    const bool additive = has_blending && strcmp(blending->valuestring, "additive") == 0;
    cJSON_Delete(document);
    return has_blending ? additive : fallback;
}

void inferSpriteSheet(const wallpaper_engine::TextureMetadata& metadata, const ParticleSystemConfig& config,
                      int& cols, int& rows, int& frames) {
    cols = (int)metadata.spritesheet_cols;
    rows = (int)metadata.spritesheet_rows;
    frames = (int)metadata.spritesheet_frames;
    if (frames > 1 || metadata.width == 0 || metadata.height == 0) return;

    // Some Workshop particle atlases are static TEX containers with no TEXS table.
    // Only infer an atlas when it is an unambiguous strip of square frames.
    if (config.animation_mode == "randomframe" || config.animation_mode == "sequence" || config.animation_mode == "once") {
        if (metadata.width > metadata.height && metadata.width % metadata.height == 0) {
            const uint32_t candidate = metadata.width / metadata.height;
            if (candidate > 1) {
                cols = (int)candidate;
                rows = 1;
                frames = (int)candidate;
            }
        } else if (metadata.height > metadata.width && metadata.height % metadata.width == 0) {
            const uint32_t candidate = metadata.height / metadata.width;
            if (candidate > 1) {
                cols = 1;
                rows = (int)candidate;
                frames = (int)candidate;
            }
        }
    }
}

}  // namespace

ParticleSystem::ParticleSystem(ParticleSystemConfig config, float scene_width, float scene_height)
    : config(std::move(config)), scene_w(scene_width), scene_h(scene_height) {
    max_particles = std::max(0, this->config.max_particles);
    particles.reserve(max_particles);
    is_additive = this->config.additive;
    emitter_timers.resize(this->config.emitters.size(), 0.0f);
}

ParticleSystem::~ParticleSystem() {
    delete material_pass;
    for (ParticleSystem* child : children) delete child;
}

void ParticleSystem::initParticleBuffers() {
    if (max_particles <= 0) return;

    sg_buffer_desc vertex_desc = {};
    vertex_desc.size = (size_t)max_particles * 4 * 17 * sizeof(float);
    vertex_desc.usage.vertex_buffer = true;
    vertex_desc.usage.stream_update = true;
    particle_vertex_buffer = sg_make_buffer(&vertex_desc);

    sg_buffer_desc index_desc = {};
    index_desc.size = (size_t)max_particles * 6 * sizeof(uint32_t);
    index_desc.usage.index_buffer = true;
    index_desc.usage.stream_update = true;
    particle_index_buffer = sg_make_buffer(&index_desc);
}

ParticleSystem* ParticleSystem::createFromPath(const char* particle_path, EngineContext& ctx, float scene_width,
                                               float scene_height, float override_alpha, float override_rate) {
    if (!particle_path || !particle_path[0]) return nullptr;
    char absolute_path[1024];
    if (!ctx.asset_mgr.resolvePath(particle_path, absolute_path, sizeof(absolute_path))) return nullptr;
    char* document_text = read_file_to_string(absolute_path);
    if (!document_text) return nullptr;
    cJSON* document = cJSON_Parse(document_text);
    free(document_text);
    if (!document) return nullptr;

    ParticleSystemConfig config = ParticleParser::parse(document);
    cJSON_Delete(document);

    ParticleSystem* particle_system = new ParticleSystem(std::move(config), scene_width, scene_height);
    particle_system->config_path = absolute_path;
    particle_system->override_alpha = override_alpha;
    particle_system->override_rate = override_rate;
    particle_system->is_additive =
        materialUsesAdditiveBlend(particle_system->config.material_path, ctx, particle_system->config.additive);

    if (!particle_system->config.material_path.empty()) {
        cJSON* material_reference = cJSON_CreateObject();
        cJSON_AddStringToObject(material_reference, "material", particle_system->config.material_path.c_str());
        particle_system->material_pass = new ShaderPass(material_reference, nullptr, ctx);
        cJSON_Delete(material_reference);

        ShaderPass* pass = particle_system->material_pass;
        pass->effect_file = particle_system->config.material_path;
        pass->combos["THICKFORMAT"] = 1;

        particle_system->has_refract = pass->combos.count("REFRACT") && pass->combos.at("REFRACT") != 0;
        if (particle_system->has_refract) {
            // generic particle refraction samples the scene snapshot through g_Texture3.
            pass->render_texture_bindings[3] = "_rt_FullFrameBuffer";
        }

        if (pass->pass_textures.texture0.id == SG_INVALID_ID) {
            std::string fallback_path;
            pass->pass_textures.texture0 = ctx.asset_mgr.resolveTexture("materials/particle.tex", &fallback_path);
            pass->pass_textures.texture0_path = fallback_path;
        }
        particle_system->texture_path = pass->pass_textures.texture0_path;

        const wallpaper_engine::TextureMetadata metadata =
            wallpaper_engine::inspectTextureMetadata(particle_system->texture_path.c_str());
        particle_system->texture_width = (int)metadata.width;
        particle_system->texture_height = (int)metadata.height;
        particle_system->spritesheet_duration = metadata.spritesheet_duration;
        inferSpriteSheet(metadata, particle_system->config, particle_system->spritesheet_cols,
                         particle_system->spritesheet_rows, particle_system->spritesheet_frames);
        if (particle_system->spritesheet_frames > 1) pass->combos["SPRITESHEET"] = 1;

        pass->init(ctx);
        if (pass->compiled.shader.id != SG_INVALID_ID && pass->compiled.vertex_layout == ShaderVertexLayout::ParticleSprite) {
            const ShaderBlendMode blend =
                particle_system->is_additive ? ShaderBlendMode::Additive : ShaderBlendMode::Alpha;
            pass->compiled.pipeline = ShaderCompiler::makePipeline(pass->compiled.shader, pass->compiled.vertex_layout, blend);
        }

        if ((particle_system->texture_width <= 0 || particle_system->texture_height <= 0) &&
            pass->pass_textures.texture0.id != SG_INVALID_ID) {
            const sg_image_desc image_desc = sg_query_image_desc(pass->pass_textures.texture0);
            particle_system->texture_width = image_desc.width;
            particle_system->texture_height = image_desc.height;
        }
    }

    particle_system->initParticleBuffers();

    for (const ParticleObjectConfig& child : particle_system->config.children) {
        ParticleSystem* child_system = createFromPath(child.particle_path.c_str(), ctx, scene_width, scene_height,
                                                      child.override_alpha, child.override_rate);
        if (child_system) particle_system->children.push_back(child_system);
    }
    for (float time = 0.0f; time < particle_system->config.start_time; time += 0.1f) particle_system->update(0.1f);
    return particle_system;
}

ParticleSystem* ParticleSystem::createFromJSON(cJSON* document, EngineContext& ctx, float scene_width,
                                               float scene_height) {
    const ParticleObjectConfig config = ParticleParser::parseObject(document);
    return createFromPath(config.particle_path.c_str(), ctx, scene_width, scene_height, config.override_alpha,
                          config.override_rate);
}

bool ParticleSystem::requiresSceneColor() const {
    if (has_refract) return true;
    for (const ParticleSystem* child : children) {
        if (child->requiresSceneColor()) return true;
    }
    return false;
}

void ParticleSystem::setSceneColorView(sg_view view) {
    scene_color_view = view;
    for (ParticleSystem* child : children) child->setSceneColorView(view);
}
