#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
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

/// Model profile settings (M5-08 设置-模型类目, DEC-027 契约输入): mirrors
/// the model layer configuration minus the runtime-only bounds. Empty
/// endpoint keeps the model layer disabled.
struct ModelSettings {
    bool enabled = false;
    std::string dialect; ///< pinned dialect name; empty keeps the default
    std::string display_name;
    std::string endpoint_origin;
    std::string api_prefix;
    std::string model_selector;
    std::string credential_env;              ///< env var carrying the API key
    std::uint64_t context_window_tokens = 0; ///< 0 unknown; otherwise configured 2048..2000000
    bool supports_reasoning = false;
};

/// Runtime configuration settings (M5-08 Runtime Configuration 类目):
/// productization review bounds (DEC-012 / DEC-007 复核条目). Zero keeps the
/// built-in default.
struct RuntimeSettings {
    std::size_t event_queue_capacity = 0;
    std::size_t max_connections = 0;
};

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
    /// Per-capability permission rules (DEC-010 / M5-07): keys are the
    /// DEC-010 capability names ("filesystem.read" … "notification.post"),
    /// values the rule vocabulary ("allow" / "confirm" / "deny"); validated
    /// by decode_settings. Entries keep the built-in default for that
    /// capability — absent key, absent override. Since M5-07 the map spans
    /// the full DEC-010 vocabulary (policy.get / policy.set carry it whole).
    std::map<std::string, std::string> permission_rules;
    /// Confirmation outcome for Confirm rules (DEC-010): "allow" / "deny";
    /// nullopt keeps the built-in fail-closed deny.
    std::optional<std::string> confirmation;
    /// Model profile block (M5-08 设置-模型类目); nullopt keeps the model
    /// layer disabled.
    std::optional<ModelSettings> model;
    std::vector<ModelSettings> models; ///< DEC-037: named catalog, at most 12
    /// Runtime configuration block (M5-08 Runtime Configuration 类目);
    /// nullopt keeps the built-in bounds.
    std::optional<RuntimeSettings> runtime;
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
