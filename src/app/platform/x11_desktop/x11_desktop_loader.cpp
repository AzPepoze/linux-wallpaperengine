#include <unistd.h>

#include <string>

#include "app/platform/x11_desktop/x11_desktop.h"
#include "shared/core/plugin.h"

namespace {
constexpr const char* kX11DesktopPlugin = "x11";

using PlaceWindowFunction = bool (*)(const char*, const char*);
using QueryPointerFunction = bool (*)(const char*, float*, float*);
}  // namespace

std::optional<OutputPointer> x11QueryPointer(const std::string& output) {
    const auto query = reinterpret_cast<QueryPointerFunction>(pluginSymbol(kX11DesktopPlugin, "lwe_x11_query_pointer"));
    if (!query) return std::nullopt;
    OutputPointer pointer;
    pointer.inside = query(output.c_str(), &pointer.x, &pointer.y);
    return pointer;
}

std::string x11DesktopTitle(const std::string& output) {
    return "lwe-desktop " + output + " " + std::to_string(getpid());
}

bool x11PlaceDesktopWindow(const std::string& title, const std::string& output) {
    const auto place =
        reinterpret_cast<PlaceWindowFunction>(pluginSymbol(kX11DesktopPlugin, "lwe_x11_place_desktop_window"));
    return place && place(title.c_str(), output.c_str());
}
