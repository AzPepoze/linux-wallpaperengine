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
    // Logging takes a mutex and can deadlock if the signal interrupts a log.
    // Publish the quit request without entering the logger from this handler.
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
