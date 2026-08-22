#ifndef BUILD_CONFIG_H
#define BUILD_CONFIG_H

// Xmake sets this for every target. Keeping a release-safe default also makes
// individual translation units predictable when they are compiled externally.
#ifndef DEBUG_BUILD
#define DEBUG_BUILD 0
#endif

#endif  // BUILD_CONFIG_H
