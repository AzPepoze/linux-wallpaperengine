#include "diagnostic_config.h"

#include <cstdlib>
#include <cstring>

#include "sokol_args.h"

namespace {
bool checkArg(const char* name) {
    for (int i = 0; i < sargs_num_args(); ++i) {
        const char* k = sargs_key_at(i);
        while (k && *k == '-') k++;
        if (k && strcmp(k, name) == 0) return true;
    }
    return false;
}

const char* getArg(const char* name) {
    for (int i = 0; i < sargs_num_args(); ++i) {
        const char* k = sargs_key_at(i);
        while (k && *k == '-') k++;
        if (k && strcmp(k, name) == 0) {
            const char* v = sargs_value_at(i);
            if (v && v[0] != '\0') return v;
            if (i + 1 < sargs_num_args()) {
                const char* next_k = sargs_key_at(i + 1);
                if (next_k && next_k[0] != '-') {
                    return next_k;
                }
            }
        }
    }
    return "";
}
}  // namespace

void DiagnosticConfig::parseFromArgs() {
    enabled = checkArg("diagnose-effects") || checkArg("diagnose_effects") || checkArg("d");

    if (checkArg("diagnostic-output")) {
        output_dir = getArg("diagnostic-output");
    } else if (checkArg("diagnostic_output")) {
        output_dir = getArg("diagnostic_output");
    }

    if (checkArg("diagnostic-frame")) {
        target_frame = (uint64_t)std::strtoull(getArg("diagnostic-frame"), nullptr, 10);
    } else if (checkArg("diagnostic_frame")) {
        target_frame = (uint64_t)std::strtoull(getArg("diagnostic_frame"), nullptr, 10);
    }

    if (checkArg("diagnostic-time")) {
        has_deterministic_time = true;
        deterministic_time = (float)std::atof(getArg("diagnostic-time"));
    } else if (checkArg("diagnostic_time")) {
        has_deterministic_time = true;
        deterministic_time = (float)std::atof(getArg("diagnostic_time"));
    }

    if (checkArg("diagnostic-effect")) {
        const char* val = getArg("diagnostic-effect");
        char* end = nullptr;
        long idx = std::strtol(val, &end, 10);
        if (end && *end == '\0') {
            isolate_effect_index = (int)idx;
        } else {
            isolate_effect_path = val;
        }
    }

    if (checkArg("diagnostic-pass")) {
        isolate_pass_index = std::atoi(getArg("diagnostic-pass"));
    }

    if (checkArg("diagnostic-stop-after-pass")) {
        stop_after_pass_index = std::atoi(getArg("diagnostic-stop-after-pass"));
    }

    if (checkArg("diagnostic-disable-pass")) {
        disable_pass_index = std::atoi(getArg("diagnostic-disable-pass"));
    }

    if (checkArg("diagnostic-output-texture")) {
        force_output_texture_slot = std::atoi(getArg("diagnostic-output-texture"));
    }

    if (checkArg("diagnostic-ab")) {
        enable_ab = true;
    }
}
