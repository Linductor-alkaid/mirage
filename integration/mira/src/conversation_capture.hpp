#pragma once
#include "ime_pinyin_planner.hpp"
#include "phase_timing.hpp"
#include <algorithm>
#include <map>
#include <mutex>
#include <optional>
#include <string>

#include <mira/json.hpp>
#include <mira/model_budget.hpp>
#include <mira/model_digest.hpp>
#include <mira/model_provider.hpp>
#include <mirage/integration/model_layer.hpp>

namespace mirage::integration::detail {
// DEC-087: per-request context reserve shared with the model layer's
// preflight budget (model-token-limits.ts parity).
inline constexpr std::uint64_t kContextReserveTokens = 1000;
// infer and BuiltinToolHandler execute inline on the same bounded caller task.
// The layer's infer_mutex also serializes installation against shutdown.
class ConversationCapture {
  public:
    explicit ConversationCapture(DialogProcessSink sink) : sink_(std::move(sink)) {}
    std::vector<conversation::Part> parts;
    conversation::Timings timings;
    std::optional<std::uint64_t> last_input_estimate;
    void publish() const {
        if (sink_)
            sink_(parts, timings);
    }
    struct CategoryUsage {
        std::size_t tool = 0;     // input + output
        std::size_t text = 0;     // kind=text
        std::size_t thinking = 0; // kind=thinking
    };
    CategoryUsage categorize_budget() const {
        CategoryUsage usage;
        for (const auto &item : parts) {
            if (item.kind == "tool") {
                usage.tool += item.input.size() + item.output.size();
            } else if (item.kind == "text") {
                usage.text += item.text.size();
            } else if (item.kind == "thinking") {
                usage.thinking += item.text.size();
            }
        }
        return usage;
    }
    void bound(std::string &value, conversation::Part &part) {
        // Calculate current usage including the part being bounded if it's already in parts.
        CategoryUsage usage = categorize_budget();
        // If this part is already in parts (e.g., when updating output after input was added),
        // we need to account for its current content in the budget calculation.
        // The categorize_budget() already includes it, so we're good.

        std::size_t budget = conversation::max_part_text;
        // DEC-093: apply per-category budget limits.
        if (part.kind == "tool") {
            // For tool parts, input and output share the tool budget.
            // When bounding output, the input is already counted in usage.tool.
            const auto remaining = usage.tool < conversation::max_tool_budget
                                       ? conversation::max_tool_budget - usage.tool
                                       : 0;
            budget = std::min(budget, remaining);
        } else if (part.kind == "text") {
            const auto remaining = usage.text < conversation::max_text_budget
                                       ? conversation::max_text_budget - usage.text
                                       : 0;
            budget = std::min(budget, remaining);
        } else if (part.kind == "thinking") {
            const auto remaining = usage.thinking < conversation::max_thinking_budget
                                       ? conversation::max_thinking_budget - usage.thinking
                                       : 0;
            budget = std::min(budget, remaining);
        }
        part.truncated = conversation::truncate_text(value, budget) || part.truncated;
    }
    void add(conversation::Part part) {
        constexpr std::string_view omitted = "（展示预算省略）";
        if (part.kind == "thinking") {
            constexpr std::size_t limit = 4096;
            if (part.text.size() > limit) {
                constexpr std::string_view gap = "\n…（中间思考因展示预算省略）…\n";
                auto head = part.text;
                conversation::truncate_text(head, 2048);
                auto begin = part.text.size() - (limit - head.size() - gap.size());
                while (begin < part.text.size() &&
                       (static_cast<unsigned char>(part.text[begin]) & 0xc0U) == 0x80U)
                    ++begin;
                part.text = head + std::string(gap) + part.text.substr(begin);
                part.truncated = part.omitted_for_display_budget = true;
            }
            auto used = categorize_budget().thinking;
            for (auto &old : parts) {
                if (used + part.text.size() <= conversation::max_thinking_budget)
                    break;
                if (old.kind != "thinking" || old.text.size() <= omitted.size())
                    continue;
                used -= old.text.size();
                old.text = omitted;
                old.truncated = old.omitted_for_display_budget = true;
                used += old.text.size();
            }
        }
        if (parts.size() == conversation::max_parts) {
            auto victim = std::find_if(parts.begin(), parts.end(),
                                       [](const auto &item) { return item.kind == "thinking"; });
            if (victim == parts.end())
                victim = std::find_if(parts.begin(), parts.end(), [](const auto &item) {
                    return item.status != "running" && item.status != "pending";
                });
            if (victim == parts.end()) {
                parts.back().truncated = true;
                return;
            }
            parts.erase(victim);
        }
        bound(part.text, part);
        bound(part.input, part);
        parts.push_back(std::move(part));
    }
    void response(const mira::ModelRequest &request, const mira::ModelResponse &response) {
        const auto prefix = request.request_id.to_string() + ".";
        // A retried request replaces its display attempt, retaining stable row identities.
        std::erase_if(parts, [&](const auto &part) { return part.id.starts_with(prefix); });
        const bool tools =
            std::any_of(response.output.begin(), response.output.end(), [](const auto &item) {
                return std::holds_alternative<mira::ToolCallOutput>(item);
            });
        std::size_t index = 0;
        for (const auto &item : response.output) {
            const auto id = request.request_id.to_string() + "." + std::to_string(index++);
            if (const auto *thinking = std::get_if<mira::ThinkingPart>(&item)) {
                if (!thinking->redacted && !thinking->text.empty()) {
                    conversation::Part part;
                    part.id = id;
                    part.kind = "thinking";
                    part.text = thinking->text;
                    add(std::move(part));
                }
            } else if (const auto *call = std::get_if<mira::ToolCallOutput>(&item)) {
                if (call->provider_name.empty() || call->provider_name.size() > 128)
                    continue;
                conversation::Part part;
                part.id = id;
                part.kind = "tool";
                part.status = "pending";
                part.name = call->provider_name;
                if ((call->provider_name == "desktop.input.type_text" ||
                     call->provider_name == "desktop.session.type_text")) {
                    // Typed payloads remain in the canonical model exchange;
                    // process events/history only project the target and size.
                    const auto *text = call->arguments.find("text");
                    const auto *window = call->arguments.find("window_id");
                    mira::JsonValue::Object summary;
                    summary.emplace_back(
                        "text_bytes",
                        mira::JsonValue{static_cast<std::int64_t>(
                            text && text->as_string() ? text->as_string()->size() : 0)});
                    if (window && window->as_string() && window->as_string()->size() <= 256)
                        summary.emplace_back("window_id", *window);
                    for (const auto *field :
                         {"observation_scope", "image_scope", "environment_epoch"})
                        if (const auto *value = call->arguments.find(field);
                            value && ((value->as_string() && value->as_string()->size() <= 128) ||
                                      value->as_integer()))
                            summary.emplace_back(field, *value);
                    part.input = mira::to_json_string(mira::JsonValue{std::move(summary)});
                } else if (call->arguments.as_object()) {
                    auto fields = *call->arguments.as_object();
                    const auto before = fields.size();
                    if (const auto *uri = call->arguments.find("uri"); uri && uri->as_string()) {
                        fields.emplace_back("uri_digest",
                                            mira::digest_string(*uri->as_string()).to_string());
                        fields.emplace_back("uri_bytes",
                                            static_cast<std::int64_t>(uri->as_string()->size()));
                    }
                    mira::JsonValue::Object condition_fields;
                    for (const auto *key :
                         {"expected_title", "expected_role", "expected_name", "expected_text"})
                        if (const auto *value = call->arguments.find(key))
                            condition_fields.emplace_back(
                                key, value->as_string()
                                         ? (value->as_string()->empty() ? "empty_string" : "string")
                                     : value->is_null() ? "null"
                                                        : "non_string");
                    std::erase_if(fields, [](const auto &entry) {
                        return entry.first == "uri" || entry.first == "expected_title" ||
                               entry.first == "expected_name" || entry.first == "expected_role" ||
                               entry.first == "expected_text";
                    });
                    if (fields.size() != before)
                        fields.emplace_back(
                            "condition_digest",
                            mira::canonical_json_digest(call->arguments).to_string());
                    if (!condition_fields.empty())
                        fields.emplace_back("condition_fields",
                                            mira::JsonValue{std::move(condition_fields)});
                    part.input = mira::to_json_string(mira::JsonValue{std::move(fields)});
                } else {
                    part.input = mira::to_json_string(call->arguments);
                }
                add(std::move(part));
            } else if (tools) {
                if (const auto *message = std::get_if<mira::MessageOutput>(&item)) {
                    conversation::Part part;
                    part.id = id;
                    part.kind = "text";
                    for (const auto &content : message->content)
                        if (const auto *text = std::get_if<mira::OutputTextPart>(&content))
                            part.text += text->text;
                    if (!part.text.empty())
                        add(std::move(part));
                }
            }
        }
        publish();
    }
    conversation::Part *start_tool(const std::string &name) {
        auto found = std::find_if(parts.begin(), parts.end(), [&](const auto &part) {
            return part.kind == "tool" && part.name == name && part.status == "pending";
        });
        if (found == parts.end())
            return nullptr;
        found->status = "running";
        publish();
        return &*found;
    }
    void finish_tool(conversation::Part *part, const mira::Result<mira::JsonValue> &result,
                     const std::function<void(std::vector<conversation::Part> &)> &project = {}) {
        if (!part)
            return;
        part->status = result                                              ? "complete"
                       : result.error().code == mira::ErrorCode::Cancelled ? "cancelled"
                                                                           : "failed";
        std::string output =
            result ? mira::to_json_string(result.value()) : result.error().safe_message;
        if (part->name == "desktop.vision.ocr" && result) {
            const auto *regions = result.value().find("regions");
            mira::JsonValue::Object summary{
                {"source", "ocr"},
                {"region_count", static_cast<std::int64_t>(regions && regions->as_array()
                                                               ? regions->as_array()->size()
                                                               : 0)},
                {"verified", false}};
            for (const auto *key :
                 {"display_id", "visual_scope", "model_revision", "backend_calls",
                  "environment_epoch", "observed_at_ms", "source_kind", "image_artifact_id",
                  "width", "height", "coordinate_space", "truncated", "total_region_count",
                  "actionable", "observation_only", "received_at_ms", "text_only", "continuous"})
                if (const auto *v = result.value().find(key))
                    summary.emplace_back(key, *v);
            output = mira::to_json_string(mira::JsonValue{std::move(summary)});
        }
        bound(output, *part);
        part->output = std::move(output);
        if (project)
            project(parts);
        publish();
    }
    void settle(bool cancelled) {
        for (auto &part : parts)
            if (part.status == "pending" || part.status == "running") {
                part.status = cancelled ? "cancelled" : "failed";
                std::string output = cancelled ? "任务已取消" : "工具未执行或未完成";
                bound(output, part);
                part.output = std::move(output);
            }
    }

  private:
    DialogProcessSink sink_;
};

// Decorates the configured public provider; inference/retries/admission stay in Mira.
class ObservedProvider final : public mira::IModelProvider {
  public:
    explicit ObservedProvider(std::shared_ptr<mira::IModelProvider> provider)
        : provider_(std::move(provider)) {}
    std::function<mira::Result<void>(const mira::ModelRequest &, const mira::OperationContext &)>
        request_guard;
    std::function<void(const mira::ModelRequest &, const mira::OperationContext &)> consumed;
    ConversationCapture *capture = nullptr; // scoped to the serialized caller task
    DialogModelActivitySink activity;
    // Diagnostics sink for provider-reported usage (counts only, no
    // content); invoked from record_usage_anchor's quality gate.
    std::function<void(const mira::ModelUsage &)> usage_observer;
    // DEC-087: the declared context window of the active model. Absent = no
    // per-request preflight shrink (ZCode: resolveModelStepMaxOutputTokens
    // keeps the baseline when the window is unknown).
    std::optional<std::uint64_t> context_window;
    const mira::ModelProfile &profile() const override { return provider_->profile(); }
    mira::Result<mira::ModelResponse> infer(const mira::ModelRequest &request,
                                            const mira::OperationContext &context,
                                            const mira::ProviderInferOptions &options) override {
        auto result = [&]() -> mira::Result<mira::ModelResponse> {
            try {
                if (request_guard) {
                    const auto allowed = request_guard(request, context);
                    if (!allowed)
                        return allowed.error();
                }
                if (activity)
                    activity(true);
                struct EndActivity {
                    DialogModelActivitySink &sink;
                    ~EndActivity() {
                        if (sink)
                            sink(false);
                    }
                } end{activity};
                // DEC-087: the host request carries the declared baseline;
                // the wire call carries the context-aware bound. Only ever
                // shrinks, so the router admission (profile limit) stays
                // truthful for both values. Deliberately touches only
                // generation.max_output_tokens (the wire truth) — the
                // request's budget block stays the whole-run declaration the
                // host boundary admitted.
                mira::ModelRequest wire_request = request;
                if (auto wire = resolve_wire_budget(request, context))
                    wire_request.generation.max_output_tokens = wire;
                PhaseTimingScope timing(capture ? &capture->timings.inference : nullptr);
                return provider_->infer(wire_request, context, options);
            } catch (...) {
                mira::Error error;
                error.code = mira::ErrorCode::PlatformError;
                error.domain = "mirage.model";
                error.safe_message = "model provider failed";
                return error;
            }
        }();
        record_usage_anchor(context, request, result);
        const bool internal_spelling = request.prompt_provenance.system_template_digest ==
                                           mira::digest_string(ime_pinyin_prompt) &&
                                       request.tools.empty();
        if (!internal_spelling && consumed && result &&
            result.value().status == mira::ModelCompletionStatus::Completed &&
            !context.cancelled_or_expired(mira::Timestamp::now()))
            consumed(request, context);
        if (capture && !internal_spelling && result &&
            result.value().status == mira::ModelCompletionStatus::Completed &&
            !context.cancelled_or_expired(mira::Timestamp::now())) {
            // DEC-102: the actual request includes system, tools and transformed
            // replay; capture one final-call estimate without changing ModelUsage.
            capture->last_input_estimate = mira::estimate_input_tokens(request);
            capture->response(request, result.value());
        } else if (capture)
            capture->publish();
        return result;
    }
    const mira::TransportTrace &last_trace() const override { return provider_->last_trace(); }
    const mira::SseStreamStats &last_sse_stats() const override {
        return provider_->last_sse_stats();
    }
    std::optional<std::chrono::milliseconds> last_retry_after_hint() const override {
        return provider_->last_retry_after_hint();
    }

    // DEC-088 capability 5: supply-time remaining-budget estimate for the
    // session, anchored on the last provider-reported input (DEC-087). One
    // turn of growth since that call is not subtracted, so callers must treat
    // the value as an upper bound — the degradation bands absorb the lag.
    // Nullopt without a declared window or without an anchor: degradation
    // fails open and the image ships unchanged.
    std::optional<std::int64_t> anchored_remaining(const std::string &session_key) const {
        if (!context_window)
            return std::nullopt;
        std::lock_guard<std::mutex> guard(anchor_mutex_);
        const auto anchor = anchors_.find(session_key);
        if (anchor == anchors_.end())
            return std::nullopt;
        return static_cast<std::int64_t>(*context_window) -
               static_cast<std::int64_t>(anchor->second.provider_input) -
               static_cast<std::int64_t>(kContextReserveTokens);
    }

  private:
    // window − estimatedCurrentUsage − 1000; a non-positive estimate keeps
    // the baseline (the local estimate is not provider-authoritative), and
    // otherwise the wire bound is min(baseline, usable). The usage estimate
    // anchors on the previous provider-reported input size (the pinned
    // ZCode preflight semantics (model-token-limits.ts): usable =
    // window − estimatedCurrentUsage − 1000; a non-positive estimate keeps
    // the baseline (the local estimate is not provider-authoritative), and
    // otherwise the wire bound is min(baseline, usable). The usage estimate
    // anchors on the previous provider-reported input size (the pinned
    // dialects report the total current input including cache reads) plus
    // the growth of the pinned byte estimator since that response — the
    // same estimator on both sides cancels its systematic bias in the delta.
    std::optional<std::uint64_t> resolve_wire_budget(const mira::ModelRequest &request,
                                                     const mira::OperationContext &context) const {
        // Absent = the host did not declare a baseline; nothing to shrink.
        if (!request.generation.max_output_tokens)
            return std::nullopt;
        const auto baseline = *request.generation.max_output_tokens;
        if (!context_window)
            return baseline;
        const auto estimate = mira::estimate_input_tokens(request);
        std::uint64_t used = estimate;
        {
            std::lock_guard<std::mutex> guard(anchor_mutex_);
            if (const auto anchor = anchors_.find(context.session.to_string());
                anchor != anchors_.end()) {
                const auto growth = estimate > anchor->second.local_estimate
                                        ? estimate - anchor->second.local_estimate
                                        : 0;
                used = anchor->second.provider_input + growth;
            }
        }
        if (*context_window <= used || *context_window - used <= kContextReserveTokens)
            return baseline;
        return std::min(baseline, *context_window - used - kContextReserveTokens);
    }
    void record_usage_anchor(const mira::OperationContext &context,
                             const mira::ModelRequest &request,
                             const mira::Result<mira::ModelResponse> &result) {
        if (!result || context.cancelled_or_expired(mira::Timestamp::now()))
            return;
        const auto &usage = result.value().usage;
        if (usage.quality != mira::UsageQuality::Exact &&
            usage.quality != mira::UsageQuality::ProviderReported)
            return;
        if (!usage.input_tokens || *usage.input_tokens > (std::uint64_t{1} << 32))
            return;
        std::lock_guard<std::mutex> guard(anchor_mutex_);
        // RULE-07: the anchor table is bounded; sessions beyond the cap drop
        // their anchors and fall back to the pure local estimate.
        if (anchors_.size() >= 512)
            anchors_.clear();
        anchors_.insert_or_assign(
            context.session.to_string(),
            UsageAnchor{*usage.input_tokens, mira::estimate_input_tokens(request)});
        if (usage_observer)
            usage_observer(usage);
    }
    struct UsageAnchor {
        std::uint64_t provider_input = 0;
        std::uint64_t local_estimate = 0;
    };
    std::shared_ptr<mira::IModelProvider> provider_;
    mutable std::mutex anchor_mutex_;
    std::map<std::string, UsageAnchor> anchors_;
};
struct CaptureScope {
    ObservedProvider &provider;
    ConversationCapture &capture;
    CaptureScope(ObservedProvider &p, ConversationCapture &c, DialogModelActivitySink activity = {})
        : provider(p), capture(c) {
        provider.capture = &capture;
        provider.activity = std::move(activity);
    }
    ~CaptureScope() {
        provider.request_guard = {};
        provider.consumed = {};
        provider.capture = nullptr;
        provider.activity = {};
    }
};
} // namespace mirage::integration::detail
