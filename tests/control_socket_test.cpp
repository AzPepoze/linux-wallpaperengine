#include <string>
#include <vector>

#include <unistd.h>

#include "app/control/control_client.h"
#include "app/control/control_server.h"
#include "test_util.h"

int main() {
    const std::string key = "unittest-" + std::to_string((long)getpid());
    ControlServer server;
    CHECK(server.bind(key));

    // No client yet: poll must yield nothing and must not block.
    std::vector<SwitchRequest> pending;
    server.poll(pending);
    CHECK(pending.empty());

    SwitchRequest request;
    request.path = "/wp/test";
    request.transition = 21;
    CHECK(ControlClient::tryHandoff(key, request));

    server.poll(pending);
    CHECK(pending.size() == 1);
    CHECK(pending[0].path == "/wp/test");
    CHECK(pending[0].transition == 21);

    // A second server on the same key fails while the first is bound.
    ControlServer duplicate;
    CHECK(!duplicate.bind(key));

    server.close();
    // After close, handoff finds no live owner.
    CHECK(!ControlClient::tryHandoff(key, request));
    return test::finish("control socket checks");
}
