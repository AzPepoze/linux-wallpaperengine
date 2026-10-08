#ifndef SCRIPT_RUNTIME_H
#define SCRIPT_RUNTIME_H

#include <cstdint>
#include <string>

struct JSContext;
struct JSRuntime;

// The one QuickJS runtime and context; the script registry and event dispatch stay in ScriptEngine.
class ScriptRuntime {
   public:
    ScriptRuntime() = default;
    ScriptRuntime(const ScriptRuntime&) = delete;
    ScriptRuntime& operator=(const ScriptRuntime&) = delete;

    // Creates the runtime and evaluates the script API. `assets_dir` supplies baseclasses.js and jsmodules/.
    void create(const std::string& assets_dir);
    void destroy();

    JSContext* context() const {
        return context_;
    }
    void setDeadlineNs(int64_t deadline_ns) {
        deadline_ns_ = deadline_ns;
    }
    bool deadlineExceeded() const;

   private:
    JSRuntime* runtime_ = nullptr;
    JSContext* context_ = nullptr;
    int64_t deadline_ns_ = 0;
};

#endif  // SCRIPT_RUNTIME_H
