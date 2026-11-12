#include "../support/test.hpp"
#include <kairo/executor.hpp>
#include <mira/model_digest.hpp>
#include <mira/sqlite_memory_store.hpp>
#include <mirage/integration/visual_memory.hpp>

#include <algorithm>
#include <filesystem>
#include <functional>
#include <iostream>
#include <limits>
#include <source_location>
#include <span>
#include <stdexcept>

namespace i = mirage::integration;
using Json = mira::JsonValue;
using Bytes = std::vector<std::byte>;
using namespace std::chrono_literals;
namespace {
class TempDirectory {
    std::filesystem::path root_ = std::filesystem::temp_directory_path() /
                                  ("mirage-visual-memory-" + mira::Id128::generate().to_string());

  public:
    TempDirectory() {
        if (!std::filesystem::create_directory(root_))
            throw std::runtime_error("cannot create private visual memory fixture");
        std::filesystem::permissions(root_, std::filesystem::perms::owner_all);
    }
    ~TempDirectory() {
        std::error_code ignored;
        std::filesystem::remove_all(root_, ignored);
    }
    const std::filesystem::path &path() const { return root_; }
};
class FailingStore final : public mira::IArtifactStore {
    static mira::Error failure() {
        mira::Error result;
        result.code = mira::ErrorCode::Unavailable;
        result.safe_message = "private fixture error body";
        return result;
    }

  public:
    mutable std::size_t opens = 0;
    mira::Result<mira::ArtifactWriter> begin(const mira::ArtifactWriteSpec &) override {
        return failure();
    }
    mira::Result<mira::ArtifactDescriptor> commit(mira::ArtifactWriter &) override {
        return failure();
    }
    mira::Result<mira::ArtifactReader> open(const mira::ArtifactDescriptor &) const override {
        ++opens;
        return failure();
    }
    mira::Result<mira::ErasureReceipt> erase(const mira::ArtifactErasureRequest &) override {
        return failure();
    }
};
template <class T>
T take(mira::Result<T> result, const std::source_location where = std::source_location::current()) {
    MIRAGE_CHECK(result);
    if (!result)
        throw std::runtime_error("visual memory fixture operation failed at " +
                                 std::to_string(where.line()) + " domain " + result.error().domain +
                                 " code " + std::to_string(static_cast<int>(result.error().code)));
    return std::move(result.value());
}
struct Fixture {
    mira::OperationContext ctx = mira::make_control_context();
    i::VisualMemoryLimits limits{1024, 8192, 5s, 120s};
    i::VisualActionEvidence input;
    Fixture() {
        ctx.task = mira::TaskId::generate();
        ctx.session = mira::SessionId::generate();
        ctx.task_epoch = 1;
        input.task = ctx.task;
        input.session = ctx.session;
        input.operation = ctx.operation;
        input.task_epoch = ctx.task_epoch;
        input.provenance = mira::EventId::generate();
        input.application = "private-fixture-app";
        input.page = "private-fixture-page";
        input.recorded_at = std::chrono::system_clock::now();
        input.expires_at = input.recorded_at + 1h;
        input.before.authority_epoch = input.after.authority_epoch = 2;
        input.before.environment_epoch = input.after.environment_epoch = 3;
        input.after.captured_at = std::chrono::steady_clock::now() - 10ms;
        input.before.captured_at = input.after.captured_at - 20ms;
        for (auto *frame : {&input.before.image, &input.after.image}) {
            frame->width = frame->height = 2;
            frame->stride = 12;
            frame->pixels.assign(24, 0xee); // Four padding bytes per row.
            frame->source_sequence = 42;
            for (std::size_t y = 0; y < 2; ++y)
                for (std::size_t x = 0; x < 8; ++x)
                    frame->pixels[y * 12 + x] = static_cast<std::uint8_t>(y * 8 + x);
        }
        input.after.image.pixels[0] = 77;
    }
    Bytes prepare() const { return take(i::prepare_visual_action_evidence(input, limits, ctx)); }
};
mira::ArtifactDescriptor commit(mira::IArtifactStore &store, const Bytes &bytes,
                                mira::Sensitivity sensitivity = mira::Sensitivity::Sensitive) {
    mira::ArtifactWriteSpec spec;
    spec.media_type = std::string(i::kVisualMemoryMediaType);
    spec.encoding = mira::ArtifactEncoding::Binary;
    spec.sensitivity = sensitivity;
    spec.content_schema = {1, 0};
    spec.max_bytes = bytes.size();
    auto writer = take(store.begin(spec));
    MIRAGE_CHECK(writer.write(bytes));
    return take(store.commit(writer));
}
void set(Json &object, const std::string &key, Json value) {
    auto fields = *object.as_object();
    for (auto &[name, item] : fields)
        if (name == key) {
            item = std::move(value);
            object = Json{std::move(fields)};
            return;
        }
    fields.emplace_back(key, std::move(value));
    object = Json{std::move(fields)};
}
Bytes edit_manifest(const Bytes &source, const std::function<void(Json &)> &edit,
                    bool canonical = true) {
    std::size_t size = 0;
    for (unsigned n = 0; n < 8; ++n)
        size |= static_cast<std::size_t>(std::to_integer<unsigned>(source[n])) << (8 * n);
    auto manifest = take(
        mira::parse_json(std::string(reinterpret_cast<const char *>(source.data() + 8), size)));
    edit(manifest);
    auto json = mira::canonical_json_string(manifest);
    if (!canonical)
        json.insert(0, " ");
    Bytes result;
    for (unsigned n = 0; n < 8; ++n)
        result.push_back(
            static_cast<std::byte>((static_cast<std::uint64_t>(json.size()) >> (8 * n)) & 255));
    const auto text = std::as_bytes(std::span(json));
    result.insert(result.end(), text.begin(), text.end());
    result.insert(result.end(), source.begin() + static_cast<std::ptrdiff_t>(8 + size),
                  source.end());
    return result;
}

void preparation_and_rejection() {
    Fixture f;
    const auto bytes = f.prepare();
    MIRAGE_CHECK(bytes == f.prepare());
    auto padded = f;
    padded.input.before.image.pixels[8] = 0x99;
    padded.input.before.image.source_sequence = 900;
    MIRAGE_CHECK(bytes == padded.prepare());
    auto exact = f;
    exact.limits.max_artifact_bytes = bytes.size();
    MIRAGE_CHECK(exact.prepare() == bytes);
    --exact.limits.max_artifact_bytes;
    MIRAGE_CHECK(!i::prepare_visual_action_evidence(exact.input, exact.limits, exact.ctx));
    const std::vector<std::function<void(Fixture &)>> edits{
        [](auto &x) { x.input.task = mira::TaskId::generate(); },
        [](auto &x) { x.input.session = mira::SessionId::generate(); },
        [](auto &x) { x.input.operation = mira::OperationId::generate(); },
        [](auto &x) { x.input.provenance = {}; },
        [](auto &x) { x.input.sensitivity = mira::Sensitivity::Internal; },
        [](auto &x) { x.input.sensitivity = mira::Sensitivity::Secret; },
        [](auto &x) { x.input.task_epoch = 0; },
        [](auto &x) { x.input.application = ""; },
        [](auto &x) { x.input.page = std::string(1025, 'x'); },
        [](auto &x) { x.input.page = std::string(1, static_cast<char>(0xff)); },
        [](auto &x) { x.input.page = "line\nbreak"; },
        [](auto &x) { x.input.recorded_at += 2h; },
        [](auto &x) { x.input.expires_at = x.input.recorded_at; },
        [](auto &x) { ++x.input.after.authority_epoch; },
        [](auto &x) { ++x.input.after.environment_epoch; },
        [](auto &x) { x.input.before.authority_epoch = x.input.after.authority_epoch = 0; },
        [](auto &x) { x.input.before.captured_at = {}; },
        [](auto &x) { x.input.after.captured_at = x.input.before.captured_at - 1ms; },
        [](auto &x) { x.input.after.captured_at += 1h; },
        [](auto &x) { x.input.before.captured_at -= 121s; },
        [](auto &x) { x.input.before.captured_at -= 6s; },
        [](auto &x) { x.input.before.image.width = 0; },
        [](auto &x) { x.input.before.image.height = -1; },
        [](auto &x) { x.input.before.image.stride = std::numeric_limits<std::size_t>::max(); },
        [](auto &x) { x.input.before.image.pixels.pop_back(); },
        [](auto &x) { x.limits.max_frame_bytes = 23; },
        [](auto &x) { x.limits.max_frame_bytes = 0; },
        [](auto &x) { x.limits.max_artifact_bytes = std::numeric_limits<std::size_t>::max(); },
        [](auto &x) { x.limits.max_pair_span = 0ms; },
        [](auto &x) { x.limits.max_capture_age = 121s; },
        [](auto &x) { x.ctx.cancellation_requested = [] { return true; }; },
        [](auto &x) { x.ctx.deadline = std::chrono::steady_clock::now() - 1ms; }};
    for (const auto &edit : edits) {
        auto changed = f;
        edit(changed);
        MIRAGE_CHECK(
            !i::prepare_visual_action_evidence(changed.input, changed.limits, changed.ctx));
    }
    auto during = f;
    int probes = 0;
    during.ctx.cancellation_requested = [&] { return ++probes >= 3; };
    const auto cancelled =
        i::prepare_visual_action_evidence(during.input, during.limits, during.ctx);
    MIRAGE_CHECK(!cancelled && cancelled.error().code == mira::ErrorCode::Cancelled);
}

void records_and_corruption() {
    Fixture f;
    const auto bytes = f.prepare();
    mira::MemoryArtifactStore store(1u << 20);
    const auto artifact = commit(store, bytes);
    const auto id = mira::MemoryId::generate();
    const auto record = take(i::make_visual_memory_record(id, artifact, store, f.limits, f.ctx));
    MIRAGE_CHECK(record.scope.kind == mira::MemoryScopeKind::Task);
    MIRAGE_CHECK(record.scope.subject_id == f.ctx.task.to_string());
    MIRAGE_CHECK(record.verification == mira::MemoryVerification::Observed);
    MIRAGE_CHECK(record.statement.find(f.input.page) == std::string::npos);
    const auto review = take(i::review_visual_memory(record, store, f.limits, f.ctx));
    FailingStore unavailable;
    const auto failed_read = i::review_visual_memory(record, unavailable, f.limits, f.ctx);
    MIRAGE_CHECK(!failed_read && failed_read.error().code == mira::ErrorCode::Unavailable);
    MIRAGE_CHECK(failed_read.error().safe_message == "visual_evidence_unavailable");
    auto stopped = f.ctx;
    stopped.cancellation_requested = [] { return true; };
    const auto stopped_read = i::review_visual_memory(record, unavailable, f.limits, stopped);
    MIRAGE_CHECK(!stopped_read && stopped_read.error().code == mira::ErrorCode::Cancelled);
    MIRAGE_CHECK(unavailable.opens == 1);
    MIRAGE_CHECK(review.before.stride == 8 && review.before.pixels.size() == 16);
    MIRAGE_CHECK(review.before.pixels[8] == 8 && review.after.pixels[0] == 77);
    MIRAGE_CHECK(review.before.source_sequence == 0 && review.after.source_sequence == 0);
    const auto metadata = mira::canonical_json_string(review.manifest);
    for (const auto *forbidden :
         {"@v", "@e", "scope", "authority_epoch", "environment_epoch", "source_sequence"})
        MIRAGE_CHECK(metadata.find(forbidden) == std::string::npos);
    const std::vector<std::function<void(mira::MemoryRecord &)>> edits{
        [](auto &x) { x.id = {}; },
        [](auto &x) { x.scope.subject_id = "other"; },
        [](auto &x) { x.scope.tenant_id = "other"; },
        [](auto &x) { x.kind = mira::MemoryKind::Procedure; },
        [](auto &x) { x.status = mira::MemoryStatus::Tombstoned; },
        [](auto &x) { x.verification = mira::MemoryVerification::Verified; },
        [](auto &x) { x.source_namespace = "import"; },
        [](auto &x) { x.statement += "changed"; },
        [](auto &x) { x.sensitivity = mira::Sensitivity::Internal; },
        [](auto &x) { x.evidence.reset(); },
        [](auto &x) { x.evidence->digest.bytes[0] ^= 1; },
        [](auto &x) { x.evidence->media_type = "image/png"; },
        [](auto &x) { x.evidence->byte_size = std::numeric_limits<std::uint64_t>::max(); },
        [](auto &x) { x.recorded_at += 1ms; },
        [](auto &x) { x.expires_at = *x.expires_at + 1ms; },
        [](auto &x) { x.provenance = {mira::EventId::generate()}; }};
    for (const auto &edit : edits) {
        auto changed = record;
        edit(changed);
        MIRAGE_CHECK(!i::review_visual_memory(changed, store, f.limits, f.ctx));
    }
    const std::vector<std::function<void(Json &)>> mutations{
        [](auto &x) { set(x, "schema", 2); },
        [](auto &x) { set(x, "schema", 1e100); },
        [](auto &x) { set(x, "sensitivity", "secret"); },
        [](auto &x) { set(x, "action_ref", "@v1"); },
        [](auto &x) { set(x, "task", mira::TaskId::generate().to_string()); },
        [](auto &x) { set(x, "session", "bad"); },
        [](auto &x) { set(x, "task_epoch", 0); },
        [](auto &x) { set(x, "outcome", "completed"); },
        [](auto &x) { set(x, "format", "png"); },
        [](auto &x) { set(x, "pair_span_ms", 5001); },
        [](auto &x) { set(x, "expires_at_ms", 1); },
        [](auto &x) { set(x, "before", Json::Object{{"width", 2}}); },
        [](auto &x) {
            auto before = *x.find("before");
            set(before, "width", std::numeric_limits<std::int64_t>::max());
            set(x, "before", before);
        },
        [](auto &x) {
            auto before = *x.find("before");
            set(before, "digest", std::string(64, '0'));
            set(x, "before", before);
        }};
    for (const auto &edit : mutations) {
        const auto changed = commit(store, edit_manifest(bytes, edit));
        MIRAGE_CHECK(!i::make_visual_memory_record(id, changed, store, f.limits, f.ctx));
    }
    auto trailing = bytes;
    trailing.push_back(std::byte{1});
    auto truncated = bytes;
    truncated.pop_back();
    auto wrong_length = bytes;
    wrong_length[7] = std::byte{0xff};
    auto pixel = bytes;
    pixel.back() ^= std::byte{1};
    for (const auto &changed :
         {trailing, truncated, wrong_length, pixel, edit_manifest(bytes, [](auto &) {}, false)}) {
        const auto stored = commit(store, changed);
        MIRAGE_CHECK(!i::make_visual_memory_record(id, stored, store, f.limits, f.ctx));
    }
    auto wrong_descriptor = artifact;
    wrong_descriptor.digest.bytes[0] ^= 1;
    MIRAGE_CHECK(!i::make_visual_memory_record(id, wrong_descriptor, store, f.limits, f.ctx));
    for (const auto sensitivity : {mira::Sensitivity::Internal, mira::Sensitivity::Public}) {
        mira::MemoryArtifactStore weak_store(16384);
        const auto weak = commit(weak_store, bytes, sensitivity);
        MIRAGE_CHECK(!i::make_visual_memory_record(id, weak, weak_store, f.limits, f.ctx));
    }
    mira::MemoryArtifactStore secret_store(16384);
    const auto secret_artifact = commit(secret_store, bytes, mira::Sensitivity::Secret);
    MIRAGE_CHECK(!i::make_visual_memory_record(id, secret_artifact, secret_store, f.limits, f.ctx));
    auto lowered = record;
    lowered.sensitivity = lowered.evidence->sensitivity = mira::Sensitivity::Internal;
    MIRAGE_CHECK(!i::review_visual_memory(lowered, store, f.limits, f.ctx));
    auto later_operation = f.ctx;
    later_operation.operation = mira::OperationId::generate();
    MIRAGE_CHECK(!i::make_visual_memory_record(id, artifact, store, f.limits, later_operation));
    MIRAGE_CHECK(i::review_visual_memory(record, store, f.limits, later_operation));
    later_operation.task = mira::TaskId::generate();
    MIRAGE_CHECK(!i::review_visual_memory(record, store, f.limits, later_operation));
    MIRAGE_CHECK(store.erase({artifact.id, "fixture cleanup"}));
    MIRAGE_CHECK(!i::review_visual_memory(record, store, f.limits, f.ctx));
}

void durable_store_reopen_and_erasure() {
    TempDirectory dir;
    Fixture f;
    const auto bytes = f.prepare();
    kairo::Executor executor;
    MIRAGE_CHECK(executor.initialize(kairo::ExecutorConfig{}));
    mira::SqliteMemoryStoreOptions options;
    options.path = dir.path() / "visual-memory.db";
    options.max_pending_requests = 4;
    mira::MemoryRecord saved;
    mira::ArtifactDescriptor artifact;
    {
        mira::FileArtifactStore artifacts(dir.path() / "artifacts", bytes.size());
        artifact = commit(artifacts, bytes);
        saved = take(i::make_visual_memory_record(mira::MemoryId::generate(), artifact, artifacts,
                                                  f.limits, f.ctx));
        auto memory = take(mira::SqliteMemoryStore::open(executor, options, &artifacts));
        mira::MemoryMutation mutation;
        mutation.id = mira::MutationId::generate();
        mutation.scope = saved.scope;
        mutation.proposed = saved;
        mutation.evidence = saved.provenance;
        take(memory->apply(mutation));
        const auto replayed = take(memory->apply(mutation));
        MIRAGE_CHECK(replayed.idempotent_replay);
        // Capacity refusal is owned by Mira, no private admission database.
        auto different = bytes;
        different.back() ^= std::byte{1};
        mira::ArtifactWriteSpec spec;
        spec.max_bytes = different.size();
        auto writer = take(artifacts.begin(spec));
        MIRAGE_CHECK(writer.write(different));
        MIRAGE_CHECK(!artifacts.commit(writer));
        MIRAGE_CHECK(artifacts.open(artifact));
        MIRAGE_CHECK(memory->close());
    }
    {
        mira::FileArtifactStore artifacts(dir.path() / "artifacts", bytes.size());
        auto memory = take(mira::SqliteMemoryStore::open(executor, options, &artifacts));
        const auto found = take(memory->get(saved.id, saved.scope));
        MIRAGE_CHECK(found.has_value());
        if (!found)
            throw std::runtime_error("visual memory durable record missing");
        const auto decoded = take(i::review_visual_memory(*found, artifacts, f.limits, f.ctx));
        MIRAGE_CHECK(decoded.after.pixels[0] == 77);
        auto wrong_scope = saved.scope;
        wrong_scope.subject_id = mira::TaskId::generate().to_string();
        MIRAGE_CHECK(!take(memory->get(saved.id, wrong_scope)).has_value());
        mira::MemoryQuery query;
        query.scopes = {saved.scope};
        query.kinds = std::vector{mira::MemoryKind::Episode};
        query.exact_terms = {"visual_action_evidence"};
        MIRAGE_CHECK(take(memory->query(query)).records.size() == 1);
        query.scopes = {wrong_scope};
        MIRAGE_CHECK(take(memory->query(query)).records.empty());
        mira::ErasureRequest erasure;
        erasure.scope = saved.scope;
        erasure.reason = "private fixture cleanup";
        const auto erased = take(memory->erase(erasure));
        MIRAGE_CHECK(erased.status == mira::ErasureStatus::Complete);
        MIRAGE_CHECK(!take(memory->get(saved.id, saved.scope)).has_value());
        MIRAGE_CHECK(!artifacts.open(artifact));
        MIRAGE_CHECK(!i::review_visual_memory(saved, artifacts, f.limits, f.ctx));
        MIRAGE_CHECK(memory->close());
    }
    {
        mira::FileArtifactStore artifacts(dir.path() / "artifacts", bytes.size());
        auto memory = take(mira::SqliteMemoryStore::open(executor, options, &artifacts));
        MIRAGE_CHECK(!take(memory->get(saved.id, saved.scope)).has_value());
        MIRAGE_CHECK(!artifacts.open(artifact));
        MIRAGE_CHECK(memory->close());
    }
    MIRAGE_CHECK(executor.shutdown(true) == kairo::ShutdownResult::Completed);
}
} // namespace

int main() {
    try {
        preparation_and_rejection();
        records_and_corruption();
        durable_store_reopen_and_erasure();
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
    return mirage::testing::finish("visual_memory_test");
}
