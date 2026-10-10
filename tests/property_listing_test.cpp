#include "wallpaper/property_listing.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <string>

#include "test_util.h"

namespace {
const char* kProject = R"({
  "general": {
    "properties": {
      "tint": {"type": "color", "text": "Tint", "value": "1 0.247059 0.835294"},
      "train": {"type": "combo", "text": "Train", "value": "2",
                "options": [{"label": "Still", "value": "1"}, {"label": "Moving", "value": "2"}]},
      "audiobars": {"type": "bool", "text": "Audio Bars 1", "value": true},
      "speed": {"type": "slider", "text": "Speed", "min": 0.1, "max": 1, "step": 0.1, "value": 0.3},
      "title": {"type": "textinput", "text": "Title", "value": "hi"},
      "note": {"type": "text", "text": "<b>Hi</b>"}
    }
  }
})";
}  // namespace

int main() {
    char dir[] = "/tmp/lwe-property-listing-XXXXXX";
    CHECK(mkdtemp(dir) != nullptr);
    const std::string project_path = std::string(dir) + "/project.json";
    FILE* project = fopen(project_path.c_str(), "w");
    CHECK(project != nullptr);
    fputs(kProject, project);
    fclose(project);

    UserProperties properties;
    CHECK(properties.loadProject(project_path));

    char* buffer = nullptr;
    size_t size = 0;
    FILE* stream = open_memstream(&buffer, &size);
    CHECK(stream != nullptr);
    printProperties(stream, properties);
    fclose(stream);
    const std::string listing(buffer, size);
    free(buffer);

    const std::string expected =
        "audiobars - boolean\n"
        "\tText: Audio Bars 1\n"
        "\tValue: 1\n"
        "\n"
        "note - text\n"
        "\tText: <b>Hi</b>\n"
        "\tValue: <b>Hi</b>\n"
        "\n"
        "speed - slider\n"
        "\tText: Speed\n"
        "\tMin: 0.1\n"
        "\tMax: 1\n"
        "\tStep: 0.1\n"
        "\tValue: 0.300000\n"
        "\n"
        "tint - color\n"
        "\tText: Tint\n"
        "\tValue: 1.000000, 0.247059, 0.835294, 1.000000\n"
        "\n"
        "title - textinput\n"
        "\tText: Title\n"
        "\tValue: hi\n"
        "\n"
        "train - combo\n"
        "\tText: Train\n"
        "\tValue: 2\n"
        "Values: \n"
        "\t\t1 = Still\n"
        "\t\t2 = Moving\n"
        "\n";
    CHECK(listing == expected);

    unlink(project_path.c_str());
    rmdir(dir);
    return test::finish("property listing checks");
}
