#include "shared/core/web_devtools.h"

#include <string>

#include "test_util.h"

int main() {
    // Relative devtoolsFrontendUrl from Chromium's /json -> absolute localhost URL.
    const std::string json =
        "[{\"id\":\"ABC\",\"devtoolsFrontendUrl\":\"/devtools/inspector.html?ws=localhost:9222/devtools/page/ABC\"}]";
    CHECK(web_devtools::parseFrontendUrl(json, 9222) ==
          "http://localhost:9222/devtools/inspector.html?ws=localhost:9222/devtools/page/ABC");

    // Escaped slashes and an absolute URL are both handled.
    const std::string escaped =
        "{\"devtoolsFrontendUrl\":\"http:\\/\\/localhost:9333\\/devtools\\/inspector.html?ws=x\"}";
    CHECK(web_devtools::parseFrontendUrl(escaped, 9333) == "http://localhost:9333/devtools/inspector.html?ws=x");

    // No target -> empty (caller falls back to the target list).
    CHECK(web_devtools::parseFrontendUrl("[]", 9222).empty());
    CHECK(web_devtools::parseFrontendUrl("not json", 9222).empty());

    // Default launcher is xdg-open; --web-devtools-browser replaces it (URL appended last).
    const auto def = web_devtools::commandFor("", "http://localhost:9222/x");
    CHECK(def.size() == 2 && def[0] == "xdg-open" && def[1] == "http://localhost:9222/x");
    const auto named = web_devtools::commandFor("chromium", "http://x");
    CHECK(named.size() == 2 && named[0] == "chromium" && named[1] == "http://x");
    const auto app = web_devtools::commandFor("chromium --app", "http://x");
    CHECK(app.size() == 3 && app[0] == "chromium" && app[1] == "--app" && app[2] == "http://x");

    return test::finish("web devtools checks");
}
