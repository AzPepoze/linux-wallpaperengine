#ifndef LIVE_FIELDS_H
#define LIVE_FIELDS_H

#include <initializer_list>
#include <string>

// The JSON object without the given top-level keys. The input comes back unchanged when it does not parse.
std::string jsonWithout(const std::string& json, std::initializer_list<const char*> keys);

// The JSON without every nested "scriptproperties" object, which a script update delivers in place.
std::string jsonWithoutScriptProperties(const std::string& json);

// The general block without the settings that update in place. `zoom_live` is false when zoom is script driven.
std::string jsonWithoutLiveGeneral(const std::string& json, bool zoom_live);

#endif  // LIVE_FIELDS_H
