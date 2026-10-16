#pragma once
#include <mira/context_manager.hpp>
#include <mira/conversation_loop.hpp>

#include <cstdint>
#include <vector>

namespace mirage::integration::detail {

/// DEC-088 capability 2/3 (downstream ledger MIRA-20261014-006): routes the
/// canonical tool-history replay of one ConversationLoop through Mira's
/// Layer 0. Only the tool round-trip segment (ToolCall/ToolResult items)
/// enters the ContextRequest - assistant text rides verbatim because Layer
/// 0's renderer has no role-faithful kind for it - and the returned replay
/// keeps the loop's original items byte for byte: Layer 0 supplies the
/// watermark compaction decisions (pair-atomic selection), never the
/// rendering. Tool items carry `replaceable_by_reference = false` because a
/// reference marker would drop the ToolCallPart/ToolResultPart and break
/// provider tool semantics; compaction shrinks by dropping consumed pairs.
class DesktopContextBuilder final {
  public:
    struct Limits {
        // Declared window of the active model; 0 disables compaction (the
        // transform returns the history unchanged - fail-open, like the
        // supply-time degradation signal).
        std::uint64_t context_window_tokens = 0;
        std::uint64_t reserved_output_tokens = 0;
    };
    struct RequestKeys {
        mira::TaskId task_id;
        mira::SessionId session_id;
        std::uint64_t task_epoch = 0;
        mira::ModelProfileId profile_id;
    };
    DesktopContextBuilder(std::shared_ptr<const mira::IContextManager> manager, Limits limits);
    /// Compacts the tool segment of `history` under the desktop watermarks
    /// (trim 0.45 / checkpoint 0.70 / hard 0.90 of the input budget). A pair
    /// split by the checkpoint watermark collapses whole (both sides drop),
    /// which keeps the replay provider-valid. Layer 0 failures surface as
    /// errors; the loop's seam degrades them to the raw replay.
    [[nodiscard]] mira::Result<std::vector<mira::ModelInputItem>>
    transform(const std::vector<mira::ModelInputItem> &history, const RequestKeys &keys) const;

  private:
    std::shared_ptr<const mira::IContextManager> manager_;
    Limits limits_;
};

} // namespace mirage::integration::detail
