// Fail-closed crypto face for builds without OpenSSL (M5-12, DEC-032):
// the updater refuses every signature loudly instead of silently trusting
// one. The Windows updater build wires the real verifier when the
// packaging step ships an OpenSSL Crypto runtime (tracked in the M5-12
// verification record).

#include <mirage/runtime/update/update_crypto.hpp>

namespace mirage::runtime::update {

bool ed25519_available() { return false; }

bool verify_ed25519(const std::uint8_t *, const std::uint8_t *, std::size_t, const std::uint8_t *) {
    return false;
}

bool base64_decode(const std::string &, std::string &) { return false; }

bool base64_decode_pem_body(const std::string &, std::string &) { return false; }

} // namespace mirage::runtime::update
