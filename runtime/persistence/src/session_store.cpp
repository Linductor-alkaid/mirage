#include <mirage/runtime/persistence/session_state.hpp>

#include <mira/json.hpp>

#include "json_document.hpp"

#include <map>
#include <set>
#include <utility>

namespace mirage::runtime::persistence {
namespace {
constexpr std::string_view kMarker = R"({"schema":2,"storage":"per-session"})";

bool valid_id(std::string_view id) {
    return id.size() == 32 && id.find_first_not_of("0123456789abcdef") == id.npos;
}

bool is_marker(std::string_view body) {
    const auto parsed = mira::parse_json(body);
    if (!parsed || !parsed.value().is_object() ||
        json_doc::has_unknown_member(parsed.value(), {"schema", "storage"}))
        return false;
    const auto *schema = json_doc::member(parsed.value(), "schema");
    const auto *storage = json_doc::member(parsed.value(), "storage");
    return schema && schema->as_integer() == 2 && storage && storage->as_string() &&
           *storage->as_string() == "per-session";
}

LocalStateStore legacy_store(const std::filesystem::path &directory) {
    return {directory, "session-state.json", kMaxSessionStateFileBytes};
}

LocalStateStore session_store(const std::filesystem::path &directory, const std::string &id) {
    return {directory / "sessions", id + ".json", kMaxSessionStateFileBytes};
}

LoadResult load_regular(const LocalStateStore &store) {
    std::error_code ec;
    const auto type = std::filesystem::symlink_status(store.file_path(), ec).type();
    if (type == std::filesystem::file_type::not_found)
        return {};
    if (ec || type != std::filesystem::file_type::regular)
        return {LoadStatus::IoError, {}, "session document must be a regular file"};
    return store.load();
}

// Only canonical filenames participate. Temp files and foreign files are never
// interpreted as sessions. LocalStateStore retains the platform atomic IO rules.
bool session_ids(const std::filesystem::path &directory, std::set<std::string> &ids,
                 std::string &error) {
    std::error_code ec;
    const auto path = directory / "sessions";
    const bool exists = std::filesystem::exists(path, ec);
    if (!ec && !exists)
        return true;
    if (!ec) {
        std::filesystem::directory_iterator it(path, ec), end;
        while (!ec && it != end) {
            const auto name = it->path().filename();
            if (name.extension() == ".json" && valid_id(name.stem().string()))
                ids.insert(name.stem().string());
            it.increment(ec);
        }
    }
    if (ec) {
        error = "io_error: cannot enumerate '" + path.string() + "': " + ec.message();
        return false;
    }
    return true;
}

SessionStateDecode load_one(const LocalStateStore &store, const std::string &id) {
    const auto loaded = load_regular(store);
    if (loaded.status != LoadStatus::Loaded)
        return {false,
                {},
                "cannot read '" + store.file_path().string() + "': " +
                    (loaded.error.empty() ? "missing or oversized session file" : loaded.error)};
    auto decoded = decode_session_state(loaded.body);
    if (decoded.ok &&
        (decoded.state.sessions.size() > 1 ||
         (!decoded.state.sessions.empty() && decoded.state.sessions.front().id != id))) {
        decoded.ok = false;
        decoded.error = "session filename and document identity disagree";
    }
    if (!decoded.ok)
        decoded.error = "invalid '" + store.file_path().string() + "': " + decoded.error;
    return decoded;
}
} // namespace

SessionStateStore::SessionStateStore(std::filesystem::path directory)
    : directory_(std::move(directory)) {}

SessionStateDecode SessionStateStore::load() const {
    const auto legacy = legacy_store(directory_).load();
    if (legacy.status == LoadStatus::Loaded && !is_marker(legacy.body))
        return decode_session_state(legacy.body);
    if (legacy.status != LoadStatus::Loaded && legacy.status != LoadStatus::Absent)
        return {false, {}, "cannot read legacy session state: " + legacy.error};

    SessionStateDecode result;
    std::set<std::string> ids;
    if (!session_ids(directory_, ids, result.error))
        return result;
    for (const auto &id : ids) {
        auto decoded = load_one(session_store(directory_, id), id);
        if (!decoded.ok) {
            if (!result.error.empty())
                result.error += '\n';
            result.error += decoded.error;
            continue; // Quarantine in place; other histories remain available.
        }
        if (!decoded.state.sessions.empty())
            result.state.sessions.push_back(std::move(decoded.state.sessions.front()));
    }
    result.ok = true;
    return result;
}

SaveResult SessionStateStore::save(const SessionState &state) const {
    auto legacy = legacy_store(directory_);
    const auto previous = legacy.load();
    const bool migrating = previous.status == LoadStatus::Loaded && !is_marker(previous.body);
    if ((previous.status != LoadStatus::Loaded && previous.status != LoadStatus::Absent) ||
        (migrating && !decode_session_state(previous.body).ok))
        return {false, "io_error: unreadable legacy session state retained; repair before saving"};

    std::map<std::string, const PersistedSession *> desired;
    for (const auto &session : state.sessions) {
        if (!valid_id(session.id) || !desired.emplace(session.id, &session).second)
            return {false, "io_error: invalid or duplicate session identity"};
    }
    std::set<std::string> existing;
    std::string error;
    if (!session_ids(directory_, existing, error))
        return {false, error};

    // A fresh store commits its format before any session data. During migration
    // the legacy aggregate remains authoritative until every file has committed.
    if (!migrating) {
        const auto marked = legacy.save(kMarker);
        if (!marked.ok)
            return marked;
    }
    for (const auto &[id, session] : desired) {
        auto store = session_store(directory_, id);
        const auto body = encode_session_state({kSessionStateSchema, {*session}});
        const auto old = load_regular(store);
        if (old.status == LoadStatus::Loaded && old.body == body)
            continue;
        const auto saved = store.save(body);
        if (!saved.ok)
            return saved;
    }
    for (const auto &id : existing) {
        if (desired.contains(id))
            continue;
        const auto decoded = load_one(session_store(directory_, id), id);
        // Corrupt files are retained. Migration may replace incomplete staging
        // files because the validated aggregate is still the authority.
        if (!migrating && (!decoded.ok || decoded.state.sessions.empty()))
            continue;
        const auto saved = session_store(directory_, id).save(encode_session_state({}));
        if (!saved.ok)
            return saved;
    }
    if (migrating)
        return legacy.save(kMarker);
    return {true, {}};
}
} // namespace mirage::runtime::persistence
