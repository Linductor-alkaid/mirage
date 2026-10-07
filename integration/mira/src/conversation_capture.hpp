#pragma once
#include <algorithm>
#include <mira/json.hpp>
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
                part.input = mira::to_json_string(call->arguments);
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
    void finish_tool(conversation::Part *part, const mira::Result<mira::JsonValue> &result) {
        if (!part)
            return;
        part->status = result                                              ? "complete"
                       : result.error().code == mira::ErrorCode::Cancelled ? "cancelled"
                                                                           : "failed";
        std::string output =
            result ? mira::to_json_string(result.value()) : result.error().safe_message;
        bound(output, *part);
        part->output = std::move(output);
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
    ConversationCapture *capture = nullptr; // scoped to the serialized caller task
    const mira::ModelProfile &profile() const override { return provider_->profile(); }
    mira::Result<mira::ModelResponse> infer(const mira::ModelRequest &request,
                                            const mira::OperationContext &context,
                                            const mira::ProviderInferOptions &options) override {
        auto result = provider_->infer(request, context, options);
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
    ~CaptureScope() { provider.capture = nullptr; }
};
} // namespace mirage::integration::detail
