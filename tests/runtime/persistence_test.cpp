// M1-07 local state persistence verification (DEC-011, independent
// verification pass). Unit-level coverage of the pinned-free persistence
// surface: LocalStateStore atomic save/load semantics (round trip, absent,
// overwrite, byte budget before disk, TooLarge without truncation, private
// directory/file modes, error paths that must not destroy the previous
// file), the strict settings codec (defaults, unknown members, schema
// checks, vocabularies, bounds) and the strict recovery codec (round trip,
// frozen vocabularies, duplicate ids, record bounds).

#include "../support/test.hpp"

#include <mirage/runtime/persistence/paths.hpp>
#include <mirage/runtime/persistence/recovery.hpp>
#include <mirage/runtime/persistence/settings.hpp>
#include <mirage/runtime/persistence/store.hpp>

#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <cerrno>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <string>
#include <system_error>

namespace {

namespace persistence = mirage::runtime::persistence;

/// Unique temporary directory under the system temp path; removed on scope
/// exit. Same shape as the ipc_io.hpp fixture but free of IPC links: this
/// test links only Mirage::persistence.
class TempDir {
  public:
    TempDir() {
        std::error_code error;
        root_ = std::filesystem::temp_directory_path(error) /
                ("mirage-persistence-" + std::to_string(::getpid()) + "-" +
                 std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        std::filesystem::create_directories(root_, error);
    }
    ~TempDir() {
        std::error_code error;
        std::filesystem::remove_all(root_, error);
    }
    TempDir(const TempDir &) = delete;
    TempDir &operator=(const TempDir &) = delete;

    [[nodiscard]] const std::filesystem::path &root() const { return root_; }

  private:
    std::filesystem::path root_;
};

void write_text_file(const std::filesystem::path &path, const std::string &content) {
    std::FILE *file = std::fopen(path.c_str(), "wb");
    if (file == nullptr) {
        return;
    }
    std::fwrite(content.data(), 1, content.size(), file);
    std::fclose(file);
}

mode_t file_mode(const std::filesystem::path &path) {
    struct stat info {};
    if (::stat(path.c_str(), &info) != 0) {
        return 0;
    }
    return info.st_mode & 0777;
}

// --- store -------------------------------------------------------------------

void scenario_store_save_load_round_trip() {
    TempDir dir;
    const persistence::LocalStateStore store(dir.root(), "state.json", 1024);
    const std::string body = "{\"hello\":\"mirage\",\"n\":42}";

    const persistence::SaveResult saved = store.save(body);
    MIRAGE_CHECK(saved.ok);
    MIRAGE_CHECK(saved.error.empty());
    MIRAGE_CHECK(store.file_path() == dir.root() / "state.json");

    const persistence::LoadResult loaded = store.load();
    MIRAGE_CHECK(loaded.status == persistence::LoadStatus::Loaded);
    MIRAGE_CHECK(loaded.body == body);
    MIRAGE_CHECK(loaded.error.empty());
}

void scenario_store_load_absent_without_file() {
    TempDir dir;
    const persistence::LocalStateStore store(dir.root(), "missing.json", 1024);
    const persistence::LoadResult loaded = store.load();
    MIRAGE_CHECK(loaded.status == persistence::LoadStatus::Absent);
    MIRAGE_CHECK(loaded.body.empty());
}

void scenario_store_save_overwrites_previous_content() {
    TempDir dir;
    const persistence::LocalStateStore store(dir.root(), "state.json", 1024);
    MIRAGE_CHECK(store.save("first generation").ok);
    MIRAGE_CHECK(store.save("second generation").ok);

    const persistence::LoadResult loaded = store.load();
    MIRAGE_CHECK(loaded.status == persistence::LoadStatus::Loaded);
    MIRAGE_CHECK(loaded.body == "second generation");
}

void scenario_store_refuses_over_budget_body_before_disk() {
    TempDir dir;
    const persistence::LocalStateStore store(dir.root(), "state.json", 16);
    // An accepted save first, so the refusal below must leave it intact.
    MIRAGE_CHECK(store.save("kept generation").ok);

    const persistence::SaveResult refused = store.save(std::string(17, 'x'));
    MIRAGE_CHECK(!refused.ok);
    MIRAGE_CHECK(refused.error.find("budget") != std::string::npos);

    const persistence::LoadResult loaded = store.load();
    MIRAGE_CHECK(loaded.status == persistence::LoadStatus::Loaded);
    MIRAGE_CHECK(loaded.body == "kept generation");

    // Exactly at the budget is still accepted (the bound is inclusive).
    MIRAGE_CHECK(store.save(std::string(16, 'y')).ok);
    MIRAGE_CHECK(store.load().body == std::string(16, 'y'));
}

void scenario_store_load_too_large_reports_without_truncation() {
    TempDir dir;
    const persistence::LocalStateStore store(dir.root(), "state.json", 32);
    // A file planted directly (bypassing save) above the budget.
    write_text_file(store.file_path(), std::string(64, 'z'));

    const persistence::LoadResult loaded = store.load();
    MIRAGE_CHECK(loaded.status == persistence::LoadStatus::TooLarge);
    MIRAGE_CHECK(loaded.body.empty()); // never silently truncated (RULE-07)
}

void scenario_store_reports_error_when_directory_is_a_file() {
    TempDir dir;
    const std::filesystem::path blocker = dir.root() / "blocker";
    write_text_file(blocker, "this is a file, not a directory");

    const persistence::LocalStateStore store(blocker, "state.json", 64);
    const persistence::SaveResult saved = store.save("body");
    MIRAGE_CHECK(!saved.ok);
    MIRAGE_CHECK(!saved.error.empty());
    MIRAGE_CHECK(saved.error.find("io_error") != std::string::npos);

    const persistence::LoadResult loaded = store.load();
    MIRAGE_CHECK(loaded.status == persistence::LoadStatus::IoError);
    MIRAGE_CHECK(!loaded.error.empty());
}

void scenario_store_creates_private_modes() {
    TempDir dir;
    // A multi-level missing chain plus a pre-existing sibling: only the
    // components the store creates may change, the rest stay untouched.
    const std::filesystem::path existing = dir.root() / "already-here";
    MIRAGE_CHECK(std::filesystem::create_directory(existing));
    const mode_t root_mode_before = file_mode(dir.root());
    const mode_t sibling_mode_before = file_mode(existing);

    const std::filesystem::path nested = dir.root() / "private" / "state";
    const persistence::LocalStateStore store(nested, "state.json", 64);

    MIRAGE_CHECK(store.save("secret").ok);
    // Every directory the store created is private regardless of the umask
    // (DEC-011 item 2) and the published file is owner-only.
    MIRAGE_CHECK(file_mode(dir.root() / "private") == 0700);
    MIRAGE_CHECK(file_mode(nested) == 0700);
    MIRAGE_CHECK(file_mode(store.file_path()) == 0600);
    // Directories that already existed keep their modes: the store must not
    // tighten (or loosen) the user's ~/.local-style ancestors or siblings.
    MIRAGE_CHECK(file_mode(dir.root()) == root_mode_before);
    MIRAGE_CHECK(file_mode(existing) == sibling_mode_before);
}

void scenario_store_failed_publish_keeps_previous_file() {
    TempDir dir;
    const persistence::LocalStateStore store(dir.root(), "state.json", 64);
    MIRAGE_CHECK(store.save("previous generation").ok);

    // Mid-save failure injection: the store's temp path is
    // "<file>.tmp.<pid>" (store_posix.cpp); planting a directory there makes
    // the exclusive temp creation fail after the directory check, which is
    // exactly the failure window the old file must survive. Same-process
    // test, so the pid is known.
    const std::filesystem::path temp =
        dir.root() / ("state.json.tmp." + std::to_string(static_cast<long>(::getpid())));
    MIRAGE_CHECK(std::filesystem::create_directory(temp));

    const persistence::SaveResult refused = store.save("new generation");
    MIRAGE_CHECK(!refused.ok);
    MIRAGE_CHECK(!refused.error.empty());

    const persistence::LoadResult loaded = store.load();
    MIRAGE_CHECK(loaded.status == persistence::LoadStatus::Loaded);
    MIRAGE_CHECK(loaded.body == "previous generation");

    // A successful save leaves no temp residue behind.
    std::filesystem::remove(temp);
    MIRAGE_CHECK(store.save("new generation").ok);
    int entries = 0;
    for (const auto &entry : std::filesystem::directory_iterator(dir.root())) {
        (void)entry;
        ++entries;
    }
    MIRAGE_CHECK(entries == 1);
}

// --- settings ----------------------------------------------------------------

void scenario_settings_round_trip_all_fields() {
    persistence::LocalSettings filled;
    filled.schema = persistence::kSettingsSchema;
    filled.socket_path = "/run/user/1000/mirage/custom.sock";
    filled.read_roots = {"/home/user/work", "/tmp/scratch"};
    filled.filesystem_read_rule = "allow";
    filled.filesystem_write_rule = "deny";
    filled.process_execute_rule = "confirm";
    filled.confirmation = "deny";

    const std::string encoded = persistence::encode_settings(filled);
    const persistence::SettingsDecode decoded = persistence::decode_settings(encoded);
    MIRAGE_CHECK(decoded.ok);
    if (!decoded.ok) {
        return;
    }
    MIRAGE_CHECK(decoded.settings.schema == persistence::kSettingsSchema);
    MIRAGE_CHECK(decoded.settings.socket_path == filled.socket_path);
    MIRAGE_CHECK(decoded.settings.read_roots == filled.read_roots);
    MIRAGE_CHECK(decoded.settings.filesystem_read_rule.value_or("?") == "allow");
    MIRAGE_CHECK(decoded.settings.filesystem_write_rule.value_or("?") == "deny");
    MIRAGE_CHECK(decoded.settings.process_execute_rule.value_or("?") == "confirm");
    MIRAGE_CHECK(decoded.settings.confirmation.value_or("?") == "deny");
}

void scenario_settings_empty_document_yields_defaults() {
    // A document without any override keeps every built-in default.
    const persistence::SettingsDecode decoded = persistence::decode_settings(R"({"schema":1})");
    MIRAGE_CHECK(decoded.ok);
    if (!decoded.ok) {
        return;
    }
    MIRAGE_CHECK(decoded.settings.socket_path.empty());
    MIRAGE_CHECK(decoded.settings.read_roots.empty());
    MIRAGE_CHECK(!decoded.settings.filesystem_read_rule.has_value());
    MIRAGE_CHECK(!decoded.settings.filesystem_write_rule.has_value());
    MIRAGE_CHECK(!decoded.settings.process_execute_rule.has_value());
    MIRAGE_CHECK(!decoded.settings.confirmation.has_value());
}

void scenario_settings_strict_decode_rejections() {
    struct Case {
        const char *body;
        const char *why;
    };
    const Case cases[] = {
        // Typo'd member: must fail loudly instead of running on defaults.
        {R"({"schema":1,"sock":"/tmp/x.sock"})", "unknown member"},
        {R"({"schema":1,"read_root":["/tmp"]})", "unknown member"},
        // Schema handling.
        {R"({"schema":2})", "future schema"},
        {R"({"socket":"/tmp/x.sock"})", "missing schema"},
        {R"({"schema":"1"})", "wrong schema type"},
        // Rule strings use the DEC-010 vocabulary only.
        {R"({"schema":1,"permission":{"process.execute":"maybe"}})", "bad rule string"},
        {R"({"schema":1,"permission":{"proces.execute":"allow"}})", "unknown capability"},
        {R"({"schema":1,"permission":"allow"})", "non-object permission"},
        {R"({"schema":1,"confirmation":"maybe"})", "bad confirmation"},
        {R"({"schema":1,"confirmation":true})", "wrong confirmation type"},
        // Bounds (DEC-011 item 3).
        {R"({"schema":1,"read_roots":"/tmp"})", "non-array read_roots"},
        {R"({"schema":1,"socket":42})", "non-string socket"},
        // Not JSON at all.
        {"not json", "invalid JSON"},
    };
    for (const Case &item : cases) {
        const persistence::SettingsDecode decoded = persistence::decode_settings(item.body);
        MIRAGE_CHECK(!decoded.ok);
        if (decoded.ok) {
            std::fprintf(stderr, "unexpected accept (%s): %s\n", item.why, item.body);
            continue;
        }
        MIRAGE_CHECK(!decoded.error.empty());
    }
}

void scenario_settings_rejects_oversized_values() {
    // 65 read roots: one beyond the 64-entry bound.
    {
        std::string body = R"({"schema":1,"read_roots":[)";
        for (std::size_t index = 0; index < 65; ++index) {
            if (index != 0) {
                body += ",";
            }
            body += "\"/root-" + std::to_string(index) + "\"";
        }
        body += "]}";
        const persistence::SettingsDecode decoded = persistence::decode_settings(body);
        MIRAGE_CHECK(!decoded.ok);
        MIRAGE_CHECK(decoded.error.find("64") != std::string::npos);
    }
    // 64 entries is inside the bound.
    {
        std::string body = R"({"schema":1,"read_roots":[)";
        for (std::size_t index = 0; index < 64; ++index) {
            if (index != 0) {
                body += ",";
            }
            body += "\"/root-" + std::to_string(index) + "\"";
        }
        body += "]}";
        MIRAGE_CHECK(persistence::decode_settings(body).ok);
    }
    // One path byte beyond the 4096-byte bound.
    {
        const std::string body =
            R"({"schema":1,"read_roots":["/)" + std::string(4096, 'p') + R"("]})";
        MIRAGE_CHECK(!persistence::decode_settings(body).ok);
    }
    {
        const std::string body = R"({"schema":1,"socket":"/)" + std::string(4096, 's') + R"("})";
        MIRAGE_CHECK(!persistence::decode_settings(body).ok);
    }
}

// --- recovery ----------------------------------------------------------------

persistence::RecoveryStep make_step(const std::string &kind, const std::string &argument,
                                    const std::string &status, const std::string &permission,
                                    bool ok, int exit_code, const std::string &result,
                                    const std::string &error) {
    persistence::RecoveryStep step;
    step.kind = kind;
    step.argument = argument;
    step.status = status;
    step.operation_id = "0123456789abcdef0123456789abcdef";
    step.permission = permission;
    step.ok = ok;
    step.exit_code = exit_code;
    step.result = result;
    step.result_truncated = false;
    step.error = error;
    return step;
}

persistence::RecoveryTask make_task(const std::string &id, const std::string &progress,
                                    std::vector<persistence::RecoveryStep> steps) {
    persistence::RecoveryTask task;
    task.id = id;
    task.goal = "goal for " + id;
    task.progress = progress;
    // Mirrors the driver's settlement semantics: Completed settles with a
    // success verdict, Failed settles with an explicit failure verdict,
    // Cancelled carries no verdict at all.
    task.has_success = progress != "Cancelled";
    task.success = progress == "Completed";
    task.steps = std::move(steps);
    return task;
}

void scenario_recovery_round_trip() {
    persistence::RecoveryState state;
    state.schema = persistence::kRecoverySchema;
    state.mirage_version = "0.7.0-test";
    state.saved_at = persistence::utc_timestamp_now();
    state.tasks.push_back(make_task(
        "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa", "Completed",
        {make_step("filesystem.read", "/tmp/a.txt", "ok", "allowed", true, -1, "file body", ""),
         make_step("process.execute", "printf hi", "ok", "allowed", true, 0, "hi", "")}));
    state.tasks.push_back(
        make_task("bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb", "Failed",
                  {make_step("filesystem.read", "/tmp/missing", "failed", "allowed", false, -1, "",
                             "not_found: no such file"),
                   make_step("process.execute", "true", "skipped", "", false, -1, "", "")}));
    state.tasks.push_back(make_task("cccccccccccccccccccccccccccccccc", "Cancelled", {}));

    const std::string encoded = persistence::encode_recovery(state);
    const persistence::RecoveryDecode decoded = persistence::decode_recovery(encoded);
    MIRAGE_CHECK(decoded.ok);
    if (!decoded.ok) {
        return;
    }
    MIRAGE_CHECK(decoded.state.schema == persistence::kRecoverySchema);
    MIRAGE_CHECK(decoded.state.mirage_version == "0.7.0-test");
    MIRAGE_CHECK(decoded.state.saved_at == state.saved_at);
    MIRAGE_CHECK(decoded.state.tasks.size() == 3);
    if (decoded.state.tasks.size() != 3) {
        return;
    }
    const persistence::RecoveryTask &completed = decoded.state.tasks[0];
    MIRAGE_CHECK(completed.id == "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
    MIRAGE_CHECK(completed.goal == "goal for aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
    MIRAGE_CHECK(completed.progress == "Completed");
    MIRAGE_CHECK(completed.has_success);
    MIRAGE_CHECK(completed.success);
    MIRAGE_CHECK(completed.steps.size() == 2);
    if (completed.steps.size() == 2) {
        MIRAGE_CHECK(completed.steps[0].kind == "filesystem.read");
        MIRAGE_CHECK(completed.steps[0].argument == "/tmp/a.txt");
        MIRAGE_CHECK(completed.steps[0].status == "ok");
        MIRAGE_CHECK(completed.steps[0].operation_id == "0123456789abcdef0123456789abcdef");
        MIRAGE_CHECK(completed.steps[0].permission == "allowed");
        MIRAGE_CHECK(completed.steps[0].ok);
        MIRAGE_CHECK(completed.steps[0].exit_code == -1);
        MIRAGE_CHECK(completed.steps[0].result == "file body");
        MIRAGE_CHECK(!completed.steps[0].result_truncated);
        MIRAGE_CHECK(completed.steps[0].error.empty());
        MIRAGE_CHECK(completed.steps[1].kind == "process.execute");
        MIRAGE_CHECK(completed.steps[1].exit_code == 0);
        MIRAGE_CHECK(completed.steps[1].result == "hi");
    }
    const persistence::RecoveryTask &failed = decoded.state.tasks[1];
    MIRAGE_CHECK(failed.progress == "Failed");
    MIRAGE_CHECK(failed.has_success);
    MIRAGE_CHECK(!failed.success);
    MIRAGE_CHECK(failed.steps.size() == 2);
    if (failed.steps.size() == 2) {
        MIRAGE_CHECK(failed.steps[0].status == "failed");
        MIRAGE_CHECK(failed.steps[0].error == "not_found: no such file");
        MIRAGE_CHECK(failed.steps[1].status == "skipped");
        MIRAGE_CHECK(failed.steps[1].permission.empty());
    }
    const persistence::RecoveryTask &cancelled = decoded.state.tasks[2];
    MIRAGE_CHECK(cancelled.progress == "Cancelled");
    MIRAGE_CHECK(cancelled.steps.empty());
}

void scenario_recovery_timestamp_shape() {
    const std::string stamp = persistence::utc_timestamp_now();
    MIRAGE_CHECK(stamp.size() == 20);
    if (stamp.size() != 20) {
        return;
    }
    const auto is_digit = [](char character) { return character >= '0' && character <= '9'; };
    for (const std::size_t position :
         {std::size_t{0}, std::size_t{1}, std::size_t{2}, std::size_t{3}, std::size_t{5},
          std::size_t{6}, std::size_t{8}, std::size_t{9}, std::size_t{11}, std::size_t{12},
          std::size_t{14}, std::size_t{15}, std::size_t{17}, std::size_t{18}}) {
        MIRAGE_CHECK(is_digit(stamp[position]));
    }
    MIRAGE_CHECK(stamp[4] == '-');
    MIRAGE_CHECK(stamp[7] == '-');
    MIRAGE_CHECK(stamp[10] == 'T');
    MIRAGE_CHECK(stamp[13] == ':');
    MIRAGE_CHECK(stamp[16] == ':');
    MIRAGE_CHECK(stamp[19] == 'Z');
}

void scenario_recovery_strict_decode_rejections() {
    struct Case {
        const char *body;
        const char *why;
    };
    const Case cases[] = {
        // Document level.
        {R"({"schema":2,"tasks":[]})", "future schema"},
        {R"({"tasks":[]})", "missing schema"},
        {R"({"schema":1,"tasks":[],"extra":1})", "unknown top-level member"},
        {R"({"schema":"1","tasks":[]})", "wrong schema type"},
        {"[]", "non-object document"},
        {"not json", "invalid JSON"},
        {R"({"schema":1})", "missing tasks"},
        {R"({"schema":1,"tasks":{}})", "non-array tasks"},
        // Task record level.
        {R"({"schema":1,"tasks":[{"id":"a","progress":"Running","steps":[]}]})",
         "progress outside the frozen vocabulary"},
        {R"({"schema":1,"tasks":[{"id":"a","progress":"completed","steps":[]}]})",
         "progress is case sensitive"},
        {R"({"schema":1,"tasks":[{"progress":"Completed","steps":[]}]})", "missing id"},
        {R"({"schema":1,"tasks":[{"id":"","progress":"Completed","steps":[]}]})", "empty id"},
        {R"({"schema":1,"tasks":[{"id":"a","progress":"Completed"}]})", "missing steps"},
        {R"({"schema":1,"tasks":[{"id":"a","progress":"Completed","steps":[],"x":1}]})",
         "unknown task member"},
        {R"({"schema":1,"tasks":[{"id":"a","progress":"Completed","steps":[],"has_success":"yes"}]})",
         "wrong has_success type"},
        // Duplicate identities cannot both be history.
        {R"({"schema":1,"tasks":[{"id":"a","progress":"Completed","steps":[]},)"
         R"({"id":"a","progress":"Failed","steps":[]}]})",
         "duplicate task id"},
        // Step record level.
        {R"({"schema":1,"tasks":[{"id":"a","progress":"Completed","steps":"
         R"[{"kind":"process.exec","status":"ok","permission":"","ok":true}]}]})",
         "step kind outside the vocabulary"},
        {R"({"schema":1,"tasks":[{"id":"a","progress":"Completed","steps":"
         R"[{"kind":"filesystem.read","status":"done","permission":"","ok":true}]}]})",
         "step status outside the vocabulary"},
        {R"({"schema":1,"tasks":[{"id":"a","progress":"Completed","steps":"
         R"[{"kind":"filesystem.read","status":"ok","permission":"maybe","ok":true}]}]})",
         "step permission outside the vocabulary"},
        {R"({"schema":1,"tasks":[{"id":"a","progress":"Completed","steps":"
         R"[{"kind":"filesystem.read","status":"ok","permission":""}]}]})",
         "missing ok flag"},
        {R"({"schema":1,"tasks":[{"id":"a","progress":"Completed","steps":"
         R"[{"kind":"filesystem.read","status":"ok","permission":"","ok":true,"exit":0}]}]})",
         "unknown step member"},
        {R"({"schema":1,"tasks":[{"id":"a","progress":"Completed","steps":"
         R"[{"kind":"filesystem.read","status":"ok","permission":"","ok":"true"}]}]})",
         "wrong ok type"},
        {R"({"schema":1,"tasks":[{"id":"a","progress":"Completed","steps":"
         R"[{"kind":"filesystem.read","status":"ok","permission":"","ok":true,"exit_code":"0"}]}]})",
         "wrong exit_code type"},
    };
    for (const Case &item : cases) {
        const persistence::RecoveryDecode decoded = persistence::decode_recovery(item.body);
        MIRAGE_CHECK(!decoded.ok);
        if (decoded.ok) {
            std::fprintf(stderr, "unexpected accept (%s): %s\n", item.why, item.body);
            continue;
        }
        MIRAGE_CHECK(!decoded.error.empty());
    }
}

void scenario_recovery_rejects_over_bound_counts() {
    // 1025 minimal task records: one beyond the 1024-entry bound.
    {
        std::string body = R"({"schema":1,"tasks":[)";
        for (std::size_t index = 0; index < 1025; ++index) {
            if (index != 0) {
                body += ",";
            }
            body += "{\"id\":\"task-" + std::to_string(index) +
                    "\",\"progress\":\"Completed\",\"steps\":[]}";
        }
        body += "]}";
        const persistence::RecoveryDecode decoded = persistence::decode_recovery(body);
        MIRAGE_CHECK(!decoded.ok);
        MIRAGE_CHECK(decoded.error.find("1024") != std::string::npos);
    }
    // 1024 records decode (the bound is inclusive).
    {
        std::string body = R"({"schema":1,"tasks":[)";
        for (std::size_t index = 0; index < 1024; ++index) {
            if (index != 0) {
                body += ",";
            }
            body += "{\"id\":\"task-" + std::to_string(index) +
                    "\",\"progress\":\"Completed\",\"steps\":[]}";
        }
        body += "]}";
        const persistence::RecoveryDecode decoded = persistence::decode_recovery(body);
        MIRAGE_CHECK(decoded.ok);
        if (decoded.ok) {
            MIRAGE_CHECK(decoded.state.tasks.size() == 1024);
        }
    }
    // 257 steps in one task: one beyond the 256-step bound.
    {
        std::string steps;
        for (std::size_t index = 0; index < 257; ++index) {
            if (index != 0) {
                steps += ",";
            }
            steps += R"({"kind":"filesystem.read","status":"ok","permission":"","ok":true})";
        }
        const std::string body =
            R"({"schema":1,"tasks":[{"id":"a","progress":"Completed","steps":[)" + steps + "]}]}";
        const persistence::RecoveryDecode decoded = persistence::decode_recovery(body);
        MIRAGE_CHECK(!decoded.ok);
        MIRAGE_CHECK(decoded.error.find("256") != std::string::npos);
    }
    // One step result beyond the 1 MiB bound.
    {
        const std::string body =
            R"({"schema":1,"tasks":[{"id":"a","progress":"Completed","steps":[)"
            R"({"kind":"filesystem.read","status":"ok","permission":"","ok":true,)"
            R"("result":")" +
            std::string(1024 * 1024 + 1, 'r') + R"("}]}]})";
        MIRAGE_CHECK(!persistence::decode_recovery(body).ok);
    }
}

// --- default directories -----------------------------------------------------

bool ends_with_mirage(const std::filesystem::path &path) {
    return !path.empty() && path.filename() == "mirage";
}

void scenario_default_directories_follow_xdg() {
    // Only shape assertions: the environment inside the test process decides
    // which branch resolves, but the leaf is always .../mirage.
    const std::filesystem::path config = persistence::default_config_directory();
    const std::filesystem::path state = persistence::default_state_directory();
    MIRAGE_CHECK(ends_with_mirage(config));
    MIRAGE_CHECK(ends_with_mirage(state));
    MIRAGE_CHECK(!config.empty());
    MIRAGE_CHECK(!state.empty());

    // A non-empty environment variable wins over the home-relative default.
    ::setenv("XDG_CONFIG_HOME", "/tmp/xdg-config-probe", 1);
    MIRAGE_CHECK(persistence::default_config_directory() == "/tmp/xdg-config-probe/mirage");
    ::setenv("XDG_STATE_HOME", "/tmp/xdg-state-probe", 1);
    MIRAGE_CHECK(persistence::default_state_directory() == "/tmp/xdg-state-probe/mirage");
    // An empty variable falls back to the home-relative default.
    ::setenv("XDG_CONFIG_HOME", "", 1);
    ::setenv("XDG_STATE_HOME", "", 1);
    const char *home = ::getenv("HOME");
    if (home != nullptr && *home != '\0') {
        MIRAGE_CHECK(persistence::default_config_directory() ==
                     std::filesystem::path(home) / ".config" / "mirage");
        MIRAGE_CHECK(persistence::default_state_directory() ==
                     std::filesystem::path(home) / ".local" / "state" / "mirage");
    }
    ::unsetenv("XDG_CONFIG_HOME");
    ::unsetenv("XDG_STATE_HOME");
}

void run_scenario(const char *name, void (*scenario)()) {
    std::fprintf(stderr, "[persistence_test] scenario: %s\n", name);
    scenario();
}

} // namespace

int main() {
    run_scenario("store_save_load_round_trip", scenario_store_save_load_round_trip);
    run_scenario("store_load_absent_without_file", scenario_store_load_absent_without_file);
    run_scenario("store_save_overwrites_previous_content",
                 scenario_store_save_overwrites_previous_content);
    run_scenario("store_refuses_over_budget_body_before_disk",
                 scenario_store_refuses_over_budget_body_before_disk);
    run_scenario("store_load_too_large_reports_without_truncation",
                 scenario_store_load_too_large_reports_without_truncation);
    run_scenario("store_reports_error_when_directory_is_a_file",
                 scenario_store_reports_error_when_directory_is_a_file);
    run_scenario("store_creates_private_modes", scenario_store_creates_private_modes);
    run_scenario("store_failed_publish_keeps_previous_file",
                 scenario_store_failed_publish_keeps_previous_file);
    run_scenario("settings_round_trip_all_fields", scenario_settings_round_trip_all_fields);
    run_scenario("settings_empty_document_yields_defaults",
                 scenario_settings_empty_document_yields_defaults);
    run_scenario("settings_strict_decode_rejections", scenario_settings_strict_decode_rejections);
    run_scenario("settings_rejects_oversized_values", scenario_settings_rejects_oversized_values);
    run_scenario("recovery_round_trip", scenario_recovery_round_trip);
    run_scenario("recovery_timestamp_shape", scenario_recovery_timestamp_shape);
    run_scenario("recovery_strict_decode_rejections", scenario_recovery_strict_decode_rejections);
    run_scenario("recovery_rejects_over_bound_counts", scenario_recovery_rejects_over_bound_counts);
    run_scenario("default_directories_follow_xdg", scenario_default_directories_follow_xdg);
    return mirage::testing::finish("persistence_test");
}
