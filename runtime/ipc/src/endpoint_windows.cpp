#include <mirage/runtime/ipc/endpoint.hpp>

#include "../../internal/win32_environment.hpp"

#include <optional>
#include <string>

namespace mirage::runtime::ipc {
namespace {

using mirage::runtime::internal::environment_utf8;

} // namespace

std::string default_socket_path() {
    // DEC-007 on Windows: the named pipe namespace. The per-user name keeps
    // fast-user-switching sessions from colliding; %USERNAME% is the uid
    // analog here — a convenience disambiguator, not a security boundary
    // (pipe access control is the caller's DACL, the same division as the
    // POSIX 0700 directory).
    std::string user = environment_utf8(L"USERNAME");
    if (user.empty()) {
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
