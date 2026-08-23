#ifndef PARTICLE_LAYER_H
#define PARTICLE_LAYER_H

#include "particle_system.h"
#include "shared/core/build_config.h"
#include "wallpaper/2d/layers/layer.h"

class EngineContext;

class ParticleLayer : public Layer {
   public:
    ParticleSystem* ps;

    ParticleLayer(const char* name, ParticleSystem* ps);
    virtual ~ParticleLayer();

    static ParticleLayer* createFromDocument(const wallpaper_engine::SceneObjectDocument& doc, EngineContext& ctx);

    void update(float dt, EngineContext& ctx) override;
    void draw(EngineContext& ctx) override;
    void drawDebug(EngineContext& ctx) override;
    bool requiresSceneColor() const;
    void setSceneColorView(sg_view view);

#if DEBUG_BUILD
    void showInspector(EngineContext& ctx) override;
#endif
};

#endif  // PARTICLE_LAYER_H
