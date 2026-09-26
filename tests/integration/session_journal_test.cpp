#include "../support/test.hpp"

#include <mirage/integration/session_journal.hpp>

#include <mira/core_contracts.hpp>

#include <cstdint>
#include <string>

namespace {

namespace integration = mirage::integration;

/// A well-formed non-nil session id (pinned Id128 hex form); the journal
/// parses identities back onto the pinned contract, so every scenario needs
/// parseable ids. Parsed once and shared.
const std::string kSessionA = [] {
    const mira::SessionId id = mira::SessionId::generate();
    return id.to_string();
}();

const std::string kSessionB = [] {
    const mira::SessionId id = mira::SessionId::generate();
    return id.to_string();
}();

const std::string kTask1 = [] {
    const mira::TaskId id = mira::TaskId::generate();
    return id.to_string();
}();

const std::string kTask2 = [] {
    const mira::TaskId id = mira::TaskId::generate();
    return id.to_string();
}();

void scenario_user_message_round_trip() {
    integration::SessionJournal journal;
    const auto appended = journal.append_user_message(kSessionA, kTask1, "inspect the host");
    MIRAGE_CHECK(appended.ok);
    MIRAGE_CHECK(appended.error.empty());
    MIRAGE_CHECK(appended.sequence >= 1);
    MIRAGE_CHECK(appended.text == "inspect the host");

    const auto history = journal.history(kSessionA, 10);
    MIRAGE_CHECK(history.ok);
    MIRAGE_CHECK(history.entries.size() == 1);
    MIRAGE_CHECK(!history.truncated);
    MIRAGE_CHECK(history.entries[0].kind == "user");
    MIRAGE_CHECK(history.entries[0].text == "inspect the host");
    MIRAGE_CHECK(history.entries[0].sequence == appended.sequence);
    MIRAGE_CHECK(history.entries[0].recorded_at_ms > 0);
}

void scenario_outcome_projection_form() {
    integration::SessionJournal journal;
    const auto appended = journal.append_outcome(kSessionA, kTask1, "Completed", 2);
    MIRAGE_CHECK(appended.ok);
    // The projected text follows the pinned conversation form.
    MIRAGE_CHECK(appended.text == "loop settled: Completed (steps 2)");

    const auto history = journal.history(kSessionA, 10);
    MIRAGE_CHECK(history.ok);
    MIRAGE_CHECK(history.entries.size() == 1);
    MIRAGE_CHECK(history.entries[0].kind == "outcome");
    MIRAGE_CHECK(history.entries[0].text == "loop settled: Completed (steps 2)");
}

void scenario_conversation_order_and_limit() {
    integration::SessionJournal journal;
    MIRAGE_CHECK(journal.append_user_message(kSessionA, kTask1, "first goal").ok);
    MIRAGE_CHECK(journal.append_outcome(kSessionA, kTask1, "Failed", 1).ok);
    MIRAGE_CHECK(journal.append_user_message(kSessionA, kTask2, "second goal").ok);
    MIRAGE_CHECK(journal.append_outcome(kSessionA, kTask2, "Completed", 3).ok);

    const auto full = journal.history(kSessionA, 10);
    MIRAGE_CHECK(full.ok);
    MIRAGE_CHECK(full.entries.size() == 4);
    MIRAGE_CHECK(!full.truncated);
    MIRAGE_CHECK(full.entries[0].kind == "user" && full.entries[0].text == "first goal");
    MIRAGE_CHECK(full.entries[1].kind == "outcome");
    MIRAGE_CHECK(full.entries[2].kind == "user" && full.entries[2].text == "second goal");
    MIRAGE_CHECK(full.entries[3].kind == "outcome");
    // Session sequences strictly increase in conversation order.
    MIRAGE_CHECK(full.entries[0].sequence < full.entries[1].sequence);
    MIRAGE_CHECK(full.entries[1].sequence < full.entries[2].sequence);
    MIRAGE_CHECK(full.entries[2].sequence < full.entries[3].sequence);

    // The newest window wins; the truncated flag reports the dropped head.
    const auto window = journal.history(kSessionA, 2);
    MIRAGE_CHECK(window.ok);
    MIRAGE_CHECK(window.truncated);
    MIRAGE_CHECK(window.entries.size() == 2);
    MIRAGE_CHECK(window.entries[0].text == "second goal");
    MIRAGE_CHECK(window.entries[1].kind == "outcome");

    // A budget of zero returns nothing but the truncation verdict.
    const auto empty = journal.history(kSessionA, 0);
    MIRAGE_CHECK(empty.ok);
    MIRAGE_CHECK(empty.entries.empty());
    MIRAGE_CHECK(empty.truncated);
}

void scenario_sessions_are_independent() {
    integration::SessionJournal journal;
    MIRAGE_CHECK(journal.append_user_message(kSessionA, kTask1, "for session a").ok);
    MIRAGE_CHECK(journal.append_user_message(kSessionB, kTask2, "for session b").ok);

    const auto history_a = journal.history(kSessionA, 10);
    MIRAGE_CHECK(history_a.ok);
    MIRAGE_CHECK(history_a.entries.size() == 1);
    MIRAGE_CHECK(history_a.entries[0].text == "for session a");

    const auto history_b = journal.history(kSessionB, 10);
    MIRAGE_CHECK(history_b.ok);
    MIRAGE_CHECK(history_b.entries.size() == 1);
    MIRAGE_CHECK(history_b.entries[0].text == "for session b");
}

void scenario_fail_closed_inputs() {
    integration::SessionJournal journal;

    // Malformed identities never reach the store.
    const auto bad_session = journal.append_user_message("not-a-session-id", kTask1, "text");
    MIRAGE_CHECK(!bad_session.ok);
    MIRAGE_CHECK(!bad_session.error.empty());

    const auto bad_task = journal.append_outcome(kSessionA, "also-not-a-task-id", "Completed", 1);
    MIRAGE_CHECK(!bad_task.ok);

    // Empty payloads are rejected before the store sees them.
    const auto empty_text = journal.append_user_message(kSessionA, kTask1, "");
    MIRAGE_CHECK(!empty_text.ok);
    const auto empty_outcome = journal.append_outcome(kSessionA, kTask1, "", 1);
    MIRAGE_CHECK(!empty_outcome.ok);

    // History for a malformed session fails closed instead of returning a
    // fake empty view.
    const auto history = journal.history("nope", 10);
    MIRAGE_CHECK(!history.ok);
    MIRAGE_CHECK(!history.error.empty());
    MIRAGE_CHECK(history.entries.empty());
}

void scenario_empty_history_is_clean() {
    integration::SessionJournal journal;
    const auto history = journal.history(kSessionB, 50);
    MIRAGE_CHECK(history.ok);
    MIRAGE_CHECK(history.entries.empty());
    MIRAGE_CHECK(!history.truncated);
}

} // namespace

int main() {
    scenario_user_message_round_trip();
    scenario_outcome_projection_form();
    scenario_conversation_order_and_limit();
    scenario_sessions_are_independent();
    scenario_fail_closed_inputs();
    scenario_empty_history_is_clean();
    return mirage::testing::finish("session_journal_test");
}
