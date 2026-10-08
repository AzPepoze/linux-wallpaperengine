#ifndef PLUGIN_H
#define PLUGIN_H

// Bumped whenever an exported plugin interface changes; a plugin built against another value is refused.
constexpr int kPluginAbi = 1;

// Loads lib<name>.so from $LWE_PLUGIN_DIR, the executable's directory, <exe>/../lib or
// <exe>/../lib/linux-wallpaperengine. Returns null when the library is missing or its lwe_plugin_abi() differs. Each
// name is tried once.
void* loadPlugin(const char* name);

// Looks up a symbol in a plugin loaded by loadPlugin; null when the plugin or symbol is missing.
void* pluginSymbol(const char* name, const char* symbol);

#endif  // PLUGIN_H
