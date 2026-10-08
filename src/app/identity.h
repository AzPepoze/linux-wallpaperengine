#ifndef APP_IDENTITY_H
#define APP_IDENTITY_H

#include <stdio.h>

// Build-time version, injected by xmake from `git describe --tags --always`.
// The fallback keeps ad-hoc builds and unit tests compiling.
#ifndef LWE_VERSION
#define LWE_VERSION "unknown"
#endif

namespace lwe::identity {

inline constexpr const char* kVersion = LWE_VERSION;

// Writes the frozen one-line JSON identity to `out`. Callers must keep stdout
// free of log output so a GUI probe can parse this line.
void printEngineIdentity(FILE* out);

}  // namespace lwe::identity

#endif  // APP_IDENTITY_H
