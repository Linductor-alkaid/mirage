#pragma once

// M5-12 (DEC-032): the update fetch port. The HTTP face is the pinned mira
// socket transport (REUSE-01) executed on the calling process's Executor
// blocking workers; the trust anchor remains the ed25519-signed manifest plus
// the Authenticode-signed installer (DEC-006 决策 5 双层), not the transport.
// No redirects are followed (fail closed on 3xx: the manifest pins exact
// URLs). Plain HTTP per DEC-032; the pinned OpenSSL TLS channel is the
// upgrade path when a deployment needs it.

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace kairo {
class Executor;
}

namespace mirage::runtime::update {

struct FetchResult {
    bool ok = false;
    int http_status = 0; ///< meaningful when ok (0 = transport failure)
    std::string body;
    std::string diagnostic; ///< meaningful when !ok
};

/// One HTTP GET exchange over the pinned transport. `host` is a bare host or
/// "host:port" (default port 80). Bounded body: a transfer beyond `max_bytes`
/// fails closed. `progress`, when set, is invoked with received-bytes-so-far.
class FetchSession final {
  public:
    // `executor` is the calling process's unique Executor owner (AGENTS.md
    // rule 7 / DEC-030 EXEC-01); the transport runs its blocking I/O on that
    // executor's blocking workers and joins them on destruction.
    explicit FetchSession(kairo::Executor &executor);
    ~FetchSession();
    FetchSession(const FetchSession &) = delete;
    FetchSession &operator=(const FetchSession &) = delete;

    FetchResult get(const std::string &host, const std::string &path, std::uint64_t max_bytes,
                    const std::function<void(std::uint64_t)> &progress = {});

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace mirage::runtime::update
