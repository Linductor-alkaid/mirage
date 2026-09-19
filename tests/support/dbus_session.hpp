#pragma once

// Private D-Bus session bus test topology (M2-05; modeled on the M2-03
// AtspiSession): a fresh dbus-daemon --session per test process, its
// address printed over a pipe. The test fixture connects by address and
// owns whatever well-known name the backend under test resolves; the
// backend is pointed at the bus through DBUS_SESSION_BUS_ADDRESS. Missing
// system pieces fail loudly (DOD-03, no skips).

#include "atspi_session.hpp" // find_system_binary

#include <fcntl.h>
#include <signal.h>
#include <unistd.h>

#include <sys/wait.h>

#include <stdexcept>
#include <string>

namespace mirage::testing {

/// One private D-Bus session bus: `bus_address()` feeds both the fixture
/// connection and DBUS_SESSION_BUS_ADDRESS for the backend under test.
class DbusSession {
  public:
    DbusSession() {
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
    }

    ~DbusSession() { teardown(); }

    DbusSession(const DbusSession &) = delete;
    DbusSession &operator=(const DbusSession &) = delete;

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
