#include "app/platform/pointer/hyprland_pointer.h"

#include <cjson/cJSON.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/un.h>
#include <unistd.h>

#include <cstdlib>
#include <cstring>

namespace {
// Keeps a stalled compositor from stalling a frame.
constexpr timeval kReplyTimeout = {0, 20000};

std::string socketPath() {
    const char* signature = std::getenv("HYPRLAND_INSTANCE_SIGNATURE");
    if (!signature || !*signature) return "";
    const char* runtime = std::getenv("XDG_RUNTIME_DIR");
    if (runtime && *runtime) return std::string(runtime) + "/hypr/" + signature + "/.socket.sock";
    return std::string("/tmp/hypr/") + signature + "/.socket.sock";
}

// Hyprland answers one request per connection and then closes it.
std::string request(const std::string& command) {
    const std::string path = socketPath();
    if (path.empty() || path.size() >= sizeof(sockaddr_un::sun_path)) return "";

    sockaddr_un addr = {};
    addr.sun_family = AF_UNIX;
    std::strncpy(addr.sun_path, path.c_str(), sizeof(addr.sun_path) - 1);

    const int fd = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0) return "";
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &kReplyTimeout, sizeof(kReplyTimeout));
    std::string reply;
    if (::connect(fd, (sockaddr*)&addr, sizeof(addr)) == 0 &&
        ::send(fd, command.data(), command.size(), MSG_NOSIGNAL) == (ssize_t)command.size()) {
        char buffer[4096];
        ssize_t received = 0;
        while ((received = ::read(fd, buffer, sizeof(buffer))) > 0) reply.append(buffer, (size_t)received);
    }
    ::close(fd);
    return reply;
}

double numberOf(const cJSON* object, const char* key, double fallback) {
    const cJSON* value = cJSON_GetObjectItemCaseSensitive(object, key);
    return cJSON_IsNumber(value) ? value->valuedouble : fallback;
}
}  // namespace

bool parseHyprlandCursor(const std::string& json, double& x, double& y) {
    cJSON* root = cJSON_Parse(json.c_str());
    if (!root) return false;
    const cJSON* xs = cJSON_GetObjectItemCaseSensitive(root, "x");
    const cJSON* ys = cJSON_GetObjectItemCaseSensitive(root, "y");
    const bool ok = cJSON_IsNumber(xs) && cJSON_IsNumber(ys);
    if (ok) {
        x = xs->valuedouble;
        y = ys->valuedouble;
    }
    cJSON_Delete(root);
    return ok;
}

bool parseHyprlandMonitor(const std::string& json, const std::string& name, HyprlandMonitor& out) {
    cJSON* root = cJSON_Parse(json.c_str());
    if (!root) return false;
    bool found = false;
    const cJSON* monitor = nullptr;
    cJSON_ArrayForEach(monitor, root) {
        const cJSON* monitor_name = cJSON_GetObjectItemCaseSensitive(monitor, "name");
        if (!cJSON_IsString(monitor_name) || name != monitor_name->valuestring) continue;
        out.name = name;
        out.x = numberOf(monitor, "x", 0.0);
        out.y = numberOf(monitor, "y", 0.0);
        out.width = numberOf(monitor, "width", 0.0);
        out.height = numberOf(monitor, "height", 0.0);
        found = out.width > 0.0 && out.height > 0.0;
        break;
    }
    cJSON_Delete(root);
    return found;
}

OutputPointer pointOnMonitor(double x, double y, const HyprlandMonitor& monitor) {
    OutputPointer out;
    const double local_x = (x - monitor.x) / monitor.width;
    const double local_y = (y - monitor.y) / monitor.height;
    out.inside = local_x >= 0.0 && local_x < 1.0 && local_y >= 0.0 && local_y < 1.0;
    out.x = (float)local_x;
    out.y = (float)local_y;
    return out;
}

bool HyprlandPointer::open(const std::string& output) {
    output_ = output;
    HyprlandMonitor monitor;
    return !output.empty() && parseHyprlandMonitor(request("j/monitors"), output, monitor);
}

std::optional<OutputPointer> HyprlandPointer::sample() {
    double x = 0.0, y = 0.0;
    HyprlandMonitor monitor;
    if (!parseHyprlandCursor(request("j/cursorpos"), x, y)) return std::nullopt;
    if (!parseHyprlandMonitor(request("j/monitors"), output_, monitor)) return std::nullopt;
    return pointOnMonitor(x, y, monitor);
}
