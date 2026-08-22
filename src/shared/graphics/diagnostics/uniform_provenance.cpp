#include "uniform_provenance.h"

const char* provenanceSourceToString(ProvenanceSource src) {
    switch (src) {
        case ProvenanceSource::ShaderMetadataDefault:
            return "shader_metadata_default";
        case ProvenanceSource::MaterialConstant:
            return "material_constant";
        case ProvenanceSource::EffectPassOverride:
            return "effect_pass_override";
        case ProvenanceSource::InstanceOverride:
            return "instance_override";
        case ProvenanceSource::RuntimeBuiltin:
            return "runtime_builtin";
        case ProvenanceSource::RuntimeInferred:
            return "runtime_inferred";
        case ProvenanceSource::Fallback:
            return "fallback";
        case ProvenanceSource::Unresolved:
        default:
            return "unresolved";
    }
}

cJSON* UniformProvenanceEntry::toJson() const {
    cJSON* root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "shader_name", shader_name.c_str());
    if (!authored_name.empty()) {
        cJSON_AddStringToObject(root, "authored_name", authored_name.c_str());
    }
    cJSON_AddStringToObject(root, "resolved_name", resolved_name.c_str());
    cJSON_AddStringToObject(root, "type", type.c_str());
    cJSON_AddStringToObject(root, "final_source", provenanceSourceToString(final_source));

    cJSON* final_val_arr = cJSON_CreateArray();
    for (float v : final_value) {
        cJSON_AddItemToArray(final_val_arr, cJSON_CreateNumber(v));
    }
    cJSON_AddItemToObject(root, "final_value", final_val_arr);

    cJSON* res_arr = cJSON_CreateArray();
    for (const auto& step : resolution) {
        cJSON* s = cJSON_CreateObject();
        cJSON_AddStringToObject(
            s, "source", step.source_name.empty() ? provenanceSourceToString(step.source) : step.source_name.c_str());
        cJSON_AddBoolToObject(s, "present", step.present);
        cJSON_AddBoolToObject(s, "applied", step.applied);
        if (step.present) {
            cJSON* vals = cJSON_CreateArray();
            for (float v : step.values) {
                cJSON_AddItemToArray(vals, cJSON_CreateNumber(v));
            }
            cJSON_AddItemToObject(s, "values", vals);
        }
        if (!step.note.empty()) {
            cJSON_AddStringToObject(s, "note", step.note.c_str());
        }
        cJSON_AddItemToArray(res_arr, s);
    }
    cJSON_AddItemToObject(root, "resolution", res_arr);

    return root;
}

cJSON* ComboProvenanceEntry::toJson() const {
    cJSON* root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "name", name.c_str());
    cJSON_AddStringToObject(root, "final_source", provenanceSourceToString(final_source));
    cJSON_AddNumberToObject(root, "final_value", final_value);

    cJSON* res_arr = cJSON_CreateArray();
    for (const auto& step : resolution) {
        cJSON* s = cJSON_CreateObject();
        cJSON_AddStringToObject(
            s, "source", step.source_name.empty() ? provenanceSourceToString(step.source) : step.source_name.c_str());
        cJSON_AddBoolToObject(s, "present", step.present);
        cJSON_AddBoolToObject(s, "applied", step.applied);
        if (step.present) {
            cJSON_AddNumberToObject(s, "value", step.value);
        }
        if (!step.note.empty()) {
            cJSON_AddStringToObject(s, "note", step.note.c_str());
        }
        cJSON_AddItemToArray(res_arr, s);
    }
    cJSON_AddItemToObject(root, "resolution", res_arr);

    return root;
}

cJSON* PassUniformProvenance::toJson() const {
    cJSON* root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "effect", effect_file.c_str());
    cJSON_AddNumberToObject(root, "pass_index", pass_index);
    cJSON_AddStringToObject(root, "shader", shader_name.c_str());

    cJSON* u_obj = cJSON_CreateObject();
    for (const auto& [name, entry] : uniforms) {
        cJSON_AddItemToObject(u_obj, name.c_str(), entry.toJson());
    }
    cJSON_AddItemToObject(root, "uniforms", u_obj);

    cJSON* c_obj = cJSON_CreateObject();
    for (const auto& [name, entry] : combos) {
        cJSON_AddItemToObject(c_obj, name.c_str(), entry.toJson());
    }
    cJSON_AddItemToObject(root, "combos", c_obj);

    return root;
}
