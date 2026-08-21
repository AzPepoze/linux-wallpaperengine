#include "render.h"

void renderer_cleanup(renderer_t* r) {
    r->pip_alpha = {};
    r->pip_add = {};
    r->pip_lines = {};
    r->vertex_buffer = {};
    r->index_buffer = {};
    r->smp_repeat = {};
    r->smp_clamp = {};
    r->white_pixel = {};
    r->white_view = {};
    r->black_pixel = {};
    r->black_view = {};
    r->gray_pixel = {};
    r->gray_view = {};
}
