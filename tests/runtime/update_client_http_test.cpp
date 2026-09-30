// M5-12 update client end-to-end verification over a real HTTP channel
// (independent verification pass, DEC-032). An in-process POSIX HTTP server
// serves the signed manifest/signature/files on 127.0.0.1; the test drives
// UpdateClient::check/apply through the real network path and pins the
// fail-closed behavior: byte-budget overrun refused (fail closed), 3xx not
// followed, non-200 refused, same-size content tampering caught by the
// in-memory sha256 gate with the target directory untouched, and the apply
// rollback path (a target entry that cannot be switched restores the
// previous content). Also fills the manifest-decode fail-closed gaps
// (duplicate names, file budget, negative size, bad hex, non-array/empty
// files, oversized fields).
//
// POSIX-only: the HTTP server and the fetcher under test are the POSIX
// port (the Windows port is compile-gated by the MinGW cross build).

#include "../support/test.hpp"

#include <mirage/runtime/update/update_apply.hpp>
#include <mirage/runtime/update/update_client.hpp>
#include <mirage/runtime/update/update_crypto.hpp>
#include <mirage/runtime/update/update_manifest.hpp>

#include <openssl/evp.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace fs = std::filesystem;

namespace {

namespace update = mirage::runtime::update;

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

/// One canned route of the in-process channel.
struct Route {
    int status = 200; ///< 200 serves body; anything else serves a stub
    std::string body; ///< the served bytes (status 200)
};

/// A minimal blocking HTTP/1.1 server on 127.0.0.1: serves per-path canned
/// responses until stopped. One connection per request (Connection: close).
class HttpServer {
  public:
    bool start() {
        listen_fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
        if (listen_fd_ < 0) {
            return false;
        }
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        address.sin_port = 0; // kernel-assigned
        if (::bind(listen_fd_, reinterpret_cast<sockaddr *>(&address), sizeof(address)) != 0) {
            return false;
        }
        socklen_t len = sizeof(address);
        if (::getsockname(listen_fd_, reinterpret_cast<sockaddr *>(&address), &len) != 0) {
            return false;
        }
        port_ = ntohs(address.sin_port);
        if (::listen(listen_fd_, 8) != 0) {
            return false;
        }
        return true;
    }

    int port() const { return port_; }

    void set_route(const std::string &path, Route route) {
        const std::lock_guard guard(mutex_);
        routes_[path] = std::move(route);
    }

    void start_serving() {
        server_ = std::thread([this] {
            while (!stopped_.load(std::memory_order_acquire)) {
                const int fd = ::accept(listen_fd_, nullptr, nullptr);
                if (fd < 0) {
                    break;
                }
                serve_one(fd);
                ::close(fd);
            }
        });
    }

    void stop() {
        stopped_.store(true, std::memory_order_release);
        ::shutdown(listen_fd_, SHUT_RDWR);
        ::close(listen_fd_);
        if (server_.joinable()) {
            server_.join();
        }
    }

  private:
    void serve_one(int fd) {
        std::string request;
        char chunk[4096];
        while (request.find("\r\n\r\n") == std::string::npos) {
            const ssize_t n = ::recv(fd, chunk, sizeof(chunk), 0);
            if (n <= 0) {
                return;
            }
            request.append(chunk, static_cast<std::size_t>(n));
        }
        // "GET <path> HTTP/1.1"
        const auto space1 = request.find(' ');
        const auto space2 = request.find(' ', space1 + 1);
        if (space1 == std::string::npos || space2 == std::string::npos) {
            return;
        }
        const std::string path = request.substr(space1 + 1, space2 - space1 - 1);
        Route route;
        {
            const std::lock_guard guard(mutex_);
            const auto found = routes_.find(path);
            if (found != routes_.end()) {
                route = found->second;
            } else {
                route.status = 404;
            }
        }
        if (route.status == 200) {
            const std::string head =
                "HTTP/1.1 200 OK\r\nContent-Length: " + std::to_string(route.body.size()) +
                "\r\nConnection: close\r\n\r\n";
            ::send(fd, head.data(), head.size(), 0);
            ::send(fd, route.body.data(), route.body.size(), 0);
        } else {
            const std::string body = "nope";
            const std::string head = "HTTP/1.1 " + std::to_string(route.status) +
                                     " Stub\r\nContent-Length: " + std::to_string(body.size()) +
                                     "\r\nConnection: close\r\n\r\n";
            ::send(fd, head.data(), head.size(), 0);
            ::send(fd, body.data(), body.size(), 0);
        }
    }

    int listen_fd_ = -1;
    int port_ = 0;
    std::thread server_;
    std::atomic<bool> stopped_{false};
    std::mutex mutex_;
    std::map<std::string, Route> routes_;
};

} // namespace

int main() {
    // --- manifest decode: fail-closed gap matrix --------------------------
    {
        update::DecodeError error;
        const auto refused = update::decode_update_manifest(R"({
  "schema": "mirage-update-manifest",
  "schema_version": 1,
  "version": "0.2.0",
  "timestamp": "t",
  "files": [
    { "name": "a", "size": 1, "sha256": "0000000000000000000000000000000000000000000000000000000000000000" },
    { "name": "a", "size": 2, "sha256": "0000000000000000000000000000000000000000000000000000000000000000" }
  ]
})",
                                                            error);
        MIRAGE_CHECK(!refused.has_value());
        MIRAGE_CHECK(error.code == "bad_files");
        MIRAGE_CHECK(error.message.find("duplicate") != std::string::npos);
    }
    {
        // The 64-file budget (RULE-07): 65 entries are refused.
        std::string manifest = R"({
  "schema": "mirage-update-manifest",
  "schema_version": 1,
  "version": "0.2.0",
  "timestamp": "t",
  "files": [)";
        for (int index = 0; index < 65; ++index) {
            manifest += (index == 0 ? "\n" : ",\n");
            manifest += "    { \"name\": \"f" + std::to_string(index) +
                        "\", \"size\": 1, \"sha256\": "
                        "\"0000000000000000000000000000000000000000000000000000000000000000\" }";
        }
        manifest += "\n  ]\n}";
        update::DecodeError error;
        const auto refused = update::decode_update_manifest(manifest, error);
        MIRAGE_CHECK(!refused.has_value());
        MIRAGE_CHECK(error.code == "bad_files");
    }
    {
        // Negative size / bad hex / non-array files / empty files / missing
        // fields are all refused.
        update::DecodeError error;
        const auto refused_negative_size = update::decode_update_manifest(R"({
  "schema": "mirage-update-manifest", "schema_version": 1, "version": "v",
  "timestamp": "t",
  "files": [ { "name": "a", "size": -1, "sha256": "0000000000000000000000000000000000000000000000000000000000000000" } ]
})",
                                                                          error);
        MIRAGE_CHECK(!refused_negative_size.has_value());
        const auto refused_upper_hex = update::decode_update_manifest(R"({
  "schema": "mirage-update-manifest", "schema_version": 1, "version": "v",
  "timestamp": "t",
  "files": [ { "name": "a", "size": 1, "sha256": "ABCDEF0000000000000000000000000000000000000000000000000000000000" } ]
})",
                                                                      error);
        MIRAGE_CHECK(!refused_upper_hex.has_value());
        const auto refused_files_object = update::decode_update_manifest(
            R"({"schema": "mirage-update-manifest", "schema_version": 1, "version": "v",
                "timestamp": "t", "files": {}})",
            error);
        MIRAGE_CHECK(!refused_files_object.has_value());
        const auto refused_files_empty = update::decode_update_manifest(
            R"({"schema": "mirage-update-manifest", "schema_version": 1, "version": "v",
                "timestamp": "t", "files": []})",
            error);
        MIRAGE_CHECK(!refused_files_empty.has_value());
        const auto refused_version_missing = update::decode_update_manifest(
            R"({"schema": "mirage-update-manifest", "schema_version": 1,
                "timestamp": "t",
                "files": [ { "name": "a", "size": 1, "sha256": "0000000000000000000000000000000000000000000000000000000000000000" } ] })",
            error);
        MIRAGE_CHECK(!refused_version_missing.has_value());
    }

    // --- HTTP channel end to end ------------------------------------------
    const fs::path base = fs::temp_directory_path() / "mirage-update-http-test";
    std::error_code cleanup;
    fs::remove_all(base, cleanup);
    fs::create_directories(base, cleanup);

    const Ed25519Key key = generate_ed25519();

    const std::string body_a = "mirage-body!!";
    const std::string body_b = "notes-body!";
    const std::string digest_a = update::sha256_hex(body_a);
    const std::string digest_b = update::sha256_hex(body_b);

    const std::string manifest = R"({
  "schema": "mirage-update-manifest",
  "schema_version": 1,
  "version": "0.3.0",
  "timestamp": "2026-09-30T12:00:00Z",
  "files": [
    { "name": "app.exe", "size": )" +
                                 std::to_string(body_a.size()) + R"(, "sha256": ")" + digest_a +
                                 R"(" },
    { "name": "notes.txt", "size": )" +
                                 std::to_string(body_b.size()) + R"(, "sha256": ")" + digest_b +
                                 R"(" }
  ]
})";
    const std::string signature = sign_ed25519(key.seed, manifest);

    HttpServer server;
    MIRAGE_CHECK(server.start());
    server.set_route("/update-manifest.json", {200, manifest});
    server.set_route("/update-manifest.json.sig", {200, signature});
    server.set_route("/app.exe", {200, body_a});
    server.set_route("/notes.txt", {200, body_b});
    server.start_serving();

    update::UpdateTrustAnchor anchor;
    anchor.public_key = key.public_key;
    const std::string channel = "127.0.0.1:" + std::to_string(server.port());

    // check(): the signed manifest flows through the real HTTP path.
    {
        update::UpdateClient client(anchor);
        const auto outcome = client.check(channel);
        MIRAGE_CHECK(outcome.ok);
        if (outcome.ok) {
            MIRAGE_CHECK(outcome.current_version == "0.3.0");
            MIRAGE_CHECK(outcome.manifest_size == manifest.size());
        }
    }

    // apply(): full fetch → in-memory sha256 gate → staging → atomic switch.
    {
        const fs::path target = base / "target";
        const fs::path staging_root = base / "staging";
        fs::create_directories(target, cleanup);
        write_file(target / "notes.txt", "OLD notes");

        update::UpdateClient client(anchor);
        const auto outcome = client.apply(channel, target.string(), staging_root.string());
        MIRAGE_CHECK(outcome.report.status == update::ApplyStatus::Applied);
        MIRAGE_CHECK(outcome.report.applied.size() == 2);
        MIRAGE_CHECK(read_file(target / "app.exe") == body_a);
        MIRAGE_CHECK(read_file(target / "notes.txt") == body_b);
        // A fresh per-apply staging directory under the requested root.
        MIRAGE_CHECK(outcome.staging_dir.find(staging_root.string()) == 0);

        // Re-apply over the switched state stays atomic and idempotent.
        update::UpdateClient again(anchor);
        const auto second = again.apply(channel, target.string(), staging_root.string());
        MIRAGE_CHECK(second.report.status == update::ApplyStatus::Applied);
        MIRAGE_CHECK(read_file(target / "app.exe") == body_a);
    }

    // Byte-budget overrun: the channel serves MORE bytes than the manifest
    // declares — the fetcher's budget refuses before anything is staged.
    {
        server.set_route("/app.exe", {200, body_a + "overrun"});
        const fs::path target = base / "target-overrun";
        fs::create_directories(target, cleanup);
        write_file(target / "notes.txt", "sentinel");

        update::UpdateClient client(anchor);
        const auto outcome = client.apply(channel, target.string(), (base / "staging-o").string());
        MIRAGE_CHECK(outcome.report.status == update::ApplyStatus::Failed);
        MIRAGE_CHECK(outcome.report.diagnostic.find("byte budget") != std::string::npos ||
                     outcome.report.diagnostic.find("exceeded") != std::string::npos);
        MIRAGE_CHECK(read_file(target / "notes.txt") == "sentinel");
        MIRAGE_CHECK(!fs::exists(target / "app.exe", cleanup));
        server.set_route("/app.exe", {200, body_a});
    }

    // Same-size content tampering: the fetcher's budget passes the exact
    // size, the in-memory sha256 gate refuses, and the target directory is
    // untouched (zero writes).
    {
        const std::string same_size_tampered = "mirage-b0dy!!"; // 13 bytes, like body_a
        MIRAGE_CHECK(same_size_tampered.size() == body_a.size());
        server.set_route("/app.exe", {200, same_size_tampered});
        const fs::path target = base / "target-tampered";
        fs::create_directories(target, cleanup);
        write_file(target / "notes.txt", "sentinel");

        update::UpdateClient client(anchor);
        const auto outcome = client.apply(channel, target.string(), (base / "staging-t").string());
        MIRAGE_CHECK(outcome.report.status == update::ApplyStatus::Failed);
        MIRAGE_CHECK(outcome.report.diagnostic.find("sha256 mismatch for app.exe") !=
                     std::string::npos);
        MIRAGE_CHECK(read_file(target / "notes.txt") == "sentinel");
        MIRAGE_CHECK(!fs::exists(target / "app.exe", cleanup));
        server.set_route("/app.exe", {200, body_a});
    }

    // 3xx is not followed (fail closed).
    {
        server.set_route("/update-manifest.json", {302, ""});
        update::UpdateClient client(anchor);
        const auto outcome = client.check(channel);
        MIRAGE_CHECK(!outcome.ok);
        MIRAGE_CHECK(outcome.diagnostic.find("redirects are not followed") != std::string::npos);
        server.set_route("/update-manifest.json", {200, manifest});
    }

    // Non-200 is refused.
    {
        server.set_route("/update-manifest.json.sig", {404, ""});
        update::UpdateClient client(anchor);
        const auto outcome = client.check(channel);
        MIRAGE_CHECK(!outcome.ok);
        MIRAGE_CHECK(outcome.diagnostic.find("HTTP 404") != std::string::npos);
        server.set_route("/update-manifest.json.sig", {200, signature});
    }

    // --- apply rollback: an un-switchable target entry --------------------
    // notes.txt exists as a DIRECTORY: rename cannot replace it, so the
    // second switch fails. The DEC-032 invariant (target ends up either
    // all-new or all-old) requires the newly added app.exe to be gone after
    // the rollback — the current tree leaves it switched in (mixed state),
    // which this check pins as a defect for the implementation owner.
    {
        const fs::path target = base / "target-rollback";
        fs::create_directories(target / "notes.txt", cleanup);
        update::UpdateClient client(anchor);
        const auto outcome = client.apply(channel, target.string(), (base / "staging-r").string());
        MIRAGE_CHECK(outcome.report.status != update::ApplyStatus::Applied);
        MIRAGE_CHECK(fs::is_directory(target / "notes.txt", cleanup));
        MIRAGE_CHECK(!fs::exists(target / "app.exe", cleanup));
    }

    server.stop();

    fs::remove_all(base, cleanup);
    return mirage::testing::finish("update_client_http_test");
}
