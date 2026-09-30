#pragma once

// M5-12 (DEC-032): the update client — the orchestrating face over
// fetch → verify → apply. Uses the injected ports only; no threads, no
// scheduling (the embedding drives the steps from its own context).

#include <cstdint>
#include <functional>
#include <optional>
#include <string>

#include <mirage/runtime/update/update_apply.hpp>
#include <mirage/runtime/update/update_manifest.hpp>

namespace mirage::runtime::update {

/// The channel's trust anchor: the release ed25519 PUBLIC key (32 raw
/// bytes), shipped with the product. This is not a secret — the signing
/// PRIVATE key lives in the release machine's keyring (M5-11 key
/// isolation); the updater holds no credentials at all (shell-binary-
/// locking §3 "凭据经系统 keyring" applies to private credential material,
/// which the client never touches).
struct UpdateTrustAnchor {
    std::string public_key; ///< 32 raw bytes
};

struct CheckOutcome {
    bool ok = false;
    std::string diagnostic;          ///< meaningful when !ok
    std::string current_version;     ///< the manifest's version
    std::uint64_t manifest_size = 0; ///< manifest bytes fetched
};

struct ApplyOutcome {
    ApplyReport report;
    std::string staging_dir; ///< where the fetched files landed
    std::uint64_t bytes_fetched = 0;
};

/// Drives one update episode against `base_url`:
///   check:  GET  <base>/update-manifest.json (+ .sig) → ed25519 verify
///   apply:  + GET every listed file into a staging dir → digest re-check
///           → atomic apply into target_dir (rollback on failure)
/// `base_url` is "host[:port]" (no scheme; the channel is plain HTTP — the
/// trust anchor is the manifest signature, DEC-006 决策 5 双层中的
/// Authenticode 层由安装器自身承载).
class UpdateClient {
  public:
    explicit UpdateClient(UpdateTrustAnchor trust_anchor);

    /// Fetch + verify the manifest only.
    CheckOutcome check(const std::string &base_url);

    /// Fetch + verify + download all files + apply into `target_dir`.
    /// `staging_root` receives the staging directory (created if needed).
    ApplyOutcome apply(const std::string &base_url, const std::string &target_dir,
                       const std::string &staging_root);

    /// Current product version (the embedding injects it; "" = unknown —
    /// version comparison then reports the manifest's version without an
    /// up-to-date verdict).
    void set_current_version(const std::string &version);

    /// Progress callback (bytes fetched so far, per transfer). Optional.
    void set_progress(const std::function<void(std::uint64_t)> &progress);

  private:
    UpdateTrustAnchor trust_anchor_;
    std::string current_version_;
    std::function<void(std::uint64_t)> progress_;
};

} // namespace mirage::runtime::update
