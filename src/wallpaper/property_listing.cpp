#include "wallpaper/property_listing.h"

#include <algorithm>
#include <string>
#include <vector>

namespace {
// Upstream calls the bool type "boolean" in its listing.
std::string typeName(const std::string& type) {
    return type == "bool" ? "boolean" : type;
}

std::string valueText(const UserPropertyDef& def) {
    char buffer[128];
    // A label has no value of its own; upstream shows its text.
    if (def.type == "text") return def.label;
    if (def.type == "slider" && def.value.type == UserPropertyValue::Type::Number) {
        std::snprintf(buffer, sizeof(buffer), "%f", def.value.n);
        return buffer;
    }
    if (def.value.type == UserPropertyValue::Type::Color) {
        // Upstream always shows alpha as 1.
        std::snprintf(buffer, sizeof(buffer), "%f, %f, %f, 1.000000", def.value.color[0], def.value.color[1],
                      def.value.color[2]);
        return buffer;
    }
    return def.value.asText();
}

void printProperty(FILE* out, const UserPropertyDef& def) {
    fprintf(out, "%s - %s\n", def.key.c_str(), typeName(def.type).c_str());
    fprintf(out, "\tText: %s\n", def.label.c_str());
    if (def.type == "slider") fprintf(out, "\tMin: %g\n\tMax: %g\n\tStep: %g\n", def.min, def.max, def.step);
    fprintf(out, "\tValue: %s\n", valueText(def).c_str());
    if (def.type == "combo") {
        fprintf(out, "Values: \n");
        for (const UserPropertyOption& option : def.options)
            fprintf(out, "\t\t%s = %s\n", option.value.c_str(), option.label.c_str());
    }
    fprintf(out, "\n");
}
}  // namespace

void printProperties(FILE* out, const UserProperties& properties) {
    std::vector<const UserPropertyDef*> sorted;
    sorted.reserve(properties.all().size());
    for (const UserPropertyDef& def : properties.all()) sorted.push_back(&def);
    std::sort(sorted.begin(), sorted.end(),
              [](const UserPropertyDef* a, const UserPropertyDef* b) { return a->key < b->key; });
    for (const UserPropertyDef* def : sorted) printProperty(out, *def);
}
