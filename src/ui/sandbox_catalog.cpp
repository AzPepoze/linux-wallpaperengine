#include "sandbox_catalog.h"

#include <algorithm>
#include <filesystem>

namespace fs = std::filesystem;

namespace {

bool entryNameLess(const SandboxEntry& left, const SandboxEntry& right) {
    return left.name < right.name;
}

std::string displayMaterialName(const fs::path& effect_path, const fs::path& material_path) {
    return effect_path.filename().string() + "/" + material_path.lexically_relative(effect_path / "materials").string();
}

void addMaterials(const fs::path& effect_path, const fs::path& preview_scene, std::vector<SandboxEntry>& entries) {
    const fs::path materials_path = effect_path / "materials";
    std::error_code error;
    if (!fs::is_directory(materials_path, error)) return;

    for (fs::recursive_directory_iterator it(materials_path, error), end; !error && it != end; it.increment(error)) {
        if (error || !it->is_regular_file(error) || it->path().extension() != ".json") continue;
        SandboxEntry entry;
        entry.kind = SandboxAssetKind::Material;
        entry.name = displayMaterialName(effect_path, it->path());
        entry.source_path = it->path().string();
        entry.preview_project_path = preview_scene.string();
        entry.available = fs::is_regular_file(preview_scene, error);
        entries.push_back(std::move(entry));
    }
}

}  // namespace

void SandboxCatalog::scan(const std::string& engine_path) {
    effects_.clear();
    materials_.clear();

    const fs::path effects_path = fs::path(engine_path) / "assets" / "effects";
    std::error_code error;
    if (!fs::is_directory(effects_path, error)) return;

    for (fs::directory_iterator it(effects_path, error), end; !error && it != end; it.increment(error)) {
        if (error || !it->is_directory(error)) continue;

        const fs::path effect_path = it->path();
        const fs::path preview_scene = effect_path / "preview" / "scene.json";
        SandboxEntry entry;
        entry.kind = SandboxAssetKind::Effect;
        entry.name = effect_path.filename().string();
        entry.source_path = effect_path.string();
        entry.preview_project_path = preview_scene.string();
        entry.available = fs::is_regular_file(preview_scene, error);
        effects_.push_back(std::move(entry));

        addMaterials(effect_path, preview_scene, materials_);
    }

    std::sort(effects_.begin(), effects_.end(), entryNameLess);
    std::sort(materials_.begin(), materials_.end(), entryNameLess);
}
