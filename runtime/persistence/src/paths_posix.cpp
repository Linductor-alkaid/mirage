#include <mirage/runtime/persistence/paths.hpp>

#include <pwd.h>
#include <sys/types.h>
#include <unistd.h>

#include <cstdlib>
#include <filesystem>
#include <optional>
#include <string>

namespace mirage::runtime::persistence {
namespace {

/// XDG resolution per DEC-011: the environment variable wins when set and
/// non-empty, otherwise the home-relative default. Home comes from $HOME,
/// then the passwd database; a fully unresolvable home leaves the path at
/// the working directory so callers always get something usable.
std::filesystem::path xdg_directory(const char *environment_variable, const char *home_suffix) {
    std::filesystem::path base;
    if (const char *value = std::getenv(environment_variable); value != nullptr && *value != '\0') {
        base = value;
    } else {
        std::optional<std::filesystem::path> home;
        if (const char *value_home = std::getenv("HOME");
            value_home != nullptr && *value_home != '\0') {
            home = value_home;
        } else if (const long uid = static_cast<long>(::getuid()); uid >= 0) {
            if (const passwd *entry = ::getpwuid(static_cast<uid_t>(uid));
                entry != nullptr && entry->pw_dir != nullptr && *entry->pw_dir != '\0') {
                home = entry->pw_dir;
            }
        }
        base = home ? *home / home_suffix : std::filesystem::path(".") / home_suffix;
    }
    return base / "mirage";
}

} // namespace

std::filesystem::path default_config_directory() {
    return xdg_directory("XDG_CONFIG_HOME", ".config");
}

std::filesystem::path default_state_directory() {
    return xdg_directory("XDG_STATE_HOME", ".local/state");
}

} // namespace mirage::runtime::persistence
