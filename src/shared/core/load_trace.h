#pragma once
#include <chrono>
#include <cstdlib>

#include "shared/core/logger.h"
namespace load_trace {
inline bool enabled() {
    static const bool value = std::getenv("LWE_LOAD_TRACE") != nullptr;
    return value;
}
inline double milliseconds(std::chrono::steady_clock::duration duration) {
    return std::chrono::duration<double, std::milli>(duration).count();
}
}  // namespace load_trace
