#pragma once
#include <mirage/conversation.hpp>
#include <mirage/runtime/persistence/store.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace mirage::runtime::persistence {

/// Schema version of the session state document (M5-08, DEC-011 session
/// history persistence): an additive layout in the same family as the
/// settings document. Decoders reject unknown schema versions.
inline constexpr int kSessionStateSchema = 1;

/// DEC-101: no fixed session count limit. Bounds apply independently to
/// each session file and its history; legacy aggregate files remain capped.
inline constexpr std::size_t kMaxSessionStateTurnsPerSession = 256;
inline constexpr std::size_t kMaxSessionStateEntriesPerSession = 1024;
inline constexpr std::size_t kMaxSessionStateTextBytes = 16 * 1024;
// DEC-049: includes bounded reasoning/tool histories, not just final reply text.
inline constexpr std::size_t kMaxSessionStateFileBytes = 8 * 1024 * 1024;

/// One conversation journal entry of a persisted session (DEC-021 hydration
/// surface): raw append inputs, so re-appending reproduces the projected
/// view exactly (user text verbatim; outcome as progress + steps count).
struct PersistedJournalEntry {
    std::string kind; ///< "user" / "outcome"
    std::string task_id;
    std::string text;    ///< user message verbatim (kind "user")
    std::string outcome; ///< settlement progress name (kind "outcome")
    std::uint32_t steps = 0;
};

/// One dialog thread turn of a persisted session (DEC-027 hydration
/// surface): the settled wire shape.
struct PersistedChatTurn {
    std::string turn_id;
    std::string status; ///< "ok" / "failed" (pending turns are never persisted)
    std::string user_text;
    std::string reply_text;
    std::string error;
    std::uint64_t sequence = 0;
    std::int64_t recorded_at_ms = 0; // DEC-079: canonical record time, updated at settlement.
    std::vector<conversation::Part> parts = {};
    std::uint64_t process_sequence = 0;
    conversation::Timings timings = {};
};

/// DEC-036: provider-reported input usage of the session's last successful request.
struct PersistedContextUsage {
    std::uint64_t input_tokens = 0;
    std::uint64_t window_tokens = 0;
    std::string model;
    bool estimated = false;
};

/// One persisted session: registry identity plus the conversation and
/// dialog state the service rehydrates at start.
struct PersistedSession {
    std::string id; ///< pinned session id (32 lowercase hex characters)
    std::int64_t created_at_ms = 0;
    std::vector<PersistedJournalEntry> journal;
    std::vector<PersistedChatTurn> chat_turns;
    std::optional<PersistedContextUsage> context_usage = {};
    // Also advances when a successful request has no usage, or a turn is replaced.
    std::uint64_t context_usage_sequence = 0;
};

/// An in-memory snapshot, also used by the legacy schema 1 codec.
struct SessionState {
    int schema = kSessionStateSchema;
    std::vector<PersistedSession> sessions;
};

/// Serializes the document (compact JSON, schema field included).
std::string encode_session_state(const SessionState &state);

struct SessionStateDecode {
    bool ok = false;
    SessionState state;
    std::string error; ///< stable reason, meaningful when !ok
};

/// Strict decode: unknown members, wrong types, oversized texts and unknown
/// schema versions are rejected with a stable reason (DEC-011 item 2).
SessionStateDecode decode_session_state(std::string_view body);

/// DEC-101: one atomic JSON file per session; callers serialize load/save.
/// Legacy session-state.json migrates on the first successful save. Deleted
/// sessions become empty tombstones. Corrupt files are retained and diagnosed.
/// load() may return ok=true with error diagnostics for quarantined files.
class SessionStateStore {
  public:
    explicit SessionStateStore(std::filesystem::path directory);
    SessionStateDecode load() const;
    SaveResult save(const SessionState &state) const;

  private:
    std::filesystem::path directory_;
};

} // namespace mirage::runtime::persistence
