#ifndef GPU_TRACE_H
#define GPU_TRACE_H

#include <stdint.h>
#include <stdio.h>

#include <atomic>
#include <string>
#include <vector>

#include "sokol_gfx.h"

struct GpuTraceInputBinding {
    int slot = 0;
    std::string semantic;
    uint32_t image_id = SG_INVALID_ID;
    uint32_t view_id = SG_INVALID_ID;
    uint64_t generation = 0;
    bool is_render_target = false;
};

struct GpuPassTraceInfo {
    uint64_t pass_serial = 0;
    uint64_t frame_index = 0;
    const char* category = "generic";
    const char* layer_name = "";
    uint32_t layer_id = 0;
    int effect_index = -1;
    const char* effect_path = "";
    int pass_index = -1;
    const char* shader_name = "";
    const char* render_target_name = "";
    float render_scale = 1.0f;
    int target_width = 0;
    int target_height = 0;
    uint32_t output_image_id = SG_INVALID_ID;
    uint32_t output_view_id = SG_INVALID_ID;
    uint64_t output_generation = 0;
    std::vector<GpuTraceInputBinding> inputs;
};

uint64_t gpu_trace_next_pass_serial();
uint64_t gpu_trace_next_target_generation();
void gpu_trace_set_current_pass_serial(uint64_t serial);
uint64_t gpu_trace_get_current_pass_serial();

void gpu_trace_rt_create(const char* kind, const char* name, uint32_t image_id, uint32_t tex_view_id,
                         uint32_t att_view_id, int width, int height, uint64_t generation);

void gpu_trace_rt_destroy(const char* reason, const char* name, uint32_t image_id, uint32_t tex_view_id,
                          uint32_t att_view_id, int width, int height, uint64_t generation);

void gpu_trace_pass_begin(const GpuPassTraceInfo& info);
void gpu_trace_pass_end(uint64_t pass_serial);

void gpu_trace_bound_slots(uint64_t pass_serial, const sg_bindings* bind);

void gpu_trace_record_event(const char* kind, const char* fmt, ...);
void gpu_trace_dump_history();
void gpu_trace_dump_history_signal_safe();

#endif  // GPU_TRACE_H
