#ifndef APP_IDENTITY_H
#define APP_IDENTITY_H

#include <stdio.h>

// Injected by xmake; the fallback keeps ad-hoc builds compiling.
#ifndef LWE_VERSION
#define LWE_VERSION "unknown"
#endif

namespace lwe::identity {

inline constexpr const char* kVersion = LWE_VERSION;

// Writes the one-line JSON identity; stdout must stay log-free for the GUI probe.
void printEngineIdentity(FILE* out);

}  // namespace lwe::identity

#endif  // APP_IDENTITY_H
