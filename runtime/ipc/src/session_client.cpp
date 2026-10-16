#include <mirage/runtime/ipc/session_client.hpp>

#include <mirage/runtime/ipc/framing.hpp>

#include <kairo/comm/channel.hpp>

#include <algorithm>
#include <deque>
#include <utility>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>
#else
#include <poll.h>
#endif

namespace mirage::runtime::ipc {
namespace {

// The stream is zero-wait non-blocking on both transports, so the run loop
// paces itself in readiness slices (the client.cpp discipline). 10 ms keeps
// queued-request and stop latency invisible to an interactive UI at a
// polling cost of one readiness check per slice.
constexpr std::chrono::milliseconds kRunSlice{10};

#ifdef _WIN32
bool wait_readable(const IpcStream &, std::chrono::milliseconds budget) {
    if (budget <= std::chrono::milliseconds::zero()) {
        return false;
    }
    ::Sleep(static_cast<DWORD>(std::min(budget, kRunSlice).count()));
    return true;
}
#else
bool wait_readable(const IpcStream &stream, std::chrono::milliseconds budget) {
    if (budget <= std::chrono::milliseconds::zero()) {
        return false;
    }
    pollfd descriptor{};
    descriptor.fd = static_cast<int>(stream.handle());
    descriptor.events = POLLIN;
    return ::poll(&descriptor, 1, static_cast<int>(budget.count())) == 1;
}
#endif

} // namespace

// REUSE-09: the kairo comm channel behind the forward-declared header
// holder; the public header stays pinned-free (DEC-096).
struct SessionClient::Outbox {
    kairo::comm::MpscChannel<Outbound> channel;
};

SessionClient::SessionClient(std::string address)
    : address_(std::move(address)),
      outbox_(new Outbox{kairo::comm::MpscChannel<Outbound>(
          kairo::comm::ChannelOptions{.capacity = 16,
                                      .drop_policy = kairo::comm::DropPolicy::RejectNewest,
                                      .enable_stats = true,
                                      .name = "mirage-ipc-session-outbox"})}) {}

SessionClient::~SessionClient() {
    {
        std::lock_guard<std::mutex> guard(mutex_);
        stop_requested_ = true;
        dead_ = true;
        // Promises must never break: a call that outlives the session
        // resolves with "unavailable" (the surface of a dead connection).
        auto pending = std::move(pending_);
        pending_.clear();
        for (auto &[id, entry] : pending) {
            Response failed;
            failed.ok = false;
            failed.error = {"unavailable", "session destroyed"};
            entry.promise.set_value(std::move(failed));
        }
    }
    outbox_->channel.close();
    stream_.close();
}

bool SessionClient::connect(std::chrono::milliseconds deadline, std::string &diagnostic) {
    IpcStream stream = connect_stream(address_, deadline, diagnostic);
    if (!stream.valid()) {
        return false;
    }
    std::lock_guard<std::mutex> guard(mutex_);
    stream_ = std::move(stream);
    connected_ = true;
    health_ = Health::Healthy; // DEC-094: start healthy
    return true;
}

bool SessionClient::connected() const {
    std::lock_guard<std::mutex> guard(mutex_);
    return connected_ && !dead_;
}

SessionClient::Health SessionClient::health() const {
    std::lock_guard<std::mutex> guard(mutex_);
    return health_;
}

std::string SessionClient::last_timeout_reason() const {
    std::lock_guard<std::mutex> guard(mutex_);
    return last_timeout_reason_;
}

void SessionClient::set_event_sink(EventSink sink) {
    std::lock_guard<std::mutex> guard(mutex_);
    sink_ = std::move(sink);
    sink_set_ = true;
}

std::future<Response> SessionClient::call(const Request &body, std::chrono::milliseconds timeout) {
    std::promise<Response> promise;
    std::future<Response> future = promise.get_future();
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    std::uint64_t id = 0;
    {
        std::lock_guard<std::mutex> guard(mutex_);
        if (!connected_ || dead_) {
            Response refused;
            refused.ok = false;
            refused.error = {"unavailable",
                             "no live session at '" + address_ + "' (not connected or closed)"};
            promise.set_value(std::move(refused));
            return future;
        }
        id = next_id_++;
        const auto inserted = pending_.emplace(id, Pending{std::move(promise), deadline});
        // Submitted under the mutex so a concurrent fail-close can never
        // interleave between the pending entry and its frame (REUSE-09).
        if (!outbox_->channel.try_send(Outbound{id, make_frame(encode_request(id, body))})) {
            auto node = pending_.extract(inserted.first);
            Response refused;
            refused.ok = false;
            refused.error = {"unavailable",
                             "session is closing; the outbound queue refused the call"};
            node.mapped().promise.set_value(std::move(refused));
            return future;
        }
    }
    if (future.wait_until(deadline) == std::future_status::ready) {
        return future;
    }
    // The deadline passed while run() had not yet resolved the entry: take
    // over the expiry here — whoever wins the race fulfills exactly once.
    {
        std::lock_guard<std::mutex> guard(mutex_);
        const auto entry = pending_.find(id);
        if (entry == pending_.end()) {
            return future; // run() fulfilled it in the race window
        }
        Pending expired = std::move(entry->second);
        pending_.erase(entry);
        Response timed_out;
        timed_out.ok = false;
        // DEC-094: Request timeout != connection failure. Mark sync as stale
        // but do not close the transport — events may still be arriving.
        // The frozen single-outstanding discipline still holds (one unanswered
        // request forbids the next), but the timeout removes this request from
        // the "unanswered" set, so the next call can proceed if transport lives.
        last_timeout_reason_ = "request " + std::to_string(id) + " timed out after " +
                               std::to_string(timeout.count()) + " ms";
        health_ = Health::SyncStale;
        // REUSE-09/DEC-094: the call no longer exists as an expectation, so
        // its queued frame is tombstoned — flushed as skipped, and its late
        // response is dropped instead of killing the session as an
        // unexpected id.
        abandoned_.insert(id);
        timed_out.error = {"unavailable", last_timeout_reason_};
        expired.promise.set_value(std::move(timed_out));
        // Do NOT set dead_ = true here (DEC-094 Phase 1)
    }
    return future;
}

void SessionClient::stop() {
    std::lock_guard<std::mutex> guard(mutex_);
    stop_requested_ = true;
}

void SessionClient::close_fail_closed(const std::string &reason) {
    std::lock_guard<std::mutex> guard(mutex_);
    dead_ = true;
    dead_reason_ = reason;
}

SessionClient::RunExit SessionClient::run(std::string &diagnostic) {
    IpcStream stream;
    {
        std::lock_guard<std::mutex> guard(mutex_);
        stream = std::move(stream_);
        if (!stream.valid()) {
            diagnostic = "run() without a connected transport";
            return RunExit::ConnectionLost;
        }
    }

    std::string read_buffer;
    std::string write_remainder; // partial frame bytes still owed to the wire
    std::size_t write_offset = 0;
    bool in_flight = false; // the DEC-012 single-outstanding discipline

    // Sends as much of the remainder + queued frames as the transport takes;
    // the queue only advances past a frame once its response arrived. Returns
    // false on a transport failure (diagnostic filled, session over).
    const auto flush_writes = [&](std::deque<Outbound> &outbox) -> bool {
        for (;;) {
            if (write_offset >= write_remainder.size()) {
                if (in_flight || outbox.empty()) {
                    return true;
                }
                Outbound next = std::move(outbox.front());
                outbox.pop_front();
                {
                    std::lock_guard<std::mutex> guard(mutex_);
                    if (abandoned_.erase(next.id) > 0) {
                        // DEC-094: the caller's deadline fired before this
                        // frame reached the wire — the request no longer
                        // exists on either side, so drop it.
                        continue;
                    }
                }
                write_remainder = std::move(next.frame);
                write_offset = 0;
                in_flight = true;
            }
            const IoResult result = stream.write_some(write_remainder.data() + write_offset,
                                                      write_remainder.size() - write_offset);
            switch (result.status) {
            case IoStatus::Ok:
                write_offset += result.bytes;
                break;
            case IoStatus::WouldBlock:
                return true;
            case IoStatus::Closed:
                diagnostic = "service closed the connection";
                return false;
            case IoStatus::Error:
                diagnostic =
                    "transport write failed (os error " + std::to_string(result.os_error) + ")";
                return false;
            }
        }
    };

    // Resolves every queued and pending call with `code` and `diagnostic`.
    const auto fail_pending = [&](const std::string &code) {
        std::lock_guard<std::mutex> guard(mutex_);
        auto pending = std::move(pending_);
        pending_.clear();
        Outbound dropped;
        while (outbox_->channel.try_receive(dropped)) {
        }
        for (auto &[id, entry] : pending) {
            Response failed;
            failed.ok = false;
            failed.error = {code, diagnostic};
            entry.promise.set_value(std::move(failed));
        }
    };

    // Every exit marks the session dead: once the loop is gone, calls must
    // refuse instead of queueing into an unserviced outbox.
    const auto finish_run = [&]() {
        std::lock_guard<std::mutex> guard(mutex_);
        dead_ = true;
        health_ = Health::Disconnected; // DEC-094
        if (dead_reason_.empty()) {
            dead_reason_ = diagnostic;
        }
        outbox_->channel.close(); // further submissions refuse explicitly
    };

    for (;;) {
        std::deque<Outbound> outbox;
        bool stop_requested = false;
        bool alive = false;
        std::string dead_reason;
        {
            std::lock_guard<std::mutex> guard(mutex_);
            stop_requested = stop_requested_;
            dead_reason = dead_reason_;
            alive = !dead_ && !stop_requested_;
        }
        // REUSE-09: drain the comm outbox outside the mutex (lock-free
        // receive); abandoned frames are filtered at flush time.
        if (alive) {
            Outbound item;
            while (outbox_->channel.try_receive(item)) {
                outbox.push_back(std::move(item));
            }
        }
        if (stop_requested) {
            diagnostic = "stopped by the owner";
            fail_pending("unavailable");
            finish_run();
            return RunExit::Stopped;
        }
        if (!dead_reason.empty()) {
            diagnostic = dead_reason;
            fail_pending("unavailable");
            finish_run();
            return RunExit::ConnectionLost;
        }

        if (!flush_writes(outbox)) {
            fail_pending("unavailable");
            finish_run();
            return RunExit::ConnectionLost;
        }

        char chunk[4096];
        const IoResult result = stream.read_some(chunk, sizeof(chunk));
        if (result.status == IoStatus::Ok && result.bytes > 0) {
            read_buffer.append(chunk, result.bytes);
            for (;;) {
                const FrameExtraction extraction = try_extract_frame(read_buffer);
                if (extraction.status == FrameExtract::NeedMoreData) {
                    break;
                }
                if (extraction.status == FrameExtract::ProtocolError) {
                    diagnostic = "service violated the framing protocol: " + extraction.reason;
                    fail_pending("internal");
                    finish_run();
                    return RunExit::ConnectionLost;
                }
                // One payload is either the response of the single
                // outstanding request or an event; both together are the
                // closed protocol-v1 surface, anything else kills the
                // session (fail closed).
                const ResponseDecode decoded = decode_response(extraction.message);
                if (decoded.ok) {
                    in_flight = false;
                    bool known = false;
                    {
                        std::lock_guard<std::mutex> guard(mutex_);
                        const auto entry = pending_.find(decoded.response.id);
                        if (entry != pending_.end()) {
                            entry->second.promise.set_value(decoded.response);
                            pending_.erase(entry);
                            known = true;
                            // DEC-094: successful response clears SyncStale
                            if (health_ == Health::SyncStale) {
                                health_ = Health::Healthy;
                            }
                        }
                    }
                    if (!known) {
                        {
                            std::lock_guard<std::mutex> guard(mutex_);
                            // DEC-094: a late response to an abandoned (timed
                            // out) request is dropped, not fatal — its caller
                            // already resolved with the timeout.
                            if (abandoned_.erase(decoded.response.id) > 0) {
                                continue;
                            }
                        }
                        diagnostic = "service sent a response with unexpected id " +
                                     std::to_string(decoded.response.id);
                        close_fail_closed(diagnostic);
                        fail_pending("internal");
                        finish_run();
                        return RunExit::ConnectionLost;
                    }
                    continue;
                }
                const EventDecode event = decode_event(extraction.message);
                if (event.ok) {
                    EventSink sink;
                    {
                        std::lock_guard<std::mutex> guard(mutex_);
                        sink = sink_;
                    }
                    // The sink runs outside the lock (its contract forbids
                    // reentry, but the mutex is not the sink's to hold).
                    if (sink) {
                        sink(event.event);
                    }
                    continue;
                }
                diagnostic =
                    "service sent an undecodable frame: " + decoded.error + " / " + event.error;
                fail_pending("internal");
                finish_run();
                return RunExit::ConnectionLost;
            }
        } else if (result.status == IoStatus::Closed) {
            diagnostic = "service closed the connection";
            fail_pending("unavailable");
            finish_run();
            return RunExit::ConnectionLost;
        } else if (result.status == IoStatus::Error) {
            diagnostic = "transport read failed (os error " + std::to_string(result.os_error) + ")";
            fail_pending("unavailable");
            finish_run();
            return RunExit::ConnectionLost;
        }

        // Deadline sweep: an expired request closes the session fail-closed
        // (the discipline set in call()); run() exits on the next pass.
        const auto now = std::chrono::steady_clock::now();
        {
            std::lock_guard<std::mutex> guard(mutex_);
            for (auto it = pending_.begin(); it != pending_.end();) {
                if (it->second.deadline <= now) {
                    const std::uint64_t id = it->first;
                    Response timed_out;
                    timed_out.ok = false;
                    timed_out.error = {"unavailable",
                                       "timed out waiting for the response (session closed "
                                       "fail-closed)"};
                    it->second.promise.set_value(std::move(timed_out));
                    it = pending_.erase(it);
                    dead_ = true;
                    dead_reason_ = "request " + std::to_string(id) + " timed out";
                } else {
                    ++it;
                }
            }
        }

        wait_readable(stream, kRunSlice);
    }
}

} // namespace mirage::runtime::ipc
