#ifndef LIVE_FIELDS_H
#define LIVE_FIELDS_H

#include <initializer_list>
#include <string>

// The JSON object without the given top-level keys. The input comes back unchanged when it does not parse.
std::string jsonWithout(const std::string& json, std::initializer_list<const char*> keys);

#endif  // LIVE_FIELDS_H
