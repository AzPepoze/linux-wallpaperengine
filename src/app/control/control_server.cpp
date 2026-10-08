#include "app/control/control_server.h"

#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

#include <cerrno>
#include <cstdio>
#include <cstring>

#include "app/control/control_endpoint.h"

namespace {
bool sockaddrFromPath(const std::string& path, sockaddr_un& addr) {
    if (path.empty() || path.size() >= sizeof(addr.sun_path)) return false;
    addr = {};
    addr.sun_family = AF_UNIX;
    std::strncpy(addr.sun_path, path.c_str(), sizeof(addr.sun_path) - 1);
    return true;
}

bool probeLiveOwner(const std::string& path) {
    sockaddr_un addr = {};
    if (!sockaddrFromPath(path, addr)) return false;
    const int fd = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0) return false;
    const bool live = ::connect(fd, (sockaddr*)&addr, sizeof(addr)) == 0;
    ::close(fd);
    return live;
}
}  // namespace

ControlServer::~ControlServer() {
    close();
}

bool ControlServer::bind(const std::string& key) {
    close();
    const std::string dir = controlSocketDir();
    if (::mkdir(dir.c_str(), 0700) != 0 && errno != EEXIST) {
        std::fprintf(stderr, "[control] cannot create %s: %s\n", dir.c_str(), std::strerror(errno));
        return false;
    }

    path_ = controlSocketPath(key);
    if (probeLiveOwner(path_)) {
        path_.clear();
        return false;
    }
    ::unlink(path_.c_str());

    sockaddr_un addr = {};
    if (!sockaddrFromPath(path_, addr)) {
        path_.clear();
        return false;
    }

    const int fd = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0) {
        path_.clear();
        return false;
    }
    if (::bind(fd, (sockaddr*)&addr, sizeof(addr)) != 0 || ::listen(fd, 8) != 0) {
        std::fprintf(stderr, "[control] bind %s failed: %s\n", path_.c_str(), std::strerror(errno));
        ::close(fd);
        ::unlink(path_.c_str());
        path_.clear();
        return false;
    }
    const int flags = ::fcntl(fd, F_GETFL, 0);
    if (flags >= 0) ::fcntl(fd, F_SETFL, flags | O_NONBLOCK);

    listen_fd_ = fd;
    return true;
}

void ControlServer::poll(std::vector<SwitchRequest>& out) {
    if (listen_fd_ < 0) return;
    for (;;) {
        const int client = ::accept(listen_fd_, nullptr, nullptr);
        if (client < 0) return;  // EAGAIN or a real error; either way stop this frame.

        const int flags = ::fcntl(client, F_GETFL, 0);
        if (flags >= 0) ::fcntl(client, F_SETFL, flags | O_NONBLOCK);

        // Give the sender a moment to deliver the request.
        pollfd pfd = {.fd = client, .events = POLLIN, .revents = 0};
        ::poll(&pfd, 1, 50);

        char buffer[65536];
        const ssize_t received = ::read(client, buffer, sizeof(buffer) - 1);
        if (received > 0) {
            SwitchRequest request;
            std::string error;
            if (decodeSwitchRequest(std::string(buffer, (size_t)received), request, error)) {
                out.push_back(std::move(request));
                const std::string reply = encodeReply(true);
                (void)::send(client, reply.data(), reply.size(), MSG_NOSIGNAL);
            } else {
                const std::string reply = encodeReply(false, error);
                (void)::send(client, reply.data(), reply.size(), MSG_NOSIGNAL);
            }
        }
        ::close(client);
    }
}

void ControlServer::close() {
    if (listen_fd_ >= 0) {
        ::close(listen_fd_);
        listen_fd_ = -1;
    }
    if (!path_.empty()) {
        ::unlink(path_.c_str());
        path_.clear();
    }
}
