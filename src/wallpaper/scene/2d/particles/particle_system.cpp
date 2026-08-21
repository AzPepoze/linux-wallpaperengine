#include "particle_system.h"

#include <cstdlib>

#include "core/logger.h"
#include "core/utils.h"
#include "wallpaper/scene/2d/parser/particle_parser.h"

#define TAG "PARTICLE"

ParticleSystem::ParticleSystem(ParticleSystemConfig config, GfxImage texture, float scene_width, float scene_height)
    : config(std::move(config)), texture(std::move(texture)), scene_w(scene_width), scene_h(scene_height) {
    max_particles = this->config.max_particles;
    particles.reserve(max_particles);
    is_additive = this->config.additive;
    emitter_timers.resize(this->config.emitters.size(), 0.0f);
}

ParticleSystem::~ParticleSystem() {
    for (ParticleSystem* child : children) delete child;
}

ParticleSystem* ParticleSystem::createFromPath(const char* particle_path, const IAssetResolver& assets,
                                               float scene_width, float scene_height, float override_alpha,
                                               float override_rate) {
    if (!particle_path || !particle_path[0]) return nullptr;
    char absolute_path[1024];
    if (!assets.resolvePath(particle_path, absolute_path, sizeof(absolute_path))) return nullptr;
    char* document_text = read_file_to_string(absolute_path);
    if (!document_text) return nullptr;
    cJSON* document = cJSON_Parse(document_text);
    free(document_text);
    if (!document) return nullptr;

    ParticleSystemConfig config = ParticleParser::parse(document);
    std::string texture_path;
    GfxImage texture = assets.resolveTexture("materials/particle.tex", &texture_path);
    if (!config.material_path.empty()) {
        GfxImage material_texture = assets.resolveMaterialTexture(config.material_path.c_str(), &texture_path);
        if (material_texture.id != SG_INVALID_ID) texture = std::move(material_texture);
    }
    cJSON_Delete(document);

    ParticleSystem* particle_system =
        new ParticleSystem(std::move(config), std::move(texture), scene_width, scene_height);
    particle_system->config_path = absolute_path;
    particle_system->texture_path = texture_path;
    particle_system->override_alpha = override_alpha;
    particle_system->override_rate = override_rate;
    for (const ParticleObjectConfig& child : particle_system->config.children) {
        ParticleSystem* child_system = createFromPath(child.particle_path.c_str(), assets, scene_width, scene_height,
                                                      child.override_alpha, child.override_rate);
        if (child_system) particle_system->children.push_back(child_system);
    }
    for (float time = 0.0f; time < particle_system->config.start_time; time += 0.1f) particle_system->update(0.1f);
    return particle_system;
}

ParticleSystem* ParticleSystem::createFromJSON(cJSON* document, const IAssetResolver& assets, float scene_width,
                                               float scene_height) {
    const ParticleObjectConfig config = ParticleParser::parseObject(document);
    return createFromPath(config.particle_path.c_str(), assets, scene_width, scene_height, config.override_alpha,
                          config.override_rate);
}
