#ifndef WEB_DEVTOOLS_H
#define WEB_DEVTOOLS_H

#include <string>
#include <vector>

namespace web_devtools {

// Absolute DevTools frontend URL from a Chromium /json response; empty if none.
std::string parseFrontendUrl(const std::string& json, int port);

// argv for opening url: browser_command words (URL appended), else xdg-open.
std::vector<std::string> commandFor(const std::string& browser_command, const std::string& url);

// Open the DevTools frontend for a remote-debugging port. True if a launcher spawned.
bool open(int port, const std::string& browser_command = "");

}  // namespace web_devtools

#endif  // WEB_DEVTOOLS_H
