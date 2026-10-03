#ifndef RENDER_DIAGNOSTICS_INTERNAL_H
#define RENDER_DIAGNOSTICS_INTERNAL_H

#include <atomic>
#include <string>

#include "render_diagnostics.h"

class EngineContext;

namespace render_diagnostics_internal {
std::string resolveWallpaperName(const EngineContext& ctx);
void exportBundleAsync(DiagnosticExportPayload payload, const std::atomic<bool>* cancel);
}  // namespace render_diagnostics_internal

#endif  // RENDER_DIAGNOSTICS_INTERNAL_H
