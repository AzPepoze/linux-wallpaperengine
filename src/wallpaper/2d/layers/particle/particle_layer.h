#ifndef PARTICLE_LAYER_H
#define PARTICLE_LAYER_H

#include "particle_system.h"
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

   private:
    // The layer's world placement from the scene tree (or its own transform), without camera parallax.
    ParticlePlacement authoredPlacement(EngineContext& ctx) const;
    // Hands the placement and the camera parallax offset to the particle system.
    void applyPlacement(EngineContext& ctx);
};

#endif  // PARTICLE_LAYER_H
