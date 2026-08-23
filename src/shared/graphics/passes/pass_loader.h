#ifndef PASS_LOADER_H
#define PASS_LOADER_H

#include <cjson/cJSON.h>

#include <memory>
#include <string>

class EngineContext;
class ShaderPass;

class PassLoader {
   public:
    static std::unique_ptr<ShaderPass> loadPass(cJSON* config, cJSON* instance_config, EngineContext& ctx);
    static std::unique_ptr<ShaderPass> loadPassFromMaterial(const char* material_rel_path, EngineContext& ctx);
};

#endif  // PASS_LOADER_H
