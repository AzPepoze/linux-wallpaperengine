#ifndef CONTROL_SERVER_H
#define CONTROL_SERVER_H

#include <string>
#include <vector>

#include "app/control/control_protocol.h"

// Owns the control socket for one display key; polling is non-blocking.
class ControlServer {
   public:
    ControlServer() = default;
    ~ControlServer();
    ControlServer(const ControlServer&) = delete;
    ControlServer& operator=(const ControlServer&) = delete;

    // Binds the socket for `key`. Returns false if another live instance owns it.
    bool bind(const std::string& key);
    // Appends any decoded requests received since the last poll.
    void poll(std::vector<SwitchRequest>& out);
    // Closes the socket and unlinks it. Idempotent.
    void close();
    bool bound() const {
        return listen_fd_ >= 0;
    }

   private:
    int listen_fd_ = -1;
    std::string path_;
};

#endif  // CONTROL_SERVER_H
