#ifndef BUILD_CONFIG_H
#define BUILD_CONFIG_H

// Xmake sets this for every target; the release-safe default keeps externally
// compiled translation units predictable.
#ifndef DEBUG_BUILD
#define DEBUG_BUILD 0
#endif

#endif  // BUILD_CONFIG_H
