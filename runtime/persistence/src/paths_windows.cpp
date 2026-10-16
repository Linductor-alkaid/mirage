#include <mirage/runtime/persistence/paths.hpp>

#include "../../internal/win32_environment.hpp"

#include <filesystem>
#include <string>

namespace mirage::runtime::persistence {
namespace {

using mirage::runtime::internal::environment_utf8;

/// DEC-011 on Windows: the known user directories replace the XDG pair.
/// %APPDATA% (roaming) carries configuration, %LOCALAPPDATA% (machine
/// local) carries state, each mirroring the POSIX base/'mirage' layout.
/// An unset or empty variable falls back to the working directory so
/// callers always get something usable, exactly like the POSIX resolution.
std::filesystem::path known_directory(const wchar_t *environment_variable,
                                      const std::filesystem::path &suffix) {
    std::filesystem::path base;
    const std::string value = environment_utf8(environment_variable);
    if (!value.empty()) {
        base = value;
    } else {
        base = std::filesystem::path(".") / suffix;
    }
    return base / "mirage";
}

} // namespace

std::filesystem::path default_config_directory() { return known_directory(L"APPDATA", "config"); }

std::filesystem::path default_state_directory() {
    return known_directory(L"LOCALAPPDATA", "state");
}

} // namespace mirage::runtime::persistence
