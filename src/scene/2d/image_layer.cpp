#include "image_layer.h"

#include <string.h>

#include "../../core/context.h"
#include "../../core/utils.h"
#include "../../render/render.h"
#include "imgui.h"

ImageLayer::ImageLayer(const char* name, sg_image img) : Layer(name), img(img) {}

ImageLayer::~ImageLayer() {
    if (img.id != SG_INVALID_ID) sg_destroy_image(img);
}

ImageLayer* ImageLayer::createFromJSON(cJSON* node) {
    ImageLayer* layer = new ImageLayer("Layer", (sg_image){SG_INVALID_ID});
    layer->loadBaseProperties(node);

    cJSON* size_node = cJSON_GetObjectItemCaseSensitive(node, "size");
    if (cJSON_IsString(size_node)) {
        sscanf(size_node->valuestring, "%f %f", &layer->size[0], &layer->size[1]);
    }

    cJSON* asset_path = cJSON_GetObjectItemCaseSensitive(node, "image");
    if (!cJSON_IsString(asset_path)) asset_path = cJSON_GetObjectItemCaseSensitive(node, "model");

    if (cJSON_IsString(asset_path)) {
        if (strstr(asset_path->valuestring, ".json"))
            layer->loadModel(asset_path->valuestring);
        else
            layer->img = state.asset_mgr.resolveTexture(asset_path->valuestring, &layer->path);

        if (layer->img.id != SG_INVALID_ID) {
            sg_image_desc desc = sg_query_image_desc(layer->img);
            // If size wasn't in JSON, use asset size
            if (layer->size[0] == 0) {
                layer->size[0] = (float)desc.width;
                layer->size[1] = (float)desc.height;
            }
        }
    }

    return layer;
}

void ImageLayer::loadMaterial(const char* mat_rel_path) {
    img = state.asset_mgr.resolveMaterialTexture(mat_rel_path, &path);
}

void ImageLayer::loadModel(const char* mdl_rel_path) {
    char abs_path[1024];
    if (!state.asset_mgr.resolvePath(mdl_rel_path, abs_path, sizeof(abs_path))) return;
    char* json_str = read_file_to_string(abs_path);
    if (!json_str) return;
    cJSON* mdl_json = cJSON_Parse(json_str);
    free(json_str);
    if (!mdl_json) return;
    cJSON* mat_ref = cJSON_GetObjectItemCaseSensitive(mdl_json, "material");
    if (cJSON_IsString(mat_ref)) loadMaterial(mat_ref->valuestring);
    cJSON_Delete(mdl_json);
}

void ImageLayer::update(float dt) {
    (void)dt;
}

void ImageLayer::draw() {
    if (!visible || img.id == SG_INVALID_ID) return;

    float rw = size[0] * scale[0] * state.render_scale;
    float rh = size[1] * scale[1] * state.render_scale;

    float px = parallax[0] * state.parallax_smooth_x * 50.0f;
    float py = parallax[1] * state.parallax_smooth_y * 50.0f;

    // Center image on its origin
    float rx = state.offset_x + (origin[0] + px) * state.render_scale - (rw * 0.5f);
    float ry = state.offset_y + (origin[1] + py) * state.render_scale - (rh * 0.5f);

    renderer_draw_sprite(&state.renderer, img, rx, ry, rw, rh, rotation, tint, false);
}

void ImageLayer::drawDebug() {
    float rw = size[0] * scale[0] * state.render_scale;
    float rh = size[1] * scale[1] * state.render_scale;
    float px = parallax[0] * state.parallax_smooth_x * 50.0f;
    float py = parallax[1] * state.parallax_smooth_y * 50.0f;
    float rx = state.offset_x + (origin[0] + px) * state.render_scale - (rw * 0.5f);
    float ry = state.offset_y + (origin[1] + py) * state.render_scale - (rh * 0.5f);

    float color[4] = {0, 1, 0, 0.3f};
    renderer_draw_rect(&state.renderer, rx, ry, rw, rh, color);
}

void ImageLayer::showInspector() {
    ImGui::Checkbox("Visible", &visible);
    ImGui::Text("Type: Image");
    if (!path.empty()) {
        ImGui::Text("Path: %s", path.c_str());
    }

    ImGui::Separator();
    ImGui::DragFloat3("Position", (float*)origin, 1.0f);
    ImGui::DragFloat3("Scale", (float*)scale, 0.01f);
    ImGui::DragFloat2("Size", (float*)size, 1.0f);
    ImGui::DragFloat("Rotation", &rotation, 1.0f, 0, 360);
    ImGui::ColorEdit4("Tint", tint);
    ImGui::DragFloat2("Parallax", (float*)parallax, 0.01f, -10, 10);
}
