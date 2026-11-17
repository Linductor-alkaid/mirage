#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <mira/environment.hpp>
#include <mira/memory_contracts.hpp>
#include <mirage/desktop/screen_provider.hpp>
#include <string>
#include <string_view>
#include <vector>

namespace mirage::integration {

/// DEC-142 operation limits, explicitly supplied by the external owner.
/// These do not replace the owner's global storage/retention admission.
struct VisualMemoryLimits {
    std::size_t max_frame_bytes = 0;
    std::size_t max_artifact_bytes = 0;
    std::chrono::milliseconds max_pair_span{0};
    std::chrono::milliseconds max_capture_age{0};
};

struct VisualMemoryFrame {
    desktop::ImageFrame image;
    std::chrono::steady_clock::time_point captured_at{};
    std::uint64_t authority_epoch = 0;
    std::uint64_t environment_epoch = 0;
};

struct VisualActionEvidence {
    mira::SessionId session;
    mira::TaskId task;
    mira::OperationId operation;
    mira::EventId provenance;
    std::uint64_t task_epoch = 0;
    std::string application;
    std::string page;
    mira::Sensitivity sensitivity = mira::Sensitivity::Sensitive;
    std::chrono::system_clock::time_point recorded_at{}, expires_at{};
    VisualMemoryFrame before, after;
};

inline constexpr std::string_view kVisualMemoryMediaType =
    "application/vnd.mirage.visual-action-pair.v1";

/// Pure bounded preparation: no capture, input, Store write or memory apply.
/// The host owns the supplied observation/provenance claims. Padding is removed.
[[nodiscard]] mira::Result<std::vector<std::byte>>
prepare_visual_action_evidence(const VisualActionEvidence &, const VisualMemoryLimits &,
                               const mira::OperationContext &);

/// Bind a caller-committed Sensitive artifact to an Observed Task Episode.
/// Mira forbids Secret memory; declared Secret evidence is rejected, never downgraded.
/// Reads and verifies actual bytes before returning; the caller owns apply and
/// failed-admission cleanup. No inferred action success or preference approval.
[[nodiscard]] mira::Result<mira::MemoryRecord>
make_visual_memory_record(mira::MemoryId, const mira::ArtifactDescriptor &,
                          const mira::IArtifactStore &, const VisualMemoryLimits &,
                          const mira::OperationContext &);

struct VisualMemoryReview {
    mira::JsonValue manifest;
    desktop::ImageFrame before, after;
};

/// Revalidate a retrieved record and its complete artifact. Returned historical
/// pixels have no source_sequence, live references or authority for actions.
[[nodiscard]] mira::Result<VisualMemoryReview> review_visual_memory(const mira::MemoryRecord &,
                                                                    const mira::IArtifactStore &,
                                                                    const VisualMemoryLimits &,
                                                                    const mira::OperationContext &);

} // namespace mirage::integration
