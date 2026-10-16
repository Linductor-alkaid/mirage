// M5-12 (DEC-032): the updater core verification — strict manifest decode,
// ed25519 fail-closed verification and the atomic apply engine with
// rollback. Homegrown harness (repo discipline; no external framework).

#include "../support/test.hpp"

#include <mirage/runtime/update/update_apply.hpp>
#include <mirage/runtime/update/update_client.hpp>
#include <mirage/runtime/update/update_crypto.hpp>
#include <mirage/runtime/update/update_manifest.hpp>

#include <openssl/evp.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

namespace fs = std::filesystem;

namespace {

std::string read_file(const fs::path &path) {
    std::ifstream in(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

void write_file(const fs::path &path, const std::string &bytes) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << bytes;
}

struct Ed25519Key {
    std::string public_key; ///< 32 raw bytes
    std::string seed;       ///< 32 raw bytes
};

Ed25519Key generate_ed25519() {
    Ed25519Key key;
    EVP_PKEY *pkey = nullptr;
    EVP_PKEY_CTX *ctx = EVP_PKEY_CTX_new_id(EVP_PKEY_ED25519, nullptr);
    EVP_PKEY_keygen_init(ctx);
    EVP_PKEY_keygen(ctx, &pkey);
    EVP_PKEY_CTX_free(ctx);
    std::size_t len = 32;
    key.public_key.resize(32);
    EVP_PKEY_get_raw_public_key(pkey, reinterpret_cast<unsigned char *>(key.public_key.data()),
                                &len);
    key.seed.resize(32);
    EVP_PKEY_get_raw_private_key(pkey, reinterpret_cast<unsigned char *>(key.seed.data()), &len);
    EVP_PKEY_free(pkey);
    return key;
}

std::string sign_ed25519(const std::string &seed, const std::string &message) {
    EVP_PKEY *pkey = EVP_PKEY_new_raw_private_key(
        EVP_PKEY_ED25519, nullptr, reinterpret_cast<const unsigned char *>(seed.data()), 32);
    EVP_MD_CTX *ctx = EVP_MD_CTX_new();
    std::string signature(64, '\0');
    std::size_t sig_len = signature.size();
    EVP_DigestSignInit(ctx, nullptr, nullptr, nullptr, pkey);
    EVP_DigestSign(ctx, reinterpret_cast<unsigned char *>(signature.data()), &sig_len,
                   reinterpret_cast<const unsigned char *>(message.data()), message.size());
    EVP_MD_CTX_free(ctx);
    EVP_PKEY_free(pkey);
    signature.resize(sig_len);
    return signature;
}

fs::path make_temp_dir(const char *name) {
    const auto base = fs::temp_directory_path() / name;
    std::error_code error;
    fs::remove_all(base, error);
    fs::create_directories(base, error);
    return base;
}

} // namespace

int main() {

    // --- manifest decode ---------------------------------------------------
    mirage::runtime::update::DecodeError error;
    const std::string canonical = R"({
  "schema": "mirage-update-manifest",
  "schema_version": 1,
  "version": "0.2.0",
  "timestamp": "2026-09-30T00:00:00Z",
  "files": [
    { "name": "mirage.exe", "size": 11, "sha256": "0000000000000000000000000000000000000000000000000000000000000000" }
  ]
})";
    const auto decoded = mirage::runtime::update::decode_update_manifest(canonical, error);
    MIRAGE_CHECK(decoded.has_value());
    if (decoded) {
        MIRAGE_CHECK(decoded->version == "0.2.0");
        MIRAGE_CHECK(decoded->schema_version == 1);
        MIRAGE_CHECK(decoded->files.size() == 1);
        if (decoded->files.size() == 1) {
            MIRAGE_CHECK(decoded->files[0].name == "mirage.exe");
            MIRAGE_CHECK(decoded->files[0].size == 11);
        }
    }

    // Fail-closed matrix.
    {
        const auto bad = mirage::runtime::update::decode_update_manifest("not json", error);
        MIRAGE_CHECK(!bad);
        MIRAGE_CHECK(error.code == "invalid_json");
    }
    {
        const auto bad = mirage::runtime::update::decode_update_manifest("[]", error);
        MIRAGE_CHECK(!bad);
        MIRAGE_CHECK(error.code == "not_an_object");
    }
    {
        const auto bad = mirage::runtime::update::decode_update_manifest(
            R"({"schema": "other-manifest", "schema_version": 1, "version": "v",
                "timestamp": "t", "files": [ { "name": "a", "size": 1,
                "sha256": "0000000000000000000000000000000000000000000000000000000000000000" } ] })",
            error);
        MIRAGE_CHECK(!bad);
        MIRAGE_CHECK(error.code == "unknown_schema");
    }
    {
        const auto bad = mirage::runtime::update::decode_update_manifest(
            R"({"schema": "mirage-update-manifest", "schema_version": 2, "version": "v",
                "timestamp": "t", "files": [ { "name": "a", "size": 1,
                "sha256": "0000000000000000000000000000000000000000000000000000000000000000" } ] })",
            error);
        MIRAGE_CHECK(!bad);
        MIRAGE_CHECK(error.code == "unsupported_schema_version");
    }
    {
        const auto bad = mirage::runtime::update::decode_update_manifest(
            R"({"schema": "mirage-update-manifest", "schema_version": 1,
                "timestamp": "t", "files": []})",
            error);
        MIRAGE_CHECK(!bad);
        MIRAGE_CHECK(error.code == "bad_version");
    }
    {
        const auto bad = mirage::runtime::update::decode_update_manifest(
            R"({"schema": "mirage-update-manifest", "schema_version": 1, "version": "v",
                "timestamp": "t", "files": []})",
            error);
        MIRAGE_CHECK(!bad);
        MIRAGE_CHECK(error.code == "bad_files");
    }

    // --- ed25519 verification ----------------------------------------------
    const auto key = generate_ed25519();
    const std::string message = "mirage-update-manifest bytes";
    const auto signature = sign_ed25519(key.seed, message);
    MIRAGE_CHECK(signature.size() == 64);
    MIRAGE_CHECK(mirage::runtime::update::ed25519_available());
    MIRAGE_CHECK(mirage::runtime::update::verify_ed25519(
        reinterpret_cast<const std::uint8_t *>(key.public_key.data()),
        reinterpret_cast<const std::uint8_t *>(message.data()), message.size(),
        reinterpret_cast<const std::uint8_t *>(signature.data())));
    const std::string tampered = message + "x";
    MIRAGE_CHECK(!mirage::runtime::update::verify_ed25519(
        reinterpret_cast<const std::uint8_t *>(key.public_key.data()),
        reinterpret_cast<const std::uint8_t *>(tampered.data()), tampered.size(),
        reinterpret_cast<const std::uint8_t *>(signature.data())));

    // --- apply engine -------------------------------------------------------
    const auto base = make_temp_dir("mirage-update-core-test");

    // Target: the "installed" old state.
    fs::create_directories(base / "target");
    write_file(base / "target" / "mirage.exe", "old-body");
    write_file(base / "target" / "notes.txt", "old-notes");

    mirage::runtime::update::UpdateManifest manifest;
    manifest.schema = "mirage-update-manifest";
    manifest.schema_version = 1;
    manifest.version = "0.2.0";
    const std::string body_a = "new-body!!";
    const std::string body_b = "new-notes-body";
    manifest.files.push_back(
        {"mirage.exe", body_a.size(), mirage::runtime::update::sha256_hex(body_a)});
    manifest.files.push_back(
        {"notes.txt", body_b.size(), mirage::runtime::update::sha256_hex(body_b)});

    // Staging with a corrupted second file: the fail-closed pre-pass must
    // refuse BEFORE touching the target.
    fs::create_directories(base / "staging");
    write_file(base / "staging" / "mirage.exe", "new-body!!");
    write_file(base / "staging" / "notes.txt", "CORRUPTED");

    const auto refused = mirage::runtime::update::apply_staged_update(
        manifest, (base / "staging").string(), (base / "target").string());
    MIRAGE_CHECK(refused.status == mirage::runtime::update::ApplyStatus::Failed);
    MIRAGE_CHECK(!refused.diagnostic.empty());
    MIRAGE_CHECK(read_file(base / "target" / "mirage.exe") == "old-body");
    MIRAGE_CHECK(read_file(base / "target" / "notes.txt") == "old-notes");

    // Fixed staging: the apply switches everything atomically.
    write_file(base / "staging" / "notes.txt", "new-notes-body");
    const auto applied = mirage::runtime::update::apply_staged_update(
        manifest, (base / "staging").string(), (base / "target").string());
    MIRAGE_CHECK(applied.status == mirage::runtime::update::ApplyStatus::Applied);
    MIRAGE_CHECK(read_file(base / "target" / "mirage.exe") == "new-body!!");
    MIRAGE_CHECK(read_file(base / "target" / "notes.txt") == "new-notes-body");

    // --- trust-anchor gate on the client face ------------------------------
    mirage::runtime::update::UpdateClient short_anchor_client(
        mirage::runtime::update::UpdateTrustAnchor{"short"});
    const auto refused_check = short_anchor_client.check("127.0.0.1:1");
    MIRAGE_CHECK(!refused_check.ok);
    const auto refused_apply = short_anchor_client.apply("127.0.0.1:1", (base / "target").string(),
                                                         (base / "staging").string());
    MIRAGE_CHECK(!refused_apply.report.diagnostic.empty());

    return mirage::testing::finish("update_core_test");
}
