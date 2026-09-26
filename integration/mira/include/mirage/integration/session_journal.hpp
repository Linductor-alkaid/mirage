#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace mirage::integration {

/// Outcome of one journal append; `sequence` is the entry's position in the
/// session's event sequence and `text` the projected conversation text of
/// the appended entry (identical to what history() reports), both meaningful
/// only when ok is true.
struct JournalAppend {
    bool ok = false;
    std::string error; ///< stable pinned-free reason, meaningful when !ok
    std::uint64_t sequence = 0;
    std::string text;
};

/// One conversation entry of the rebuilt session view (DEC-021): `kind` is
/// "user" (a task goal) or "outcome" (a task settlement summary); `text` is
/// the projected message body; `sequence` is the entry's position in the
/// session's event sequence; `recorded_at_ms` is the wall-clock time the
/// underlying event was recorded.
struct JournalEntry {
    std::string kind;
    std::string text;
    std::uint64_t sequence = 0;
    std::int64_t recorded_at_ms = 0;
};

/// One session's rebuilt conversation view: the newest `limit` entries in
/// session order; `truncated` marks that older entries exist beyond the
/// requested budget.
struct JournalHistory {
    bool ok = false;
    std::string error; ///< stable pinned-free reason, meaningful when !ok
    std::vector<JournalEntry> entries;
    bool truncated = false;
};

/// Conversation journal over the pinned event store (DEC-021): appends the
/// same event types the pinned agent loop records (`UserMessageInjected` /
/// `LoopSettled`, identical payload envelopes) and rebuilds the view through
/// the pinned conversation projection, so the transitional driver form and
/// the future model loop feed one store and one projection (RULE-07: the
/// store is the only source of truth, the view is rebuildable). The backing
/// store is in-memory and bounded; a saturated store fails the append closed
/// instead of dropping entries silently. Thread-safe: appends may come from
/// the service serial context and driver tasks concurrently.
class SessionJournal {
  public:
    explicit SessionJournal(std::size_t max_events = 10000);
    ~SessionJournal();
    SessionJournal(const SessionJournal &) = delete;
    SessionJournal &operator=(const SessionJournal &) = delete;

    /// Records one task goal as the session's user message; `text` is stored
    /// verbatim and projected back unchanged. Empty texts fail closed.
    JournalAppend append_user_message(const std::string &session_id, const std::string &task_id,
                                      const std::string &text);

    /// Records one task settlement as the session's outcome message; the
    /// projected text follows the pinned conversation form
    /// ("loop settled: <outcome> (steps N)"). Empty outcomes fail closed.
    JournalAppend append_outcome(const std::string &session_id, const std::string &task_id,
                                 const std::string &outcome, std::uint32_t steps);

    /// Rebuilds the session view and returns the newest `limit` entries;
    /// `limit` 0 returns nothing but the truncated flag. Unknown or malformed
    /// session ids fail closed (not_found shape in `error`).
    JournalHistory history(const std::string &session_id, std::size_t limit) const;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace mirage::integration
