#pragma once

#include <filesystem>
#include <mira/sqlite_memory_store.hpp>
#include <mirage/integration/workflow_learning_memory.hpp>

namespace kairo {
class Executor;
}
namespace mirage::integration {
/// DEC-146: external process owner. Drain Host/Workflow before close(), and
/// close before Executor shutdown. The facade survives as a closed handle;
/// the SQLite connection and worker do not survive close().
class WorkflowLearningStore final {
  public:
    static std::unique_ptr<WorkflowLearningStore> open(kairo::Executor &,
                                                       const std::filesystem::path &directory,
                                                       std::uint64_t max_bytes,
                                                       std::string &safe_error);
    ~WorkflowLearningStore();
    std::shared_ptr<WorkflowLearningMemory> binding() const;
    mira::Result<mira::SqliteMemoryCapacitySnapshot> capacity_snapshot() const;
    bool close(std::string &safe_error);

  private:
    class Impl;
    explicit WorkflowLearningStore(std::shared_ptr<Impl>);
    std::shared_ptr<Impl> impl_;
};
} // namespace mirage::integration
