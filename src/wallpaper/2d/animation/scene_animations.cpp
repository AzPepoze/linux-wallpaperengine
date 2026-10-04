#include "scene_animations.h"

#include <algorithm>

#include "wallpaper/2d/layers/image/image_layer.h"
#include "wallpaper/2d/layers/layer.h"
#include "wallpaper/2d/tree/scene_tree.h"

namespace {
constexpr double kRadToDeg = 180.0 / 3.14159265358979323846;
}  // namespace

Layer* SceneAnimations::layerFor(uint32_t object_id) const {
    for (Layer* layer : ctx_.scene.layers)
        if (layer->scene_object_id == object_id) return layer;
    return nullptr;
}

void SceneAnimations::update(float dt) {
    timelines_.update(dt, [this](const AnimatedValue& animated) { apply(animated); });
}

void SceneAnimations::apply(const AnimatedValue& animated) {
    SceneTreeNode* node = ctx_.scene.scene_tree ? ctx_.scene.scene_tree->find(animated.object_id) : nullptr;
    Layer* layer = layerFor(animated.object_id);

    std::array<float, 3> current = {0.0f, 0.0f, 0.0f};
    std::array<float, 3>* target = nullptr;
    double scale = 1.0;
    if (animated.property == "origin" && node) {
        target = &node->origin;
    } else if (animated.property == "scale" && node) {
        target = &node->scale;
    } else if (animated.property == "angles" && node) {
        target = &node->angles;
        scale = kRadToDeg;  // authored in radians, the tree stores degrees
    } else if (animated.property == "color" && layer) {
        current = {layer->tint[0], layer->tint[1], layer->tint[2]};
    } else if (animated.property == "alpha" && layer) {
        current = {layer->tint[3], 0.0f, 0.0f};
    } else {
        return;
    }
    if (target) current = *target;

    const auto key = std::make_pair(animated.object_id, animated.property);
    auto base = bases_.find(key);
    if (base == bases_.end()) base = bases_.emplace(key, current).first;

    std::array<float, 3> result = current;
    for (size_t i = 0; i < 3; ++i) {
        if (!animated.has[i]) continue;
        const float value = (float)(animated.value[i] * scale);
        result[i] = animated.relative ? base->second[i] + value : value;
    }

    if (target) {
        *target = result;
    } else if (animated.property == "color") {
        for (size_t i = 0; i < 3; ++i) layer->tint[i] = result[i];
    } else {
        layer->tint[3] = std::clamp(result[0], 0.0f, 1.0f);
        if (auto* image = dynamic_cast<ImageLayer*>(layer)) image->alpha_from_timeline = true;
    }
}
