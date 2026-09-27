#include <mirage/integration/session_journal.hpp>

#include <mira/conversation_log.hpp>
#include <mira/core_contracts.hpp>
#include <mira/event_store.hpp>
#include <mira/json.hpp>

#include <chrono>
#include <mutex>
#include <optional>

namespace mirage::integration {
namespace {

using mira::AppendRequest;
using mira::ConversationEntry;
using mira::EventClass;
using mira::EventId;
using mira::EventPayload;
using mira::JsonValue;
using mira::MemoryEventStore;
using mira::RuntimeId;
using mira::SessionId;
using mira::TaskId;

/// The pinned loop's event envelope (agent_loop.cpp emit): `detail` is the
/// summary object itself; the projection reads only that object, but writing
/// the same envelope keeps the store byte-compatible with model-loop events.
JsonValue loop_envelope(const TaskId &task, JsonValue detail) {
    JsonValue::Object envelope;
    envelope.emplace_back("task_id", task.to_string());
    envelope.emplace_back("task_epoch", static_cast<std::int64_t>(0));
    envelope.emplace_back("detail", std::move(detail));
    return JsonValue{std::move(envelope)};
}

std::optional<SessionId> parse_session(const std::string &session_id) {
    const auto parsed = SessionId::parse(session_id);
    if (!parsed || parsed->is_nil()) {
        return std::nullopt;
    }
    return parsed;
}

std::optional<TaskId> parse_task(const std::string &task_id) {
    const auto parsed = TaskId::parse(task_id);
    if (!parsed || parsed->is_nil()) {
        return std::nullopt;
    }
    return parsed;
}

JournalEntry to_entry(const ConversationEntry &entry) {
    JournalEntry journal_entry;
    journal_entry.kind = entry.kind == ConversationEntry::Kind::UserMessage ? "user" : "outcome";
    journal_entry.text = entry.text;
    journal_entry.sequence = entry.session_sequence;
    journal_entry.recorded_at_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                       entry.recorded_at.wall.time_since_epoch())
                                       .count();
    return journal_entry;
}

} // namespace

struct SessionJournal::Impl {
    explicit Impl(std::size_t max_events) : runtime(RuntimeId::generate()), store(max_events) {}

    /// One projection walk returning the full session view; the caller picks
    /// the window. Kept here so both append read-back and history() share the
    /// pinned rebuild path.
    mira::Result<std::vector<ConversationEntry>> view(const SessionId &session) const {
        return mira::build_conversation_view(store, session);
    }

    RuntimeId runtime;
    mutable std::mutex mutex;
    MemoryEventStore store;
};

SessionJournal::SessionJournal(std::size_t max_events)
    : impl_(std::make_unique<Impl>(max_events)) {}

SessionJournal::~SessionJournal() = default;

JournalAppend SessionJournal::append_user_message(const std::string &session_id,
                                                  const std::string &task_id,
                                                  const std::string &text) {
    JournalAppend result;
    const auto session = parse_session(session_id);
    const auto task = parse_task(task_id);
    if (!session || !task) {
        result.error = "journal append requires well-formed session and task identities";
        return result;
    }
    if (text.empty()) {
        result.error = "journal append requires a non-empty message text";
        return result;
    }
    JsonValue::Object detail;
    detail.emplace_back("text", text);
    detail.emplace_back("bytes", static_cast<std::int64_t>(text.size()));
    detail.emplace_back("digest", mira::digest_string(text).to_string());

    AppendRequest append;
    append.event_id = EventId::generate();
    append.runtime_id = impl_->runtime;
    append.session_id = *session;
    append.task_id = *task;
    append.payload =
        EventPayload{"UserMessageInjected",
                     mira::to_json_string(loop_envelope(*task, JsonValue{std::move(detail)})),
                     EventClass::State};
    std::lock_guard lock(impl_->mutex);
    const auto receipt = impl_->store.append(append);
    if (!receipt) {
        result.error = receipt.error().safe_message;
        return result;
    }
    result.ok = true;
    result.sequence = receipt.value().session_sequence;
    result.text = text;
    return result;
}

JournalAppend SessionJournal::append_outcome(const std::string &session_id,
                                             const std::string &task_id, const std::string &outcome,
                                             std::uint32_t steps) {
    JournalAppend result;
    const auto session = parse_session(session_id);
    const auto task = parse_task(task_id);
    if (!session || !task) {
        result.error = "journal append requires well-formed session and task identities";
        return result;
    }
    if (outcome.empty()) {
        result.error = "journal append requires a non-empty outcome";
        return result;
    }
    JsonValue::Object detail;
    detail.emplace_back("outcome", outcome);
    detail.emplace_back("steps", static_cast<std::int64_t>(steps));
    detail.emplace_back("recoveries", static_cast<std::int64_t>(0));

    AppendRequest append;
    append.event_id = EventId::generate();
    append.runtime_id = impl_->runtime;
    append.session_id = *session;
    append.task_id = *task;
    append.payload = EventPayload{
        "LoopSettled", mira::to_json_string(loop_envelope(*task, JsonValue{std::move(detail)})),
        EventClass::State};
    std::lock_guard lock(impl_->mutex);
    const auto receipt = impl_->store.append(append);
    if (!receipt) {
        result.error = receipt.error().safe_message;
        return result;
    }
    result.ok = true;
    result.sequence = receipt.value().session_sequence;
    // Read the projected text back so the event stream and the history view
    // carry one wording (the pinned projection composes the outcome form).
    auto view = impl_->view(*session);
    if (view) {
        for (const auto &entry : view.value()) {
            if (entry.session_sequence == receipt.value().session_sequence) {
                result.text = entry.text;
                break;
            }
        }
    }
    return result;
}

JournalHistory SessionJournal::history(const std::string &session_id, std::size_t limit) const {
    JournalHistory result;
    const auto session = parse_session(session_id);
    if (!session) {
        result.error = "journal history requires a well-formed session identity";
        return result;
    }
    std::lock_guard lock(impl_->mutex);
    auto view = impl_->view(*session);
    if (!view) {
        result.error = view.error().safe_message;
        return result;
    }
    auto &entries = view.value();
    result.truncated = entries.size() > limit;
    if (result.truncated) {
        entries.erase(entries.begin(), entries.end() - static_cast<std::ptrdiff_t>(limit));
    }
    for (const auto &entry : entries) {
        result.entries.push_back(to_entry(entry));
    }
    result.ok = true;
    return result;
}

} // namespace mirage::integration
