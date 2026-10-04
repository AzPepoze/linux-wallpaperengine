#include <string>

#include "app/control/control_protocol.h"
#include "test_util.h"
#include "wallpaper/transition/transition_catalog.h"

int main() {
    SwitchRequest request;
    request.path = "/wp/a dir/with spaces/项目";
    request.is_pkg = true;
    request.properties = {{"greeting", "hello world"}, {"k", "1"}};
    request.transition = (int)lwe::transition::Effect::Crt;
    request.transition_time_ms = 750;

    const std::string json = encodeSwitchRequest(request);
    SwitchRequest decoded;
    std::string error;
    CHECK(decodeSwitchRequest(json, decoded, error));
    CHECK(decoded.path == request.path);
    CHECK(decoded.is_pkg);
    CHECK(decoded.properties == request.properties);
    CHECK(decoded.transition == request.transition);
    CHECK(decoded.transition_time_ms == request.transition_time_ms);

    SwitchRequest unused;
    CHECK(!decodeSwitchRequest("", unused, error));
    CHECK(!decodeSwitchRequest("{", unused, error));
    CHECK(!decodeSwitchRequest("{\"is_pkg\":true}", unused, error));  // missing path

    CHECK(encodeReply(true) == std::string("{\"ok\":true}"));
    CHECK(encodeReply(false, "bad").find("\"ok\":false") != std::string::npos);
    return test::finish("control protocol checks");
}
