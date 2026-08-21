#ifndef UNIFORM_PROVENANCE_H
#define UNIFORM_PROVENANCE_H

#include <cjson/cJSON.h>

#include <map>
#include <string>
#include <vector>

enum class ProvenanceSource {
    ShaderMetadataDefault,
    MaterialConstant,
    EffectPassOverride,
    InstanceOverride,
    RuntimeBuiltin,
    RuntimeInferred,
    Fallback,
    Unresolved
};

const char* provenanceSourceToString(ProvenanceSource src);

struct UniformResolutionStep {
    ProvenanceSource source = ProvenanceSource::Unresolved;
    std::string source_name;
    bool present = false;
    bool applied = false;
    std::vector<float> values;
    std::string note;
};

struct UniformProvenanceEntry {
    std::string shader_name;
    std::string authored_name;
    std::string resolved_name;
    std::string type = "float";
    std::vector<UniformResolutionStep> resolution;
    ProvenanceSource final_source = ProvenanceSource::Unresolved;
    std::vector<float> final_value;

    cJSON* toJson() const;
};

struct ComboResolutionStep {
    ProvenanceSource source = ProvenanceSource::Unresolved;
    std::string source_name;
    bool present = false;
    bool applied = false;
    int value = 0;
    std::string note;
};

struct ComboProvenanceEntry {
    std::string name;
    std::vector<ComboResolutionStep> resolution;
    ProvenanceSource final_source = ProvenanceSource::Unresolved;
    int final_value = 0;

    cJSON* toJson() const;
};

struct PassUniformProvenance {
    std::string effect_file;
    int pass_index = 0;
    std::string shader_name;
    std::map<std::string, UniformProvenanceEntry> uniforms;
    std::map<std::string, ComboProvenanceEntry> combos;

    cJSON* toJson() const;
};

#endif  // UNIFORM_PROVENANCE_H
