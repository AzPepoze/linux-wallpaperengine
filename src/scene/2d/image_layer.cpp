#include "image_layer.h"

#include "../../core/context.h"
#include "../../render/render.h"
#include "imgui.h"

ImageLayer::ImageLayer(const char* name, sg_image img) : Layer(name), img(img) {}

ImageLayer::~ImageLayer() {
    if (img.id != SG_INVALID_ID) sg_destroy_image(img);
}

void ImageLayer::update(float dt) {
    (void)dt;
}

void ImageLayer::draw() {
    if (!visible || img.id == SG_INVALID_ID) return;

    float px = state.parallax_smooth_x * parallax[0];
    float py = state.parallax_smooth_y * parallax[1];

    float rw = size[0] * state.render_scale;
    float rh = size[1] * state.render_scale;

    float rx = state.offset_x + (origin[0] + px) * state.render_scale - (rw * 0.5f);
    float ry = state.offset_y + (origin[1] + py) * state.render_scale - (rh * 0.5f);

    renderer_draw_sprite(&state.renderer, img, rx, ry, rw, rh, rotation, tint);
}

void ImageLayer::showInspector() {
    ImGui::Checkbox("Visible", &visible);
    ImGui::DragFloat2("Position", (float*)origin, 1.0f);
    ImGui::DragFloat2("Size", (float*)size, 1.0f);
    ImGui::DragFloat("Rotation", &rotation, 1.0f, 0, 360);
    ImGui::ColorEdit4("Tint", tint);
    ImGui::DragFloat2("Parallax", (float*)parallax, 0.01f, -10, 10);
}
