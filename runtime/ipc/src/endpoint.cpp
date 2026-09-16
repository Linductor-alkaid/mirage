#include <mirage/runtime/ipc/endpoint.hpp>

#include <unistd.h>

#include <cstdlib>
#include <string>

namespace mirage::runtime::ipc {

std::string default_socket_path() {
    const char* runtime_dir = std::getenv("XDG_RUNTIME_DIR");
    if (runtime_dir != nullptr && *runtime_dir != '\0') {
        return std::string(runtime_dir) + "/mirage/mirage-service.sock";
    }
    return "/tmp/mirage-" + std::to_string(static_cast<long>(::getuid())) +
           "/mirage-service.sock";
}

std::string socket_directory(const std::string& socket_path) {
    const auto position = socket_path.rfind('/');
    if (position == std::string::npos) {
        return ".";
    }
    if (position == 0) {
        return "/";
    }
    return socket_path.substr(0, position);
}

} // namespace mirage::runtime::ipc
