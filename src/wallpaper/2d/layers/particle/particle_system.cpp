#include "particle_system.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>

#include "particle_parser.h"
#include "shared/assets/tex_decoder.h"
#include "shared/core/engine_context.h"
#include "shared/core/logger.h"
#include "shared/core/utils.h"
#include "shared/graphics/passes/shader_pass.h"

#define TAG "PARTICLE"

namespace {
ParticleSpawnType parseSpawnType(const std::string& type) {
    if (type == "eventfollow") return ParticleSpawnType::EventFollow;
    if (type == "eventspawn") return ParticleSpawnType::EventSpawn;
    if (type == "eventdeath") return ParticleSpawnType::EventDeath;
    return ParticleSpawnType::Static;
}

bool materialUsesAdditiveBlend(const std::string& material_path, EngineContext& ctx, bool fallback) {
    char absolute_path[1024];
    if (material_path.empty() ||
        !ctx.asset_mgr->resolvePath(material_path.c_str(), absolute_path, sizeof(absolute_path)))
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

int wallpaperTextureFormatForImage(sg_image image) {
    if (image.id == SG_INVALID_ID) return 0;

    const sg_image_desc desc = sg_query_image_desc(image);
    switch (desc.pixel_format) {
        case SG_PIXELFORMAT_R8:
            return 9;
        case SG_PIXELFORMAT_RG8:
            return 8;
        case SG_PIXELFORMAT_BC1_RGBA:
            return 4;
        case SG_PIXELFORMAT_BC2_RGBA:
            return 5;
        case SG_PIXELFORMAT_BC3_RGBA:
            return 6;
        case SG_PIXELFORMAT_RGBA8:
        default:
            return 0;
    }
}

}  // namespace

ParticleSystem::~ParticleSystem() {
    delete material_pass;
    for (ParticleSystem* child : children) delete child;
}

void ParticleSystem::initParticleBuffers() {
    if (max_particles <= 0) return;

    sg_buffer_desc vertex_desc = {};
    const size_t segments = is_rope_trail ? trailSegments() : 1;
    vertex_desc.size = (size_t)max_particles * segments * 4 * (is_rope ? 26 : 17) * sizeof(float);
    vertex_desc.usage.vertex_buffer = true;
    vertex_desc.usage.stream_update = true;
    particle_vertex_buffer = sg_make_buffer(&vertex_desc);

    sg_buffer_desc index_desc = {};
    index_desc.size = (size_t)max_particles * segments * 6 * sizeof(uint32_t);
    index_desc.usage.index_buffer = true;
    index_desc.usage.stream_update = true;
    particle_index_buffer = sg_make_buffer(&index_desc);
}

ParticleSystem* ParticleSystem::createFromPath(const char* particle_path, EngineContext& ctx, float scene_width,
                                               float scene_height, const ParticleObjectConfig& overrides) {
    if (!particle_path || !particle_path[0]) return nullptr;
    char absolute_path[1024];
    if (!ctx.asset_mgr->resolvePath(particle_path, absolute_path, sizeof(absolute_path))) return nullptr;
    char* document_text = read_file_to_string(absolute_path);
    if (!document_text) return nullptr;
    cJSON* document = cJSON_Parse(document_text);
    free(document_text);
    if (!document) return nullptr;

    ParticleSystemConfig config = ParticleParser::parse(document);
    cJSON_Delete(document);

    ParticleSystem* particle_system = new ParticleSystem(std::move(config), scene_width, scene_height);
    particle_system->config_path = absolute_path;
    particle_system->override_alpha = overrides.override_alpha;
    particle_system->override_rate = overrides.override_rate;
    particle_system->override_size = overrides.override_size;
    particle_system->override_count = overrides.override_count;
    particle_system->override_speed = overrides.override_speed;
    particle_system->override_lifetime = overrides.override_lifetime;
    particle_system->override_color_is_legacy = overrides.override_color_is_legacy;

    constexpr int kDisableColor = 8, kDisableCount = 16, kDisableLifetime = 32, kDisableSize = 64, kDisableSpeed = 128;
    const int flags = particle_system->config.flags;
    if (flags & kDisableCount) particle_system->override_count = 1.0f;
    if (flags & kDisableLifetime) particle_system->override_lifetime = 1.0f;
    if (flags & kDisableSize) particle_system->override_size = 1.0f;
    if (flags & kDisableSpeed) particle_system->override_speed = 1.0f;
    if (overrides.has_override_color && !(flags & kDisableColor)) {
        particle_system->has_override_color = true;
        for (int component = 0; component < 3; ++component)
            particle_system->override_color[component] = overrides.override_color[component];
    }
    particle_system->is_trail =
        particle_system->config.renderer.type == "spritetrail" || particle_system->config.renderer.type == "trail";
    particle_system->use_perspective = (particle_system->config.flags & 4) != 0;
    particle_system->world_space = (particle_system->config.flags & 1) != 0;
    particle_system->is_additive =
        materialUsesAdditiveBlend(particle_system->config.material_path, ctx, particle_system->config.additive);

    if (!particle_system->config.material_path.empty()) {
        cJSON* material_reference = cJSON_CreateObject();
        cJSON_AddStringToObject(material_reference, "material", particle_system->config.material_path.c_str());
        particle_system->material_pass = new ShaderPass(material_reference, nullptr, ctx);
        cJSON_Delete(material_reference);

        ShaderPass* pass = particle_system->material_pass;
        if (particle_system->is_rope && pass->shader_name == "genericparticle") pass->shader_name = "genericropeparticle";
        pass->effect_file = particle_system->config.material_path;
        pass->combos["THICKFORMAT"] = 1;
        pass->combos["GS_ENABLED"] = 0;
        if (particle_system->is_rope_trail) pass->combos["TRAILRENDERER"] = 1;
        if (particle_system->is_trail) pass->combos["TRAILRENDERER"] = 1;

        particle_system->has_refract = pass->combos.count("REFRACT") && pass->combos.at("REFRACT") != 0;
        if (particle_system->has_refract) {
            // generic particle refraction samples the scene snapshot through g_Texture3.
            pass->render_texture_bindings[3] = "_rt_FullFrameBuffer";

            // Texture1 provides the refraction normal map.
            if (!pass->pass_textures.textures.empty() && pass->pass_textures.textures[0].id != SG_INVALID_ID)
                pass->combos["NORMALMAP"] = 1;
        }

        if (pass->pass_textures.texture0.id == SG_INVALID_ID) {
            std::string fallback_path;
            pass->pass_textures.texture0 = ctx.asset_mgr->resolveTexture("materials/particle.tex", &fallback_path);
            pass->pass_textures.texture0_path = fallback_path;
        }
        particle_system->texture_path = pass->pass_textures.texture0_path;

        const wallpaper_engine::TextureMetadata metadata =
            wallpaper_engine::inspectTextureMetadata(particle_system->texture_path.c_str());
        particle_system->texture_width = (int)metadata.width;
        particle_system->texture_height = (int)metadata.height;
        particle_system->spritesheet_duration = metadata.spritesheet_duration;
        // Only the TEXS frame table establishes an atlas. A non-square static
        // texture may be a beam or trail; splitting it into squares crops its
        // UVs and cancels the sprite's authored aspect ratio.
        particle_system->spritesheet_cols = (int)metadata.spritesheet_cols;
        particle_system->spritesheet_rows = (int)metadata.spritesheet_rows;
        particle_system->spritesheet_frames = (int)metadata.spritesheet_frames;
        if (particle_system->spritesheet_frames > 1) {
            pass->combos["SPRITESHEET"] = 1;
            // Particle flag bit 2 disables interpolation between sequence frames.
            if (particle_system->config.animation_mode == "sequence" && (particle_system->config.flags & 2) == 0)
                pass->combos["SPRITESHEETBLEND"] = 1;
        }

        // WE's generic particle shader treats single/dual-channel textures differently from RGBA;
        // keep that in TEX0FORMAT so ConvertTexture0Format() can turn R8 into an alpha mask.
        pass->combos["TEX0FORMAT"] = wallpaperTextureFormatForImage(pass->pass_textures.texture0);
        if (!pass->pass_textures.textures.empty()) {
            // The normal decoder uses Texture1's independently authored format.
            pass->combos["TEX1FORMAT"] = wallpaperTextureFormatForImage(pass->pass_textures.textures[0]);
        }

        pass->init(ctx);
        if (pass->compiled.shader.id != SG_INVALID_ID &&
            pass->compiled.vertex_layout != ShaderVertexLayout::Sprite2D) {
            const ShaderBlendMode blend =
                particle_system->is_additive ? ShaderBlendMode::Additive : ShaderBlendMode::Alpha;
            pass->compiled.pipeline =
                ShaderCompiler::makePipeline(pass->compiled.shader, pass->compiled.vertex_layout, blend);
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
        ParticleSystem* child_system =
            createFromPath(child.particle_path.c_str(), ctx, scene_width, scene_height, child);
        if (child_system) {
            child_system->spawn_type = parseSpawnType(child.type);
            for (int i = 0; i < 3; ++i) {
                child_system->child_offset[i] = child.origin[i];
                child_system->child_angles[i] = child.angles[i];
                child_system->child_scale[i] = child.scale[i];
            }
            child_system->child_maxcount = child.maxcount;
            child_system->child_probability = child.probability;
            child_system->parent_system = particle_system;
            if (child_system->spawn_type == ParticleSpawnType::EventFollow ||
                child_system->spawn_type == ParticleSpawnType::EventSpawn) {
                // The particle file's max count is the capacity of each instance; the system needs room for all of
                // its instances (the child entry's max count) at once.
                const int instances = std::clamp(child.maxcount, 1, 128);
                child_system->max_particles = std::max(child_system->max_particles, child_system->config.max_particles * instances);
                child_system->particles.reserve((size_t)child_system->max_particles);
                child_system->initParticleBuffers();
            }
            particle_system->children.push_back(child_system);
        }
    }
    // Pre-simulation needs the layer placement for worldspace systems, so it runs on the first update.
    particle_system->pending_warmup = particle_system->config.start_time;
    return particle_system;
}

ParticleSystem* ParticleSystem::createFromJSON(cJSON* document, EngineContext& ctx, float scene_width,
                                               float scene_height) {
    const ParticleObjectConfig config = ParticleParser::parseObject(document);
    return createFromPath(config.particle_path.c_str(), ctx, scene_width, scene_height, config);
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
