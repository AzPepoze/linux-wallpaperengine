#include "wallpaper/live_fields.h"

#include "test_util.h"
using test::expect;

namespace {

void testDropsOnlyListedKeys() {
    const std::string json = R"({"visible":true,"scale":"1 1 1","origin":"5 6 0","name":"a"})";
    const std::string kept = jsonWithout(json, {"visible", "scale"});
    expect("drops listed keys", kept.find("visible") == std::string::npos, "visible is removed");
    expect("drops listed keys", kept.find("scale") == std::string::npos, "scale is removed");
    expect("drops listed keys", kept.find("origin") != std::string::npos, "origin is kept");
    expect("drops listed keys", kept.find("name") != std::string::npos, "name is kept");
}

void testDifferenceOutsideListedKeysIsKept() {
    const std::string before = R"({"visible":true,"origin":"5 6 0"})";
    const std::string after = R"({"visible":false,"origin":"9 6 0"})";
    expect("outside keys", jsonWithout(before, {"visible"}) != jsonWithout(after, {"visible"}),
           "an origin change still differs");
}

void testSameExceptListedKeysCompareEqual() {
    const std::string before = R"({"visible":true,"alpha":0.2,"name":"a"})";
    const std::string after = R"({"visible":false,"alpha":0.9,"name":"a"})";
    expect("equal except", jsonWithout(before, {"visible", "alpha"}) == jsonWithout(after, {"visible", "alpha"}),
           "only listed keys changed, so the objects compare equal");
}

void testUnparsableInputComesBackUnchanged() {
    const std::string broken = "{not json";
    expect("unparsable", jsonWithout(broken, {"visible"}) == broken, "input is returned as is");
}

void testScriptPropertiesAreRemovedAtAnyDepth() {
    const std::string before =
        R"({"origin":{"script":"a","scriptproperties":{"x":{"user":"x3","value":0.1}},"value":"1 2 0"},"name":"a"})";
    const std::string after =
        R"({"origin":{"script":"a","scriptproperties":{"x":{"user":"x3","value":0.9}},"value":"1 2 0"},"name":"a"})";
    expect("script properties", jsonWithoutScriptProperties(before) == jsonWithoutScriptProperties(after),
           "only the scriptproperties values changed, so the objects compare equal");
    expect("script properties", jsonWithoutScriptProperties(before).find("scriptproperties") == std::string::npos,
           "the scriptproperties key is removed");
    expect("script properties", jsonWithoutScriptProperties(before).find("\"script\"") != std::string::npos,
           "the script source is kept");
}

void testScriptSourceChangeStillDiffers() {
    const std::string before = R"({"origin":{"script":"a","value":"1 2 0"}})";
    const std::string after = R"({"origin":{"script":"b","value":"1 2 0"}})";
    expect("script source", jsonWithoutScriptProperties(before) != jsonWithoutScriptProperties(after),
           "a changed script source still differs");
}

void testLiveGeneralSettingsCompareEqual() {
    const std::string before = R"({"camerashakeamplitude":0.5,"bloomstrength":1.0,"ambientcolor":"0.3 0.3 0.3","zoom":1.0})";
    const std::string after = R"({"camerashakeamplitude":0.9,"bloomstrength":2.0,"ambientcolor":"0.1 0.2 0.3","zoom":1.4})";
    expect("live general", jsonWithoutLiveGeneral(before, true) == jsonWithoutLiveGeneral(after, true),
           "live general values differ only, so the blocks compare equal");
}

void testNonLiveGeneralSettingStillDiffers() {
    const std::string before = R"({"bloom":false,"hdr":true})";
    const std::string after = R"({"bloom":true,"hdr":true})";
    expect("non-live general", jsonWithoutLiveGeneral(before, true) != jsonWithoutLiveGeneral(after, true),
           "toggling bloom still rebuilds");
}

void testScriptDrivenZoomStillDiffers() {
    const std::string before = R"({"zoom":1.0})";
    const std::string after = R"({"zoom":1.4})";
    expect("script zoom", jsonWithoutLiveGeneral(before, false) != jsonWithoutLiveGeneral(after, false),
           "a script-driven zoom is not live and still rebuilds");
}

void testPassConstantsAreIgnoredButCombosAreNot() {
    const std::string before =
        R"({"passes":[{"constantshadervalues":{"Strength":0.5},"combos":{"MODE":1}}],"file":"effects/a/effect.json"})";
    const std::string tuned =
        R"({"passes":[{"constantshadervalues":{"Strength":0.9},"combos":{"MODE":1}}],"file":"effects/a/effect.json"})";
    const std::string combo =
        R"({"passes":[{"constantshadervalues":{"Strength":0.5},"combos":{"MODE":2}}],"file":"effects/a/effect.json"})";
    expect("pass constants", jsonWithoutPassConstants(before) == jsonWithoutPassConstants(tuned),
           "a constant value change compares equal");
    expect("pass constants", jsonWithoutPassConstants(before) != jsonWithoutPassConstants(combo),
           "a combo change still differs");
}

void testEffectConstantsInObjectAreIgnored() {
    const std::string before =
        R"({"name":"a","effects":[{"file":"f","passes":[{"constantshadervalues":{"alpha":1.0},"id":3}]}]})";
    const std::string after =
        R"({"name":"a","effects":[{"file":"f","passes":[{"constantshadervalues":{"alpha":0.4},"id":3}]}]})";
    const std::string hidden =
        R"({"name":"a","effects":[{"file":"g","passes":[{"constantshadervalues":{"alpha":0.4},"id":3}]}]})";
    expect("object effects", jsonWithoutEffectConstants(before) == jsonWithoutEffectConstants(after),
           "a constant change inside an effect compares equal");
    expect("object effects", jsonWithoutEffectConstants(before) != jsonWithoutEffectConstants(hidden),
           "an effect file change still differs");
}

void testInstanceOverridesAreIgnored() {
    const std::string before =
        R"({"particle":"particles/a.json","instanceoverride":{"rate":1.0,"alpha":0.5,"emitterflag":1}})";
    const std::string after =
        R"({"particle":"particles/a.json","instanceoverride":{"rate":2.0,"alpha":0.1,"emitterflag":1}})";
    const std::string other =
        R"({"particle":"particles/a.json","instanceoverride":{"rate":2.0,"alpha":0.1,"emitterflag":2}})";
    expect("instance overrides", jsonWithoutInstanceOverrides(before) == jsonWithoutInstanceOverrides(after),
           "a rate and alpha change compares equal");
    expect("instance overrides", jsonWithoutInstanceOverrides(before) != jsonWithoutInstanceOverrides(other),
           "an unknown instance override still differs");
}

}  // namespace

int main() {
    testDropsOnlyListedKeys();
    testDifferenceOutsideListedKeysIsKept();
    testSameExceptListedKeysCompareEqual();
    testUnparsableInputComesBackUnchanged();
    testScriptPropertiesAreRemovedAtAnyDepth();
    testScriptSourceChangeStillDiffers();
    testLiveGeneralSettingsCompareEqual();
    testNonLiveGeneralSettingStillDiffers();
    testScriptDrivenZoomStillDiffers();
    testPassConstantsAreIgnoredButCombosAreNot();
    testEffectConstantsInObjectAreIgnored();
    testInstanceOverridesAreIgnored();
    return test::finish("live fields tests");
}
