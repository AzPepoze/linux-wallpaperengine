#ifndef LAYER_H
#define LAYER_H

#include <stdint.h>

#include <string>
#include <vector>

#include "core/interfaces.h"
#include "formats/wallpaper_engine/scene/scene_document.h"
#include "linmath.h"
#include "sokol_gfx.h"
#include "wallpaper/scene/2d/effects/effect.h"

class Layer : public ILayer {
   public:
    uint32_t scene_object_id = 0;
    std::string name;
    bool visible = true;
    bool solo = false;
    vec3 origin = {0, 0, 0};
    vec2 size = {0, 0};
    vec3 scale = {1, 1, 1};
    float rotation = 0.0f;
    float tint[4] = {1, 1, 1, 1};
    vec2 parallax = {0, 0};
    std::string path;
    std::string asset_metadata;
    std::vector<Effect*> effects;

    Layer(const char* name) : name(name) {}
    virtual ~Layer() {
        for (auto eff : effects) delete eff;
        effects.clear();
    }

    Layer(const Layer&) = delete;
    Layer& operator=(const Layer&) = delete;

    virtual void update(float dt, EngineContext& ctx) = 0;
    virtual void draw(EngineContext& ctx) = 0;
    virtual void drawDebug(EngineContext& ctx) override {}

    virtual const std::string& get_name() const override {
        return name;
    }
    virtual bool is_visible() const override {
        return visible;
    }

    bool draw_debug_bounds = false;

    void initFromDocument(const wallpaper_engine::SceneObjectDocument& doc, EngineContext& ctx);
};

#endif  // LAYER_H
