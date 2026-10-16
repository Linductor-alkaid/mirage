// The update fetch port over the pinned mira socket transport (M5-12,
// DEC-032; REUSE-01). One implementation serves every platform: the pinned
// adapter compiles against POSIX sockets and Winsock and owns the platform
// boundary, the blocking-I/O workers and the bounded waits this port used
// to hand-roll. DEC-032 posture kept: no redirects (max_redirects = 0,
// the manifest pins exact URLs), private/loopback channels allowed (LAN
// mirrors are legitimate; trust is the ed25519 manifest, not the transport),
// hard body budget fails closed.

#include <mirage/runtime/update/update_fetcher.hpp>

#include <mira/adapters/net/socket_transport.hpp>
#include <mira/model_transport.hpp>

#include <algorithm>
#include <string>
#include <string_view>

namespace mirage::runtime::update {

constexpr std::uint64_t kMaxBodyBytesHardCap = 2ull * 1024 * 1024 * 1024; // 2 GiB

struct FetchSession::Impl {
    mira::adapters::net::SocketHttpTransport transport;
    bool started = false;

    explicit Impl(kairo::Executor &executor)
        : transport(executor, std::make_shared<mira::NullSecretResolver>(),
                    /*tls=*/nullptr,
                    mira::adapters::net::SocketTransportConfig{.worker_name = "mirage-update-io"}) {
    }
    ~Impl() {
        if (started) {
            transport.shutdown(); // settles queued exchanges, joins the workers
        }
    }
};

FetchSession::FetchSession(kairo::Executor &executor) : impl_(std::make_unique<Impl>(executor)) {
    impl_->started = impl_->transport.start();
}

FetchSession::~FetchSession() = default;

FetchResult FetchSession::get(const std::string &host, const std::string &path,
                              std::uint64_t max_bytes,
                              const std::function<void(std::uint64_t)> &progress) {
    FetchResult result;
    if (!impl_ || !impl_->started) {
        result.diagnostic = "update fetch session failed to start on the executor";
        return result;
    }
    const std::uint64_t budget =
        max_bytes == 0 ? kMaxBodyBytesHardCap : std::min(max_bytes, kMaxBodyBytesHardCap);

    mira::HttpRequest request;
    request.method = "GET";
    request.url = "http://" + host + path;
    request.headers = {{"User-Agent", "mirage-updater"}, {"Accept", "*/*"}};

    mira::TransportLimits limits;
    limits.deadlines = mira::TransportDeadlines{}; // staged 5s/30s defaults bound every wait
    limits.max_response_bytes = budget;
    limits.max_redirects = 0;              // DEC-032: channel redirects fail closed
    limits.allow_private_endpoints = true; // LAN/localhost mirrors; trust is the signed manifest

    std::string body;
    std::uint64_t received = 0;
    mira::HttpChunkCallback on_chunk = [&](std::string_view chunk) {
        received += chunk.size();
        body.append(chunk.data(), chunk.size());
        if (progress) {
            progress(received);
        }
    };

    mira::TransportTrace trace;
    const auto exchange =
        impl_->transport.execute(request, limits, mira::make_control_context(), on_chunk, trace);
    if (!exchange) {
        result.diagnostic = exchange.error().safe_message.empty() ? "update fetch failed"
                                                                  : exchange.error().safe_message;
        return result;
    }
    result.http_status = exchange.value().status;
    if (result.http_status != 200) {
        result.diagnostic = "update channel returned HTTP " + std::to_string(result.http_status);
        return result;
    }
    result.ok = true;
    result.body = std::move(body);
    return result;
}

} // namespace mirage::runtime::update
