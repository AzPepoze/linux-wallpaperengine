#include "app/identity.h"

#include <cstdio>
#include <cstdlib>
#include <string>

#include "test_util.h"

int main() {
    char* buffer = nullptr;
    size_t size = 0;
    FILE* stream = open_memstream(&buffer, &size);
    CHECK(stream != nullptr);
    lwe::identity::printEngineIdentity(stream);
    fclose(stream);
    const std::string json(buffer, size);
    free(buffer);

    CHECK(json.rfind("{\"name\":\"linux-wallpaperengine\"", 0) == 0);
    CHECK(json.find("\"implementation\":\"azpepoze\"") != std::string::npos);
    CHECK(json.find("\"control_socket\":true") != std::string::npos);
    CHECK(json.find("\"features\":[\"control-socket\",\"transition\",\"set-property\",\"scaling\",\"volume\","
                    "\"fps\"]") != std::string::npos);
    CHECK(json.find("\"version\":\"") != std::string::npos);
    CHECK(!json.empty());
    CHECK(json.back() == '\n');
    CHECK(json.find('\n') == json.size() - 1);  // exactly one line
    return test::finish("identity checks");
}
