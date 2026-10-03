#include <stdio.h>

#include <mutex>
#include <string>

#include "gpu_trace.h"
#include "gpu_trace_internal.h"

static const char* sokol_state_str(sg_resource_state st) {
    switch (st) {
        case SG_RESOURCESTATE_INITIAL:
            return "INITIAL";
        case SG_RESOURCESTATE_ALLOC:
            return "ALLOC";
        case SG_RESOURCESTATE_UNSEALED:
            return "UNSEALED";
        case SG_RESOURCESTATE_VALID:
            return "VALID";
        case SG_RESOURCESTATE_FAILED:
            return "FAILED";
        case SG_RESOURCESTATE_INVALID:
            return "INVALID";
        default:
            return "UNKNOWN";
    }
}

bool gpu_trace_validate_bindings(uint64_t pass_serial, uint64_t frame_index, const sg_bindings* bind) {
    if (!bind) return true;

    std::lock_guard<std::mutex> reg_lock(g_registry_mutex);

    for (int slot = 0; slot < 12; ++slot) {
        uint32_t v_id = bind->views[slot].id;
        if (v_id == SG_INVALID_ID) continue;

        sg_resource_state v_state = sg_query_view_state(bind->views[slot]);
        if (v_state == SG_RESOURCESTATE_FAILED || v_state == SG_RESOURCESTATE_INVALID) {
            fprintf(
                stderr,
                "[GPU-VALIDATION] INVALID SOKOL VIEW STATE BOUND frame=%lu pass_serial=%lu slot=%d view=%u state=%s\n",
                (unsigned long)frame_index, (unsigned long)pass_serial, slot, v_id, sokol_state_str(v_state));
            fflush(stderr);
            return false;
        }

        auto it_v = g_registry_views.find(v_id);
        if (it_v != g_registry_views.end()) {
            if (!it_v->second.alive) {
                fprintf(
                    stderr,
                    "[GPU-VALIDATION] DEAD RESOURCE BOUND frame=%lu pass_serial=%lu slot=%d resource_type=view view=%u "
                    "image=%u owner=\"%s\" name=\"%s\" gen=%lu created_at_serial=%lu destroyed_at_serial=%lu "
                    "reason=%s\n",
                    (unsigned long)frame_index, (unsigned long)pass_serial, slot, v_id, it_v->second.image_id,
                    it_v->second.owner_kind.c_str(), it_v->second.logical_name.c_str(),
                    (unsigned long)it_v->second.generation, (unsigned long)it_v->second.create_pass_serial,
                    (unsigned long)it_v->second.destroy_pass_serial, it_v->second.destroy_reason.c_str());
                fflush(stderr);
                return false;
            }
        }

        sg_image img = sg_query_view_image(bind->views[slot]);
        if (img.id != SG_INVALID_ID) {
            sg_resource_state img_state = sg_query_image_state(img);
            if (img_state == SG_RESOURCESTATE_FAILED || img_state == SG_RESOURCESTATE_INVALID) {
                fprintf(stderr,
                        "[GPU-VALIDATION] INVALID SOKOL IMAGE STATE BOUND frame=%lu pass_serial=%lu slot=%d view=%u "
                        "image=%u "
                        "state=%s\n",
                        (unsigned long)frame_index, (unsigned long)pass_serial, slot, v_id, img.id,
                        sokol_state_str(img_state));
                fflush(stderr);
                return false;
            }

            auto it_img = g_registry_images.find(img.id);
            if (it_img != g_registry_images.end()) {
                if (!it_img->second.alive) {
                    fprintf(
                        stderr,
                        "[GPU-VALIDATION] DEAD RESOURCE BOUND frame=%lu pass_serial=%lu slot=%d resource_type=image "
                        "view=%u image=%u owner=\"%s\" name=\"%s\" gen=%lu created_at_serial=%lu "
                        "destroyed_at_serial=%lu "
                        "reason=%s\n",
                        (unsigned long)frame_index, (unsigned long)pass_serial, slot, v_id, img.id,
                        it_img->second.owner_kind.c_str(), it_img->second.logical_name.c_str(),
                        (unsigned long)it_img->second.generation, (unsigned long)it_img->second.create_pass_serial,
                        (unsigned long)it_img->second.destroy_pass_serial, it_img->second.destroy_reason.c_str());
                    fflush(stderr);
                    return false;
                }
            }
        }
    }

    for (int vb_slot = 0; vb_slot < SG_MAX_VERTEXBUFFER_BINDSLOTS; ++vb_slot) {
        uint32_t b_id = bind->vertex_buffers[vb_slot].id;
        if (b_id == SG_INVALID_ID) continue;

        sg_resource_state b_state = sg_query_buffer_state(bind->vertex_buffers[vb_slot]);
        if (b_state == SG_RESOURCESTATE_FAILED || b_state == SG_RESOURCESTATE_INVALID) {
            fprintf(stderr,
                    "[GPU-VALIDATION] INVALID SOKOL BUFFER STATE BOUND frame=%lu pass_serial=%lu vb_slot=%d buf=%u "
                    "state=%s\n",
                    (unsigned long)frame_index, (unsigned long)pass_serial, vb_slot, b_id, sokol_state_str(b_state));
            fflush(stderr);
            return false;
        }

        auto it_buf = g_registry_buffers.find(b_id);
        if (it_buf != g_registry_buffers.end()) {
            if (!it_buf->second.alive) {
                fprintf(
                    stderr,
                    "[GPU-VALIDATION] DEAD RESOURCE BOUND frame=%lu pass_serial=%lu vb_slot=%d resource_type=buffer "
                    "buf=%u size=%zu role=%s owner=\"%s\" name=\"%s\" created_at_serial=%lu destroyed_at_serial=%lu "
                    "reason=%s\n",
                    (unsigned long)frame_index, (unsigned long)pass_serial, vb_slot, b_id, it_buf->second.size,
                    it_buf->second.role.c_str(), it_buf->second.owner_kind.c_str(), it_buf->second.logical_name.c_str(),
                    (unsigned long)it_buf->second.create_pass_serial, (unsigned long)it_buf->second.destroy_pass_serial,
                    it_buf->second.destroy_reason.c_str());
                fflush(stderr);
                return false;
            }
        }
    }

    uint32_t ib_id = bind->index_buffer.id;
    if (ib_id != SG_INVALID_ID) {
        sg_resource_state ib_state = sg_query_buffer_state(bind->index_buffer);
        if (ib_state == SG_RESOURCESTATE_FAILED || ib_state == SG_RESOURCESTATE_INVALID) {
            fprintf(stderr,
                    "[GPU-VALIDATION] INVALID SOKOL BUFFER STATE BOUND frame=%lu pass_serial=%lu ib=b=%u state=%s\n",
                    (unsigned long)frame_index, (unsigned long)pass_serial, ib_id, sokol_state_str(ib_state));
            fflush(stderr);
            return false;
        }

        auto it_ib = g_registry_buffers.find(ib_id);
        if (it_ib != g_registry_buffers.end()) {
            if (!it_ib->second.alive) {
                fprintf(
                    stderr,
                    "[GPU-VALIDATION] DEAD RESOURCE BOUND frame=%lu pass_serial=%lu resource_type=index_buffer buf=%u "
                    "size=%zu role=%s owner=\"%s\" name=\"%s\" created_at_serial=%lu destroyed_at_serial=%lu "
                    "reason=%s\n",
                    (unsigned long)frame_index, (unsigned long)pass_serial, ib_id, it_ib->second.size,
                    it_ib->second.role.c_str(), it_ib->second.owner_kind.c_str(), it_ib->second.logical_name.c_str(),
                    (unsigned long)it_ib->second.create_pass_serial, (unsigned long)it_ib->second.destroy_pass_serial,
                    it_ib->second.destroy_reason.c_str());
                fflush(stderr);
                return false;
            }
        }
    }

    for (int smp_slot = 0; smp_slot < SG_MAX_SAMPLER_BINDSLOTS; ++smp_slot) {
        uint32_t s_id = bind->samplers[smp_slot].id;
        if (s_id == SG_INVALID_ID) continue;

        sg_resource_state s_state = sg_query_sampler_state(bind->samplers[smp_slot]);
        if (s_state == SG_RESOURCESTATE_FAILED || s_state == SG_RESOURCESTATE_INVALID) {
            fprintf(stderr,
                    "[GPU-VALIDATION] INVALID SOKOL SAMPLER STATE BOUND frame=%lu pass_serial=%lu slot=%d sampler=%u "
                    "state=%s\n",
                    (unsigned long)frame_index, (unsigned long)pass_serial, smp_slot, s_id, sokol_state_str(s_state));
            fflush(stderr);
            return false;
        }
    }

    return true;
}
