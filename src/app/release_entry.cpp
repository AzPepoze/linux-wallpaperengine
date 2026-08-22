#include <cstdio>
#include <cstring>

#include "shared/core/build_config.h"

#if !DEBUG_BUILD
#include "sokol_app.h"

extern "C" sapp_desc lwe_app_descriptor(int argc, char* argv[]);

int main(int argc, char* argv[]) {
    for (int index = 1; index < argc; ++index) {
        if (strcmp(argv[index], "--sandbox") == 0) {
            fprintf(stderr, "--sandbox is available only in debug builds.\n");
            return 1;
        }
    }

    const sapp_desc desc = lwe_app_descriptor(argc, argv);
    sapp_run(&desc);
    return 0;
}
#endif
