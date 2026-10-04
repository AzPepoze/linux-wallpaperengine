#ifndef PHASE_TIMER_H
#define PHASE_TIMER_H

#include <chrono>

#include "logger.h"

class PhaseTimer {
   public:
    explicit PhaseTimer(const char* phase) : phase_(phase), start_(std::chrono::steady_clock::now()) {}
    ~PhaseTimer() {
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start_).count();
        LOG_I("Startup: %s took %.1f ms", phase_, ms);
    }
    PhaseTimer(const PhaseTimer&) = delete;
    PhaseTimer& operator=(const PhaseTimer&) = delete;

   private:
    const char* phase_;
    std::chrono::steady_clock::time_point start_;
};

#endif  // PHASE_TIMER_H
