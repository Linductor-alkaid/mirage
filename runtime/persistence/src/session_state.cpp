#include <mirage/runtime/persistence/session_state.hpp>

#include <mira/json.hpp>

#include <string>

namespace mirage::runtime::persistence {
namespace {

using mira::JsonValue;

constexpr std::size_t kMaxTextBytes = 16 * 1024;

JsonValue make_object() { return JsonValue{JsonValue::Object{}}; }

void put(JsonValue &object, std::string key, JsonValue value) {
    object.set(std::move(key), std::move(value));
}

const JsonValue *member(const JsonValue &object, std::string_view key) {
    const auto *entries = object.as_object();
    if (entries == nullptr) {
        return nullptr;
    }
    for (const auto &entry : *entries) {
        if (entry.first == key) {
            return &entry.second;
        }
    }
    return nullptr;
}

bool has_unknown_member(const JsonValue &object, std::initializer_list<std::string_view> known) {
    const auto *entries = object.as_object();
    if (entries == nullptr) {
        return false;
    }
    for (const auto &entry : *entries) {
        bool found = false;
        for (const std::string_view name : known) {
            if (entry.first == name) {
                found = true;
                break;
            }
        }
        if (!found) {
            return true;
        }
    }
    return false;
}

std::optional<std::string> bounded_string(const JsonValue &value, std::string &error) {
    const auto *text = value.as_string();
    if (text == nullptr) {
        error = "expected a string";
        return std::nullopt;
    }
    if (text->size() > kMaxSessionStateTextBytes) {
        error = "string exceeds " + std::to_string(kMaxSessionStateTextBytes) + " bytes";
        return std::nullopt;
    }
    return *text;
}

} // namespace

std::string encode_session_state(const SessionState &state) {
    JsonValue object = make_object();
    put(object, "schema", JsonValue{static_cast<std::int64_t>(state.schema)});
    JsonValue::Array sessions;
    sessions.reserve(state.sessions.size());
    for (const PersistedSession &session : state.sessions) {
        JsonValue session_object = make_object();
        put(session_object, "id", JsonValue{session.id});
        put(session_object, "created_at_ms",
            JsonValue{static_cast<std::int64_t>(session.created_at_ms)});
        JsonValue::Array journal;
        journal.reserve(session.journal.size());
        for (const PersistedJournalEntry &entry : session.journal) {
            JsonValue entry_object = make_object();
            put(entry_object, "kind", JsonValue{entry.kind});
            put(entry_object, "task_id", JsonValue{entry.task_id});
            if (entry.kind == "user") {
                put(entry_object, "text", JsonValue{entry.text});
            } else {
                put(entry_object, "outcome", JsonValue{entry.outcome});
                put(entry_object, "steps", JsonValue{static_cast<std::int64_t>(entry.steps)});
            }
            journal.emplace_back(std::move(entry_object));
        }
        put(session_object, "journal", JsonValue{std::move(journal)});
        JsonValue::Array turns;
        turns.reserve(session.chat_turns.size());
        for (const PersistedChatTurn &turn : session.chat_turns) {
            JsonValue turn_object = make_object();
            put(turn_object, "turn_id", JsonValue{turn.turn_id});
            put(turn_object, "status", JsonValue{turn.status});
            put(turn_object, "user_text", JsonValue{turn.user_text});
            if (turn.status == "ok") {
                put(turn_object, "reply_text", JsonValue{turn.reply_text});
            } else {
                put(turn_object, "error", JsonValue{turn.error});
            }
            put(turn_object, "sequence", JsonValue{static_cast<std::int64_t>(turn.sequence)});
            put(turn_object, "recorded_at_ms",
                JsonValue{static_cast<std::int64_t>(turn.recorded_at_ms)});
            turns.emplace_back(std::move(turn_object));
        }
        put(session_object, "chat_turns", JsonValue{std::move(turns)});
        sessions.emplace_back(std::move(session_object));
    }
    put(object, "sessions", JsonValue{std::move(sessions)});
    return mira::to_json_string(object);
}

SessionStateDecode decode_session_state(std::string_view body) {
    SessionStateDecode result;
    auto parsed = mira::parse_json(body);
    if (!parsed) {
        result.error = "invalid JSON: " + parsed.error().safe_message;
        return result;
    }
    const JsonValue &document = parsed.value();
    if (!document.is_object()) {
        result.error = "session state document must be a JSON object";
        return result;
    }
    if (has_unknown_member(document, {"schema", "sessions"})) {
        result.error = "session state document contains an unknown member";
        return result;
    }
    const JsonValue *schema = member(document, "schema");
    if (schema == nullptr) {
        result.error = "session state document lacks the 'schema' member";
        return result;
    }
    const auto version = schema->as_integer();
    if (!version || *version != kSessionStateSchema) {
        result.error = "unsupported session state schema version";
        return result;
    }
    const JsonValue *sessions_value = member(document, "sessions");
    if (sessions_value == nullptr || !sessions_value->is_array()) {
        result.error = "member 'sessions' must be an array";
        return result;
    }
    const auto *sessions = sessions_value->as_array();
    if (sessions->size() > kMaxSessionStateSessions) {
        result.error =
            "member 'sessions' exceeds " + std::to_string(kMaxSessionStateSessions) + " entries";
        return result;
    }
    for (const JsonValue &session_value : *sessions) {
        if (!session_value.is_object() ||
            has_unknown_member(session_value, {"id", "created_at_ms", "journal", "chat_turns"})) {
            result.error = "session entries must carry only 'id', 'created_at_ms', 'journal' "
                           "and 'chat_turns'";
            return result;
        }
        PersistedSession session;
        if (const auto *id = member(session_value, "id"); id != nullptr) {
            if (const auto text = bounded_string(*id, result.error)) {
                session.id = *text;
            } else {
                result.error = "member 'id' invalid: " + result.error;
                return result;
            }
        }
        // The header pins the id form to the pinned session id (32 lowercase
        // hex characters); anything else fails closed.
        constexpr std::string_view kHexDigits = "0123456789abcdef";
        if (session.id.size() != 32 ||
            session.id.find_first_not_of(kHexDigits) != std::string::npos) {
            result.error = "session entry 'id' must be 32 lowercase hex characters";
            return result;
        }
        if (const auto *created = member(session_value, "created_at_ms"); created != nullptr) {
            const auto value = created->as_integer();
            if (!value || *value < 0) {
                result.error = "member 'created_at_ms' must be a non-negative integer";
                return result;
            }
            session.created_at_ms = *value;
        }
        if (const auto *journal_value = member(session_value, "journal");
            journal_value != nullptr) {
            const auto *entries = journal_value->as_array();
            if (entries == nullptr || entries->size() > kMaxSessionStateEntriesPerSession) {
                result.error = "member 'journal' must be an array of at most " +
                               std::to_string(kMaxSessionStateEntriesPerSession) + " entries";
                return result;
            }
            for (const JsonValue &entry : *entries) {
                if (!entry.is_object() ||
                    has_unknown_member(entry, {"kind", "task_id", "text", "outcome", "steps"})) {
                    result.error = "journal entries must carry only 'kind', 'task_id', 'text', "
                                   "'outcome' and 'steps'";
                    return result;
                }
                PersistedJournalEntry record;
                {
                    const auto *kind_value = member(entry, "kind");
                    if (kind_value == nullptr) {
                        result.error = "journal entry requires a 'kind' member";
                        return result;
                    }
                    if (const auto kind = bounded_string(*kind_value, result.error)) {
                        record.kind = *kind;
                    } else {
                        result.error = "member 'kind' invalid: " + result.error;
                        return result;
                    }
                }
                if (record.kind != "user" && record.kind != "outcome") {
                    result.error = "journal entry 'kind' must be \"user\" or \"outcome\"";
                    return result;
                }
                if (const auto *task = member(entry, "task_id"); task != nullptr) {
                    if (const auto text = bounded_string(*task, result.error)) {
                        record.task_id = *text;
                    } else {
                        result.error = "member 'task_id' invalid: " + result.error;
                        return result;
                    }
                }
                if (record.kind == "user") {
                    if (const auto *text_value = member(entry, "text"); text_value != nullptr) {
                        if (const auto text = bounded_string(*text_value, result.error)) {
                            record.text = *text;
                        } else {
                            result.error = "member 'text' invalid: " + result.error;
                            return result;
                        }
                    } else {
                        result.error = "journal 'user' entries require a 'text' member";
                        return result;
                    }
                } else {
                    if (const auto *outcome_value = member(entry, "outcome");
                        outcome_value != nullptr) {
                        if (const auto text = bounded_string(*outcome_value, result.error)) {
                            record.outcome = *text;
                        } else {
                            result.error = "member 'outcome' invalid: " + result.error;
                            return result;
                        }
                    } else {
                        result.error = "journal 'outcome' entries require an 'outcome' member";
                        return result;
                    }
                    if (const auto *steps_value = member(entry, "steps"); steps_value != nullptr) {
                        const auto steps = steps_value->as_integer();
                        if (!steps || *steps < 0) {
                            result.error = "member 'steps' must be a non-negative integer";
                            return result;
                        }
                        record.steps = static_cast<std::uint32_t>(*steps);
                    }
                }
                session.journal.push_back(std::move(record));
            }
        }
        if (const auto *turns_value = member(session_value, "chat_turns"); turns_value != nullptr) {
            const auto *turns = turns_value->as_array();
            if (turns == nullptr || turns->size() > kMaxSessionStateTurnsPerSession) {
                result.error = "member 'chat_turns' must be an array of at most " +
                               std::to_string(kMaxSessionStateTurnsPerSession) + " entries";
                return result;
            }
            for (const JsonValue &entry : *turns) {
                if (!entry.is_object() ||
                    has_unknown_member(entry, {"turn_id", "status", "user_text", "reply_text",
                                               "error", "sequence", "recorded_at_ms"})) {
                    result.error = "chat turns must carry only 'turn_id', 'status', 'user_text', "
                                   "'reply_text', 'error', 'sequence' and 'recorded_at_ms'";
                    return result;
                }
                PersistedChatTurn turn;
                if (const auto *field = member(entry, "turn_id"); field != nullptr) {
                    if (const auto text = bounded_string(*field, result.error)) {
                        turn.turn_id = *text;
                    } else {
                        result.error = "member 'turn_id' invalid: " + result.error;
                        return result;
                    }
                }
                if (const auto *field = member(entry, "status"); field != nullptr) {
                    if (const auto text = bounded_string(*field, result.error)) {
                        turn.status = *text;
                    } else {
                        result.error = "member 'status' invalid: " + result.error;
                        return result;
                    }
                }
                if (turn.turn_id.empty() || (turn.status != "ok" && turn.status != "failed")) {
                    result.error = "chat turns require a 'turn_id' and a settled 'status' "
                                   "(\"ok\" / \"failed\")";
                    return result;
                }
                if (const auto *field = member(entry, "user_text"); field != nullptr) {
                    if (const auto text = bounded_string(*field, result.error)) {
                        turn.user_text = *text;
                    } else {
                        result.error = "member 'user_text' invalid: " + result.error;
                        return result;
                    }
                } else {
                    result.error = "chat turns require a 'user_text' member";
                    return result;
                }
                if (turn.status == "ok") {
                    if (const auto *field = member(entry, "reply_text"); field != nullptr) {
                        if (const auto text = bounded_string(*field, result.error)) {
                            turn.reply_text = *text;
                        } else {
                            result.error = "member 'reply_text' invalid: " + result.error;
                            return result;
                        }
                    } else {
                        result.error = "settled \"ok\" chat turns require a 'reply_text' member";
                        return result;
                    }
                } else {
                    if (const auto *field = member(entry, "error"); field != nullptr) {
                        if (const auto text = bounded_string(*field, result.error)) {
                            turn.error = *text;
                        } else {
                            result.error = "member 'error' invalid: " + result.error;
                            return result;
                        }
                    } else {
                        result.error = "settled \"failed\" chat turns require an 'error' member";
                        return result;
                    }
                }
                if (const auto *field = member(entry, "sequence"); field != nullptr) {
                    const auto value = field->as_integer();
                    if (!value || *value < 1) {
                        result.error = "member 'sequence' must be a positive integer";
                        return result;
                    }
                    turn.sequence = static_cast<std::uint64_t>(*value);
                }
                if (const auto *field = member(entry, "recorded_at_ms"); field != nullptr) {
                    const auto value = field->as_integer();
                    if (!value || *value < 0) {
                        result.error = "member 'recorded_at_ms' must be a non-negative integer";
                        return result;
                    }
                    turn.recorded_at_ms = *value;
                }
                session.chat_turns.push_back(std::move(turn));
            }
        }
        result.state.sessions.push_back(std::move(session));
    }
    result.ok = true;
    return result;
}

} // namespace mirage::runtime::persistence
