#include "render.h"
#include "shared/core/engine_context.h"

void renderer_draw_mesh(EngineContext& ctx, renderer_t* r, sg_buffer position_buffer, sg_buffer uv_buffer,
                        sg_buffer index_buffer, int index_count, sg_image image, sg_view main_view, const float tint[4],
                        float width, float height) {
    (void)ctx;
    (void)image;
    if (!r || r->pip_mesh.id == SG_INVALID_ID || index_count <= 0 || position_buffer.id == SG_INVALID_ID ||
        uv_buffer.id == SG_INVALID_ID || index_buffer.id == SG_INVALID_ID || main_view.id == SG_INVALID_ID) {
        return;
    }

    mat4x4 proj;
    mat4x4_ortho(proj, 0.0f, width, height, 0.0f, -1.0f, 1.0f);

    sg_apply_pipeline(r->pip_mesh);
    r->bind.vertex_buffers[0] = position_buffer;
    r->bind.vertex_buffers[1] = uv_buffer;
    r->bind.index_buffer = index_buffer;
    r->bind.views[0] = main_view;
    r->bind.samplers[0] = r->smp_clamp;
    for (int i = 1; i < 12; ++i) r->bind.views[i] = r->black_view;

    sg_range mvp_range = SG_RANGE(proj);
    sg_apply_uniforms(0, &mvp_range);
    sg_range tint_range = {.ptr = tint, .size = sizeof(float) * 4};
    sg_apply_uniforms(1, &tint_range);

    sg_apply_bindings(&r->bind);
    sg_draw(0, index_count, 1);
    r->draw_calls++;

    for (int i = 0; i < 12; ++i) r->bind.views[i] = (sg_view){SG_INVALID_ID};
    r->bind.vertex_buffers[0] = r->vertex_buffer;
    r->bind.vertex_buffers[1] = (sg_buffer){SG_INVALID_ID};
    r->bind.index_buffer = r->index_buffer;
}
