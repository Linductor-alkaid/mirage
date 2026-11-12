#pragma once

#include <mira/context_memory_supervisor.hpp>
#include <mirage/integration/visual_memory.hpp>

namespace mirage::integration {

/// The attempted boundary, not an atomic rollback status. Commit/apply errors
/// may follow a write. Known committed evidence is Error::diagnostic_artifact.
enum class VisualMemoryWriteStage : std::int32_t {
    Preparation = 1,
    ArtifactWrite,
    ArtifactVerify,
    ProvenanceCommit,
    ProvenanceRead,
    MemoryApply,
};

struct VisualMemoryWriteResult {
    mira::ArtifactDescriptor artifact;
    mira::AppendReceipt provenance;
    mira::MemoryMutationResult mutation;
    bool completed_after_cancel = false;
};

/// DEC-143: finite Critical operation on the existing Mira supervisor. Borrowed
/// stores and cancellation-probe owner must outlive the consumed future and
/// supervisor drain. No store owner,
/// persistent quota, private queue, automatic retry or orphan deletion is added.
/// The host owns authorization, source truth and stable admission identities.
[[nodiscard]] std::future<mira::Result<VisualMemoryWriteResult>> schedule_visual_memory_write(
    mira::ContextMemorySupervisor &, mira::IEventStore &, mira::IArtifactStore &, mira::IMemory &,
    mira::RuntimeId, mira::MemoryId, mira::MutationId, std::shared_ptr<const VisualActionEvidence>,
    VisualMemoryLimits, mira::OperationContext);

} // namespace mirage::integration
