#include "app/control/control_client.h"

#include <fcntl.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>

#include "app/control/control_endpoint.h"

namespace {
void setError(std::string* error, const char* message) {
    if (error) *error = message;
}
}  // namespace

bool ControlClient::tryHandoff(const std::string& key, const SwitchRequest& request, std::string* error) {
    const std::string path = controlSocketPath(key);
    if (path.empty() || path.size() >= sizeof(sockaddr_un::sun_path)) {
        setError(error, "invalid control path");
        return false;
    }

    sockaddr_un addr = {};
    addr.sun_family = AF_UNIX;
    std::strncpy(addr.sun_path, path.c_str(), sizeof(addr.sun_path) - 1);

    const int fd = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0) {
        setError(error, "socket() failed");
        return false;
    }

    if (::connect(fd, (sockaddr*)&addr, sizeof(addr)) != 0) {
        setError(error, std::strerror(errno));
        ::close(fd);
        return false;
    }

    const std::string payload = encodeSwitchRequest(request);
    if (::send(fd, payload.data(), payload.size(), MSG_NOSIGNAL) < 0) {
        setError(error, "send failed");
        ::close(fd);
        return false;
    }

    // Read the ack only if already available; the owner drains its socket from the frame loop.
    const int flags = ::fcntl(fd, F_GETFL, 0);
    if (flags >= 0) ::fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    char reply[4096];
    const ssize_t received = ::read(fd, reply, sizeof(reply) - 1);
    ::close(fd);

    if (received > 0) {
        const std::string text(reply, (size_t)received);
        if (text.find("\"ok\":false") != std::string::npos) {
            setError(error, "owner rejected the request");
            return false;
        }
    }
    if (error) error->clear();
    return true;
}
