#include "app/flag_config.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "test_util.h"

int main() {
    char dir[] = "/tmp/lwe-flag-config-XXXXXX";
    CHECK(mkdtemp(dir) != nullptr);
    CHECK(chdir(dir) == 0);

    FILE* config = fopen("config.json", "w");
    CHECK(config != nullptr);
    fputs("{\"web_transport\":\"off-screen\",\"transition_duration_ms\":250}", config);
    fclose(config);

    CHECK(flag_config::string("web_transport") == "off-screen");
    CHECK(flag_config::integer("transition_duration_ms") == 250);
    CHECK(flag_config::string("missing").empty());
    CHECK(flag_config::integer("missing") == 0);

    chdir("/");
    return test::finish("flag config checks");
}
