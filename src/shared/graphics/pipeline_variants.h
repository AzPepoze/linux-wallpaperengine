#pragma once

#include <cstdint>

#include "sokol_gfx.h"

// Pipelines must match the sample count of the pass they draw into. They are built single-sample;
// when MSAA is on, a multisampled twin is created on first use inside an MSAA pass.
namespace pipeline_variants {
// Returns `requested` when the GPU can multisample the scene formats, otherwise 1.
int supportedSamples(int requested);
// Sample count of the multisampled scene pass; 1 turns variants off. Call before creating pipelines.
void configure(int samples);
// Sample count of the pass being recorded; the scene draw sets it around its MSAA passes.
void setPassSamples(int samples);
// Creates a pipeline and remembers its description while MSAA is configured.
sg_pipeline make(const sg_pipeline_desc& desc);
// Applies the pipeline, or its twin matching the active pass sample count.
void apply(sg_pipeline pipeline);
// Drops the twin when the pipeline is destroyed.
void forget(uint32_t pipeline_id);
}  // namespace pipeline_variants
