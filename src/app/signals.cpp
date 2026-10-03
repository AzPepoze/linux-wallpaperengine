#include "app/signals.h"

#include <signal.h>

#include <atomic>

#include "shared/core/logger.h"
#include "shared/graphics/backend/surface.h"
#include "sokol_app.h"

namespace {
std::atomic<bool> g_terminating{false};

void crashSignalHandler(int sig) {
    LOG_E("[CRASH] Fatal signal %d (%s) received", sig, (sig == SIGSEGV) ? "SIGSEGV" : "SIGABRT");
    signal(sig, SIG_DFL);
    raise(sig);
}

void terminationSignalHandler(int sig) {
    LOG_I("[SIGNAL] Caught signal %d (%s), requesting clean quit...", sig, (sig == SIGINT) ? "SIGINT" : "SIGTERM");
    g_terminating.store(true, std::memory_order_relaxed);
    signal(sig, SIG_DFL);
    surface::requestQuit();
}
}  // namespace

void installSignalHandlers() {
    signal(SIGSEGV, crashSignalHandler);
    signal(SIGABRT, crashSignalHandler);
    signal(SIGINT, terminationSignalHandler);
    signal(SIGTERM, terminationSignalHandler);
}

bool terminationRequested() {
    return g_terminating.load(std::memory_order_relaxed);
}
