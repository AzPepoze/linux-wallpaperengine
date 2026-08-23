#include "pass_loader.h"

#include <cstdlib>

#include "shader_pass.h"
#include "shared/core/engine_context.h"
#include "shared/core/utils.h"

std::unique_ptr<ShaderPass> PassLoader::loadPass(cJSON* config, cJSON* instance_config, EngineContext& ctx) {
    if (!config) return nullptr;
    auto pass = std::make_unique<ShaderPass>(config, instance_config, ctx);
    pass->init(ctx);
    return pass;
}

std::unique_ptr<ShaderPass> PassLoader::loadPassFromMaterial(const char* material_rel_path, EngineContext& ctx) {
    if (!material_rel_path || material_rel_path[0] == '\0') return nullptr;

    char abs_path[1024];
    if (!ctx.asset_mgr.resolvePath(material_rel_path, abs_path, sizeof(abs_path))) return nullptr;

    char* json_str = read_file_to_string(abs_path);
    if (!json_str) return nullptr;

    cJSON* root = cJSON_Parse(json_str);
    free(json_str);
    if (!root) return nullptr;

    cJSON* pass_json = root;
    cJSON* passes = cJSON_GetObjectItemCaseSensitive(root, "passes");
    if (cJSON_IsArray(passes) && cJSON_GetArraySize(passes) > 0) {
        pass_json = cJSON_GetArrayItem(passes, 0);
    }

    std::unique_ptr<ShaderPass> pass = loadPass(pass_json, nullptr, ctx);
    cJSON_Delete(root);
    return pass;
}
