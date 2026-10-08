// Checks plugin lookup: the MPRIS plugin loads from the executable directory, and a missing plugin is null.
// Needs the mpris target built next to this test (xmake build builds both).

#include "shared/core/plugin.h"
#include "test_util.h"
using test::expect;

int main() {
    expect("load", loadPlugin("mpris") != nullptr, "mpris loads from the executable directory");
    expect("load", pluginSymbol("mpris", "lwe_create_mpris_source") != nullptr, "entry point is exported");
    expect("load", pluginSymbol("mpris", "lwe_no_such_symbol") == nullptr, "a missing symbol is null");
    expect("load", loadPlugin("lwe_no_such_plugin") == nullptr, "a missing plugin is null");
    expect("load", loadPlugin("lwe_no_such_plugin") == nullptr, "a missing plugin stays null on a second try");
    return test::finish("plugin loader tests");
}
