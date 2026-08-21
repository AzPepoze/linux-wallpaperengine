#include "particle_parser.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

void ParticleParser::readVec3(const cJSON* node, vec3 out) {
    if (cJSON_IsString(node) && node->valuestring) {
        sscanf(node->valuestring, "%f %f %f", &out[0], &out[1], &out[2]);
    } else if (cJSON_IsArray(node) && cJSON_GetArraySize(node) >= 3) {
        for (int i = 0; i < 3; ++i) {
            const cJSON* value = cJSON_GetArrayItem(node, i);
            out[i] = cJSON_IsNumber(value) ? (float)value->valuedouble : 0.0f;
        }
    } else if (cJSON_IsNumber(node)) {
        out[0] = out[1] = out[2] = (float)node->valuedouble;
    } else {
        out[0] = out[1] = out[2] = 0.0f;
    }
}

float ParticleParser::readFloat(const cJSON* node) {
    if (!node) return 0.0f;
    if (cJSON_IsNumber(node)) return (float)node->valuedouble;
    return cJSON_IsString(node) && node->valuestring ? (float)atof(node->valuestring) : 0.0f;
}

ParticleObjectConfig ParticleParser::parseObject(const wallpaper_engine::SceneObjectDocument& document) {
    ParticleObjectConfig config;
    config.name = document.name.empty() ? "Particle" : document.name;
    config.particle_path = document.particle.particle;
    config.override_alpha = document.particle.override_alpha;
    config.override_rate = document.particle.override_rate;
    config.has_override_color = document.particle.has_override_color;
    config.override_color_is_legacy = document.particle.override_color_is_legacy;
    for (int i = 0; i < 3; ++i) config.override_color[i] = document.particle.override_color[i];
    return config;
}

ParticleObjectConfig ParticleParser::parseObject(const cJSON* document) {
    ParticleObjectConfig config;
    const cJSON* path = cJSON_GetObjectItemCaseSensitive(document, "particle");
    if (!path) path = cJSON_GetObjectItemCaseSensitive(document, "name");
    if (!path) path = cJSON_GetObjectItemCaseSensitive(document, "file");
    if (cJSON_IsString(path) && path->valuestring) config.particle_path = path->valuestring;
    const cJSON* overrides = cJSON_GetObjectItemCaseSensitive(document, "instanceoverride");
    if (cJSON_IsObject(overrides)) {
        const cJSON* alpha = cJSON_GetObjectItemCaseSensitive(overrides, "alpha");
        const cJSON* rate = cJSON_GetObjectItemCaseSensitive(overrides, "rate");
        if (alpha) config.override_alpha = readFloat(alpha);
        if (rate) config.override_rate = readFloat(rate);

        const cJSON* color = cJSON_GetObjectItemCaseSensitive(overrides, "color");
        const cJSON* colorn = cJSON_GetObjectItemCaseSensitive(overrides, "colorn");
        if (color) {
            readVec3(color, config.override_color);
            config.has_override_color = true;
            config.override_color_is_legacy = true;
        } else if (colorn) {
            readVec3(colorn, config.override_color);
            config.has_override_color = true;
            config.override_color_is_legacy = false;
        }
    }
    return config;
}

ParticleSystemConfig ParticleParser::parse(const cJSON* document) {
    ParticleSystemConfig config;
    const cJSON* material = cJSON_GetObjectItemCaseSensitive(document, "material");
    if (cJSON_IsString(material) && material->valuestring) config.material_path = material->valuestring;

    const cJSON* animation_mode = cJSON_GetObjectItemCaseSensitive(document, "animationmode");
    if (cJSON_IsString(animation_mode) && animation_mode->valuestring) config.animation_mode = animation_mode->valuestring;
    const cJSON* sequence_multiplier = cJSON_GetObjectItemCaseSensitive(document, "sequencemultiplier");
    if (sequence_multiplier) {
        config.sequence_multiplier = readFloat(sequence_multiplier);
        if (config.sequence_multiplier <= 0.0f) config.sequence_multiplier = 1.0f;
    }

    const cJSON* max_count = cJSON_GetObjectItemCaseSensitive(document, "maxcount");
    if (cJSON_IsNumber(max_count)) config.max_particles = max_count->valueint;
    const cJSON* flags = cJSON_GetObjectItemCaseSensitive(document, "flags");
    if (cJSON_IsNumber(flags)) config.flags = flags->valueint;

    const cJSON* renderers = cJSON_GetObjectItemCaseSensitive(document, "renderer");
    if (!cJSON_IsArray(renderers)) renderers = cJSON_GetObjectItemCaseSensitive(document, "renderers");
    const cJSON* renderer = cJSON_IsArray(renderers) ? cJSON_GetArrayItem(renderers, 0) : nullptr;
    if (cJSON_IsObject(renderer)) {
        const cJSON* type = cJSON_GetObjectItemCaseSensitive(renderer, "name");
        if (cJSON_IsString(type) && type->valuestring) config.renderer.type = type->valuestring;
        config.renderer.length = readFloat(cJSON_GetObjectItemCaseSensitive(renderer, "length"));
        config.renderer.max_length = readFloat(cJSON_GetObjectItemCaseSensitive(renderer, "maxlength"));
    }

    // Legacy particle files sometimes carry a pass directly. Keep this fallback,
    // but the referenced material is the authoritative source for rendering state.
    const cJSON* passes = cJSON_GetObjectItemCaseSensitive(document, "passes");
    const cJSON* pass = cJSON_IsArray(passes) ? cJSON_GetArrayItem(passes, 0) : nullptr;
    const cJSON* blending = cJSON_GetObjectItemCaseSensitive(pass, "blending");
    config.additive =
        cJSON_IsString(blending) && blending->valuestring && strcmp(blending->valuestring, "additive") == 0;
    config.start_time = readFloat(cJSON_GetObjectItemCaseSensitive(document, "starttime"));

    const cJSON* emitters = cJSON_GetObjectItemCaseSensitive(document, "emitter");
    cJSON* emitter;
    cJSON_ArrayForEach(emitter, emitters) {
        ParticleEmitterConfig emitter_config;
        const cJSON* type = cJSON_GetObjectItemCaseSensitive(emitter, "name");
        if (cJSON_IsString(type) && type->valuestring) emitter_config.type = type->valuestring;
        readVec3(cJSON_GetObjectItemCaseSensitive(emitter, "origin"), emitter_config.origin);
        readVec3(cJSON_GetObjectItemCaseSensitive(emitter, "distancemax"), emitter_config.distance_max);
        emitter_config.distance_min = readFloat(cJSON_GetObjectItemCaseSensitive(emitter, "distancemin"));
        emitter_config.rate = readFloat(cJSON_GetObjectItemCaseSensitive(emitter, "rate"));
        config.emitters.push_back(emitter_config);
    }

    const cJSON* initializers = cJSON_GetObjectItemCaseSensitive(document, "initializer");
    cJSON* initializer;
    cJSON_ArrayForEach(initializer, initializers) {
        ParticleInitializerConfig initializer_config;
        const cJSON* type = cJSON_GetObjectItemCaseSensitive(initializer, "name");
        if (cJSON_IsString(type) && type->valuestring) initializer_config.type = type->valuestring;
        readVec3(cJSON_GetObjectItemCaseSensitive(initializer, "min"), initializer_config.minimum);
        readVec3(cJSON_GetObjectItemCaseSensitive(initializer, "max"), initializer_config.maximum);
        initializer_config.minimum_scalar = readFloat(cJSON_GetObjectItemCaseSensitive(initializer, "min"));
        initializer_config.maximum_scalar = readFloat(cJSON_GetObjectItemCaseSensitive(initializer, "max"));
        config.initializers.push_back(initializer_config);
    }

    const cJSON* operators = cJSON_GetObjectItemCaseSensitive(document, "operator");
    cJSON* particle_operator;
    cJSON_ArrayForEach(particle_operator, operators) {
        ParticleOperatorConfig operator_config;
        const cJSON* type = cJSON_GetObjectItemCaseSensitive(particle_operator, "name");
        if (cJSON_IsString(type) && type->valuestring) operator_config.type = type->valuestring;
        readVec3(cJSON_GetObjectItemCaseSensitive(particle_operator, "gravity"), operator_config.gravity);
        operator_config.drag = readFloat(cJSON_GetObjectItemCaseSensitive(particle_operator, "drag"));
        operator_config.fade_in_time = readFloat(cJSON_GetObjectItemCaseSensitive(particle_operator, "fadeintime"));
        operator_config.fade_out_time = readFloat(cJSON_GetObjectItemCaseSensitive(particle_operator, "fadeouttime"));
        operator_config.frequency_min = readFloat(cJSON_GetObjectItemCaseSensitive(particle_operator, "frequencymin"));
        operator_config.frequency_max = readFloat(cJSON_GetObjectItemCaseSensitive(particle_operator, "frequencymax"));
        operator_config.scale_min = readFloat(cJSON_GetObjectItemCaseSensitive(particle_operator, "scalemin"));
        operator_config.scale_max = readFloat(cJSON_GetObjectItemCaseSensitive(particle_operator, "scalemax"));
        operator_config.speed_min = readFloat(cJSON_GetObjectItemCaseSensitive(particle_operator, "speedmin"));
        operator_config.speed_max = readFloat(cJSON_GetObjectItemCaseSensitive(particle_operator, "speedmax"));
        config.operators.push_back(operator_config);
    }

    const cJSON* children = cJSON_GetObjectItemCaseSensitive(document, "children");
    cJSON* child;
    cJSON_ArrayForEach(child, children) {
        config.children.push_back(parseObject(child));
    }
    return config;
}
