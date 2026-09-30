// M5-12 (DEC-032): `mirage update` verbs — the CLI entry over the updater
// core. `update check` reports the channel state; `update apply` fetches,
// verifies and atomically switches the local install. The tray / desktop
// shell adopt the same core later (the in-app surface).
#include "update_command.hpp"

#include <mirage/runtime/update/update_client.hpp>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>

namespace mirage::cli {
namespace {

constexpr std::string_view kUpdateUsage =
    "usage: mirage update check --url HOST[:PORT] --pubkey FILE\n"
    "       mirage update apply --url HOST[:PORT] --pubkey FILE\n"
    "                        [--target DIR] [--staging DIR]\n"
    "\n"
    "check  fetches the signed update manifest and reports the channel.\n"
    "apply  fetches, verifies (fail closed) and atomically switches the\n"
    "       local install; on any failure the previous state is restored.\n"
    "The --pubkey file is the raw 32-byte ed25519 release public key\n"
    "(shipped trust anchor; the signing private key never leaves the\n"
    "release machine keyring).\n";

/// Raw 32-byte read of the trust-anchor public key file (fail closed).
bool read_pubkey(const std::string &path, std::string &out) {
    std::FILE *handle = std::fopen(path.c_str(), "rb");
    if (handle == nullptr) {
        std::cerr << "mirage update: cannot open public key '" << path << "'\n";
        return false;
    }
    char buffer[32];
    const auto read = std::fread(buffer, 1, sizeof(buffer), handle);
    std::fclose(handle);
    if (read != sizeof(buffer)) {
        std::cerr << "mirage update: public key must be exactly 32 raw bytes\n";
        return false;
    }
    out.assign(buffer, sizeof(buffer));
    return true;
}

} // namespace

int command_update_check(int argc, char **argv) {
    std::string url;
    std::string pubkey;
    for (int i = 1; i < argc; ++i) {
        const std::string_view argument{argv[i]};
        if (argument == "--url" && i + 1 < argc) {
            url = argv[++i];
        } else if (argument == "--pubkey" && i + 1 < argc) {
            pubkey = argv[++i];
        } else {
            std::cerr << kUpdateUsage;
            return 2;
        }
    }
    if (url.empty() || pubkey.empty()) {
        std::cerr << kUpdateUsage;
        return 2;
    }
    std::string anchor;
    if (!read_pubkey(pubkey, anchor)) {
        return 1;
    }
    mirage::runtime::update::UpdateClient client(
        mirage::runtime::update::UpdateTrustAnchor{std::move(anchor)});

    const auto outcome = client.check(url);
    if (!outcome.ok) {
        std::cerr << "mirage update: channel check failed: " << outcome.diagnostic << '\n';
        return 1;
    }
    std::cout << "channel version: " << outcome.current_version << '\n';
    return 0;
}

int command_update_apply(int argc, char **argv) {
    std::string url;
    std::string pubkey;
    std::string target;
    std::string staging;
    for (int i = 1; i < argc; ++i) {
        const std::string_view argument{argv[i]};
        if (argument == "--url" && i + 1 < argc) {
            url = argv[++i];
        } else if (argument == "--pubkey" && i + 1 < argc) {
            pubkey = argv[++i];
        } else if (argument == "--target" && i + 1 < argc) {
            target = argv[++i];
        } else if (argument == "--staging" && i + 1 < argc) {
            staging = argv[++i];
        } else {
            std::cerr << kUpdateUsage;
            return 2;
        }
    }
    if (url.empty() || pubkey.empty() || target.empty()) {
        std::cerr << kUpdateUsage;
        return 2;
    }
    if (staging.empty()) {
        staging = (std::filesystem::path(target) / ".mirage-update-staging").string();
    }
    std::string anchor;
    if (!read_pubkey(pubkey, anchor)) {
        return 1;
    }
    mirage::runtime::update::UpdateClient client(
        mirage::runtime::update::UpdateTrustAnchor{std::move(anchor)});

    const auto outcome = client.apply(url, target, staging);
    if (outcome.report.status == mirage::runtime::update::ApplyStatus::Applied) {
        std::cout << "update applied (" << outcome.report.applied.size()
                  << " file(s)); bytes fetched: " << outcome.bytes_fetched << '\n';
        std::cout << "restart the product processes to run the new version\n";
        return 0;
    }
    if (outcome.report.status == mirage::runtime::update::ApplyStatus::RolledBack) {
        std::cerr << "mirage update: apply failed and was rolled back: "
                  << outcome.report.diagnostic << '\n';
        return 1;
    }
    std::cerr << "mirage update: apply failed: " << outcome.report.diagnostic << '\n';
    return 1;
}

} // namespace mirage::cli
