#ifndef SANDBOX_CATALOG_H
#define SANDBOX_CATALOG_H

#include <string>
#include <vector>

enum class SandboxAssetKind { Effect, Material };

struct SandboxEntry {
    SandboxAssetKind kind = SandboxAssetKind::Effect;
    std::string name;
    std::string source_path;
    std::string preview_project_path;
    bool available = false;
};

class SandboxCatalog {
   public:
    void scan(const std::string& engine_path);

    const std::vector<SandboxEntry>& effects() const {
        return effects_;
    }
    const std::vector<SandboxEntry>& materials() const {
        return materials_;
    }

   private:
    std::vector<SandboxEntry> effects_;
    std::vector<SandboxEntry> materials_;
};

#endif  // SANDBOX_CATALOG_H
