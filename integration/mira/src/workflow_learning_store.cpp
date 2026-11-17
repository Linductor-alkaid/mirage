#include <algorithm>
#include <mirage/integration/workflow_learning_store.hpp>

namespace mirage::integration {
namespace {
mira::Error closed() {
    mira::Error e;
    e.code = mira::ErrorCode::Unavailable;
    e.domain = "mirage.workflow.learning";
    e.safe_message = "workflow learning store is closed";
    return e;
}
} // namespace
class WorkflowLearningStore::Impl final : public mira::IMemory {
  public:
    std::unique_ptr<mira::SqliteMemoryStore> store;
    mira::Result<mira::MemoryQueryResult> query(const mira::MemoryQuery &q) const override {
        return store ? store->query(q) : mira::Result<mira::MemoryQueryResult>{closed()};
    }
    mira::Result<std::optional<mira::MemoryRecord>> get(mira::MemoryId id,
                                                        const mira::MemoryScope &s) const override {
        return store ? store->get(id, s)
                     : mira::Result<std::optional<mira::MemoryRecord>>{closed()};
    }
    mira::Result<mira::MemoryMutationResult> apply(const mira::MemoryMutation &m) override {
        return store ? store->apply(m) : mira::Result<mira::MemoryMutationResult>{closed()};
    }
    mira::Result<mira::MemoryCompactionResult> compact(const mira::MemoryScope &s) override {
        return store ? store->compact(s) : mira::Result<mira::MemoryCompactionResult>{closed()};
    }
    mira::Result<mira::ErasureResult> erase(const mira::ErasureRequest &r) override {
        return store ? store->erase(r) : mira::Result<mira::ErasureResult>{closed()};
    }
};
WorkflowLearningStore::WorkflowLearningStore(std::shared_ptr<Impl> impl) : impl_(std::move(impl)) {}
WorkflowLearningStore::~WorkflowLearningStore() {
    std::string ignored;
    close(ignored);
}
std::unique_ptr<WorkflowLearningStore>
WorkflowLearningStore::open(kairo::Executor &executor, const std::filesystem::path &directory,
                            std::uint64_t max_bytes, std::string &error) {
    error.clear();
    if (directory.empty() || !directory.is_absolute() || max_bytes < 1024 * 1024) {
        error = "invalid workflow learning directory or capacity";
        return {};
    }
    std::error_code ec;
    const auto status = std::filesystem::symlink_status(directory, ec);
    if ((!ec && std::filesystem::exists(status) && !std::filesystem::is_directory(status)) ||
        (ec && ec != std::errc::no_such_file_or_directory)) {
        error = "workflow learning directory is not private storage";
        return {};
    }
    ec.clear();
    std::filesystem::create_directories(directory, ec);
    if (!ec)
        std::filesystem::permissions(directory, std::filesystem::perms::owner_all,
                                     std::filesystem::perm_options::replace, ec);
    if (ec) {
        error = "workflow learning directory could not be prepared";
        return {};
    }
    mira::SqliteMemoryStoreOptions options;
    options.path = directory / "memory.sqlite";
    options.max_storage_bytes = max_bytes;
    options.capacity_reserve_bytes = std::min<std::uint64_t>(4 * 1024 * 1024, max_bytes / 4);
    options.max_pending_requests = 64;
    options.operation_timeout = std::chrono::seconds{2};
    options.busy_timeout = std::chrono::milliseconds{250};
    auto opened = mira::SqliteMemoryStore::open(executor, options);
    if (!opened) {
        error = "workflow learning storage open failed";
        return {};
    }
    auto impl = std::make_shared<Impl>();
    impl->store = std::move(opened.value());
    return std::unique_ptr<WorkflowLearningStore>(new WorkflowLearningStore(std::move(impl)));
}
std::shared_ptr<WorkflowLearningMemory> WorkflowLearningStore::binding() const {
    auto result = std::make_shared<WorkflowLearningMemory>();
    result->memory = impl_;
    result->agent_subject_id = "mirage.workflow.local-user.v1";
    return result;
}
mira::Result<mira::SqliteMemoryCapacitySnapshot> WorkflowLearningStore::capacity_snapshot() const {
    return impl_->store ? impl_->store->capacity_snapshot()
                        : mira::Result<mira::SqliteMemoryCapacitySnapshot>{closed()};
}
bool WorkflowLearningStore::close(std::string &error) {
    error.clear();
    if (!impl_->store)
        return true;
    const auto result = impl_->store->close();
    if (!result) {
        error = "workflow learning storage drain failed";
        return false;
    }
    impl_->store.reset();
    return true;
}
} // namespace mirage::integration
