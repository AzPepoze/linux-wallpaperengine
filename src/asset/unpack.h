#ifndef UNPACK_H
#define UNPACK_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

bool extract_pkg(const char* pkg_path, const char* output_dir);

#ifdef __cplusplus
}
#endif

#endif  // UNPACK_H
