#pragma once

// M5-12 (DEC-032): ed25519 manifest-signature verification for the updater.
// Backed by libcrypto (OpenSSL EVP) where the toolchain provides it; where
// it does not, the compiled face fails closed — a build without crypto
// refuses every signature instead of silently trusting one (the transport
// then reports the stable "ed25519 unavailable" error and the update ends).

#include <cstdint>
#include <string>

namespace mirage::runtime::update {

/// True when this build carries a working ed25519 verifier.
bool ed25519_available();

/// Ed25519 verification over `message` with a 32-byte raw public key and a
/// 64-byte raw signature. Fail closed: any size/format problem is a false
/// return, never an exception.
bool verify_ed25519(const std::uint8_t *pubkey32, const std::uint8_t *message,
                    std::size_t message_size, const std::uint8_t *sig64);

/// Unpem: strips PEM armor lines and decodes the base64 body. Returns false
/// on malformed armor/base64.
bool base64_decode_pem_body(const std::string &pem, std::string &out);

/// Raw base64 decode (standard alphabet, padding required).
bool base64_decode(const std::string &text, std::string &out);

} // namespace mirage::runtime::update
