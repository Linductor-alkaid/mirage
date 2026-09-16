#pragma once

#include <filesystem>

namespace mirage::runtime::persistence {

/// Default directory holding Mirage's local configuration files (design doc
/// section 16, DEC-011): `$XDG_CONFIG_HOME/mirage`, falling back to
/// `$HOME/.config/mirage`. Never empty; a fully unresolvable home falls
/// back to the working directory so callers always get a usable path.
std::filesystem::path default_config_directory();

/// Default directory holding Mirage's runtime state files, among them the
/// M1 recovery state (DEC-011): `$XDG_STATE_HOME/mirage`, falling back to
/// `$HOME/.local/state/mirage`, then to the working directory. POSIX only
/// in M1; the Windows directory policy lands with the M4 backend.
std::filesystem::path default_state_directory();

} // namespace mirage::runtime::persistence
