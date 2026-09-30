#pragma once

// M5-12 (DEC-032): the update fetch port — minimal HTTP/1.1 GET over a
// blocking socket (no TLS). The update channel's trust anchor is the
// ed25519-signed manifest plus the Authenticode-signed installer
// (DEC-006 决策 5 双层), not the transport; deployments may front the
// channel with HTTPS if they wish. No redirects are followed (fail closed
// on 3xx: the manifest pins exact URLs).

#include <cstdint>
#include <functional>
#include <string>

namespace mirage::runtime::update {

struct FetchResult {
    bool ok = false;
    int http_status = 0; ///< meaningful when ok (0 = transport failure)
    std::string body;
    std::string diagnostic; ///< meaningful when !ok
};

/// Minimal HTTP/1.1 GET. `host` is a bare host or "host:port" (default
/// port 80). Bounded body: a transfer beyond `max_bytes` fails closed.
/// `progress`, when set, is invoked with received-bytes-so-far.
FetchResult http_get(const std::string &host, const std::string &path, std::uint64_t max_bytes,
                     const std::function<void(std::uint64_t)> &progress = {});

} // namespace mirage::runtime::update
