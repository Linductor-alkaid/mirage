#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace mirage::runtime::persistence {

/// Schema version of the session state document (M5-08, DEC-011 session
/// history persistence): an additive layout in the same family as the
/// settings document. Decoders reject unknown schema versions.
inline constexpr int kSessionStateSchema = 1;

/// Decode-side bounds (RULE-07): the document holds at most the service's
/// session capacity per session list and a bounded number of turns per
/// dialog thread; entries beyond the budget fail the decode loudly.
inline constexpr std::size_t kMaxSessionStateSessions = 64;
inline constexpr std::size_t kMaxSessionStateTurnsPerSession = 256;
inline constexpr std::size_t kMaxSessionStateEntriesPerSession = 1024;
inline constexpr std::size_t kMaxSessionStateTextBytes = 16 * 1024;

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
    std::int64_t recorded_at_ms = 0;
};

/// One persisted session: registry identity plus the conversation and
/// dialog state the service rehydrates at start.
struct PersistedSession {
    std::string id; ///< pinned session id (32 lowercase hex characters)
    std::int64_t created_at_ms = 0;
    std::vector<PersistedJournalEntry> journal;
    std::vector<PersistedChatTurn> chat_turns;
};

/// The whole session state document.
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

} // namespace mirage::runtime::persistence
