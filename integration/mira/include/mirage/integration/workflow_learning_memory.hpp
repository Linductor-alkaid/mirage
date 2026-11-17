#pragma once

#include <memory>
#include <mira/memory_contracts.hpp>

namespace mirage::integration {
/// DEC-145/146: externally owned memory on the Workflow's Executor. Direct
/// Host use defaults to its primary Session; a trusted product owner may
/// explicitly select the private local-user Agent subject below.
/// Stop/drain Host before closing memory, then shut down the Executor last.
/// No product store, total quota or retention policy is installed here.
struct WorkflowLearningMemory {
    std::shared_ptr<mira::IMemory> memory;
    /// DEC-146: only a trusted product owner opts into a private per-OS-user
    /// Agent scope. Empty preserves DEC-145's primary Session isolation.
    std::string agent_subject_id;
};
} // namespace mirage::integration
