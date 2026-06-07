#ifndef RENDER_H
#define RENDER_H

#include <stdint.h>

#include "../../libs/linmath.h"
#include "../../libs/sokol/sokol_gfx.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    float x, y;
    float u, v;
} vertex_t;

typedef struct {
    sg_pipeline pip;
    sg_bindings bind;
    sg_sampler smp;
    float view_width;
    float view_height;
} renderer_t;

void renderer_init(renderer_t* r, float w, float h);
void renderer_update_viewport(renderer_t* r, float w, float h);
void renderer_draw_sprite(renderer_t* r, sg_image img, float x, float y, float w, float h, float rotation,
                          float tint[4]);
void renderer_cleanup(renderer_t* r);

#ifdef __cplusplus
}
#endif

#endif  // RENDER_H
