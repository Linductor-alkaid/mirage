#include "../support/workflow_recovery_fixture.hpp"
#include <mira/sqlite_memory_store.hpp>
#include <mira/workflow_learning.hpp>

#include <filesystem>

namespace {
class Directory {
    std::filesystem::path root_ = std::filesystem::temp_directory_path() /
                                  ("mirage-host-learning-" + mira::Id128::generate().to_string());

  public:
    Directory() {
        if (!std::filesystem::create_directory(root_))
            throw std::runtime_error("cannot create learning fixture directory");
        std::filesystem::permissions(root_, std::filesystem::perms::owner_all);
    }
    ~Directory() {
        std::error_code ignored;
        std::filesystem::remove_all(root_, ignored);
    }
    std::filesystem::path database() const { return root_ / "memory.sqlite"; }
};

// Fault injection forwards to a real store. No second in-memory record map.
class Memory final : public mira::IMemory {
    Directory directory_;

    static mira::Error unavailable() {
        mira::Error e;
        e.code = mira::ErrorCode::Unavailable;
        e.domain = "fixture.memory";
        e.safe_message = "private-memory-backend-marker";
        return e;
    }

  public:
    std::unique_ptr<mira::SqliteMemoryStore> store;
    std::atomic<bool> fail_apply{false}, commit_then_fail{false}, fail_query{false}, hold{false};
    mutable std::atomic<bool> entered{false}, invalid_scope{false}, saw_lesson{false};
    mutable std::atomic<unsigned> queries{0};
    explicit Memory(kairo::Executor &executor) {
        mira::SqliteMemoryStoreOptions options;
        options.path = directory_.database();
        options.operation_timeout = 2s;
        store = take(mira::SqliteMemoryStore::open(executor, options));
    }
    ~Memory() override { MIRAGE_CHECK(store->close()); }
    const std::filesystem::path path() const { return directory_.database(); }
    mira::Result<mira::MemoryQueryResult> query(const mira::MemoryQuery &q) const override {
        ++queries;
        entered = true;
        if (q.scopes.size() != 1 || q.scopes[0].kind != mira::MemoryScopeKind::Session ||
            q.max_results > 8 || q.token_budget > 1024 || q.deadline > 250ms)
            invalid_scope = true;
        const auto end = std::chrono::steady_clock::now() + q.deadline;
        while (hold && std::chrono::steady_clock::now() < end)
            std::this_thread::sleep_for(1ms);
        if (fail_query || hold)
            return unavailable();
        auto result = store->query(q);
        if (result)
            for (const auto &record : result.value().records)
                if (record.kind == mira::MemoryKind::RecoveryLesson)
                    saw_lesson = true;
        return result;
    }
    mira::Result<std::optional<mira::MemoryRecord>>
    get(mira::MemoryId id, const mira::MemoryScope &scope) const override {
        return store->get(id, scope);
    }
    mira::Result<mira::MemoryMutationResult> apply(const mira::MemoryMutation &m) override {
        if (fail_apply)
            return unavailable();
        auto result = store->apply(m);
        if (result && commit_then_fail)
            return unavailable();
        return result;
    }
    mira::Result<mira::MemoryCompactionResult> compact(const mira::MemoryScope &s) override {
        return store->compact(s);
    }
    mira::Result<mira::ErasureResult> erase(const mira::ErasureRequest &r) override {
        return store->erase(r);
    }
};
std::shared_ptr<mira::IMemory> memory_factory(kairo::Executor &executor) {
    return std::make_shared<Memory>(executor);
}
mira::MemoryScope scope(const Fixture &f) {
    return {mira::MemoryScopeKind::Session, f.host.primary_session().id, std::nullopt};
}
std::vector<mira::EventEnvelope> events(const Fixture &f) {
    const auto page =
        f.events->read({*mira::SessionId::parse(f.host.primary_session().id), std::nullopt, 1000});
    if (!page)
        throw std::runtime_error("cannot read learning audit");
    return page.value().events;
}
std::optional<mira::WorkflowEpisodeRecordedEvent> episode_audit(const Fixture &f,
                                                                const std::string &run) {
    for (const auto &e : events(f)) {
        if (e.payload.type != "WorkflowEpisodeRecorded")
            continue;
        const auto parsed = mira::parse_workflow_episode_recorded(e.payload);
        if (!parsed)
            throw std::runtime_error("invalid learning audit");
        if (parsed.value().run_id.to_string() == run)
            return parsed.value();
    }
    return std::nullopt;
}
void await_episode(const Fixture &f, const Run &run, const std::string &outcome = "recorded") {
    until([&] {
        const auto audit = episode_audit(f, run.id);
        return audit && audit->outcome == outcome;
    });
}
mira::MemoryRecord record(Memory &memory, const Fixture &f, const Run &run, bool lesson = false) {
    auto stored = take(memory.get(lesson ? mira::recovery_lesson_memory_id(run.id)
                                         : mira::workflow_episode_memory_id(run.id),
                                  scope(f)));
    MIRAGE_CHECK(stored);
    if (!stored)
        throw std::runtime_error("expected learning record is absent");
    MIRAGE_CHECK(stored->scope == scope(f));
    MIRAGE_CHECK(stored->verification == mira::MemoryVerification::Verified);
    MIRAGE_CHECK(stored->statement.find("private-parameter-marker") == std::string::npos);
    MIRAGE_CHECK(stored->statement.find("private-rationale-marker") == std::string::npos);
    MIRAGE_CHECK(stored->statement.find("/fixture/") == std::string::npos);
    MIRAGE_CHECK(!stored->provenance.empty());
    return *stored;
}
r::WorkflowRecoveryLessonResult lesson(Fixture &f, const Run &run) {
    const auto result = f.host.record_workflow_recovery_lesson(run.id);
    MIRAGE_CHECK(result.ok);
    if (!result.ok)
        throw std::runtime_error("Host lesson rejected: " + result.error.message);
    MIRAGE_CHECK(result.run_id == run.id && !result.lesson_id.empty() && !result.digest.empty());
    return result;
}
void repair(Fixture &f, const Run &run) {
    f.provider->decision = patch(run.step);
    until([&] {
        const auto started = f.host.start_workflow_recovery(run.id);
        if (!started.ok && started.error.code != "invalid_state")
            throw std::runtime_error("learning recovery admission failed");
        return started.ok;
    });
    MIRAGE_CHECK(f.settled(run.id).outcome == "patched-and-resumed");
    f.state(run.id, "completed");
    await_episode(f, run);
}

void recovery_learning_and_reopen() {
    Fixture f({}, true, memory_factory);
    auto &memory = static_cast<Memory &>(*f.learning->memory);
    // Host owns its copied shared memory handle after the assembly binding changes.
    f.learning->memory.reset();
    const auto a = f.waiting();
    MIRAGE_CHECK(!f.host.record_workflow_recovery_lesson(a.id).ok);
    repair(f, a);
    const auto ep = record(memory, f, a);
    const auto episode =
        take(mira::workflow_episode_from_json(take(mira::parse_json(ep.statement))));
    MIRAGE_CHECK(episode.outcome == "completed" && episode.escalations == 1);
    MIRAGE_CHECK(!take(memory.get(mira::recovery_lesson_memory_id(a.id), scope(f))));
    const auto first = lesson(f, a);
    const auto stored = record(memory, f, a, true);
    const auto parsed = take(mira::recovery_lesson_from_record(stored));
    MIRAGE_CHECK(parsed.recovered_run_id == a.id && parsed.recovery.size() == 1);
    MIRAGE_CHECK(first.lesson_id == parsed.lesson_id);
    MIRAGE_CHECK(first.digest == mira::recovery_lesson_digest(parsed).to_string());
    const auto repeated = lesson(f, a);
    MIRAGE_CHECK(first.lesson_id == repeated.lesson_id && first.digest == repeated.digest);
    MIRAGE_CHECK(record(memory, f, a, true).version == stored.version);

    memory.entered = false;
    memory.hold = true;
    const auto b = f.start_same(a);
    f.state(b.id, "waiting_agent");
    until([&] { return memory.entered.load(); });
    const auto calls = f.provider->calls.load();
    const auto busy = f.host.start_workflow_recovery(b.id);
    MIRAGE_CHECK(!busy.ok && busy.error.code == "invalid_state");
    MIRAGE_CHECK(f.provider->calls == calls);
    memory.hold = false;
    repair(f, b);
    MIRAGE_CHECK(memory.saw_lesson && !memory.invalid_scope);
    MIRAGE_CHECK(f.provider->last_text.find("recovered_run_id") != std::string::npos);
    MIRAGE_CHECK(f.provider->last_text.find(a.id) != std::string::npos);
    bool kept = false;
    for (const auto &e : events(f)) {
        if (e.payload.type != "WorkflowRecoveryAttempted")
            continue;
        const auto audit = take(mira::parse_workflow_recovery_attempted(e.payload));
        if (audit.run_id.to_string() == b.id)
            kept = audit.lessons_kept > 0;
    }
    MIRAGE_CHECK(kept);
    const auto c = f.start_same(a);
    f.state(c.id, "waiting_agent");
    f.allow = false;
    f.provider->decision = patch(c.step);
    until([&] { return f.host.start_workflow_recovery(c.id).ok; });
    MIRAGE_CHECK(f.settled(c.id).outcome == "patched-and-resumed");
    f.state(c.id, "waiting_agent");
    MIRAGE_CHECK(f.files.successes == 2);
    MIRAGE_CHECK(!f.host.record_workflow_recovery_lesson(c.id).ok);

    const auto session_scope = scope(f);
    const mira::MemoryScope other{mira::MemoryScopeKind::Session,
                                  mira::SessionId::generate().to_string(), std::nullopt};
    MIRAGE_CHECK(!take(memory.get(mira::recovery_lesson_memory_id(a.id), other)));
    ok(f.host.shutdown_workflow_surface());
    const auto stopped = f.host.shutdown();
    MIRAGE_CHECK(stopped.ok && stopped.report.clean);
    MIRAGE_CHECK(!f.host.record_workflow_recovery_lesson(a.id).ok);
    MIRAGE_CHECK(memory.store->close());
    mira::SqliteMemoryStoreOptions options;
    options.path = memory.path();
    {
        auto reopened = take(mira::SqliteMemoryStore::open(f.executor.value, options));
        MIRAGE_CHECK(take(reopened->get(mira::workflow_episode_memory_id(a.id), session_scope)));
        MIRAGE_CHECK(take(reopened->get(mira::recovery_lesson_memory_id(a.id), session_scope)));
        MIRAGE_CHECK(!take(reopened->get(mira::recovery_lesson_memory_id(a.id), other)));
        mira::ErasureRequest erase;
        erase.scope = session_scope;
        erase.reason = "fixture privacy erasure";
        erase.include_artifacts = false;
        MIRAGE_CHECK(take(reopened->erase(erase)).status == mira::ErasureStatus::Complete);
        MIRAGE_CHECK(reopened->close());
    }
    auto reopened = take(mira::SqliteMemoryStore::open(f.executor.value, options));
    MIRAGE_CHECK(!take(reopened->get(mira::workflow_episode_memory_id(a.id), session_scope)));
    MIRAGE_CHECK(!take(reopened->get(mira::recovery_lesson_memory_id(a.id), session_scope)));
    MIRAGE_CHECK(reopened->close());
}

void terminals_failures_and_absent_context() {
    {
        Fixture f({}, true, memory_factory);
        auto &memory = static_cast<Memory &>(*f.learning->memory);
        for (const std::string policy : {"strict", "dry_run"}) {
            const auto run = f.start_new("/fixture/good", policy);
            f.state(run.id, "completed");
            if (policy == "strict") {
                await_episode(f, run);
                MIRAGE_CHECK(record(memory, f, run).kind == mira::MemoryKind::Episode);
            } else {
                MIRAGE_CHECK(!take(memory.get(mira::workflow_episode_memory_id(run.id), scope(f))));
            }
            MIRAGE_CHECK(!f.host.record_workflow_recovery_lesson(run.id).ok);
        }
        const auto failed = f.start_new("/fixture/missing", "strict");
        f.state(failed.id, "failed");
        await_episode(f, failed);
        const auto failed_ep = record(memory, f, failed);
        MIRAGE_CHECK(
            take(mira::workflow_episode_from_json(take(mira::parse_json(failed_ep.statement))))
                .outcome == "failed");
        const auto cancelled = f.waiting();
        MIRAGE_CHECK(f.host.cancel_workflow_run(cancelled.id).ok);
        await_episode(f, cancelled);
        MIRAGE_CHECK(take(mira::workflow_episode_from_json(
                              take(mira::parse_json(record(memory, f, cancelled).statement))))
                         .outcome == "cancelled");
        for (const auto &id : {failed.id, cancelled.id, std::string{"bad"},
                               mira::WorkflowRunId::generate().to_string(), std::string(32, '0')}) {
            const auto rejected = f.host.record_workflow_recovery_lesson(id);
            MIRAGE_CHECK(!rejected.ok && rejected.lesson_id.empty() && rejected.digest.empty() &&
                         rejected.run_id.empty());
        }
        memory.fail_apply = true;
        const auto write_failed = f.start_new("/fixture/good", "strict");
        f.state(write_failed.id, "completed");
        await_episode(f, write_failed, "failed");
        MIRAGE_CHECK(
            !take(memory.get(mira::workflow_episode_memory_id(write_failed.id), scope(f))));
        memory.fail_apply = false;
        const auto recovered = f.waiting();
        repair(f, recovered);
        memory.commit_then_fail = true;
        const auto ambiguous = f.host.record_workflow_recovery_lesson(recovered.id);
        MIRAGE_CHECK(!ambiguous.ok && ambiguous.digest.empty() && ambiguous.lesson_id.empty());
        MIRAGE_CHECK(ambiguous.error.message.find("private-memory-backend-marker") ==
                     std::string::npos);
        MIRAGE_CHECK(record(memory, f, recovered, true).version == 1);
        memory.commit_then_fail = false;
        lesson(f, recovered);
        MIRAGE_CHECK(record(memory, f, recovered, true).version == 1);
        memory.fail_query = true;
        const auto degraded = f.start_same(recovered);
        f.state(degraded.id, "waiting_agent");
        f.provider->decision = decision("need_user");
        until([&] { return f.host.start_workflow_recovery(degraded.id).ok; });
        MIRAGE_CHECK(f.settled(degraded.id).outcome == "deferred-to-host");
        MIRAGE_CHECK(f.provider->last_text.find("recovered_run_id") == std::string::npos);
        f.state(degraded.id, "waiting_agent");
        bool lesson_failed_audit = false;
        for (const auto &e : events(f)) {
            MIRAGE_CHECK(e.payload.data.find("private-memory-backend-marker") == std::string::npos);
            if (e.payload.type == "WorkflowLessonRecorded") {
                const auto audit = take(mira::parse_workflow_lesson_recorded(e.payload));
                if (audit.run_id.to_string() == recovered.id && audit.outcome == "failed")
                    lesson_failed_audit = !audit.reason_code.empty();
            }
        }
        MIRAGE_CHECK(lesson_failed_audit);
    }
    {
        Fixture f;
        const auto run = f.waiting();
        f.provider->decision = patch(run.step);
        f.start_recovery(run.id);
        f.settled(run.id);
        f.state(run.id, "completed");
        MIRAGE_CHECK(!f.host.record_workflow_recovery_lesson(run.id).ok);
        MIRAGE_CHECK(!episode_audit(f, run.id));
    }
    {
        ExecutorOwner executor;
        Files files;
        r::MiraHost host;
        ok(host.start(std::make_shared<Binding>()));
        const auto bridge = std::make_shared<i::WorkflowEventBridge>();
        const auto tools = i::DesktopAtomToolset::build(
            &files, [](const auto &, const auto &, const auto &) { return false; });
        const auto empty = std::make_shared<i::WorkflowLearningMemory>();
        MIRAGE_CHECK(!host.attach_workflow_surface(executor.value, bridge, tools, empty).ok);
        ok(host.attach_workflow_surface(executor.value, bridge, tools));
        MIRAGE_CHECK(!host.attach_workflow_surface(executor.value, bridge, tools, empty).ok);
        ok(host.shutdown_workflow_surface());
        const auto stopped = host.shutdown();
        MIRAGE_CHECK(stopped.ok && stopped.report.clean);
    }
}
} // namespace
int main() {
    try {
        recovery_learning_and_reopen();
        terminals_failures_and_absent_context();
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
    return mirage::testing::finish("workflow_learning_host_test");
}
