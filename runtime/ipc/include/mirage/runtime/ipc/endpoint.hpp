#pragma once

#include <string>

namespace mirage::runtime::ipc {

/// Default Local IPC endpoint (DEC-007):
/// `$XDG_RUNTIME_DIR/mirage/mirage-service.sock`, falling back to
/// `/tmp/mirage-<uid>/mirage-service.sock` when XDG_RUNTIME_DIR is unset or
/// empty. The path is never empty; the caller creates the parent directory
/// with 0700 before binding (the directory is the transport's access
/// control).
std::string default_socket_path();

/// Directory component of a socket path; the service creates it (0700)
/// before binding and removes the socket file on ordered shutdown.
std::string socket_directory(const std::string& socket_path);

} // namespace mirage::runtime::ipc
