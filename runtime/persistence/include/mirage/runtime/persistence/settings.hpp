#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace mirage::runtime::persistence {

/// Schema version of the settings document (DEC-011). A future incompatible
/// layout bumps the constant; decoders reject anything they do not know.
inline constexpr int kSettingsSchema = 1;

/// Decode-side bounds of the settings document (DEC-011 item 3 budgets).
inline constexpr std::size_t kMaxSettingsFileBytes = 64 * 1024;
inline constexpr std::size_t kMaxReadRoots = 64;
inline constexpr std::size_t kMaxPathBytes = 4096;

/// Mirage's local configuration for the runtime service (design doc section
/// 16, DEC-011): the M1-configurable surface of Application Settings,
/// Runtime Configuration and Desktop Permissions. Every member is optional
/// in the file sense: a nullopt / empty value keeps the built-in default,
/// so the document only carries overrides. Rule strings use the DEC-010
/// vocabulary ("allow" / "confirm" / "deny") and are validated by
/// decode_settings; mapping onto permission types is the embedding app's
/// job, keeping this module free of permission dependencies.
struct LocalSettings {
    int schema = kSettingsSchema;
    /// IPC endpoint override; empty keeps the DEC-007 default endpoint.
    std::string socket_path;
    /// Filesystem read roots for the reference Linux backend (DEC-009);
    /// empty keeps the built-in empty scope (every read denied).
    std::vector<std::string> read_roots;
    /// Per-capability permission rules (DEC-010); nullopt keeps the
    /// built-in default for that capability.
    std::optional<std::string> filesystem_read_rule;
    std::optional<std::string> filesystem_write_rule;
    std::optional<std::string> process_execute_rule;
    /// Confirmation outcome for Confirm rules (DEC-010): "allow" / "deny";
    /// nullopt keeps the built-in fail-closed deny.
    std::optional<std::string> confirmation;
};

/// Serializes the document (compact JSON, schema field included).
std::string encode_settings(const LocalSettings &settings);

struct SettingsDecode {
    bool ok = false;
    LocalSettings settings;
    std::string error; ///< stable reason, meaningful when !ok
};

/// Strict decode: unknown members, wrong types, out-of-vocabulary rule
/// strings, oversized paths and unknown schema versions are rejected with
/// a stable reason (DEC-011 item 2) — a typoed config file must fail
/// loudly instead of silently running on defaults.
SettingsDecode decode_settings(std::string_view body);

} // namespace mirage::runtime::persistence
