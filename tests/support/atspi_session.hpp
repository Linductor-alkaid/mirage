#pragma once

// AT-SPI2 test topology (M2-03, DEC-015): a private D-Bus session bus on
// which the test fixture owns the org.a11y.atspi.Registry name and exports
// the desktop tree itself, with libatspi pointed at the bus via
// AT_SPI_BUS_ADDRESS. The real at-spi2-registryd is deliberately NOT used:
// Ubuntu 24.04's registryd (2.52.0) segfaults inside libdbus while
// dispatching Socket.Embed (core-dump verified, upstream bug), and the
// backend under test only reads the desktop through atspi_get_desktop —
// which the fixture can serve exactly. Missing system pieces fail loudly
// (DOD-03, no skips).

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
#include <string_view>
#include <vector>

namespace mirage::testing {

/// Locates a system binary in the usual paths; "" when absent.
inline std::string find_system_binary(const std::string &name, const char *fallback) {
    if (const char *override_env = std::getenv("MIRAGE_DBUS_DAEMON");
        override_env != nullptr && override_env[0] != '\0') {
        return override_env;
    }
    if (fallback != nullptr && ::access(fallback, X_OK) == 0) {
        return fallback;
    }
    const char *path_env = std::getenv("PATH");
    if (path_env == nullptr) {
        return "";
    }
    std::string_view paths(path_env);
    for (std::size_t start = 0; start <= paths.size();) {
        const std::size_t end = paths.find(':', start);
        const std::string_view dir =
            paths.substr(start, end == std::string_view::npos ? paths.size() - start : end - start);
        if (!dir.empty()) {
            const std::filesystem::path candidate = std::filesystem::path(dir) / name;
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
    return "";
}

/// One private D-Bus session bus usable as the accessibility bus:
/// `bus_address()` feeds the fixture connection and the AT_SPI_BUS_ADDRESS
/// environment variable feeds libatspi in the backend under test.
class AtspiSession {
  public:
    AtspiSession() {
        dbus_daemon_ = find_system_binary("dbus-daemon", "/usr/bin/dbus-daemon");
        if (dbus_daemon_.empty()) {
            throw std::runtime_error(
                "dbus-daemon not found: install dbus or set MIRAGE_DBUS_DAEMON");
        }
        int address_pipe[2] = {-1, -1};
        if (::pipe(address_pipe) != 0) {
            throw std::runtime_error("pipe failed for dbus-daemon address");
        }
        dbus_pid_ = ::fork();
        if (dbus_pid_ < 0) {
            throw std::runtime_error("fork failed for dbus-daemon");
        }
        if (dbus_pid_ == 0) {
            ::dup2(address_pipe[1], STDOUT_FILENO);
            ::close(address_pipe[0]);
            ::close(address_pipe[1]);
            ::setpgid(0, 0);
            ::execl(dbus_daemon_.c_str(), dbus_daemon_.c_str(), "--session", "--nopidfile",
                    "--print-address=1", static_cast<char *>(nullptr));
            _exit(127);
        }
        ::close(address_pipe[1]);
        char buffer[512] = {};
        const ssize_t got = ::read(address_pipe[0], buffer, sizeof(buffer) - 1);
        ::close(address_pipe[0]);
        if (got <= 0) {
            teardown();
            throw std::runtime_error("dbus-daemon did not print a session bus address");
        }
        bus_address_ = std::string(buffer, static_cast<std::size_t>(got));
        while (!bus_address_.empty() &&
               (bus_address_.back() == '\n' || bus_address_.back() == '\r')) {
            bus_address_.pop_back();
        }
        // libatspi reads this before initializing; the private bus plays
        // both the session and the accessibility role for the test.
        ::setenv("AT_SPI_BUS_ADDRESS", bus_address_.c_str(), 1);
        // A dead AT_SPI_BUS_ADDRESS from the surrounding session must not
        // leak in, and a stale inherited one must not outlive the test.
        ::setenv("NO_AT_BRIDGE", "1", 1);
    }

    ~AtspiSession() { teardown(); }

    AtspiSession(const AtspiSession &) = delete;
    AtspiSession &operator=(const AtspiSession &) = delete;

    const std::string &bus_address() const { return bus_address_; }

  private:
    void teardown() {
        if (dbus_pid_ > 0) {
            ::kill(dbus_pid_, SIGTERM);
            int status = 0;
            ::waitpid(dbus_pid_, &status, 0);
            dbus_pid_ = -1;
        }
    }

    std::string dbus_daemon_;
    std::string bus_address_;
    pid_t dbus_pid_ = -1;
};

} // namespace mirage::testing
