#ifndef PARTICLE_INSPECTOR_H
#define PARTICLE_INSPECTOR_H

#include "shared/core/build_config.h"

#if DEBUG_BUILD

class ParticleLayer;
struct EngineContext;

namespace Inspector {
void showParticleLayerInspector(EngineContext& ctx, ParticleLayer& layer);
}

#endif  // DEBUG_BUILD

#endif  // PARTICLE_INSPECTOR_H
