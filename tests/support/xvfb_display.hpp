#pragma once

// Headless X server lifecycle for the M2-02 Linux backend tests (DEC-015
// test topology): spawn a private Xvfb on a fresh display number, hand the
// display name to everything under test, tear the server down on scope exit.
//
// Locator order: $MIRAGE_XVFB -> $PATH -> ~/.local/mirage-sysroot/usr/bin/
// Xvfb (user-prefix extraction of the distro xvfb package for environments
// without root). A missing binary is a LOUD failure, never a skip: the
// suite's exit conditions count skipped coverage as absent (DOD-03).

#include <fcntl.h>
#include <signal.h>
#include <unistd.h>

#include <sys/wait.h>

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>

namespace mirage::testing {

/// Returns the path of the first usable Xvfb binary, or "" when none exists.
inline std::string find_xvfb() {
    if (const char *explicit_path = std::getenv("MIRAGE_XVFB")) {
        if (explicit_path[0] != '\0') {
            return explicit_path;
        }
    }
    const char *path_env = std::getenv("PATH");
    if (path_env != nullptr) {
        std::string_view paths(path_env);
        for (std::size_t start = 0; start <= paths.size();) {
            const std::size_t end = paths.find(':', start);
            const std::string_view dir = paths.substr(
                start, end == std::string_view::npos ? paths.size() - start : end - start);
            if (!dir.empty()) {
                const std::filesystem::path candidate = std::filesystem::path(dir) / "Xvfb";
                std::error_code ec;
                if (std::filesystem::is_regular_file(candidate, ec) &&
                    ::access(candidate.c_str(), X_OK) == 0) {
                    return candidate.string();
                }
            }
            if (end == std::string_view::npos) {
                break;
            }
            start = end + 1;
        }
    }
    const char *home = std::getenv("HOME");
    if (home != nullptr) {
        const std::filesystem::path candidate =
            std::filesystem::path(home) / ".local/mirage-sysroot/usr/bin/Xvfb";
        std::error_code ec;
        if (std::filesystem::is_regular_file(candidate, ec) &&
            ::access(candidate.c_str(), X_OK) == 0) {
            return candidate.string();
        }
    }
    return {};
}

/// One private Xvfb instance. The constructor blocks until the server
/// reports its ready display number; failures throw (loud, per DOD-03).
class XvfbDisplay {
  public:
    explicit XvfbDisplay(std::string xvfb_binary, std::string geometry = "640x480x24")
        : binary_(std::move(xvfb_binary)) {
        int ready_fd[2] = {-1, -1};
        if (::pipe(ready_fd) != 0) {
            throw std::runtime_error(std::string("pipe failed: ") + std::strerror(errno));
        }
        const pid_t pid = ::fork();
        if (pid < 0) {
            ::close(ready_fd[0]);
            ::close(ready_fd[1]);
            throw std::runtime_error(std::string("fork failed: ") + std::strerror(errno));
        }
        if (pid == 0) {
            // Child: hand the write end to Xvfb on a high, stable fd
            // (-displayfd takes the fd number as its argument). A low fd
            // would collide with the pipe fds themselves.
            const int kReadyFd = 200;
            ::dup2(ready_fd[1], kReadyFd);
            ::close(ready_fd[0]);
            ::close(ready_fd[1]);
            ::setpgid(0, 0);
            const std::string fd_arg = std::to_string(kReadyFd);
            ::execl(binary_.c_str(), binary_.c_str(), "-displayfd", fd_arg.c_str(), "-screen", "0",
                    geometry.c_str(), "-nolisten", "tcp", static_cast<char *>(nullptr));
            _exit(127);
        }
        ::close(ready_fd[1]);
        char digit = '\0';
        std::string number;
        for (;;) {
            const ssize_t got = ::read(ready_fd[0], &digit, 1);
            if (got <= 0) {
                break;
            }
            if (digit == '\n' || digit == '\r') {
                break;
            }
            number.push_back(digit);
        }
        ::close(ready_fd[0]);
        if (number.empty()) {
            int status = 0;
            ::kill(pid, SIGTERM);
            ::waitpid(pid, &status, 0);
            throw std::runtime_error("Xvfb exited before reporting a display number");
        }
        pid_ = pid;
        display_name_ = ":" + number;
    }

    ~XvfbDisplay() {
        if (pid_ > 0) {
            ::kill(pid_, SIGTERM);
            int status = 0;
            ::waitpid(pid_, &status, 0);
        }
    }

    XvfbDisplay(const XvfbDisplay &) = delete;
    XvfbDisplay &operator=(const XvfbDisplay &) = delete;

    const std::string &display_name() const { return display_name_; }

  private:
    std::string binary_;
    std::string display_name_;
    pid_t pid_ = -1;
};

} // namespace mirage::testing
