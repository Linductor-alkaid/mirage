#pragma once
#include <algorithm>
#include <mira/json.hpp>
#include <mira/model_digest.hpp>
#include <mira/model_provider.hpp>
#include <mirage/integration/model_layer.hpp>

namespace mirage::integration::detail {
// infer and BuiltinToolHandler execute inline on the same bounded caller task.
// The layer's infer_mutex also serializes installation against shutdown.
class ConversationCapture {
  public:
    explicit ConversationCapture(DialogProcessSink sink) : sink_(std::move(sink)) {}
    std::vector<conversation::Part> parts;
    void publish() const {
        if (sink_)
            sink_(parts);
    }
    void bound(std::string &value, conversation::Part &part) {
        std::size_t used = 0;
        for (const auto &item : parts)
            used += item.text.size() + item.input.size() + item.output.size();
        const auto budget =
            std::min(conversation::max_part_text, conversation::max_content_bytes - used);
        part.truncated = conversation::truncate_text(value, budget) || part.truncated;
    }
    void add(conversation::Part part) {
        if (parts.size() == conversation::max_parts) {
            parts.back().truncated = true;
            return;
        }
        // One display field per producer operation; no opaque signatures/raw payloads.
        bound(part.text, part);
        bound(part.input, part);
        parts.push_back(std::move(part));
    }
    void response(const mira::ModelRequest &request, const mira::ModelResponse &response) {
        const auto previous = parts.size();
        const auto prefix = request.request_id.to_string() + ".";
        // A retried request replaces its display attempt, retaining stable row identities.
        const bool replaced = std::erase_if(parts, [&](const auto &part) {
                                  return part.id.starts_with(prefix);
                              }) != 0;
        const bool was_truncated = !parts.empty() && parts.back().truncated;
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
        if (replaced || parts.size() != previous ||
            (!parts.empty() && parts.back().truncated != was_truncated))
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
                  "actionable", "observation_only", "received_at_ms"})
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
    ConversationCapture *capture = nullptr; // scoped to the serialized caller task
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
                return provider_->infer(request, context, options);
            } catch (...) {
                mira::Error error;
                error.code = mira::ErrorCode::PlatformError;
                error.domain = "mirage.model";
                error.safe_message = "model provider failed";
                return error;
            }
        }();
        if (capture && result && result.value().status == mira::ModelCompletionStatus::Completed &&
            !context.cancelled_or_expired(mira::Timestamp::now()))
            capture->response(request, result.value());
        return result;
    }
    const mira::TransportTrace &last_trace() const override { return provider_->last_trace(); }
    const mira::SseStreamStats &last_sse_stats() const override {
        return provider_->last_sse_stats();
    }
    std::optional<std::chrono::milliseconds> last_retry_after_hint() const override {
        return provider_->last_retry_after_hint();
    }

  private:
    std::shared_ptr<mira::IModelProvider> provider_;
};
struct CaptureScope {
    ObservedProvider &provider;
    ConversationCapture &capture;
    CaptureScope(ObservedProvider &p, ConversationCapture &c) : provider(p), capture(c) {
        provider.capture = &capture;
    }
    ~CaptureScope() {
        provider.request_guard = {};
        provider.capture = nullptr;
    }
};
} // namespace mirage::integration::detail
