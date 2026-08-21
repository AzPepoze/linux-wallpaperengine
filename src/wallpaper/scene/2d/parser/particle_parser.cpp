#include "particle_parser.h"

#include <cstdio>
#include <cstdlib>

void ParticleParser::readVec3(const cJSON* node, vec3 out) {
    if (cJSON_IsString(node) && node->valuestring) {
        sscanf(node->valuestring, "%f %f %f", &out[0], &out[1], &out[2]);
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
