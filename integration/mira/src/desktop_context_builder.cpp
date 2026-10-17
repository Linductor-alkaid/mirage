#include "desktop_context_builder.hpp"

#include <algorithm>
#include <map>
#include <string>
#include <vector>

namespace mirage::integration::detail {
namespace {
using mira::ContextAuthority;
using mira::ContextItem;
using mira::ContextItemDisposition;
using mira::ContextItemKind;
using mira::ContextLimits;
using mira::ContextPriority;
using mira::ContextRequest;

constexpr double kDesktopTrimWatermark = 0.45;
constexpr double kDesktopCheckpointWatermark = 0.70;
constexpr double kDesktopHardWatermark = 0.90;
// DEC-088: one full-resolution desktop screenshot plus margin; tool items in
// the compaction request carry no inline images, so the cap is nominal headroom.
constexpr std::uint64_t kDesktopImageBudgetTokens = 10'000;

// The call identity and kind of one canonical tool part; a history item can
// carry several (providers batch multiple calls into one assistant turn), so
// mapping and collapse work per part, never per item.
struct ToolSegmentKey {
    ContextItemKind kind;
    std::string call_key;
};
std::vector<ToolSegmentKey> tool_segment_keys(const mira::ModelInputItem &item) {
    std::vector<ToolSegmentKey> keys;
    keys.reserve(item.content.size());
    for (const auto &part : item.content) {
        if (const auto *call = std::get_if<mira::ToolCallPart>(&part)) {
            keys.push_back({ContextItemKind::ToolCall, call->provider_call_id.value});
        } else if (const auto *result = std::get_if<mira::ToolResultPart>(&part)) {
            keys.push_back({ContextItemKind::ToolResult, result->provider_call_id.value});
        }
    }
    return keys;
}

bool kept_disposition(ContextItemDisposition disposition) {
    return disposition == ContextItemDisposition::Selected ||
           disposition == ContextItemDisposition::SelectedByReference ||
           disposition == ContextItemDisposition::Compressed;
}
} // namespace

DesktopContextBuilder::DesktopContextBuilder(std::shared_ptr<const mira::IContextManager> manager,
                                             Limits limits)
    : manager_(std::move(manager)), limits_(limits) {}

mira::Result<std::vector<mira::ModelInputItem>>
DesktopContextBuilder::transform(const std::vector<mira::ModelInputItem> &history,
                                 const RequestKeys &keys) const {
    // Fail-open: without a declared window or without Layer 0 there is no
    // compaction, and the replay stays exactly what the loop would have sent.
    if (limits_.context_window_tokens == 0 || !manager_) {
        return history;
    }
    std::vector<ContextItem> items;
    items.reserve(history.size());
    for (std::size_t index = 0; index < history.size(); ++index) {
        // One Layer 0 item per tool part: pairing, watermark estimates and
        // the audit stay per provider_call_id even when a provider batched
        // several calls (or a call and text) into one history item. The
        // mapped item carries only its own part - the replay is rebuilt from
        // the original items, so nothing here renders anywhere.
        const auto &content = history[index].content;
        for (std::size_t part_index = 0; part_index < content.size(); ++part_index) {
            ToolSegmentKey segment;
            if (const auto *call = std::get_if<mira::ToolCallPart>(&content[part_index])) {
                segment = {ContextItemKind::ToolCall, call->provider_call_id.value};
            } else if (const auto *result =
                           std::get_if<mira::ToolResultPart>(&content[part_index])) {
                segment = {ContextItemKind::ToolResult, result->provider_call_id.value};
            } else {
                continue;
            }
            ContextItem item;
            item.id = mira::ContextItemId::generate();
            item.kind = segment.kind;
            item.authority =
                ContextAuthority::VerifiedState; // renders the Internal tier the loop uses
            item.priority = ContextPriority::Disposable;
            item.content.push_back(content[part_index]);
            item.consumed = true;                  // observed by a model turn; droppable in pairs
            item.replaceable_by_reference = false; // markers would break tool protocol
            item.tool_call_key = segment.call_key;
            item.sequence = index;
            items.push_back(std::move(item));
        }
    }
    if (items.empty()) {
        return history;
    }

    ContextLimits limits;
    limits.context_window_tokens = limits_.context_window_tokens;
    limits.reserved_output_tokens = limits_.reserved_output_tokens;
    limits.trim_watermark = kDesktopTrimWatermark;
    limits.checkpoint_watermark = kDesktopCheckpointWatermark;
    limits.hard_watermark = kDesktopHardWatermark;
    limits.max_image_tokens = kDesktopImageBudgetTokens;

    ContextRequest request;
    request.task_id = keys.task_id;
    request.session_id = keys.session_id;
    request.task_epoch = keys.task_epoch;
    request.profile_id = keys.profile_id;
    request.limits = limits;
    request.items = std::move(items);

    auto prepared = manager_->prepare(request);
    if (!prepared) {
        return prepared.error();
    }
    // The audit keeps request order, so dispositions map back onto the tool
    // items by index. A pair split by the checkpoint watermark collapses
    // whole: rendering a call without its result (or the reverse) would be
    // rejected by the provider dialects. Layer 0 can split between sibling
    // parts of one batched item (unequal payloads in the greedy pass, or the
    // strip stopping inside a shared sequence), so collapse runs to a
    // fixpoint: a multi-part item whose keys disagree drops whole, which
    // falsifies its surviving keys, which in turn drops their paired result
    // items - and so on until nothing orphaned remains.
    std::map<std::string, bool> pair_kept;
    const auto &audit = prepared.value().item_audit;
    for (std::size_t index = 0; index < request.items.size() && index < audit.size(); ++index) {
        const auto &key = *request.items[index].tool_call_key;
        const bool kept = kept_disposition(audit[index].disposition);
        const auto found = pair_kept.find(key);
        pair_kept[key] = found == pair_kept.end() ? kept : found->second && kept;
    }
    const auto item_whole = [&pair_kept](const mira::ModelInputItem &item) {
        // Bind first: begin/end must come from the same vector.
        const auto segment_keys = tool_segment_keys(item);
        return std::all_of(segment_keys.begin(), segment_keys.end(),
                           [&](const ToolSegmentKey &key) {
                               const auto kept = pair_kept.find(key.call_key);
                               return kept != pair_kept.end() && kept->second;
                           });
    };
    for (bool changed = true; changed;) {
        changed = false;
        for (const auto &item : history) {
            if (item_whole(item)) {
                continue;
            }
            for (const auto &key : tool_segment_keys(item)) {
                auto &kept = pair_kept[key.call_key];
                if (kept) {
                    kept = false;
                    changed = true;
                }
            }
        }
    }

    std::vector<mira::ModelInputItem> replay;
    replay.reserve(history.size());
    for (auto &item : history) {
        if (tool_segment_keys(item).empty() || item_whole(item)) {
            replay.push_back(item);
        }
    }
    return replay;
}

std::vector<mira::ModelInputItem>
retain_recent_groups(const std::vector<mira::ModelInputItem> &history, std::size_t max_items,
                     std::size_t max_bytes) {
    if (history.size() <= max_items && (max_bytes == 0 || history.empty())) {
        return history;
    }
    // Group starts: an item carrying a tool call opens a group; results and
    // trailing continuation text extend it. The first item always opens one.
    std::vector<std::size_t> starts;
    for (std::size_t index = 0; index < history.size(); ++index) {
        const auto carries_call = std::any_of(
            history[index].content.begin(), history[index].content.end(),
            [](const auto &part) { return std::holds_alternative<mira::ToolCallPart>(part); });
        if (carries_call || starts.empty()) {
            starts.push_back(index);
        }
    }
    // Per-group canonical input bytes under the pinned request codec; the
    // sum over retained groups upper-bounds the array serialization the loop
    // measures against max_history_bytes.
    std::vector<std::size_t> group_bytes(starts.size(), 0);
    if (max_bytes != 0) {
        for (std::size_t group = 0; group < starts.size(); ++group) {
            const auto group_end = group + 1 == starts.size() ? history.size() : starts[group + 1];
            mira::ModelRequest probe;
            probe.input.assign(history.begin() + static_cast<std::ptrdiff_t>(starts[group]),
                               history.begin() + static_cast<std::ptrdiff_t>(group_end));
            group_bytes[group] =
                mira::to_json_string(*mira::model_request_to_json(probe).find("input")).size();
        }
    }
    // Keep the longest group suffix that fits both bounds; the newest group
    // alone always fits the item bound (a per-turn batch is capped far below
    // it). A single group beyond the byte bound cannot exist for bounded
    // tool receipts — if one ever did, keeping it is the honest fail-loud
    // state: the loop re-measures exactly and settles with its explicit
    // capacity summary instead of a silent crop.
    std::size_t begin = starts.size();
    std::size_t count = 0;
    std::size_t bytes = 0;
    while (begin > 0) {
        const auto group_end = begin == starts.size() ? history.size() : starts[begin];
        const auto group_size = group_end - starts[begin - 1];
        if (count + group_size > max_items || bytes + group_bytes[begin - 1] > max_bytes) {
            break;
        }
        count += group_size;
        bytes += group_bytes[begin - 1];
        --begin;
    }
    return {history.begin() + static_cast<std::ptrdiff_t>(starts[begin]), history.end()};
}
} // namespace mirage::integration::detail
