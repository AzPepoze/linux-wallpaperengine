#include "shared/core/web_devtools.h"

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <unistd.h>

namespace {
bool inPath(const char* name) {
    const char* path = getenv("PATH");
    if (!path) return false;
    const std::string search(path);
    size_t start = 0;
    while (start <= search.size()) {
        const size_t colon = search.find(':', start);
        std::string dir = search.substr(start, colon == std::string::npos ? std::string::npos : colon - start);
        if (dir.empty()) dir = ".";
        if (access((dir + "/" + name).c_str(), X_OK) == 0) return true;
        if (colon == std::string::npos) break;
        start = colon + 1;
    }
    return false;
}

// Double-fork so the browser is reparented to init (no zombie).
void spawnDetached(const std::vector<std::string>& argv) {
    if (argv.empty()) return;
    const pid_t pid = fork();
    if (pid != 0) {
        if (pid > 0) waitpid(pid, nullptr, 0);
        return;
    }
    setsid();
    const pid_t grandchild = fork();
    if (grandchild != 0) _exit(0);
    const int devnull = open("/dev/null", O_RDWR);
    if (devnull >= 0) {
        dup2(devnull, STDIN_FILENO);
        dup2(devnull, STDOUT_FILENO);
        dup2(devnull, STDERR_FILENO);
        if (devnull > 2) close(devnull);
    }
    std::vector<char*> raw;
    raw.reserve(argv.size() + 1);
    for (const std::string& word : argv) raw.push_back(const_cast<char*>(word.c_str()));
    raw.push_back(nullptr);
    execvp(raw[0], raw.data());
    _exit(127);
}

std::string httpGetLocalhost(int port) {
    const int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return {};
    timeval timeout{1, 0};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));

    sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<uint16_t>(port));
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
    if (connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        close(fd);
        return {};
    }

    char request[128];
    const int n = snprintf(request, sizeof(request), "GET /json HTTP/1.0\r\nHost: 127.0.0.1\r\n\r\n");
    if (n <= 0 || write(fd, request, static_cast<size_t>(n)) < 0) {
        close(fd);
        return {};
    }

    std::string response;
    char buffer[4096];
    ssize_t got;
    while ((got = read(fd, buffer, sizeof(buffer))) > 0) {
        response.append(buffer, static_cast<size_t>(got));
        if (response.size() > 256 * 1024) break;
    }
    close(fd);
    return response;
}
}  // namespace

namespace web_devtools {

std::string parseFrontendUrl(const std::string& json, int port) {
    const std::string key = "\"devtoolsFrontendUrl\":\"";
    const size_t start = json.find(key);
    if (start == std::string::npos) return {};
    size_t pos = start + key.size();
    std::string url;
    while (pos < json.size() && json[pos] != '"') {
        if (json[pos] == '\\' && pos + 1 < json.size()) {
            url.push_back(json[pos + 1]);  // unescape \/
            pos += 2;
        } else {
            url.push_back(json[pos]);
            ++pos;
        }
    }
    if (url.empty()) return {};
    if (url.rfind("http", 0) != 0) url = "http://localhost:" + std::to_string(port) + url;
    return url;
}

std::vector<std::string> commandFor(const std::string& browser_command, const std::string& url) {
    std::vector<std::string> argv;
    size_t start = 0;
    while (start < browser_command.size()) {
        const size_t space = browser_command.find(' ', start);
        std::string word =
            browser_command.substr(start, space == std::string::npos ? std::string::npos : space - start);
        if (!word.empty()) argv.push_back(std::move(word));
        if (space == std::string::npos) break;
        start = space + 1;
    }
    if (argv.empty()) argv.push_back("xdg-open");
    argv.push_back(url);
    return argv;
}

bool open(int port, const std::string& browser_command) {
    std::string url = parseFrontendUrl(httpGetLocalhost(port), port);
    if (url.empty()) url = "http://localhost:" + std::to_string(port);
    const std::vector<std::string> argv = commandFor(browser_command, url);
    if (!inPath(argv.front().c_str())) return false;
    spawnDetached(argv);
    return true;
}

}  // namespace web_devtools
