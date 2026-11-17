#include "../support/test.hpp"
#include <kairo/executor.hpp>
#include <mira/sqlite_memory_store.hpp>
#include <mirage/integration/visual_memory_write.hpp>

#include <filesystem>
#include <iostream>
#include <source_location>
#include <stdexcept>
#include <thread>

namespace i = mirage::integration;
using Stage = i::VisualMemoryWriteStage;
using namespace std::chrono_literals;
namespace {
template <class T>
T take(mira::Result<T> result, const std::source_location at = std::source_location::current()) {
    MIRAGE_CHECK(result);
    if (!result)
        throw std::runtime_error("write fixture failure at " + std::to_string(at.line()));
    return std::move(result.value());
}
mira::Error failure(mira::ErrorCode code = mira::ErrorCode::Unavailable) {
    mira::Error e;
    e.code = code;
    e.safe_message = "private injected storage error body";
    return e;
}
struct TempDirectory {
    std::filesystem::path path = std::filesystem::temp_directory_path() /
                                 ("mirage-visual-write-" + mira::Id128::generate().to_string());
    TempDirectory() {
        if (!std::filesystem::create_directory(path))
            throw std::runtime_error("cannot create write fixture");
        std::filesystem::permissions(path, std::filesystem::perms::owner_all);
    }
    ~TempDirectory() {
        std::error_code ec;
        std::filesystem::remove_all(path, ec);
    }
};
struct Artifacts final : mira::IArtifactStore {
    mira::IArtifactStore &real;
    std::string mode;
    std::function<void()> on_begin, on_commit;
    int commits = 0, erases = 0;
    explicit Artifacts(mira::IArtifactStore &value) : real(value) {}
    mira::Result<mira::ArtifactWriter> begin(const mira::ArtifactWriteSpec &s) override {
        if (on_begin)
            on_begin();
        if (mode == "begin")
            return failure();
        return real.begin(s);
    }
    mira::Result<mira::ArtifactDescriptor> commit(mira::ArtifactWriter &w) override {
        ++commits;
        if (mode == "throw")
            throw std::runtime_error("private injected exception body");
        if (mode == "commit")
            return failure();
        auto result = real.commit(w);
        if (mode == "uncertain")
            return failure(); // Real commit already happened.
        if (on_commit)
            on_commit();
        return result;
    }
    mira::Result<mira::ArtifactReader> open(const mira::ArtifactDescriptor &a) const override {
        if (mode == "verify")
            return failure();
        return real.open(a);
    }
    mira::Result<mira::ErasureReceipt> erase(const mira::ArtifactErasureRequest &r) override {
        ++erases;
        return real.erase(r);
    }
};
struct Events final : mira::IEventStore {
    mira::IEventStore &real;
    std::string mode;
    int appends = 0;
    mutable int reads = 0;
    explicit Events(mira::IEventStore &value) : real(value) {}
    mira::Result<mira::AppendReceipt> append(const mira::AppendRequest &r) override {
        ++appends;
        if (mode == "throw")
            throw std::runtime_error("private injected exception body");
        if (mode == "append")
            return failure();
        auto result = real.append(r);
        if (!result)
            return result.error();
        if (mode == "uncertain")
            return failure();
        if (mode == "buffered")
            result.value().achieved = mira::Durability::Buffered;
        if (mode == "receipt")
            result.value().session_sequence = 0;
        return result;
    }
    mira::Result<std::vector<mira::AppendReceipt>>
    append_batch(std::span<const mira::AppendRequest> r) override {
        return real.append_batch(r);
    }
    mira::Result<mira::EventPage> read(const mira::EventQuery &q) const override {
        ++reads;
        if (mode == "read")
            return failure();
        auto result = real.read(q);
        if (!result)
            return result.error();
        if (mode == "missing")
            result.value().events.clear();
        if (mode == "identity" && !result.value().events.empty())
            result.value().events[0].event_id = mira::EventId::generate();
        return result;
    }
    mira::Result<mira::StoreRecoveryReport> recover(const mira::RecoveryOptions &r) override {
        return real.recover(r);
    }
    mira::Result<void> flush(mira::Durability d) override { return real.flush(d); }
};
struct Memory final : mira::IMemory {
    mira::IMemory &real;
    std::string mode;
    std::function<void()> after_apply;
    int applies = 0;
    explicit Memory(mira::IMemory &value) : real(value) {}
    mira::Result<mira::MemoryMutationResult> apply(const mira::MemoryMutation &m) override {
        ++applies;
        if (mode == "apply")
            return failure(mira::ErrorCode::ResourceExhausted);
        if (mode == "throw")
            throw std::runtime_error("private injected exception body");
        auto result = real.apply(m);
        if (!result)
            return result.error();
        if (after_apply)
            after_apply();
        if (mode == "receipt")
            result.value().record = mira::MemoryId::generate();
        return result;
    }
    mira::Result<mira::MemoryQueryResult> query(const mira::MemoryQuery &q) const override {
        return real.query(q);
    }
    mira::Result<std::optional<mira::MemoryRecord>> get(mira::MemoryId id,
                                                        const mira::MemoryScope &s) const override {
        return real.get(id, s);
    }
    mira::Result<mira::MemoryCompactionResult> compact(const mira::MemoryScope &s) override {
        return real.compact(s);
    }
    mira::Result<mira::ErasureResult> erase(const mira::ErasureRequest &r) override {
        return real.erase(r);
    }
};
struct ExecutorOwner {
    kairo::Executor value;
    ExecutorOwner() { MIRAGE_CHECK(value.initialize(kairo::ExecutorConfig{})); }
    ~ExecutorOwner() { MIRAGE_CHECK(value.shutdown(true) == kairo::ShutdownResult::Completed); }
};
struct Harness {
    ExecutorOwner executor; // Declared first: shutdown after every borrowed Store is destroyed.
    mira::FileArtifactStore artifacts;
    mira::FileEventStore events;
    std::unique_ptr<mira::SqliteMemoryStore> sqlite;
    std::unique_ptr<mira::ContextMemorySupervisor> supervisor;
    std::unique_ptr<Memory> memory;
    Artifacts a;
    Events e;
    mira::RuntimeId runtime = mira::RuntimeId::generate();
    mira::MemoryId id;
    mira::MutationId mutation;
    mira::OperationContext ctx;
    i::VisualMemoryLimits limits{1024, 8192, 5s, 120s};
    std::shared_ptr<const i::VisualActionEvidence> input;
    bool cancelled = false;
    explicit Harness(const std::filesystem::path &root, std::size_t capacity = 2)
        : artifacts(root / "artifacts", 1u << 20), events(root / "events", 64), a(artifacts),
          e(events) {
        mira::SqliteMemoryStoreOptions options;
        options.path = root / "memory.db";
        sqlite = take(mira::SqliteMemoryStore::open(executor.value, options, &artifacts));
        memory = std::make_unique<Memory>(*sqlite);
        mira::SupervisorConfig config;
        config.max_in_flight = capacity;
        config.critical_drain_timeout = 5s;
        supervisor = std::make_unique<mira::ContextMemorySupervisor>(executor.value, config);
        reset();
    }
    void reset() {
        a.mode.clear();
        a.on_begin = {};
        a.on_commit = {};
        a.commits = a.erases = 0;
        e.mode.clear();
        e.appends = e.reads = 0;
        memory->mode.clear();
        memory->after_apply = {};
        memory->applies = 0;
        cancelled = false;
        id = mira::MemoryId::generate();
        mutation = mira::MutationId::generate();
        ctx = mira::make_control_context();
        ctx.task = mira::TaskId::generate();
        ctx.session = mira::SessionId::generate();
        ctx.task_epoch = 1;
        ctx.cancellation_requested = [this] { return cancelled; };
        auto value = std::make_shared<i::VisualActionEvidence>();
        value->session = ctx.session;
        value->task = ctx.task;
        value->operation = ctx.operation;
        value->provenance = mira::EventId::generate();
        value->task_epoch = 1;
        value->application = "private app";
        value->page = "private page";
        value->recorded_at = std::chrono::system_clock::now();
        value->expires_at = value->recorded_at + 1h;
        value->before.authority_epoch = value->after.authority_epoch = 1;
        value->before.environment_epoch = value->after.environment_epoch = 1;
        value->before.captured_at = std::chrono::steady_clock::now() - 2ms;
        value->after.captured_at = std::chrono::steady_clock::now() - 1ms;
        for (auto *frame : {&value->before.image, &value->after.image}) {
            frame->width = frame->height = 1;
            frame->stride = 4;
            frame->pixels = {1, 2, 3, 255};
        }
        input = std::move(value);
    }
    auto submit() {
        return i::schedule_visual_memory_write(*supervisor, e, a, *memory, runtime, id, mutation,
                                               input, limits, ctx);
    }
    ~Harness() {
        MIRAGE_CHECK(supervisor->begin_shutdown().critical_drain_complete);
        MIRAGE_CHECK(sqlite->close());
        supervisor.reset();
        memory.reset();
        sqlite.reset();
    }
};

void writes_and_faults() {
    TempDirectory dir;
    Harness f(dir.path);
    struct Case {
        std::string where, mode;
        Stage stage;
        bool known;
    };
    const std::vector<Case> cases{{"artifact", "begin", Stage::ArtifactWrite, false},
                                  {"artifact", "commit", Stage::ArtifactWrite, false},
                                  {"artifact", "throw", Stage::ArtifactWrite, false},
                                  {"artifact", "uncertain", Stage::ArtifactWrite, false},
                                  {"artifact", "verify", Stage::ArtifactVerify, true},
                                  {"event", "append", Stage::ProvenanceCommit, true},
                                  {"event", "uncertain", Stage::ProvenanceCommit, true},
                                  {"event", "throw", Stage::ProvenanceCommit, true},
                                  {"event", "buffered", Stage::ProvenanceCommit, true},
                                  {"event", "receipt", Stage::ProvenanceCommit, true},
                                  {"event", "read", Stage::ProvenanceRead, true},
                                  {"event", "missing", Stage::ProvenanceRead, true},
                                  {"event", "identity", Stage::ProvenanceRead, true},
                                  {"memory", "apply", Stage::MemoryApply, true},
                                  {"memory", "throw", Stage::MemoryApply, true},
                                  {"memory", "receipt", Stage::MemoryApply, true}};
    for (const auto &c : cases) {
        f.reset();
        if (c.where == "artifact")
            f.a.mode = c.mode;
        if (c.where == "event")
            f.e.mode = c.mode;
        if (c.where == "memory")
            f.memory->mode = c.mode;
        const auto before = f.supervisor->stats().failed;
        const auto result = f.submit().get();
        MIRAGE_CHECK(!result);
        if (result)
            throw std::runtime_error("unexpected successful fault injection");
        MIRAGE_CHECK(result.error().domain == "mirage.visual_memory.write");
        MIRAGE_CHECK(result.error().domain_code == static_cast<std::int32_t>(c.stage));
        MIRAGE_CHECK(result.error().diagnostic_artifact.has_value() == c.known);
        MIRAGE_CHECK(result.error().operation_id == f.ctx.operation);
        MIRAGE_CHECK(result.error().safe_message.find("private") == std::string::npos);
        MIRAGE_CHECK(f.supervisor->stats().failed == before + 1);
        MIRAGE_CHECK(f.a.erases == 0);
        if (c.where != "memory")
            MIRAGE_CHECK(f.memory->applies == 0);
        const auto record = take(f.sqlite->get(
            f.id, {mira::MemoryScopeKind::Task, f.ctx.task.to_string(), std::nullopt}));
        MIRAGE_CHECK(record.has_value() == (c.where == "memory" && c.mode == "receipt"));
    }
    f.reset();
    f.input.reset();
    const auto invalid = f.submit().get();
    MIRAGE_CHECK(!invalid && invalid.error().code == mira::ErrorCode::InvalidArgument);
    MIRAGE_CHECK(f.a.commits == 0 && f.e.appends == 0 && f.memory->applies == 0);
    f.reset();
    f.cancelled = true;
    const auto queued_cancel = f.submit().get();
    MIRAGE_CHECK(!queued_cancel && queued_cancel.error().code == mira::ErrorCode::Cancelled);
    MIRAGE_CHECK(f.a.commits == 0);
    f.reset();
    f.a.on_commit = [&] { f.cancelled = true; };
    const auto running_cancel = f.submit().get();
    MIRAGE_CHECK(!running_cancel && running_cancel.error().code == mira::ErrorCode::Cancelled);
    MIRAGE_CHECK(running_cancel.error().diagnostic_artifact.has_value());
    MIRAGE_CHECK(f.e.appends == 0 && f.memory->applies == 0 && f.a.erases == 0);
    f.reset();
    f.ctx.deadline = std::chrono::steady_clock::now() - 1ms;
    const auto expired = f.submit().get();
    MIRAGE_CHECK(!expired && expired.error().code == mira::ErrorCode::DeadlineExceeded);
    f.reset();
    f.ctx.deadline = std::chrono::steady_clock::now() + 100ms;
    f.a.on_commit = [] {
        std::this_thread::sleep_for(150ms);
    }; // Controlled I/O delay on the supervised worker.
    const auto during_io = f.submit().get();
    MIRAGE_CHECK(!during_io && during_io.error().code == mira::ErrorCode::DeadlineExceeded);
    MIRAGE_CHECK(f.e.appends == 0 && f.memory->applies == 0);
    f.reset();
    f.memory->after_apply = [&] { f.cancelled = true; };
    const auto late = take(f.submit().get());
    MIRAGE_CHECK(late.completed_after_cancel && late.mutation.record == f.id);
    MIRAGE_CHECK(take(f.sqlite->get(f.id, {mira::MemoryScopeKind::Task, f.ctx.task.to_string(),
                                           std::nullopt}))
                     .has_value());
}

void capacity_and_drain() {
    TempDirectory dir;
    Harness f(dir.path, 1);
    f.a.on_begin = [&] {
        const auto rejected =
            f.submit().get(); // At capacity: resolves without dispatching another worker.
        MIRAGE_CHECK(!rejected && rejected.error().code == mira::ErrorCode::ResourceExhausted);
    };
    take(f.submit().get());
    MIRAGE_CHECK(f.supervisor->stats().rejected_capacity == 1);
    f.reset();
    auto accepted = f.submit();
    const auto drained = f.supervisor->begin_shutdown();
    MIRAGE_CHECK(drained.critical_drain_complete);
    take(accepted.get());
    const auto closed = f.submit().get();
    MIRAGE_CHECK(!closed && closed.error().code == mira::ErrorCode::Unavailable);
    MIRAGE_CHECK(f.supervisor->stats().rejected_closed == 1);
}

void durable_sources_and_capacity_gap() {
    TempDirectory dir;
    mira::MemoryId id;
    mira::MemoryScope scope;
    mira::OperationContext context;
    i::VisualMemoryWriteResult receipt;
    {
        Harness f(dir.path);
        id = f.id;
        scope = {mira::MemoryScopeKind::Task, f.ctx.task.to_string(), std::nullopt};
        context = f.ctx;
        receipt = take(f.submit().get());
        MIRAGE_CHECK(receipt.provenance.achieved != mira::Durability::Buffered);
        const auto events =
            take(f.events.read({context.session, receipt.provenance.session_sequence - 1, 1}));
        MIRAGE_CHECK(events.events.size() == 1 && events.events[0].event_id == f.input->provenance);
        MIRAGE_CHECK(events.events[0].payload.data.find("private") == std::string::npos);
        context.cancellation_requested =
            {}; // Do not retain the fixture owner's probe across destruction.
    }
    {
        Harness f(dir.path);
        const auto record = take(f.sqlite->get(id, scope));
        MIRAGE_CHECK(record.has_value());
        if (!record)
            throw std::runtime_error("durable visual memory missing");
        take(i::review_visual_memory(*record, f.artifacts, f.limits, context));
        const auto source =
            take(f.events.read({context.session, receipt.provenance.session_sequence - 1, 1}));
        MIRAGE_CHECK(source.events.size() == 1 &&
                     source.events[0].event_id == receipt.provenance.event_id);
        MIRAGE_CHECK(source.events[0].timestamp_origin ==
                     mira::EventTimestampOrigin::PersistedWall);
    }
    // MIRA-20261112-001: measure only an owned fixture tree through filesystem APIs.
    // A one-byte payload cap does not bound retained metadata after repeated erase.
    const auto root = dir.path / "capacity-probe";
    mira::FileArtifactStore artifacts(root, 1);
    const auto occupied = [&] {
        std::uintmax_t bytes = 0;
        for (const auto &entry : std::filesystem::recursive_directory_iterator(root))
            if (entry.is_regular_file())
                bytes += entry.file_size();
        return bytes;
    };
    std::uintmax_t midpoint = 0;
    for (unsigned n = 0; n < 16; ++n) {
        mira::ArtifactWriteSpec spec;
        spec.max_bytes = 1;
        auto writer = take(artifacts.begin(spec));
        MIRAGE_CHECK(writer.write(std::vector{static_cast<std::byte>(n)}));
        const auto committed = take(artifacts.commit(writer));
        MIRAGE_CHECK(artifacts.erase({committed.id, "private capacity probe"}));
        MIRAGE_CHECK(!artifacts.open(committed));
        if (n == 7)
            midpoint = occupied();
    }
    const auto final_bytes = occupied();
    MIRAGE_CHECK(midpoint > 1 && final_bytes > midpoint);
    std::cout << "Pinned artifact capacity probe: payload cap=1 byte; after 8 erasures=" << midpoint
              << "; after 16 erasures=" << final_bytes << " bytes\n";
}
} // namespace
int main() {
    try {
        writes_and_faults();
        capacity_and_drain();
        durable_sources_and_capacity_gap();
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
    return mirage::testing::finish("visual_memory_write_test");
}
