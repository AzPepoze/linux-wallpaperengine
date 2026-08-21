#ifndef GPU_DEBUG_LABELS_H
#define GPU_DEBUG_LABELS_H

#include "sokol_gfx.h"

void gpu_set_image_debug_label(sg_image image, const char* name);
void gpu_set_pipeline_debug_label(sg_pipeline pipeline, const char* name);

#endif  // GPU_DEBUG_LABELS_H
