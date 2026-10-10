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

}  // namespace

int main() {
    testDropsOnlyListedKeys();
    testDifferenceOutsideListedKeysIsKept();
    testSameExceptListedKeysCompareEqual();
    testUnparsableInputComesBackUnchanged();
    return test::finish("live fields tests");
}
