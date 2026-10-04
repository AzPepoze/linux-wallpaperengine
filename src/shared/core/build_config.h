#ifndef BUILD_CONFIG_H
#define BUILD_CONFIG_H

// Xmake sets this for every target; the release-safe default keeps externally
// compiled translation units predictable.
#ifndef DEBUG_BUILD
#define DEBUG_BUILD 0
#endif

// Set by the `web` xmake option; off by default so the core build has no Qt
// dependency. See xmake.lua.
#ifndef LWE_WEB
#define LWE_WEB 0
#endif

// Set by the `mpris` xmake option; on when libsystemd is available.
#ifndef LWE_MPRIS
#define LWE_MPRIS 0
#endif

#endif  // BUILD_CONFIG_H
