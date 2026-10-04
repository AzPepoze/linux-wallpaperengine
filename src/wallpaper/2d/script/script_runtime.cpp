#include "script_runtime.h"

#include <quickjs.h>

#include <string>

#include "script_engine.h"
#include "script_engine_internal.h"
#include "shared/core/logger.h"

#define TAG "SCRIPT"

namespace {

int interruptHandler(JSRuntime*, void* opaque) {
    return static_cast<ScriptRuntime*>(opaque)->deadlineExceeded() ? 1 : 0;
}

}  // namespace

void ScriptRuntime::create(const std::string& assets_dir) {
    runtime_ = JS_NewRuntime();
    if (!runtime_) {
        LOG_TAG_W(TAG, "QuickJS runtime creation failed");
        return;
    }
    JS_SetMemoryLimit(runtime_, 64u * 1024u * 1024u);
    JS_SetInterruptHandler(runtime_, interruptHandler, this);
    installScriptModuleLoader(runtime_);
    context_ = JS_NewContext(runtime_);
    if (!context_) {
        LOG_TAG_W(TAG, "QuickJS context creation failed");
        return;
    }
    installScriptHostFunctions(context_);

    std::string base_classes;
    if (!assets_dir.empty()) base_classes = readScriptFile(assets_dir + "/scripts/jsclasses/baseclasses.js");
    if (base_classes.empty())
        LOG_TAG_W(
            TAG, "baseclasses.js not found under the assets folder; Vec2/Vec3/Mat4 and WEMath modules are unavailable");

    struct Chunk {
        const char* name;
        const std::string* text;
    };
    const std::string prelude = scriptPreludeSource();
    const Chunk chunks[] = {{"baseclasses.js", &base_classes}, {"<script-prelude>", &prelude}};
    for (const Chunk& chunk : chunks) {
        if (chunk.text->empty()) continue;
        ScriptEngine::CallScope scope(ScriptEngine::instance(), nullptr, 0, 500.0);
        JSValue result = JS_Eval(context_, chunk.text->c_str(), chunk.text->size(), chunk.name, JS_EVAL_TYPE_GLOBAL);
        if (JS_IsException(result)) scriptLogException(context_, chunk.name);
        JS_FreeValue(context_, result);
    }
}

void ScriptRuntime::destroy() {
    if (context_) JS_FreeContext(context_);
    if (runtime_) JS_FreeRuntime(runtime_);
    context_ = nullptr;
    runtime_ = nullptr;
    deadline_ns_ = 0;
}

bool ScriptRuntime::deadlineExceeded() const {
    return deadline_ns_ != 0 && scriptNowNs() > deadline_ns_;
}
