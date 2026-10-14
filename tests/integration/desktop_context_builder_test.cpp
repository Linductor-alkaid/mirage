#include "../../integration/mira/src/desktop_context_builder.hpp"
#include "../support/fake_desktop_environment.hpp"
#include "../support/provider_tool_name.hpp"
#include <algorithm>
#include <iostream>
#include <kairo/executor.hpp>
#include <map>
#include <mira/context_manager.hpp>
#include <mira/json.hpp>
#include <mira/model_digest.hpp>
#include <mira/model_provider.hpp>
#include <mirage/desktop/action_coordinator.hpp>
#include <mirage/integration/model_layer.hpp>
#include <source_location>
#include <stdexcept>
#include <string>
#include <vector>

// DEC-088 capability 2/3 (MIRA-20261014-006): verification tests for the
// DesktopContextBuilder mapping and its ConversationLoop seam wiring. The
// builder unit tests drive the pure transform directly (real Layer 0 for the
// watermark behaviour, a scripted IContextManager for the pair-collapse and
// error surfaces); the harness tests drive ModelLayer.complete_harness_turn
// over a scripted provider and a fake desktop: a declared window with
// headroom (seam applies, replay stays whole), a declared window under
// watermark pressure (early pairs leave, requests stay provider-valid), no
// declared window (byte-faithful replay, zero transforms), and a window that
// cannot fund an input budget (honest visible degradation).

namespace detail = mirage::integration::detail;
namespace integration = mirage::integration;

namespace {
int checks = 0;
void check(bool condition, const std::source_location location = std::source_location::current()) {
    ++checks;
    if (!condition)
        throw std::runtime_error("desktop context builder check failed: " + std::to_string(checks) +
                                 " at line " + std::to_string(location.line()));
}

mira::OperationContext context() {
    auto ctx = mira::make_control_context();
    ctx.task = mira::TaskId::generate();
    ctx.session = mira::SessionId::generate();
    ctx.task_epoch = 7;
    return ctx;
}

// ---------------------------------------------------------------------------
// History item construction and structural comparison
// ---------------------------------------------------------------------------

mira::ModelInputItem text_item(mira::ModelRole role, std::string text) {
    mira::ModelInputItem item;
    item.role = role;
    item.authority = mira::Sensitivity::Internal;
    mira::TextPart part;
    part.text = std::move(text);
    part.sensitivity = mira::Sensitivity::Internal;
    item.content.emplace_back(std::move(part));
    return item;
}

mira::ModelInputItem call_item(const std::string &call_id, const std::string &args_json) {
    mira::ModelInputItem item;
    item.role = mira::ModelRole::Assistant;
    mira::ToolCallPart part;
    part.provider_call_id.value = call_id;
    part.wire_name = "desktop_screen_capture";
    part.arguments = mira::parse_json(args_json).value();
    part.arguments_digest = mira::canonical_json_digest(part.arguments);
    item.content.emplace_back(std::move(part));
    return item;
}

mira::ModelInputItem result_item(const std::string &call_id, const std::string &result_json) {
    mira::ModelInputItem item;
    item.role = mira::ModelRole::User;
    mira::ToolResultPart part;
    part.provider_call_id.value = call_id;
    part.result = mira::parse_json(result_json).value();
    item.content.emplace_back(std::move(part));
    return item;
}

const mira::ToolCallPart *call_part(const mira::ModelInputItem &item) {
    for (const auto &part : item.content)
        if (const auto *call = std::get_if<mira::ToolCallPart>(&part))
            return call;
    return nullptr;
}

const mira::ToolResultPart *result_part(const mira::ModelInputItem &item) {
    for (const auto &part : item.content)
        if (const auto *result = std::get_if<mira::ToolResultPart>(&part))
            return result;
    return nullptr;
}

// Byte-faithful comparison of the fields the replay contract must preserve:
// role, authority, provenance and every content part it can carry here.
bool same_item(const mira::ModelInputItem &lhs, const mira::ModelInputItem &rhs) {
    if (lhs.role != rhs.role || lhs.authority != rhs.authority)
        return false;
    if (lhs.provenance.source != rhs.provenance.source ||
        lhs.provenance.method != rhs.provenance.method)
        return false;
    if (lhs.content.size() != rhs.content.size())
        return false;
    for (std::size_t index = 0; index < lhs.content.size(); ++index) {
        const auto &left = lhs.content[index];
        const auto &right = rhs.content[index];
        if (left.index() != right.index())
            return false;
        if (const auto *text = std::get_if<mira::TextPart>(&left)) {
            const auto &other = std::get<mira::TextPart>(right);
            if (text->text != other.text || text->sensitivity != other.sensitivity)
                return false;
        } else if (const auto *call = std::get_if<mira::ToolCallPart>(&left)) {
            const auto &other = std::get<mira::ToolCallPart>(right);
            if (call->provider_call_id != other.provider_call_id ||
                call->wire_name != other.wire_name ||
                mira::to_json_string(call->arguments) != mira::to_json_string(other.arguments) ||
                call->arguments_digest != other.arguments_digest)
                return false;
        } else if (const auto *result = std::get_if<mira::ToolResultPart>(&left)) {
            const auto &other = std::get<mira::ToolResultPart>(right);
            if (result->provider_call_id != other.provider_call_id ||
                mira::to_json_string(result->result) != mira::to_json_string(other.result) ||
                result->failed != other.failed ||
                result->safe_error_summary != other.safe_error_summary)
                return false;
        } else {
            return false; // the other part kinds are not produced by these fixtures
        }
    }
    return true;
}

// The replay of one request must keep the provider tool protocol: every call
// id that appears has its result in the same list and vice versa, and each
// call precedes its result.
void check_pairing(const std::vector<mira::ModelInputItem> &items) {
    std::map<std::string, std::size_t> call_at, result_at;
    for (std::size_t index = 0; index < items.size(); ++index) {
        for (const auto &part : items[index].content) {
            if (const auto *call = std::get_if<mira::ToolCallPart>(&part)) {
                const auto id = call->provider_call_id.value;
                check(!call_at.count(id) && !result_at.count(id));
                call_at[id] = index;
            } else if (const auto *result = std::get_if<mira::ToolResultPart>(&part)) {
                const auto id = result->provider_call_id.value;
                check(call_at.count(id)); // call seen earlier
                check(call_at.at(id) < index);
                check(!result_at.count(id));
                result_at[id] = index;
            }
        }
    }
    check(call_at.size() == result_at.size());
    for (const auto &[id, index] : call_at)
        check(result_at.count(id));
}

// Every replay entry must be the original item it replaces, in the original
// relative order: the replay is a verbatim subsequence of the history.
bool verbatim_subsequence(const std::vector<mira::ModelInputItem> &replay,
                          const std::vector<mira::ModelInputItem> &history) {
    std::size_t cursor = 0;
    for (const auto &item : history)
        if (cursor < replay.size() && same_item(replay[cursor], item))
            ++cursor;
    return cursor == replay.size();
}

std::vector<std::string> call_ids(const mira::ModelRequest &request) {
    std::vector<std::string> ids;
    for (const auto &item : request.input)
        if (const auto *call = call_part(item))
            ids.push_back(call->provider_call_id.value);
    return ids;
}

std::vector<std::string> result_ids(const mira::ModelRequest &request) {
    std::vector<std::string> ids;
    for (const auto &item : request.input)
        if (const auto *result = result_part(item))
            ids.push_back(result->provider_call_id.value);
    return ids;
}

bool has_part_id(const mira::ModelRequest &request, const std::string &id) {
    const auto calls = call_ids(request);
    const auto results = result_ids(request);
    return std::find(calls.begin(), calls.end(), id) != calls.end() ||
           std::find(results.begin(), results.end(), id) != results.end();
}

// ---------------------------------------------------------------------------
// Scripted IContextManager implementations
// ---------------------------------------------------------------------------

// Records the ContextRequest the builder assembled and hands back per-item
// dispositions chosen by the test, so the pair-collapse logic is exercised
// deterministically (the real manager only splits a pair when the checkpoint
// watermark lands between one call and its result).
class ScriptedAuditManager final : public mira::IContextManager {
  public:
    std::vector<mira::ContextItemDisposition> dispositions;
    mutable mira::ContextRequest seen;
    mira::Result<mira::PreparedModelContext>
    prepare(const mira::ContextRequest &request) const override {
        seen = request;
        mira::PreparedModelContext prepared;
        prepared.task_id = request.task_id;
        prepared.session_id = request.session_id;
        prepared.profile_id = request.profile_id;
        prepared.item_audit.reserve(request.items.size());
        for (std::size_t index = 0; index < request.items.size(); ++index) {
            const auto &item = request.items[index];
            prepared.item_audit.push_back(mira::ContextItemAudit{
                item.id, item.kind,
                index < dispositions.size() ? dispositions[index]
                                            : mira::ContextItemDisposition::Selected,
                "scripted", 0});
        }
        return prepared;
    }
};

class FailingManager final : public mira::IContextManager {
  public:
    mira::Result<mira::PreparedModelContext> prepare(const mira::ContextRequest &) const override {
        mira::Error error;
        error.code = mira::ErrorCode::PlatformError;
        error.domain = "test.context";
        error.safe_message = "scripted layer 0 failure";
        return error;
    }
};

// Forwards to the real Layer 0 unchanged and keeps the request it saw plus
// the audit it returned, so a test can pin the watermark arithmetic (per-item
// token estimates, dispositions, reasons, budget report) without altering a
// single decision.
class RecordingManager final : public mira::IContextManager {
  public:
    explicit RecordingManager(std::shared_ptr<const mira::IContextManager> inner)
        : inner_(std::move(inner)) {}
    mutable mira::ContextRequest seen;
    mutable mira::PreparedModelContext prepared;
    mira::Result<mira::PreparedModelContext>
    prepare(const mira::ContextRequest &request) const override {
        seen = request;
        auto outcome = inner_->prepare(request);
        if (outcome.has_value())
            prepared = outcome.value();
        return outcome;
    }

  private:
    std::shared_ptr<const mira::IContextManager> inner_;
};

detail::DesktopContextBuilder::RequestKeys keys() {
    detail::DesktopContextBuilder::RequestKeys value;
    value.task_id = mira::TaskId::generate();
    value.session_id = mira::SessionId::generate();
    value.task_epoch = 7;
    value.profile_id = mira::ModelProfileId::generate();
    return value;
}

std::shared_ptr<const mira::IContextManager> standard_manager() {
    return std::make_shared<mira::StandardContextManager>(
        std::make_shared<mira::ConservativeTokenCounter>());
}

// ---------------------------------------------------------------------------
// Builder unit tests
// ---------------------------------------------------------------------------

// The Layer 0 mapping: only the tool round-trip segment enters the request,
// with the contract kinds and flags the compaction design fixes, and the
// desktop watermarks ride the limits verbatim.
void builder_mapping_and_limits() {
    ScriptedAuditManager manager;
    detail::DesktopContextBuilder builder(
        std::shared_ptr<const mira::IContextManager>(&manager, [](const auto *) {}), {4096, 2048});
    std::vector<mira::ModelInputItem> history;
    history.emplace_back(text_item(mira::ModelRole::User, "goal"));
    history.emplace_back(call_item("call-0", R"({"n":0})"));
    history.emplace_back(result_item("call-0", R"({"v":0})"));
    history.emplace_back(text_item(mira::ModelRole::Assistant, "assistant note"));
    history.emplace_back(call_item("call-1", R"({"n":1})"));
    history.emplace_back(result_item("call-1", R"({"v":1})"));
    const auto request_keys = keys();
    const auto transformed = builder.transform(history, request_keys);
    check(transformed.has_value());
    // Default dispositions keep everything, verbatim and in order.
    check(transformed.value().size() == history.size());
    for (std::size_t index = 0; index < history.size(); ++index)
        check(same_item(transformed.value()[index], history[index]));
    // Only the four tool items entered the Layer 0 request.
    const auto &seen = manager.seen;
    check(seen.task_id == request_keys.task_id);
    check(seen.session_id == request_keys.session_id);
    check(seen.task_epoch == request_keys.task_epoch);
    check(seen.profile_id == request_keys.profile_id);
    check(seen.items.size() == 4);
    const std::size_t tool_indices[] = {1, 2, 4, 5};
    for (std::size_t entry = 0; entry < 4; ++entry) {
        const auto &item = seen.items[entry];
        const auto history_index = tool_indices[entry];
        check(item.sequence == history_index);
        check(item.authority == mira::ContextAuthority::VerifiedState);
        check(item.priority == mira::ContextPriority::Disposable);
        check(item.consumed);
        check(!item.replaceable_by_reference);
        check(item.tool_call_key.has_value());
        const bool is_call = call_part(history[history_index]) != nullptr;
        check(is_call ? item.kind == mira::ContextItemKind::ToolCall
                      : item.kind == mira::ContextItemKind::ToolResult);
        check(*item.tool_call_key ==
              (is_call ? call_part(history[history_index])->provider_call_id.value
                       : result_part(history[history_index])->provider_call_id.value));
        // Content rides byte for byte so the replay can restore the original.
        check(item.content.size() == history[history_index].content.size());
    }
    // The desktop watermarks and budgets the builder must declare.
    check(seen.limits.context_window_tokens == 4096);
    check(seen.limits.reserved_output_tokens == 2048);
    check(seen.limits.trim_watermark == 0.45);
    check(seen.limits.checkpoint_watermark == 0.70);
    check(seen.limits.hard_watermark == 0.90);
    check(seen.limits.max_image_tokens == 10'000);
}

// Fail-open: an undeclared window or a missing Layer 0 returns the history
// unchanged - the seam must never mutate the replay when it cannot decide.
void fail_open_paths() {
    std::vector<mira::ModelInputItem> history;
    history.emplace_back(text_item(mira::ModelRole::User, "goal"));
    history.emplace_back(call_item("call-0", R"({"n":0})"));
    history.emplace_back(result_item("call-0", R"({"v":0})"));
    history.emplace_back(text_item(mira::ModelRole::Assistant, "note"));
    const auto request_keys = keys();
    // Declared window of zero: no compaction, byte-identical replay.
    detail::DesktopContextBuilder undeclared(standard_manager(), {0, 2048});
    const auto untouched = undeclared.transform(history, request_keys);
    check(untouched.has_value());
    check(untouched.value().size() == history.size());
    for (std::size_t index = 0; index < history.size(); ++index)
        check(same_item(untouched.value()[index], history[index]));
    // Declared window but no Layer 0: the same fail-open contract.
    detail::DesktopContextBuilder managerless(nullptr, {4096, 2048});
    const auto degraded = managerless.transform(history, request_keys);
    check(degraded.has_value());
    check(degraded.value().size() == history.size());
    for (std::size_t index = 0; index < history.size(); ++index)
        check(same_item(degraded.value()[index], history[index]));
}

// A history without tool items never enters Layer 0 and passes through.
void no_tool_items_pass_through() {
    detail::DesktopContextBuilder builder(standard_manager(), {4096, 2048});
    std::vector<mira::ModelInputItem> history;
    history.emplace_back(text_item(mira::ModelRole::User, "goal"));
    history.emplace_back(text_item(mira::ModelRole::Assistant, "intermediate thought"));
    history.emplace_back(text_item(mira::ModelRole::User, "follow-up"));
    const auto transformed = builder.transform(history, keys());
    check(transformed.has_value());
    check(transformed.value().size() == history.size());
    for (std::size_t index = 0; index < history.size(); ++index)
        check(same_item(transformed.value()[index], history[index]));
}

// Ample budget: every tool pair survives, order and content are untouched.
void budget_fits_verbatim() {
    detail::DesktopContextBuilder builder(standard_manager(), {200'000, 2048});
    std::vector<mira::ModelInputItem> history;
    history.emplace_back(text_item(mira::ModelRole::User, "capture goal"));
    for (int round = 0; round < 3; ++round) {
        history.emplace_back(
            call_item("call-" + std::to_string(round),
                      R"({"source":"display","n":)" + std::to_string(round) + "})"));
        history.emplace_back(text_item(mira::ModelRole::Assistant,
                                       "thinking between rounds " + std::to_string(round)));
        history.emplace_back(
            result_item("call-" + std::to_string(round),
                        R"({"v":)" + std::to_string(round) + R"(,"detail":"observed frame"})"));
    }
    history.emplace_back(text_item(mira::ModelRole::Assistant, "wrap-up note"));
    const auto transformed = builder.transform(history, keys());
    check(transformed.has_value());
    check(transformed.value().size() == history.size());
    for (std::size_t index = 0; index < history.size(); ++index)
        check(same_item(transformed.value()[index], history[index]));
    check_pairing(transformed.value());
}

// Compaction under a small window: the oldest pairs leave the replay whole,
// the newest pair stays, every surviving item is byte-identical, and the
// non-tool text entries keep their original content and relative order.
// Layer 0 pressure arithmetic: the conservative counter counts the tool wire
// payload bytes at 4 bytes per token (upstream PR#97), so each pair costs
// roughly its receipt bytes plus overhead - 24 pairs with 400-byte receipts
// far exceed the 1024-token input budget (4096 - 2048 reserved - 1024
// safety), so the greedy pass drops the oldest pairs and the checkpoint
// watermark then strips items one by one (splitting a pair mid-way, which
// the builder must collapse whole).
void compaction_drops_oldest_pairs() {
    detail::DesktopContextBuilder builder(standard_manager(), {4096, 2048});
    const std::string payload(400, 'x');
    std::vector<mira::ModelInputItem> history;
    history.emplace_back(text_item(mira::ModelRole::User, "capture goal"));
    constexpr int rounds = 24;
    for (int round = 0; round < rounds; ++round) {
        history.emplace_back(
            call_item("call-" + std::to_string(round),
                      R"({"source":"display","n":)" + std::to_string(round) + "})"));
        history.emplace_back(result_item("call-" + std::to_string(round),
                                         R"({"v":)" + std::to_string(round) + R"(,"payload":")" +
                                             payload + R"("})"));
    }
    history.emplace_back(text_item(mira::ModelRole::Assistant, "wrap-up note"));
    const auto transformed = builder.transform(history, keys());
    check(transformed.has_value());
    const auto &replay = transformed.value();
    check(replay.size() < history.size());
    check_pairing(replay);
    check(verbatim_subsequence(replay, history));
    // Whole-pair disappearance: the first round is gone on both sides, the
    // last round survives on both sides.
    std::size_t kept_pairs = 0;
    for (int round = 0; round < rounds; ++round) {
        const auto id = "call-" + std::to_string(round);
        bool call_present = false, result_present = false;
        for (const auto &item : replay) {
            if (const auto *call = call_part(item))
                call_present |= call->provider_call_id.value == id;
            if (const auto *result = result_part(item))
                result_present |= result->provider_call_id.value == id;
        }
        check(call_present == result_present); // no orphan side survives
        if (call_present)
            ++kept_pairs;
    }
    check(kept_pairs < rounds);
    check(kept_pairs >= 1);
    const auto present = [&replay](const std::string &id) {
        for (const auto &item : replay) {
            if (const auto *call = call_part(item); call && call->provider_call_id.value == id)
                return true;
            if (const auto *result = result_part(item);
                result && result->provider_call_id.value == id)
                return true;
        }
        return false;
    };
    check(!present("call-0"));
    check(present("call-" + std::to_string(rounds - 1)));
    // Non-tool entries ride verbatim regardless of compaction.
    bool goal_found = false, note_found = false;
    for (const auto &item : replay) {
        if (auto *text = std::get_if<mira::TextPart>(&item.content.front()))
            goal_found |= text->text == "capture goal";
        if (auto *text = std::get_if<mira::TextPart>(&item.content.front()))
            note_found |= text->text == "wrap-up note";
    }
    check(goal_found && note_found);
}

// Payload bytes must drive compaction (MIRA-20261014-007, upstream PR#97).
// The conservative counter used to estimate every single-part tool item at
// its fixed overhead floor (~20 part/item tokens + 4 assembly tokens), so a
// whole call/result pair cost ~48 tokens regardless of receipt size. The
// counter now also counts the wire payload bytes at the 4-bytes-per-token
// density, and this fixture pins the discriminating case: five pairs whose
// receipts carry 8 KiB text each fit the Layer 0 input budget (the greedy
// pass keeps everything) while their payload-driven estimate crosses the 70%
// checkpoint watermark, so the checkpoint strip must drop the oldest pair.
// Under the pre-fix floor arithmetic the same items estimate
// 5 x 48 = 240 tokens - 1.7% of the budget, below even the 45% trim
// watermark - no pass could fire and the replay would have stayed verbatim.
void large_tool_receipts_drive_checkpoint_compaction() {
    // 16384 window - 1024 reserved output - 1024 safety = 14336 input budget;
    // the checkpoint watermark sits at 70% = 10035 tokens.
    RecordingManager manager(standard_manager());
    detail::DesktopContextBuilder builder(
        std::shared_ptr<const mira::IContextManager>(&manager, [](const auto *) {}), {16384, 1024});
    const std::string receipt(8192, 'x');
    std::vector<mira::ModelInputItem> history;
    history.emplace_back(text_item(mira::ModelRole::User, "capture goal"));
    constexpr int rounds = 5;
    for (int round = 0; round < rounds; ++round) {
        history.emplace_back(
            call_item("call-" + std::to_string(round),
                      R"({"source":"display","n":)" + std::to_string(round) + "})"));
        history.emplace_back(result_item("call-" + std::to_string(round),
                                         R"({"v":)" + std::to_string(round) + R"(,"receipt":")" +
                                             receipt + R"("})"));
    }
    history.emplace_back(text_item(mira::ModelRole::Assistant, "wrap-up note"));
    const auto transformed = builder.transform(history, keys());
    check(transformed.has_value());
    const auto &replay = transformed.value();
    // Compaction applied: the four younger pairs plus the untouched text
    // entries are exactly what the replay carries.
    check(replay.size() == 1 + 2 * (rounds - 1) + 1);
    check_pairing(replay);
    check(verbatim_subsequence(replay, history));
    const auto present = [&replay](const std::string &id) {
        for (const auto &item : replay) {
            if (const auto *call = call_part(item); call && call->provider_call_id.value == id)
                return true;
            if (const auto *result = result_part(item);
                result && result->provider_call_id.value == id)
                return true;
        }
        return false;
    };
    // The oldest large-receipt pair left whole; every younger pair survived
    // on both sides, and no orphan side remains anywhere.
    check(!present("call-0"));
    for (int round = 0; round < rounds; ++round) {
        const auto id = "call-" + std::to_string(round);
        bool call_present = false, result_present = false;
        for (const auto &item : replay) {
            if (const auto *call = call_part(item))
                call_present |= call->provider_call_id.value == id;
            if (const auto *result = result_part(item))
                result_present |= result->provider_call_id.value == id;
        }
        check(call_present == result_present);
    }
    check(present("call-" + std::to_string(rounds - 1)));
    // A surviving receipt keeps its full 8 KiB payload byte for byte.
    const mira::ModelInputItem *newest_result = nullptr;
    for (const auto &item : replay)
        if (const auto *result = result_part(item);
            result && result->provider_call_id.value == "call-" + std::to_string(rounds - 1))
            newest_result = &item;
    check(newest_result != nullptr);
    check(mira::to_json_string(result_part(*newest_result)->result).find(receipt) !=
          std::string::npos);
    // Layer 0 saw only the tool segment under the desktop watermarks.
    check(manager.seen.items.size() == 2 * rounds);
    check(manager.seen.limits.context_window_tokens == 16384);
    check(manager.seen.limits.reserved_output_tokens == 1024);
    check(manager.seen.limits.trim_watermark == 0.45);
    check(manager.seen.limits.checkpoint_watermark == 0.70);
    // The payload-driven estimate crossed the checkpoint watermark inside the
    // budget: the strip engaged, dropped exactly the oldest pair, and left
    // the rest selected - not the greedy budget pass making the decision.
    check(manager.prepared.budget.input_budget_tokens == 14336);
    check(manager.prepared.budget.checkpoint_recommended);
    check(manager.prepared.budget.dropped_items == 2);
    check(manager.prepared.budget.selected_items == 2 * rounds - 2);
    const auto disposition_of = [&](std::size_t index) {
        return manager.prepared.item_audit[index].disposition;
    };
    check(disposition_of(0) == mira::ContextItemDisposition::Dropped);
    check(disposition_of(1) == mira::ContextItemDisposition::Dropped);
    for (std::size_t index = 2; index < 2 * rounds; ++index)
        check(disposition_of(index) == mira::ContextItemDisposition::Selected);
    check(manager.prepared.item_audit[0].reason == "watermark_checkpoint_drop");
    check(manager.prepared.item_audit[1].reason == "watermark_checkpoint_drop");
    // The assembled output drops the pair whole: neither side of call-0
    // remains in it.
    for (const auto &item : manager.prepared.input) {
        for (const auto &part : item.content) {
            if (const auto *call = std::get_if<mira::ToolCallPart>(&part))
                check(call->provider_call_id.value != "call-0");
            else if (const auto *result = std::get_if<mira::ToolResultPart>(&part))
                check(result->provider_call_id.value != "call-0");
        }
    }
    // Verification probe: the same tool items under the payload-counting
    // counter versus the pre-fix per-item floor arithmetic.
    std::uint64_t estimated_tokens = 0;
    for (const auto &audit : manager.prepared.item_audit)
        estimated_tokens += audit.estimated_tokens;
    std::cout << "payload compaction probe: " << rounds
              << " tool pairs with 8 KiB receipts estimate " << estimated_tokens
              << " tokens against a " << manager.prepared.budget.input_budget_tokens
              << "-token budget (pre-fix floor arithmetic: " << rounds * 48 << " tokens)\n";
}

// A pair the checkpoint watermark split collapses whole: a lone selected side
// must not reach the replay, whichever side the manager kept.
void split_pair_collapses_whole() {
    std::vector<mira::ModelInputItem> history;
    history.emplace_back(call_item("call-0", R"({"n":0})"));
    history.emplace_back(result_item("call-0", R"({"v":0})"));
    history.emplace_back(call_item("call-1", R"({"n":1})"));
    history.emplace_back(result_item("call-1", R"({"v":1})"));
    const auto request_keys = keys();
    const auto replay_with = [&](std::vector<mira::ContextItemDisposition> dispositions) {
        ScriptedAuditManager manager;
        manager.dispositions = std::move(dispositions);
        detail::DesktopContextBuilder builder(
            std::shared_ptr<const mira::IContextManager>(&manager, [](const auto *) {}),
            {4096, 2048});
        auto transformed = builder.transform(history, request_keys);
        check(transformed.has_value());
        return transformed.value();
    };
    {
        // Call dropped, result selected: the whole pair leaves the replay.
        const auto replay = replay_with(
            {mira::ContextItemDisposition::Dropped, mira::ContextItemDisposition::Selected,
             mira::ContextItemDisposition::Selected, mira::ContextItemDisposition::Selected});
        check(replay.size() == 2);
        check(!call_part(replay[0]) || call_part(replay[0])->provider_call_id.value != "call-0");
        check_pairing(replay);
    }
    {
        // Result dropped, call selected: symmetric collapse.
        const auto replay = replay_with(
            {mira::ContextItemDisposition::Selected, mira::ContextItemDisposition::Dropped,
             mira::ContextItemDisposition::Selected, mira::ContextItemDisposition::Selected});
        check(replay.size() == 2);
        check_pairing(replay);
        for (const auto &item : replay)
            if (const auto *call = call_part(item))
                check(call->provider_call_id.value != "call-0");
    }
    {
        // Compressed still counts as kept; both pairs decided independently.
        const auto replay = replay_with(
            {mira::ContextItemDisposition::Compressed, mira::ContextItemDisposition::Selected,
             mira::ContextItemDisposition::Dropped, mira::ContextItemDisposition::Dropped});
        check(replay.size() == 2);
        check_pairing(replay);
        check(same_item(replay[0], history[0]) && same_item(replay[1], history[1]));
    }
    {
        // Everything selected: nothing collapses.
        const auto replay = replay_with(
            {mira::ContextItemDisposition::Selected, mira::ContextItemDisposition::Selected,
             mira::ContextItemDisposition::Selected, mira::ContextItemDisposition::Selected});
        check(replay.size() == 4);
        check_pairing(replay);
    }
}

// Layer 0 failures surface as errors from the builder; the loop's seam is the
// component that degrades them to the raw replay.
void manager_error_surfaces() {
    detail::DesktopContextBuilder builder(std::make_shared<FailingManager>(), {4096, 2048});
    std::vector<mira::ModelInputItem> history;
    history.emplace_back(call_item("call-0", R"({"n":0})"));
    history.emplace_back(result_item("call-0", R"({"v":0})"));
    const auto transformed = builder.transform(history, keys());
    check(!transformed.has_value());
    check(transformed.error().code == mira::ErrorCode::PlatformError);
}

// ---------------------------------------------------------------------------
// Harness wiring: ModelLayer + scripted provider + fake desktop
// ---------------------------------------------------------------------------

class CompactionScript final : public mira::IModelProvider {
  public:
    mira::ModelProfile profile_;
    std::vector<mira::ModelRequest> requests;
    std::size_t rounds;
    CompactionScript(mira::ModelProfile profile, std::size_t round_count)
        : profile_(std::move(profile)), rounds(round_count) {}
    const mira::ModelProfile &profile() const override { return profile_; }
    mira::Result<mira::ModelResponse> infer(const mira::ModelRequest &request,
                                            const mira::OperationContext &,
                                            const mira::ProviderInferOptions &) override {
        requests.push_back(request);
        mira::ModelResponse response;
        response.request_id = request.request_id;
        response.operation_id = request.operation_id;
        response.profile_id = profile_.id;
        response.status = mira::ModelCompletionStatus::Completed;
        if (requests.size() <= rounds) {
            const auto tool =
                std::find_if(request.tools.begin(), request.tools.end(), [&](const auto &value) {
                    return value.wire_name == "desktop_screen_capture";
                });
            check(tool != request.tools.end());
            mira::ToolCallOutput call;
            call.provider_call_id.value = "compact-capture-" + std::to_string(requests.size());
            call.tool_id = tool->tool_id;
            call.provider_name = tool->wire_name;
            call.arguments = mira::parse_json(R"({"source":"display","source_id":"d1"})").value();
            call.arguments_digest = mira::canonical_json_digest(call.arguments);
            response.output.emplace_back(std::move(call));
        } else {
            mira::MessageOutput message;
            message.content.emplace_back(mira::OutputTextPart{"captured enough; done", {}});
            response.output.emplace_back(std::move(message));
        }
        return response;
    }
};

// One multi-round capture task through the real harness path with headroom in
// the Layer 0 input budget. With a declared window the seam still resolves
// once per assembled request with a non-empty history (the completion counters
// report every application), but the small replay sits far below the trim
// watermark, so the whole tool-history replay reaches the wire. Without a
// declared window no transform fires at all and the replay stays complete.
void harness_compaction(kairo::Executor &executor, bool declare_window) {
    integration::DesktopToolContext tools;
    auto env = std::make_shared<mirage::testing::FakeDesktopEnvironment>();
    env->displays = {{"d1", {-120, -40, 128, 64}, true}};
    env->windows = {{"w1", "Fixture", {-100, -30, 32, 16}, true}};
    tools.environment = env;
    tools.actions = std::make_shared<mirage::desktop::ActionCoordinator>();
    tools.gate = [](const auto &capability, const auto &, const auto &cancel) {
        return capability == "screen.capture" && !cancel();
    };
    constexpr std::size_t rounds = 12;
    std::shared_ptr<CompactionScript> script;
    integration::ModelProviderOverride provider([&](const auto &profile) {
        script = std::make_shared<CompactionScript>(profile, rounds);
        return script;
    });
    integration::ModelLayerConfig config;
    config.enabled = config.supports_images = true;
    config.model_selector = "compaction-fixture";
    // Per-call output bound of this fixture: min(64, profile envelope) = 64.
    // Layer 0 input budget = 131072 - 64 - 1024 = 129984 tokens; the 12 replay
    // pairs cost ~576 tokens under the conservative counter, far below the 45%
    // trim watermark - headroom, not pressure.
    config.max_output_tokens = 64;
    if (declare_window)
        config.context_window_tokens = 131072;
    integration::ModelLayer model(executor, config, &provider, tools);
    const auto ctx = context();
    const auto result = executor
                            .submit_auto([&] {
                                return model.complete_harness_turn(
                                    "", "capture the desktop repeatedly, then answer", ctx);
                            })
                            .get();
    check(result.ok);
    check(!result.failed && !result.cancelled);
    check(script && script->requests.size() == rounds + 1);
    // Every emitted request keeps the provider tool protocol.
    for (const auto &request : script->requests) {
        const auto calls = call_ids(request);
        const auto results = result_ids(request);
        check(calls.size() == results.size());
        std::map<std::string, std::size_t> call_at;
        for (std::size_t index = 0; index < request.input.size(); ++index) {
            for (const auto &part : request.input[index].content) {
                if (const auto *call = std::get_if<mira::ToolCallPart>(&part)) {
                    check(!call_at.count(call->provider_call_id.value));
                    call_at[call->provider_call_id.value] = index;
                } else if (const auto *loop_result = std::get_if<mira::ToolResultPart>(&part)) {
                    const auto found = call_at.find(loop_result->provider_call_id.value);
                    check(found != call_at.end() && found->second < index);
                }
            }
        }
        for (const auto &id : results)
            check(call_at.count(id));
    }
    if (declare_window) {
        // The seam resolved once per assembled request with a non-empty
        // history: requests 2..N, exactly `rounds` applications, no degrade.
        check(result.history_transforms == rounds);
        check(result.history_transform_degradations == 0);
        // Ample input budget: nothing crosses the trim watermark, so the
        // replay keeps every executed pair, first through newest, whole.
        const auto &last = script->requests.back();
        const auto calls = call_ids(last);
        const auto results = result_ids(last);
        check(calls.size() == rounds);
        for (std::size_t round = 1; round <= rounds; ++round) {
            const auto id = "compact-capture-" + std::to_string(round);
            check(std::find(calls.begin(), calls.end(), id) != calls.end());
            check(std::find(results.begin(), results.end(), id) != results.end());
        }
        check(result.supplied_input_reuse_annotations >= 1); // identical captures annotate
    } else {
        // Undeclared window: no transform fires and the replay stays whole.
        check(result.history_transforms == 0);
        check(result.history_transform_degradations == 0);
        const auto &last = script->requests.back();
        const auto calls = call_ids(last);
        const auto results = result_ids(last);
        check(calls.size() == rounds);
        for (std::size_t round = 1; round <= rounds; ++round) {
            const auto id = "compact-capture-" + std::to_string(round);
            check(std::find(calls.begin(), calls.end(), id) != calls.end());
            check(std::find(results.begin(), results.end(), id) != results.end());
        }
    }
    model.shutdown();
}

// Real compaction through the harness path: a small declared window with a
// per-call output that still leaves an input budget. 4096 window, per-call
// output 2304 -> Layer 0 input budget = 4096 - 2304 - 1024 = 768 tokens,
// checkpoint watermark 537.6. The conservative counter counts tool wire
// payload bytes (upstream PR#97), so pairs cost their capture-receipt bytes
// at 4 bytes per token plus overhead and the 15th round's replay crosses the
// checkpoint: the oldest pairs leave the replay and the pair the watermark
// split collapses whole, while the newest pairs stay. The loop's 16-turn
// ceiling makes 15 capture rounds the most this path can accumulate.
void harness_compaction_under_pressure(kairo::Executor &executor) {
    integration::DesktopToolContext tools;
    auto env = std::make_shared<mirage::testing::FakeDesktopEnvironment>();
    env->displays = {{"d1", {-120, -40, 128, 64}, true}};
    env->windows = {{"w1", "Fixture", {-100, -30, 32, 16}, true}};
    tools.environment = env;
    tools.actions = std::make_shared<mirage::desktop::ActionCoordinator>();
    tools.gate = [](const auto &capability, const auto &, const auto &cancel) {
        return capability == "screen.capture" && !cancel();
    };
    constexpr std::size_t rounds = 15;
    std::shared_ptr<CompactionScript> script;
    integration::ModelProviderOverride provider([&](const auto &profile) {
        script = std::make_shared<CompactionScript>(profile, rounds);
        return script;
    });
    integration::ModelLayerConfig config;
    config.enabled = config.supports_images = true;
    config.model_selector = "compaction-fixture";
    config.max_output_tokens = 2304; // non-thinking: used verbatim as the per-call bound
    config.context_window_tokens = 4096;
    integration::ModelLayer model(executor, config, &provider, tools);
    const auto ctx = context();
    const auto result = executor
                            .submit_auto([&] {
                                return model.complete_harness_turn(
                                    "", "capture the desktop repeatedly, then answer", ctx);
                            })
                            .get();
    check(result.ok);
    check(!result.failed && !result.cancelled);
    check(script && script->requests.size() == rounds + 1);
    // Every emitted request keeps the provider tool protocol.
    for (const auto &request : script->requests) {
        const auto calls = call_ids(request);
        const auto results = result_ids(request);
        check(calls.size() == results.size());
        std::map<std::string, std::size_t> call_at;
        for (std::size_t index = 0; index < request.input.size(); ++index) {
            for (const auto &part : request.input[index].content) {
                if (const auto *call = std::get_if<mira::ToolCallPart>(&part)) {
                    check(!call_at.count(call->provider_call_id.value));
                    call_at[call->provider_call_id.value] = index;
                } else if (const auto *loop_result = std::get_if<mira::ToolResultPart>(&part)) {
                    const auto found = call_at.find(loop_result->provider_call_id.value);
                    check(found != call_at.end() && found->second < index);
                }
            }
        }
        for (const auto &id : results)
            check(call_at.count(id));
    }
    // The seam applied on every request with a non-empty history and never
    // degraded.
    check(result.history_transforms == rounds);
    check(result.history_transform_degradations == 0);
    // Compaction dropped the early pairs: the first capture round is gone on
    // both sides, the newest round survives, and strictly fewer pairs than
    // executed rounds are replayed.
    const auto &last = script->requests.back();
    check(!has_part_id(last, "compact-capture-1"));
    check(has_part_id(last, "compact-capture-" + std::to_string(rounds)));
    check(call_ids(last).size() < rounds);
    check(call_ids(last).size() >= 1);
    model.shutdown();
}
// Honest degradation for a window that cannot fund an input budget: the
// default per-call output bound (32000) exceeds this declared window, so
// Layer 0's limits validation rejects the build and the loop seam degrades
// every transform to the raw replay with a visible degradation count. That is
// the settled contract - compaction cannot help when the reserved output
// alone fills the window, and the DEC-087 preflight keeps the wire call
// bounded - so these assertions pin it deliberately.
void tiny_window_without_input_budget_degrades(kairo::Executor &executor) {
    integration::DesktopToolContext tools;
    auto env = std::make_shared<mirage::testing::FakeDesktopEnvironment>();
    env->displays = {{"d1", {-120, -40, 128, 64}, true}};
    env->windows = {{"w1", "Fixture", {-100, -30, 32, 16}, true}};
    tools.environment = env;
    tools.actions = std::make_shared<mirage::desktop::ActionCoordinator>();
    tools.gate = [](const auto &capability, const auto &, const auto &cancel) {
        return capability == "screen.capture" && !cancel();
    };
    constexpr std::size_t rounds = 3;
    std::shared_ptr<CompactionScript> script;
    integration::ModelProviderOverride provider([&](const auto &profile) {
        script = std::make_shared<CompactionScript>(profile, rounds);
        return script;
    });
    integration::ModelLayerConfig config;
    config.enabled = config.supports_images = true;
    config.model_selector = "compaction-fixture";
    // max_output_tokens stays undeclared: the per-call bound defaults to
    // 32000, which alone exceeds the 8192-token window.
    config.context_window_tokens = 8192;
    integration::ModelLayer model(executor, config, &provider, tools);
    const auto ctx = context();
    const auto result = executor
                            .submit_auto([&] {
                                return model.complete_harness_turn(
                                    "", "capture the desktop repeatedly, then answer", ctx);
                            })
                            .get();
    check(result.ok);
    check(script && script->requests.size() == rounds + 1);
    check(result.history_transforms == 0);
    check(result.history_transform_degradations == rounds);
    // The degraded raw replay keeps the requests pair-valid and complete.
    const auto &last = script->requests.back();
    check(call_ids(last).size() == rounds);
    check(has_part_id(last, "compact-capture-1"));
    model.shutdown();
}

} // namespace

int main() {
    try {
        builder_mapping_and_limits();
        fail_open_paths();
        no_tool_items_pass_through();
        budget_fits_verbatim();
        compaction_drops_oldest_pairs();
        large_tool_receipts_drive_checkpoint_compaction();
        split_pair_collapses_whole();
        manager_error_surfaces();
        kairo::Executor executor;
        kairo::ExecutorConfig executor_config;
        executor_config.min_threads = executor_config.max_threads = 3;
        check(static_cast<bool>(executor.initialize(executor_config)));
        harness_compaction(executor, true);
        harness_compaction(executor, false);
        harness_compaction_under_pressure(executor);
        tiny_window_without_input_budget_degrades(executor);
        executor.shutdown(true);
        std::cout << checks << " desktop context builder checks passed\n";
        return 0;
    } catch (const std::exception &failure) {
        std::cerr << failure.what() << '\n';
        return 1;
    }
}
