#include "../../integration/mira/src/tool_alias_provider.hpp"
#include "../support/test.hpp"

#include <mira/model_dialect.hpp>
#include <mira/model_tool.hpp>

#include <algorithm>
#include <memory>
#include <string>
#include <utility>

namespace {
using namespace mira;
namespace detail = mirage::integration::detail;

class WireProvider final : public IModelProvider {
  public:
    explicit WireProvider(ProtocolDialect dialect) {
        profile_.id = ModelProfileId::generate();
        profile_.dialect = dialect;
        profile_.model_selector = "fixture";
        profile_.endpoint_origin = "https://fixture.example";
        profile_.api_prefix = "/v1";
        profile_.capabilities.text.supported = true;
        profile_.capabilities.function_tools.supported = true;
        profile_.capabilities.sse.supported = true;
        trace_.headers_received = true;
        if (dialect == ProtocolDialect::OpenAIChatCompletionsV1)
            mapper_ = std::make_unique<ChatCompletionsV1Mapper>();
        else if (dialect == ProtocolDialect::OpenAIResponsesV1)
            mapper_ = std::make_unique<ResponsesV1Mapper>();
        else
            mapper_ = std::make_unique<AnthropicMessagesV1Mapper>();
    }
    const ModelProfile &profile() const override { return profile_; }
    const TransportTrace &last_trace() const override { return trace_; }
    std::optional<std::chrono::milliseconds> last_retry_after_hint() const override {
        return std::chrono::milliseconds{123};
    }
    Result<ModelResponse> infer(const ModelRequest &request, const OperationContext &,
                                const ProviderInferOptions &options) override {
        ++calls;
        observed = request;
        const auto encoded = mapper_->encode_request(request, profile_, options.stream, artifacts_);
        if (!encoded)
            return encoded.error();
        wire = encoded.value();
        if (fail)
            return make_model_error(ModelDomainCode::TransportFailed, "fixture transport", true);
        if (unknown_name || wrong_id) {
            ModelResponse response;
            ToolCallOutput call;
            call.tool_id = wrong_id ? ToolId::generate() : request.tools.front().tool_id;
            call.provider_name = unknown_name ? "undeclared" : request.tools.front().wire_name;
            response.output.emplace_back(std::move(call));
            return response;
        }
        WireHttpResponse response;
        response.status = 200;
        if (profile_.dialect == ProtocolDialect::OpenAIChatCompletionsV1)
            response.body =
                R"({"id":"chat","model":"fixture","choices":[{"message":{"role":"assistant","content":null,"tool_calls":[{"id":"response-call","type":"function","function":{"name":"desktop_window_list","arguments":"{\"text\":\"desktop.window.list\"}"}}]},"finish_reason":"tool_calls"}],"usage":{"prompt_tokens":2,"completion_tokens":1}})";
        else if (profile_.dialect == ProtocolDialect::OpenAIResponsesV1)
            response.body =
                R"({"id":"response","status":"completed","model":"fixture","output":[{"type":"function_call","call_id":"response-call","name":"desktop_window_list","arguments":"{\"text\":\"desktop.window.list\"}"}],"usage":{}})";
        else
            response.body =
                R"({"id":"message","type":"message","role":"assistant","model":"fixture","content":[{"type":"tool_use","id":"response-call","name":"desktop_window_list","input":{"text":"desktop.window.list"}}],"stop_reason":"tool_use","usage":{"input_tokens":2,"output_tokens":1}})";
        return mapper_->decode_response(request, profile_, response);
    }
    ModelProfile profile_;
    TransportTrace trace_;
    ModelRequest observed;
    JsonValue wire;
    int calls = 0;
    bool fail = false, unknown_name = false, wrong_id = false;

  private:
    std::unique_ptr<IDialectMapper> mapper_;
    NullArtifactSource artifacts_;
};

ModelRequest request_for(const ModelProfile &profile) {
    ModelRequest request;
    request.contract_version = {1, 0};
    request.request_id = ModelRequestId::generate();
    request.operation_id = OperationId::generate();
    request.task_id = TaskId::generate();
    request.profile_id = profile.id;
    request.output_contract.mode = OutputMode::Text;
    request.generation.max_output_tokens = 64;
    ExposedToolSpec tool;
    tool.tool_id = ToolId::generate();
    tool.version = {1, 0, 0};
    tool.wire_name = "desktop.window.list";
    tool.description = "Use desktop.window.list or window.list";
    tool.parameters_schema.root =
        parse_json(
            R"({"type":"object","properties":{"text":{"type":"string"}},"additionalProperties":false})")
            .value();
    request.tools.push_back(tool);
    request.tool_choice = {ToolChoiceMode::Named, tool.tool_id};
    ModelInputItem user;
    user.role = ModelRole::User;
    user.content.emplace_back(TextPart{"Literal desktop.window.list", Sensitivity::Public});
    request.input.push_back(user);
    ModelInputItem assistant;
    assistant.role = ModelRole::Assistant;
    ToolCallPart call;
    call.wire_name = tool.wire_name;
    call.provider_call_id.value = "prior-call";
    call.arguments = parse_json(R"({"text":"desktop.window.list"})").value();
    call.arguments_digest = canonical_json_digest(call.arguments);
    assistant.content.emplace_back(call);
    request.input.push_back(assistant);
    ModelInputItem result;
    result.role = ModelRole::User;
    ToolResultPart part;
    part.tool_id = tool.tool_id;
    part.provider_call_id = call.provider_call_id;
    part.result = parse_json(R"({"text":"desktop.window.list"})").value();
    result.content.emplace_back(part);
    request.input.push_back(result);
    return request;
}

void wire_roundtrip(ProtocolDialect dialect, bool stream) {
    auto delegate = std::make_shared<WireProvider>(dialect);
    auto provider = detail::make_tool_alias_provider(delegate);
    auto request = request_for(delegate->profile());
    ProviderInferOptions options;
    options.stream = stream;
    const auto response = provider->infer(request, {}, options);
    MIRAGE_CHECK(response.has_value());
    if (!response)
        return;
    const auto &tools = *delegate->wire.find("tools")->as_array();
    const auto &definition = dialect == ProtocolDialect::OpenAIChatCompletionsV1
                                 ? *tools.front().find("function")
                                 : tools.front();
    MIRAGE_CHECK(*definition.find("name")->as_string() == "desktop_window_list");
    MIRAGE_CHECK(*definition.find("description")->as_string() ==
                 "Use desktop_window_list or desktop_window_list");
    const auto &choice = *delegate->wire.find("tool_choice");
    const auto &named =
        dialect == ProtocolDialect::OpenAIChatCompletionsV1 ? *choice.find("function") : choice;
    MIRAGE_CHECK(*named.find("name")->as_string() == "desktop_window_list");
    const auto *stream_field = delegate->wire.find("stream");
    MIRAGE_CHECK((stream_field && stream_field->as_boolean().value_or(false)) == stream);
    const auto body = to_json_string(delegate->wire);
    MIRAGE_CHECK(body.find("prior-call") != std::string::npos);
    MIRAGE_CHECK(body.find("Literal desktop.window.list") != std::string::npos);
    const auto &replayed = std::get<ToolCallPart>(delegate->observed.input[1].content[0]);
    MIRAGE_CHECK(replayed.wire_name == "desktop_window_list" &&
                 replayed.arguments ==
                     std::get<ToolCallPart>(request.input[1].content[0]).arguments);
    const auto &result = std::get<ToolResultPart>(delegate->observed.input[2].content[0]);
    MIRAGE_CHECK(result.provider_call_id.value == "prior-call" &&
                 result.tool_id == request.tools.front().tool_id &&
                 result.result == std::get<ToolResultPart>(request.input[2].content[0]).result);
    const auto &call = std::get<ToolCallOutput>(response.value().output.front());
    MIRAGE_CHECK(call.provider_name == "desktop.window.list" &&
                 call.provider_call_id.value == "response-call" &&
                 call.tool_id == request.tools.front().tool_id);
    MIRAGE_CHECK(*call.arguments.find("text")->as_string() == "desktop.window.list");
    MIRAGE_CHECK(resolve_tool_calls(request, response.value()).has_value());
    MIRAGE_CHECK(request.tools.front().wire_name == "desktop.window.list" &&
                 std::get<ToolCallPart>(request.input[1].content[0]).wire_name ==
                     "desktop.window.list");
    MIRAGE_CHECK(provider->last_trace().headers_received &&
                 provider->last_retry_after_hint() == std::chrono::milliseconds{123});
    delegate->unknown_name = true;
    const auto unknown = provider->infer(request, {}, options);
    MIRAGE_CHECK(!unknown && !unknown.error().retryable);
    delegate->unknown_name = false;
    delegate->wrong_id = true;
    MIRAGE_CHECK(!provider->infer(request, {}, options));
    delegate->wrong_id = false;
    delegate->fail = true;
    const auto failed = provider->infer(request, {}, options);
    MIRAGE_CHECK(!failed && failed.error().retryable &&
                 failed.error().safe_message == "fixture transport");
    delegate->fail = false;
    auto collision = request.tools.front();
    collision.tool_id = ToolId::generate();
    collision.wire_name = "desktop_window_list";
    request.tools.push_back(collision);
    const auto calls = delegate->calls;
    MIRAGE_CHECK(!provider->infer(request, {}, options) && delegate->calls == calls);
}
} // namespace

int main() {
    MIRAGE_CHECK(detail::model_tool_name("desktop.window.list") == "desktop_window_list");
    MIRAGE_CHECK(detail::model_tool_name("alpha-beta") == "alpha_beta");
    MIRAGE_CHECK(detail::model_tool_name("wait") == "wait");
    MIRAGE_CHECK(detail::model_tool_name("9tool") == "tool_9tool");
    MIRAGE_CHECK(detail::model_tool_name("_tool") == "tool__tool");
    MIRAGE_CHECK(detail::model_tool_name(std::string(64, 'a')) == std::string(64, 'a'));
    MIRAGE_CHECK(detail::model_tool_name("9" + std::string(58, 'a')).size() == 64);
    for (const auto &invalid : {std::string{}, std::string(65, 'a'), "9" + std::string(59, 'a'),
                                std::string{"工具"}, std::string{"a/b"}})
        MIRAGE_CHECK(detail::model_tool_name(invalid).empty());
    ExposedToolSpec tool;
    tool.wire_name = "desktop.window.list";
    std::string guidance = "Use desktop.window.list or window.list; preserve other.window.list and "
                           "desktop.window.list_extra.";
    detail::alias_host_tool_names(guidance, std::span<const ExposedToolSpec>{&tool, 1});
    MIRAGE_CHECK(guidance == "Use desktop_window_list or desktop_window_list; preserve "
                             "other.window.list and desktop.window.list_extra.");
    for (const auto dialect :
         {ProtocolDialect::OpenAIChatCompletionsV1, ProtocolDialect::AnthropicMessagesV1,
          ProtocolDialect::OpenAIResponsesV1})
        for (const bool stream : {false, true})
            wire_roundtrip(dialect, stream);
    return mirage::testing::finish("tool_alias_provider_test");
}
