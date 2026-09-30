#pragma once

// M5-12 (DEC-032): the update manifest face of the Windows in-app updater.
// Strict decode of the signed update manifest produced by the release-side
// tooling (packaging/update/make-update-manifest.sh; schema
// "mirage-update-manifest" v1) — every field fail-closed: a manifest that
// misses a field, carries an unknown schema/version or holds a malformed
// entry is rejected, never partially accepted.

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace mirage::runtime::update {

/// One file the manifest ships: `name` is the artifact file name, `size`
/// its byte size and `sha256` the lowercase hex digest over the full bytes.
struct UpdateFile {
    std::string name;
    std::uint64_t size = 0;
    std::string sha256;
};

/// Decoded update manifest (DEC-006 决策 5: 版本 / 文件清单 / SHA-256; the
/// ed25519 signature is verified OUTSIDE this struct — the decode face
/// trusts nothing and only shapes bytes).
struct UpdateManifest {
    std::string schema;              ///< must be "mirage-update-manifest"
    std::int64_t schema_version = 0; ///< must be 1
    std::string version;             ///< release version string of the update
    std::string timestamp;           ///< RFC 3339 UTC stamp from the release tooling
    std::vector<UpdateFile> files;

    /// Upper bounds (RULE-07): a manifest or entry beyond these is refused.
    static constexpr std::size_t kMaxManifestBytes = 256 * 1024;
    static constexpr std::size_t kMaxFiles = 64;
    static constexpr std::size_t kMaxNameBytes = 256;
    static constexpr std::size_t kMaxVersionBytes = 64;
    static constexpr std::size_t kMaxTimestampBytes = 64;

    /// True when `value` is safe to join into a filesystem path: no path
    /// separators, no '..' and no '.' as the whole value (the name and the
    /// version both feed path joins in the apply/staging flow — defense in
    /// depth against a compromised signing key, M5-12 round-2 review).
    static bool is_path_safe(const std::string &value);
};

struct DecodeError {
    std::string code;    ///< stable snake_case code for logs/tests
    std::string message; ///< safe detail
};

/// Strict decode; returns the error face on ANY deviation from the schema.
std::optional<UpdateManifest> decode_update_manifest(const std::string &json_text,
                                                     DecodeError &error);

/// Lowercase hex sha256 over `bytes` (libcrypto).
std::string sha256_hex(const std::string &bytes);

} // namespace mirage::runtime::update
