// M5-12 (DEC-032): ed25519 verification over libcrypto (OpenSSL EVP).
// This translation unit is compiled only where OpenSSL was found (the
// same optional-boundary discipline as the pinned openssl transport);
// without it the updater fails closed at the crypto face.

#include <mirage/runtime/update/update_crypto.hpp>

#include <openssl/evp.h>

#include <cstring>

namespace mirage::runtime::update {
namespace {

constexpr std::size_t kEd25519KeyBytes = 32;
constexpr std::size_t kEd25519SigBytes = 64;

} // namespace

bool ed25519_available() { return true; }

bool verify_ed25519(const std::uint8_t *pubkey32, const std::uint8_t *message,
                    std::size_t message_size, const std::uint8_t *sig64) {
    if (pubkey32 == nullptr || message == nullptr || sig64 == nullptr) {
        return false;
    }
    EVP_PKEY *key =
        EVP_PKEY_new_raw_public_key(EVP_PKEY_ED25519, nullptr, pubkey32, kEd25519KeyBytes);
    if (key == nullptr) {
        return false;
    }
    EVP_MD_CTX *ctx = EVP_MD_CTX_new();
    bool ok = false;
    do {
        if (ctx == nullptr) {
            break;
        }
        if (EVP_DigestVerifyInit(ctx, nullptr, nullptr, nullptr, key) != 1) {
            break;
        }
        if (EVP_DigestVerify(ctx, sig64, kEd25519SigBytes, message, message_size) == 1) {
            ok = true;
        }
    } while (false);
    EVP_MD_CTX_free(ctx);
    EVP_PKEY_free(key);
    return ok;
}

bool base64_decode(const std::string &text, std::string &out) {
    static constexpr char kAlphabet[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    int table[256];
    for (int i = 0; i < 256; ++i) {
        table[i] = -1;
    }
    for (int i = 0; i < 64; ++i) {
        table[static_cast<unsigned char>(kAlphabet[i])] = i;
    }

    out.clear();
    out.reserve(text.size() / 4 * 3);
    int buffer = 0;
    int bits = 0;
    bool seen_padding = false;
    for (const char character : text) {
        const auto raw = static_cast<unsigned char>(character);
        if (raw == 10 || raw == 13 || raw == 32 || raw == 9) { // LF / CR / SP / HTAB
            continue; // whitespace is ignored everywhere (PEM bodies wrap)
        }
        if (raw == '=') { // padding: nothing meaningful may follow
            seen_padding = true;
            continue;
        }
        if (seen_padding) {
            return false; // data after padding
        }
        const int value = table[raw];
        if (value < 0) {
            return false; // invalid character
        }
        buffer = (buffer << 6) | value;
        bits += 6;
        if (bits >= 8) {
            out.push_back(static_cast<char>((buffer >> (bits - 8)) & 0xFF));
            bits -= 8;
        }
    }
    return true;
}

bool base64_decode_pem_body(const std::string &pem, std::string &out) {
    // Strip armor lines, feed the body to base64_decode (whitespace and
    // padding handled there).
    std::string stripped;
    for (std::size_t pos = 0; pos < pem.size();) {
        const auto eol = pem.find('\n', pos);
        const std::string line =
            pem.substr(pos, eol == std::string::npos ? std::string::npos : eol - pos);
        if (line.rfind("-----", 0) != 0) {
            stripped += line;
        }
        if (eol == std::string::npos) {
            break;
        }
        pos = eol + 1;
    }
    return base64_decode(stripped, out);
}

} // namespace mirage::runtime::update
