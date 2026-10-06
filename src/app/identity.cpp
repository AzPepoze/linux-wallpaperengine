#include "app/identity.h"

namespace lwe::identity {

void printEngineIdentity(FILE* out) {
    if (!out) return;
    // Single line, exact keys: the GUI parses this from stdout before any logs.
    fprintf(out,
            "{\"name\":\"linux-wallpaperengine\",\"implementation\":\"azpepoze\",\"version\":\"%s\","
            "\"control_socket\":true,\"features\":[\"control-socket\",\"transition\",\"set-property\",\"scaling\","
            "\"volume\",\"fps\"]}\n",
            kVersion);
}

}  // namespace lwe::identity
