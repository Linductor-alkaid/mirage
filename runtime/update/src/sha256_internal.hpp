#pragma once

// M5-12 (DEC-032): internal SHA-256 (FIPS 180-4) for the updater module.
// Embedded so the manifest digests work on every toolchain including
// Windows/MSVC where no OpenSSL is available (the ed25519 face keeps its
// OpenSSL-only fail-closed gate). Not a public header; not for keyed use.

#include <cstddef>
#include <cstdint>
#include <string>

namespace mirage::runtime::update::sha256_internal {

std::string hex(const std::string &bytes);

} // namespace mirage::runtime::update::sha256_internal
