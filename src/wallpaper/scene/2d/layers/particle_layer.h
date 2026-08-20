#ifndef PARTICLE_LAYER_H
#define PARTICLE_LAYER_H

#include "wallpaper/scene/2d/layers/layer.h"
#include "wallpaper/scene/2d/particles/particle_system.h"

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
};

#endif  // PARTICLE_LAYER_H
