#include "layer.h"

void Layer::initFromDocument(const wallpaper_engine::SceneObjectDocument& doc, EngineContext& ctx) {
    if (doc.node.valid && doc.node.id > 0) {
        scene_object_id = doc.node.id;
    }
    name = doc.name;
    visible = doc.visible;
    origin[0] = doc.node.origin[0];
    origin[1] = doc.node.origin[1];
    origin[2] = doc.node.origin[2];
    scale[0] = doc.node.scale[0];
    scale[1] = doc.node.scale[1];
    scale[2] = doc.node.scale[2];
    rotation = doc.node.angles[2];
    parallax[0] = doc.node.parallax_depth[0];
    parallax[1] = doc.node.parallax_depth[1];

    for (const auto& eff_doc : doc.effects) {
        Effect* effect = Effect::loadFromDocument(eff_doc, ctx);
        if (effect) effects.push_back(effect);
    }
}
