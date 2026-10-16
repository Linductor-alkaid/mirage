// M10-06 (DEC-095): disk-backed retained command output. Bounded by the
// frozen budget table — 4 MiB per stream, 32 MiB per session, 128 MiB
// global, refs expire one hour after the command's terminal state — with
// admission before spawn and lazy expiry sweeps. Refs are runtime state:
// contents do not survive process restarts, and every lookup failure
// (unknown, expired, swept, foreign session) reports the same
// output_expired code so nothing about other sessions leaks.

#include <mirage/runtime/basic_output_store.hpp>

#include <mira/core_contracts.hpp>
#include <mira/model_contracts.hpp>

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <map>
#include <mutex>
#include <system_error>
#include <utility>
#include <vector>

namespace mirage::runtime {
namespace {

using mirage::desktop::ProviderError;
using mirage::desktop::TextWindowLimits;
using mirage::desktop::TextWindowOutcome;
using mirage::integration::BasicOutputStreamFacts;

ProviderError provider_error(std::string code, std::string message) {
    return {std::move(code), std::move(message)};
}

/// Session keys become directory names: accept exactly the pinned id
/// alphabet (hex/dash uuid strings) and nothing else.
bool session_key_valid(const std::string &session) {
    if (session.empty() || session.size() > 64) {
        return false;
    }
    return std::all_of(session.begin(), session.end(), [](char c) {
        return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F') ||
               c == '-';
    });
}

bool ref_key_valid(const std::string &ref) { return session_key_valid(ref); }

/// Pages one retained stream with the file-window contract (1-based lines,
/// CRLF-normalized view, line_too_long, bounded scan). Mirrors the
/// platform provider's streaming semantics minus version facts.
TextWindowOutcome page_stream(const std::filesystem::path &path, std::uint64_t skip_lines,
                              const TextWindowLimits &limits) {
    TextWindowOutcome outcome;
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        outcome.error = provider_error("output_expired", "retained output is no longer available");
        return outcome;
    }
    const auto deadline = std::chrono::steady_clock::now() + limits.max_scan_time;
    std::uint64_t scanned = 0;
    std::uint64_t line_index = 0;
    std::string pending;
    std::vector<char> buffer(64u << 10);
    const auto scan_exceeded = [&] {
        outcome.error =
            provider_error("scan_budget_exceeded", "scan exceeded its budget; eof position unknown");
        return outcome;
    };
    while (line_index < skip_lines) {
        if (scanned > limits.max_scan_bytes || std::chrono::steady_clock::now() > deadline) {
            return scan_exceeded();
        }
        stream.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const std::size_t received = static_cast<std::size_t>(stream.gcount());
        if (received == 0) {
            outcome.error = provider_error(
                "invalid_argument", "offset beyond end of retained output (scanned " +
                                        std::to_string(line_index) + " complete lines)");
            return outcome;
        }
        scanned += received;
        const std::string_view chunk(buffer.data(), received);
        std::size_t consumed = 0;
        while (line_index < skip_lines) {
            const std::size_t at = chunk.find('\n', consumed);
            if (at == std::string_view::npos) {
                consumed = chunk.size();
                break;
            }
            ++line_index;
            consumed = at + 1;
        }
        pending.assign(chunk.substr(consumed));
    }
    std::uint64_t rendered = 0;
    bool window_has_crlf = false;
    bool hit_eof = false;
    bool stopped = false;
    std::uint64_t collected = 0;
    for (;;) {
        for (;;) {
            const std::size_t at = pending.find('\n');
            const bool terminated = at != std::string::npos;
            if (!terminated && (!hit_eof || pending.empty())) {
                break;
            }
            const std::size_t length = terminated ? at : pending.size();
            if (collected >= limits.max_lines) {
                stopped = true;
                break;
            }
            if (length > limits.max_bytes) {
                outcome.error = provider_error(
                    "line_too_long", "line " + std::to_string(line_index + 1) + " has " +
                                         std::to_string(length) +
                                         " bytes, beyond the window budget of " +
                                         std::to_string(limits.max_bytes) + " bytes");
                return outcome;
            }
            if (rendered + length > limits.max_bytes) {
                stopped = true;
                break;
            }
            std::string_view view(pending.data(), length);
            if (terminated && !view.empty() && view.back() == '\r') {
                view.remove_suffix(1);
                window_has_crlf = true;
            }
            outcome.lines.emplace_back(view);
            rendered += length;
            ++collected;
            ++line_index;
            pending.erase(0, length + (terminated ? 1u : 0u));
            if (!terminated) {
                break;
            }
        }
        if (stopped || hit_eof) {
            break;
        }
        if (scanned > limits.max_scan_bytes || std::chrono::steady_clock::now() > deadline) {
            return scan_exceeded();
        }
        stream.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const std::size_t received = static_cast<std::size_t>(stream.gcount());
        if (received == 0) {
            hit_eof = true;
            continue;
        }
        scanned += received;
        pending.append(buffer.data(), received);
    }
    outcome.ok = true;
    outcome.first_line = skip_lines + 1;
    outcome.eof = hit_eof && !stopped;
    outcome.next_offset = skip_lines + outcome.lines.size();
    if (outcome.eof) {
        outcome.total_lines = outcome.next_offset;
    }
    // Same stale-offset rule as the platform provider: a window at eof of a
    // newline-terminated stream is a caller bug, not an empty window.
    if (outcome.eof && outcome.lines.empty() && skip_lines > 0) {
        outcome = TextWindowOutcome{};
        outcome.error = provider_error(
            "invalid_argument", "offset beyond end of retained output (scanned " +
                                    std::to_string(line_index) + " complete lines)");
        return outcome;
    }
    outcome.has_crlf = window_has_crlf;
    outcome.version.size = scanned; // informational; retained output has no file version
    return outcome;
}

} // namespace

struct BasicOutputStore::Impl {
    struct Entry {
        std::string session;
        std::filesystem::path payload;
        bool standard_output = true;
        std::uint64_t bytes = 0;
        bool truncated = false;    ///< per-stream cap reached
        bool failed = false;       ///< quota/disk failure mid-flight
        bool settled = false;
        std::chrono::steady_clock::time_point expiry{};
    };

    struct Streams final : mirage::integration::BasicOutputStreams {
        Impl *impl = nullptr;
        std::string session;
        std::string refs[2];
        std::ofstream files[2];
        std::uint64_t bytes[2] = {0, 0};
        bool truncated[2] = {false, false};
        bool failed[2] = {false, false};
        bool settled = false;

        bool retain(bool standard_output, std::string_view chunk) override {
            const std::lock_guard lock(impl->mutex);
            const int side = standard_output ? 0 : 1;
            if (settled || failed[side] || truncated[side]) {
                return false;
            }
            if (bytes[side] >= impl->config.max_stream_bytes ||
                impl->session_bytes[session] >= impl->config.max_session_bytes ||
                impl->global_bytes >= impl->config.max_global_bytes) {
                // Per-call cap: the drain continues, the outcome reports the
                // truncation; a budget cap reached mid-flight is a fact, not
                // a command failure.
                truncated[side] = true;
                return false;
            }
            files[side].write(chunk.data(), static_cast<std::streamsize>(chunk.size()));
            if (!files[side]) {
                failed[side] = true; // disk full or I/O failure: stop retaining this stream
                return false;
            }
            bytes[side] += chunk.size();
            impl->session_bytes[session] += chunk.size();
            impl->global_bytes += chunk.size();
            // Keep the entry's byte facts live (BUG-20261016-004): an
            // un-settled handle destroyed later, or its entry swept, must
            // release exactly what was consumed. A handle that outlived its
            // retention window (pathological: exec is bounded far below it)
            // stops retaining instead of counting bytes nothing will return.
            const auto entry = impl->entries.find(refs[side]);
            if (entry == impl->entries.end()) {
                failed[side] = true;
                return false;
            }
            entry->second.bytes = bytes[side];
            entry->second.truncated = truncated[side];
            entry->second.failed = failed[side];
            return true;
        }

        BasicOutputStreamFacts settle() override {
            const std::lock_guard lock(impl->mutex);
            BasicOutputStreamFacts facts;
            if (settled) {
                facts.stdout_ref = refs[0];
                facts.stderr_ref = refs[1];
                facts.stdout_bytes = bytes[0];
                facts.stderr_bytes = bytes[1];
                facts.stdout_truncated = truncated[0];
                facts.stderr_truncated = truncated[1];
                facts.retention_failed = failed[0] || failed[1];
                return facts;
            }
            settled = true;
            for (int side = 0; side < 2; ++side) {
                files[side].flush();
                files[side].close();
            }
            impl->open_count[session] = std::max<std::size_t>(0, impl->open_count[session] - 1);
            const auto expiry = std::chrono::steady_clock::now() + impl->config.retention_after_terminal;
            for (int side = 0; side < 2; ++side) {
                const auto found = impl->entries.find(refs[side]);
                if (found != impl->entries.end()) {
                    found->second.settled = true;
                    found->second.expiry = expiry;
                    found->second.bytes = bytes[side];
                    found->second.truncated = truncated[side];
                    found->second.failed = failed[side];
                }
                (side == 0 ? facts.stdout_ref : facts.stderr_ref) = refs[side];
                (side == 0 ? facts.stdout_bytes : facts.stderr_bytes) = bytes[side];
                (side == 0 ? facts.stdout_truncated : facts.stderr_truncated) = truncated[side];
            }
            facts.retention_failed = failed[0] || failed[1];
            return facts;
        }

        ~Streams() override {
            const std::lock_guard lock(impl->mutex);
            if (settled) {
                return;
            }
            // An un-settled handle (spawn failed, handler threw) releases
            // its admission immediately and its entries expire now.
            impl->open_count[session] = std::max<std::size_t>(0, impl->open_count[session] - 1);
            for (int side = 0; side < 2; ++side) {
                files[side].close();
                const auto found = impl->entries.find(refs[side]);
                if (found != impl->entries.end()) {
                    impl->release_locked(found->second);
                    impl->entries.erase(found);
                }
            }
        }
    };

    void release_locked(const Entry &entry) {
        session_bytes[entry.session] =
            entry.bytes > session_bytes[entry.session] ? 0 : session_bytes[entry.session] - entry.bytes;
        global_bytes = entry.bytes > global_bytes ? 0 : global_bytes - entry.bytes;
        std::error_code ec;
        std::filesystem::remove(entry.payload, ec);
    }

    void sweep_locked(const std::chrono::steady_clock::time_point &now) {
        std::vector<std::string> dead;
        for (const auto &[ref, entry] : entries) {
            if (now >= entry.expiry) {
                dead.push_back(ref);
            }
        }
        for (const auto &ref : dead) {
            const auto found = entries.find(ref);
            release_locked(found->second);
            entries.erase(found);
        }
    }

    BasicOutputStoreConfig config;
    std::mutex mutex;
    std::map<std::string, Entry> entries; ///< ref -> entry
    std::map<std::string, std::uint64_t> session_bytes;
    std::map<std::string, std::size_t> open_count;
    std::uint64_t global_bytes = 0;
};

BasicOutputStore::BasicOutputStore(BasicOutputStoreConfig config) : impl_(std::make_unique<Impl>()) {
    impl_->config = std::move(config);
    // Refs are runtime state: nothing under the store directory survives a
    // restart readable. Best-effort cleanup; failures keep the bounded
    // sweep in charge.
    std::error_code ec;
    if (!impl_->config.directory.empty()) {
        std::filesystem::remove_all(impl_->config.directory, ec);
    }
}

BasicOutputStore::~BasicOutputStore() = default;

std::shared_ptr<mirage::integration::BasicOutputStreams>
BasicOutputStore::open(const std::string &session, std::string *refusal_code) {
    const auto refuse = [&](const char *code) {
        if (refusal_code != nullptr) {
            *refusal_code = code;
        }
        return std::shared_ptr<mirage::integration::BasicOutputStreams>{};
    };
    const std::lock_guard lock(impl_->mutex);
    const auto now = std::chrono::steady_clock::now();
    impl_->sweep_locked(now);
    if (!session_key_valid(session)) {
        return refuse("invalid_argument");
    }
    const auto session_bytes = impl_->session_bytes.find(session);
    if ((session_bytes != impl_->session_bytes.end() &&
         session_bytes->second >= impl_->config.max_session_bytes) ||
        impl_->global_bytes >= impl_->config.max_global_bytes) {
        return refuse("capacity_exceeded");
    }
    const auto open_count = impl_->open_count.find(session);
    if (open_count != impl_->open_count.end() &&
        open_count->second >= impl_->config.max_open_executions) {
        return refuse("capacity_exceeded");
    }
    std::error_code ec;
    const std::filesystem::path session_dir = impl_->config.directory / session;
    std::filesystem::create_directories(session_dir, ec);
    if (ec) {
        return refuse("io_error");
    }
    auto streams = std::make_shared<Impl::Streams>();
    streams->impl = impl_.get();
    streams->session = session;
    const auto expiry = now + impl_->config.retention_after_terminal;
    for (int side = 0; side < 2; ++side) {
        std::string ref;
        std::filesystem::path payload;
        for (int attempt = 0; attempt < 8; ++attempt) {
            ref = mira::OperationId::generate().to_string();
            payload = session_dir / (ref + (side == 0 ? ".out" : ".err"));
            std::error_code create_ec;
            if (!std::filesystem::exists(payload, create_ec)) {
                break;
            }
            ref.clear();
        }
        if (ref.empty()) {
            return refuse("io_error");
        }
        streams->refs[side] = ref;
        streams->files[side].open(payload, std::ios::binary | std::ios::out | std::ios::trunc);
        if (!streams->files[side]) {
            return refuse("io_error");
        }
        std::filesystem::permissions(payload,
                                     std::filesystem::perms::owner_read |
                                         std::filesystem::perms::owner_write,
                                     ec); // best-effort owner-only
        Impl::Entry entry;
        entry.session = session;
        entry.payload = payload;
        entry.standard_output = side == 0;
        entry.expiry = expiry;
        impl_->entries.emplace(ref, std::move(entry));
    }
    impl_->open_count[session] += 1;
    return streams;
}

TextWindowOutcome BasicOutputStore::read(const std::string &session, const std::string &ref,
                                         std::uint64_t skip_lines,
                                         const TextWindowLimits &limits) {
    const std::lock_guard lock(impl_->mutex);
    impl_->sweep_locked(std::chrono::steady_clock::now());
    TextWindowOutcome outcome;
    if (!session_key_valid(session) || !ref_key_valid(ref) || limits.max_lines == 0 ||
        limits.max_bytes == 0 || limits.max_scan_bytes == 0 ||
        limits.max_scan_time <= std::chrono::milliseconds::zero()) {
        outcome.error = provider_error("invalid_argument", "malformed output reference request");
        return outcome;
    }
    const auto found = impl_->entries.find(ref);
    if (found == impl_->entries.end() || found->second.session != session) {
        outcome.error = provider_error(
            "output_expired",
            "output reference is unknown, expired, or belongs to another session");
        return outcome;
    }
    return page_stream(found->second.payload, skip_lines, limits);
}

} // namespace mirage::runtime
