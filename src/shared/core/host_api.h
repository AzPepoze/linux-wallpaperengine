#ifndef HOST_API_H
#define HOST_API_H

// Marks a host function or class that a plugin calls. The binary exports these (see --export-dynamic in xmake.lua).
#define LWE_HOST_API __attribute__((visibility("default")))

#endif  // HOST_API_H
