#ifndef CONTEXT_H
#define CONTEXT_H

#include "engine_context.h"

#ifdef __cplusplus
extern "C" {
#endif

// The global 'state' is being phased out in favor of explicit EngineContext&.
// For the transition, we keep it here but we'll eventually remove it.
// extern struct EngineContext state;

#ifdef __cplusplus
}
#endif

#endif  // CONTEXT_H
