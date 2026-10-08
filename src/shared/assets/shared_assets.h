#ifndef SHARED_ASSETS_H
#define SHARED_ASSETS_H

#include <memory>
#include <string>

#include "shared/assets/providers/asset_provider.h"
#include "shared/assets/texture_decode_cache.h"

// Process-wide install and internal providers and their decode cache; built once at startup.
struct SharedAssets {
    std::string engine_path;
    std::unique_ptr<EngineAssetProvider> engine_provider;
    std::unique_ptr<InternalAssetProvider> internal_provider;
    std::unique_ptr<TextureDecodeCache> decode_cache;
};

#endif  // SHARED_ASSETS_H
