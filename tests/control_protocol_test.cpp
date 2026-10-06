#include "app/control/control_protocol.h"

#include <string>

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

    {
        SwitchRequest mode_request;
        mode_request.path = "/wp";
        mode_request.continue_previous = true;
        SwitchRequest mode_decoded;
        std::string mode_error;
        CHECK(decodeSwitchRequest(encodeSwitchRequest(mode_request), mode_decoded, mode_error));
        CHECK(mode_decoded.continue_previous);
    }
    {
        SwitchRequest freeze_request;
        freeze_request.path = "/wp";  // default: freeze
        SwitchRequest freeze_decoded;
        std::string freeze_error;
        CHECK(decodeSwitchRequest(encodeSwitchRequest(freeze_request), freeze_decoded, freeze_error));
        CHECK(!freeze_decoded.continue_previous);
    }
    {
        SwitchRequest bad_mode;
        std::string bad_mode_error;
        CHECK(!decodeSwitchRequest("{\"path\":\"/wp\",\"transition_mode\":\"bogus\"}", bad_mode, bad_mode_error));
        CHECK(!bad_mode_error.empty());
    }

    // Optional live settings round-trip and are only encoded when set.
    {
        SwitchRequest extended;
        extended.path = "/wp";
        extended.scaling = "stretch";
        extended.volume = 42.0f;
        extended.has_volume = true;
        extended.muted = true;
        extended.has_muted = true;
        extended.fps = 75;
        extended.has_fps = true;

        const std::string extended_json = encodeSwitchRequest(extended);
        CHECK(extended_json.find("\"scaling\":\"stretch\"") != std::string::npos);
        CHECK(extended_json.find("\"volume\":42") != std::string::npos);
        CHECK(extended_json.find("\"muted\":true") != std::string::npos);
        CHECK(extended_json.find("\"fps\":75") != std::string::npos);

        SwitchRequest extended_decoded;
        std::string extended_error;
        CHECK(decodeSwitchRequest(extended_json, extended_decoded, extended_error));
        CHECK(extended_decoded.scaling == "stretch");
        CHECK(extended_decoded.has_volume && extended_decoded.volume == 42.0f);
        CHECK(extended_decoded.has_muted && extended_decoded.muted);
        CHECK(extended_decoded.has_fps && extended_decoded.fps == 75);
    }
    {
        SwitchRequest plain;
        plain.path = "/wp";
        const std::string plain_json = encodeSwitchRequest(plain);
        CHECK(plain_json.find("\"scaling\"") == std::string::npos);
        CHECK(plain_json.find("\"volume\"") == std::string::npos);
        CHECK(plain_json.find("\"muted\"") == std::string::npos);
        CHECK(plain_json.find("\"fps\"") == std::string::npos);
    }
    {
        // A payload from an older sender leaves the optional fields at their defaults.
        SwitchRequest legacy;
        std::string legacy_error;
        CHECK(decodeSwitchRequest("{\"path\":\"/old\",\"is_pkg\":false,\"transition_mode\":\"freeze\"}", legacy,
                                  legacy_error));
        CHECK(legacy.path == "/old");
        CHECK(legacy.scaling.empty());
        CHECK(!legacy.has_volume);
        CHECK(!legacy.has_muted);
        CHECK(!legacy.has_fps);
        CHECK(legacy.fps == 0);
    }
    return test::finish("control protocol checks");
}
