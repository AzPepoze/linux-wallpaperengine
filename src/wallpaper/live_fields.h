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

// An effect instance without the constant values of each pass, which update in place.
std::string jsonWithoutPassConstants(const std::string& instance_json);

// An object without the constant values of its effects' passes, which update in place.
std::string jsonWithoutEffectConstants(const std::string& object_json);

// An object without the particle instance overrides that update in place.
std::string jsonWithoutInstanceOverrides(const std::string& object_json);

#endif  // LIVE_FIELDS_H
