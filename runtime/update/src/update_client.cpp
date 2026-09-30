#include <mirage/runtime/update/update_client.hpp>

#include <mirage/runtime/update/update_crypto.hpp>
#include <mirage/runtime/update/update_fetcher.hpp>

#include <mirage/runtime/update/update_crypto.hpp>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <filesystem>

namespace fs = std::filesystem;

namespace mirage::runtime::update {
namespace {

constexpr const char *kManifestPath = "/update-manifest.json";
constexpr const char *kSignatureSuffix = ".sig";
constexpr std::uint64_t kManifestBudget = 256 * 1024;
constexpr std::uint64_t kFileBudget = 2ull * 1024 * 1024 * 1024;

/// Verifies `sig` over `message` under the trust anchor. Fails closed when
/// this build has no ed25519 verifier.
bool verify_signature(const UpdateTrustAnchor &anchor, const std::string &message,
                      const std::string &sig_raw) {
    // The channel serves the RAW 64-byte signature (the M5-11 producer
    // tooling emits raw pkeyutl output; verify-update-manifest.sh consumes
    // the same bytes). Base64 re-encoding is a distribution choice, not a
    // wire fact here.
    if (!ed25519_available() || anchor.public_key.size() != 32) {
        return false;
    }
    if (sig_raw.size() != 64) {
        return false;
    }
    return verify_ed25519(reinterpret_cast<const std::uint8_t *>(anchor.public_key.data()),
                          reinterpret_cast<const std::uint8_t *>(message.data()), message.size(),
                          reinterpret_cast<const std::uint8_t *>(sig_raw.data()));
}

} // namespace

UpdateClient::UpdateClient(UpdateTrustAnchor trust_anchor)
    : trust_anchor_(std::move(trust_anchor)) {}

void UpdateClient::set_current_version(const std::string &version) { current_version_ = version; }

void UpdateClient::set_progress(const std::function<void(std::uint64_t)> &progress) {
    progress_ = progress;
}

CheckOutcome UpdateClient::check(const std::string &base_url) {
    CheckOutcome outcome;
    auto fetch = http_get(base_url, kManifestPath, kManifestBudget, progress_);
    if (!fetch.ok) {
        outcome.diagnostic = fetch.diagnostic;
        return outcome;
    }
    auto signature = http_get(base_url, std::string(kManifestPath) + kSignatureSuffix,
                              kManifestBudget, progress_);
    if (!signature.ok) {
        outcome.diagnostic = signature.diagnostic;
        return outcome;
    }
    if (!verify_signature(trust_anchor_, fetch.body, signature.body)) {
        outcome.diagnostic = "update manifest signature invalid (fail closed)";
        return outcome;
    }
    DecodeError error;
    auto manifest = decode_update_manifest(fetch.body, error);
    if (!manifest) {
        outcome.diagnostic = error.code + ": " + error.message;
        return outcome;
    }
    outcome.ok = true;
    outcome.current_version = manifest->version;
    outcome.manifest_size = fetch.body.size();
    return outcome;
}

ApplyOutcome UpdateClient::apply(const std::string &base_url, const std::string &target_dir,
                                 const std::string &staging_root) {
    ApplyOutcome outcome;

    // 1. manifest + signature + verify.
    auto manifest_fetch = http_get(base_url, kManifestPath, kManifestBudget, progress_);
    if (!manifest_fetch.ok) {
        outcome.report.diagnostic = "manifest fetch failed: " + manifest_fetch.diagnostic;
        outcome.staging_dir = staging_root;
        return outcome;
    }
    auto signature_fetch = http_get(base_url, std::string(kManifestPath) + kSignatureSuffix,
                                    kManifestBudget, progress_);
    if (!signature_fetch.ok) {
        outcome.report.diagnostic = "signature fetch failed: " + signature_fetch.diagnostic;
        outcome.staging_dir = staging_root;
        return outcome;
    }
    if (!verify_signature(trust_anchor_, manifest_fetch.body, signature_fetch.body)) {
        outcome.report.diagnostic = "update manifest signature invalid (fail closed)";
        outcome.staging_dir = staging_root;
        return outcome;
    }
    DecodeError error;
    auto manifest = decode_update_manifest(manifest_fetch.body, error);
    if (!manifest) {
        outcome.report.diagnostic = error.code + ": " + error.message;
        outcome.staging_dir = staging_root;
        return outcome;
    }

    // 2. staging directory: <staging_root>/<version>-<pid>-<counter> — a
    //    fresh staging area per apply, never shared.
    static std::atomic<unsigned long long> staging_counter{0};
    const auto staging =
        fs::path(staging_root) / (manifest->version + "-" + std::to_string(++staging_counter));
    std::error_code fs_error;
    fs::create_directories(staging, fs_error);
    if (fs_error) {
        outcome.report.diagnostic = "cannot create staging dir: " + fs_error.message();
        outcome.staging_dir = staging_root;
        return outcome;
    }
    outcome.staging_dir = staging.string();

    // 3. download every listed file into staging (bounded, progress-fed).
    for (const auto &file : manifest->files) {
        auto file_fetch = http_get(base_url, "/" + file.name, file.size, progress_);
        if (!file_fetch.ok) {
            outcome.report.diagnostic =
                "fetch failed for " + file.name + ": " + file_fetch.diagnostic;
            outcome.staging_dir = staging.string();
            return outcome;
        }
        const std::uint64_t fetched = file_fetch.body.size();
        if (fetched != file.size) {
            outcome.report.diagnostic = "size mismatch for " + file.name;
            outcome.staging_dir = staging.string();
            return outcome;
        }
        // Digest re-check in memory before anything touches the disk.
        if (sha256_hex(file_fetch.body) != file.sha256) {
            outcome.report.diagnostic = "sha256 mismatch for " + file.name;
            outcome.staging_dir = staging.string();
            return outcome;
        }
        const auto staged_file = staging / file.name;
        std::FILE *handle = std::fopen(staged_file.string().c_str(), "wb");
        if (handle == nullptr) {
            outcome.report.diagnostic = "cannot persist staged file: " + file.name;
            outcome.staging_dir = staging.string();
            return outcome;
        }
        const auto written = std::fwrite(file_fetch.body.data(), 1, file_fetch.body.size(), handle);
        std::fclose(handle);
        if (written != file_fetch.body.size()) {
            outcome.report.diagnostic = "cannot persist staged file: " + file.name;
            outcome.staging_dir = staging.string();
            return outcome;
        }
        outcome.bytes_fetched += file.size;
    }

    // 4. apply (fail-closed digest pre-pass + atomic switch + rollback).
    outcome.report = apply_staged_update(*manifest, staging.string(), target_dir);
    return outcome;
}

} // namespace mirage::runtime::update
