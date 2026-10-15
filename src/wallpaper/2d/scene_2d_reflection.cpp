#include <algorithm>

#include "scene_2d.h"
#include "shared/core/logger.h"
#include "shared/graphics/pass_util.h"
#include "wallpaper/2d/layers/model/static_model_layer.h"

namespace {

const StaticModelLayer* asModel(const Layer* layer) {
    return dynamic_cast<const StaticModelLayer*>(layer);
}

StaticModelLayer* asModel(Layer* layer) {
    return dynamic_cast<StaticModelLayer*>(layer);
}

bool isReflected(const Layer* layer) {
    const StaticModelLayer* model = asModel(layer);
    return model && model->reflected();
}

bool isMirrorSurface(const Layer* layer) {
    const StaticModelLayer* model = asModel(layer);
    return model && model->samplesReflection();
}

// The first visible mirror surface sets the plane; scenes have a single floor.
float mirrorPlaneY(const std::vector<Layer*>& layers) {
    for (const Layer* layer : layers) {
        if (layer->visible && isMirrorSurface(layer)) return asModel(layer)->mirrorPlaneY();
    }
    return 0.0f;
}

}  // namespace

bool Scene2DRuntime::hasReflectedLayers() const {
    const auto& layers = ctx.scene.layers;
    return std::any_of(layers.begin(), layers.end(), isReflected) &&
           std::any_of(layers.begin(), layers.end(), isMirrorSurface);
}

void Scene2DRuntime::updateReflectionTarget(int width, int height, sg_pixel_format format) {
    // Only 3D scenes have a depth target, so 2D scenes skip the layer scan.
    if (!depth_target.valid() || !hasReflectedLayers()) {
        resetReflectionTarget();
        return;
    }
    if (reflection_target.image.id != SG_INVALID_ID && reflection_target.width == width &&
        reflection_target.height == height && reflection_target.pixel_format == format && reflection_depth.valid())
        return;
    if (!reflection_target.create(width, height, format) || !reflection_depth.create(width, height)) {
        LOG_TAG_E("SCENE_2D", "Could not create the reflection target (%dx%d)", width, height);
        resetReflectionTarget();
    }
}

void Scene2DRuntime::resetReflectionTarget() {
    if (reflection_target.image.id == SG_INVALID_ID && !reflection_depth.valid()) return;
    for (Layer* layer : ctx.scene.layers) {
        if (isMirrorSurface(layer)) asModel(layer)->setReflectionView({}, ctx);
    }
    reflection_target.reset();
    reflection_depth.reset();
}

void Scene2DRuntime::renderReflection() {
    if (!reflection_depth.valid()) return;
    const float plane_y = mirrorPlaneY(ctx.scene.layers);

    sg_pass pass = colorPass(reflection_target.attachment_view, SG_LOADACTION_CLEAR, 1.0f);
    pass.attachments.depth_stencil = reflection_depth.attachment_view;
    pass.action.depth = {.load_action = SG_LOADACTION_CLEAR, .store_action = SG_STOREACTION_DONTCARE,
                         .clear_value = 1.0f};
    sg_begin_pass(&pass);
    for (Layer* layer : ctx.scene.layers) {
        if (layer->visible && isReflected(layer)) asModel(layer)->drawReflection(ctx, plane_y);
    }
    sg_end_pass();

    for (Layer* layer : ctx.scene.layers) {
        if (isMirrorSurface(layer)) asModel(layer)->setReflectionView(reflection_target.texture_view, ctx);
    }
}
