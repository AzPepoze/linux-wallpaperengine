#include "pipeline_variants.h"

#include <unordered_map>

#include "shared/core/logger.h"

namespace pipeline_variants {
namespace {
struct Entry {
    sg_pipeline_desc desc;
    sg_pipeline variant = {SG_INVALID_ID};
};

int configured_samples = 1;
int pass_samples = 1;
std::unordered_map<uint32_t, Entry> entries;

sg_pipeline variantOf(Entry& entry) {
    if (entry.variant.id != SG_INVALID_ID) return entry.variant;
    sg_pipeline_desc desc = entry.desc;
    desc.sample_count = configured_samples;
    entry.variant = sg_make_pipeline(&desc);
    return entry.variant;
}

bool canMultisample(sg_pixel_format format) {
    return sg_query_pixelformat(format).msaa;
}
}  // namespace

int supportedSamples(int requested) {
    if (requested <= 1) return 1;
    if (canMultisample(SG_PIXELFORMAT_RGBA16F) && canMultisample(SG_PIXELFORMAT_RGBA8) &&
        canMultisample(SG_PIXELFORMAT_DEPTH)) {
        return requested;
    }
    LOG_TAG_W("MSAA", "The GPU cannot multisample the scene formats; using 1 sample");
    return 1;
}

void configure(int samples) {
    configured_samples = samples;
}

void setPassSamples(int samples) {
    pass_samples = samples;
}

sg_pipeline make(const sg_pipeline_desc& desc) {
    const sg_pipeline pipeline = sg_make_pipeline(&desc);
    if (configured_samples > 1 && pipeline.id != SG_INVALID_ID) entries[pipeline.id] = Entry{desc};
    return pipeline;
}

void apply(sg_pipeline pipeline) {
    if (pass_samples > 1) {
        const auto found = entries.find(pipeline.id);
        if (found != entries.end()) pipeline = variantOf(found->second);
    }
    sg_apply_pipeline(pipeline);
}

void forget(uint32_t pipeline_id) {
    const auto found = entries.find(pipeline_id);
    if (found == entries.end()) return;
    if (found->second.variant.id != SG_INVALID_ID) sg_destroy_pipeline(found->second.variant);
    entries.erase(found);
}
}  // namespace pipeline_variants
