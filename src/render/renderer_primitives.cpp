#include <math.h>

#include "render.h"

void renderer_draw_rect(renderer_t* r, float x, float y, float w, float h, float color[4]) {
    renderer_draw_line(r, x, y, x + w, y, color);
    renderer_draw_line(r, x + w, y, x + w, y + h, color);
    renderer_draw_line(r, x + w, y + h, x, y + h, color);
    renderer_draw_line(r, x, y + h, x, y, color);
}

void renderer_draw_line(renderer_t* r, float x0, float y0, float x1, float y1, float color[4]) {
    r->bind.views[0] = r->white_view;
    for (int i = 1; i < 12; i++) r->bind.views[i] = r->black_view;
    sg_apply_pipeline(r->pip_lines);
    sg_apply_bindings(&r->bind);

    mat4x4 proj, model, mvp;
    mat4x4_ortho(proj, 0, r->view_width, r->view_height, 0, -1.0f, 1.0f);
    mat4x4_identity(model);
    const float dx = x1 - x0;
    const float dy = y1 - y0;
    mat4x4_translate_in_place(model, x0, y0, 0.0f);
    mat4x4_rotate_Z(model, model, atan2f(dy, dx));
    mat4x4_scale_aniso(model, model, sqrtf(dx * dx + dy * dy), 1.0f, 1.0f);
    mat4x4_mul(mvp, proj, model);
    sg_range mvp_range = SG_RANGE(mvp);
    sg_apply_uniforms(0, &mvp_range);
    sg_range tint_range = {.ptr = color, .size = sizeof(float) * 4};
    sg_apply_uniforms(1, &tint_range);
    sg_draw(0, 6, 1);
    r->draw_calls++;
    r->bind.views[0] = (sg_view){SG_INVALID_ID};
}
