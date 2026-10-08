#ifndef CONTROL_CLIENT_H
#define CONTROL_CLIENT_H

#include <string>

#include "app/control/control_protocol.h"

class ControlClient {
   public:
    // Returns true only when a live instance accepted the request.
    static bool tryHandoff(const std::string& key, const SwitchRequest& request, std::string* error = nullptr);
};

#endif  // CONTROL_CLIENT_H
