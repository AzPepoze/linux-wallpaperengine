#ifndef ASSET_PROVIDER_H
#define ASSET_PROVIDER_H

#include <memory>
#include <string>
#include <vector>

class IAssetProvider {
   public:
    virtual ~IAssetProvider() = default;
    virtual bool resolvePath(const char* rel_path, char* out_abs_path, int max_len) const = 0;
    virtual const char* getProviderName() const = 0;
};

class EngineAssetProvider : public IAssetProvider {
   public:
    explicit EngineAssetProvider(std::string engine_path);
    bool resolvePath(const char* rel_path, char* out_abs_path, int max_len) const override;
    const char* getProviderName() const override {
        return "EngineAssets";
    }
    const std::string& getEnginePath() const {
        return engine_path;
    }

   private:
    std::string engine_path;
};

class WallpaperAssetProvider : public IAssetProvider {
   public:
    explicit WallpaperAssetProvider(std::string wallpaper_path);
    bool resolvePath(const char* rel_path, char* out_abs_path, int max_len) const override;
    const char* getProviderName() const override {
        return "WallpaperAssets";
    }
    const std::string& getWallpaperPath() const {
        return wallpaper_path;
    }

   private:
    std::string wallpaper_path;
};

class InternalAssetProvider : public IAssetProvider {
   public:
    InternalAssetProvider() = default;
    bool resolvePath(const char* rel_path, char* out_abs_path, int max_len) const override;
    const char* getProviderName() const override {
        return "InternalAssets";
    }
};

#endif  // ASSET_PROVIDER_H
