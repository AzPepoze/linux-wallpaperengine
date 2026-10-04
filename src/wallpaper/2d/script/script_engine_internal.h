#ifndef SCRIPT_ENGINE_INTERNAL_H
#define SCRIPT_ENGINE_INTERNAL_H

#include <cstdint>
#include <string>

struct JSContext;
struct JSRuntime;

// The embedded SceneScript runtime prelude (the JavaScript half of the script API), evaluated once per context.
const char* scriptPreludeSource();

// Installs the `__lwe*` C functions (console.log, localStorage, the scene bridge) on the context's global object.
void installScriptHostFunctions(JSContext* ctx);

// Installs the ES-module loader so scripts can import the assets' jsmodules/*.js.
void installScriptModuleLoader(JSRuntime* runtime);

// Reads a file whole; returns an empty string when it cannot be opened.
std::string readScriptFile(const std::string& path);

// Monotonic clock in nanoseconds, shared by the host functions and the call deadline.
int64_t scriptNowNs();

#endif  // SCRIPT_ENGINE_INTERNAL_H
