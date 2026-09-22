#include <mirage/runtime/ipc/endpoint.hpp>

#include <cstdlib>

namespace mirage::runtime::ipc {

std::string default_socket_path() {
    // DEC-007 on Windows: the named pipe namespace. The per-user name keeps
    // fast-user-switching sessions from colliding; %USERNAME% is the uid
    // analog here — a convenience disambiguator, not a security boundary
    // (pipe access control is the caller's DACL, the same division as the
    // POSIX 0700 directory).
    const char *user = std::getenv("USERNAME");
    if (user == nullptr || *user == '\0') {
        user = "default";
    }
    return std::string("\\\\.\\pipe\\mirage-") + user + "-service";
}

std::string socket_directory(const std::string &socket_path) {
    // Pipes have no directory: the POSIX "create the socket directory
    // first" step has no Windows analog, and the bind path carries its own
    // namespace. Kept for interface parity; never called on this platform's
    // bind path.
    (void)socket_path;
    return {};
}

} // namespace mirage::runtime::ipc
