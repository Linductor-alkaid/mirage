#pragma once
#include <cstdint>
#include <map>
#include <mirage/runtime/persistence/store.hpp>
#include <string>
#include <vector>

namespace mirage::runtime::persistence {
inline constexpr std::size_t kWorkflowCatalogMaxEntries = 128;
inline constexpr std::size_t kWorkflowCatalogMaxBytes = 72 * 1024 * 1024;
struct StoredWorkflow {
    std::string head_json, head_digest, published_json, published_digest;
    bool head_published = false, callable = false;
    std::int64_t updated_at_ms = 0;
};
using WorkflowCatalog = std::map<std::string, StoredWorkflow>;
struct WorkflowCatalogLoad {
    bool ok = false;
    WorkflowCatalog entries;
    std::vector<std::string> publish_order;
    std::string error;
};
// Validates both IR bodies, content identities and the committed call DAG.
WorkflowCatalogLoad decode_workflow_catalog(const std::string &body,
                                            std::size_t capacity = kWorkflowCatalogMaxEntries);
std::string encode_workflow_catalog(const WorkflowCatalog &entries);
class WorkflowCatalogStore {
  public:
    explicit WorkflowCatalogStore(std::filesystem::path directory,
                                  std::size_t capacity = kWorkflowCatalogMaxEntries);
    WorkflowCatalogLoad load() const;
    SaveResult save(const WorkflowCatalog &entries) const;

  private:
    LocalStateStore store_;
    std::size_t capacity_;
};
} // namespace mirage::runtime::persistence
