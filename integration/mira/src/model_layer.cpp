#include <mirage/integration/model_layer.hpp>

#include <mira/adapters/net/socket_transport.hpp>
#include <mira/conversation_loop.hpp>
#include <mira/environment.hpp>
#include <mira/event_store.hpp>
#include <mira/model_gateway.hpp>
#include <mira/model_profile.hpp>
#include <mira/model_provider.hpp>
#include <mira/tool_executor.hpp>

#ifdef MIRAGE_HAVE_OPENSSL_TLS
#include <mira/adapters/net/openssl_tls.hpp>
#endif

#include "model_layer_env.hpp"

#include <algorithm>
#include <mutex>
#include <utility>

namespace pinned_net = mira::adapters::net;

namespace mirage::integration {
namespace {
std::optional<mira::ReasoningEffort> reasoning_value(const std::string &value) {
    if (value == "minimal")
        return mira::ReasoningEffort::Minimal;
    if (value == "low")
        return mira::ReasoningEffort::Low;
    if (value == "medium")
        return mira::ReasoningEffort::Medium;
    if (value == "high")
        return mira::ReasoningEffort::High;
    return {};
}
} // namespace
namespace {

/// Resolves SecretRef names against the process environment at the transport
/// boundary only (pinned secret discipline): the plaintext never leaves the
/// transport, never enters events or digests. An unset variable fails closed
/// (PermissionDenied) instead of sending an unauthenticated request.
class ProfileSecretResolver final : public mira::ISecretResolver {
  public:
    explicit ProfileSecretResolver(const ModelLayerConfig &config)
        : lookup_(config.credential_lookup) {}
    [[nodiscard]] mira::Result<std::string> resolve(const mira::SecretRef &reference) override {
        mira::Error error;
        const bool stored = reference.name.starts_with("mirage:");
        const auto resolved =
            stored && lookup_ ? lookup_(reference.name.substr(7)) : std::optional<std::string>{};
        const auto value =
            stored ? resolved.value_or("")
                   : (reference.name.empty() ? std::string{}
                                             : detail::read_environment_value(reference.name));
        if (value.empty()) {
            error.code = mira::ErrorCode::PermissionDenied;
            error.domain = "mirage.dialog";
            error.safe_message =
                stored ? "系统保存的 API Key 不可用，请解锁钥匙环或重新配置。"
                       : "credential environment variable is not set: " + reference.name;
            return error;
        }
        return value;
    }

  private:
    std::function<std::optional<std::string>(const std::string &)> lookup_;
};

} // namespace

void capture_context_usage(DialogCompletion &completion, const mira::ModelResponse &response,
                           const ModelLayerConfig &config) {
    if (response.usage.quality == mira::UsageQuality::Exact ||
        response.usage.quality == mira::UsageQuality::ProviderReported) {
        if (response.usage.input_tokens && *response.usage.input_tokens <= 2000000000)
            completion.input_tokens = response.usage.input_tokens;
        completion.context_window_tokens = config.context_window_tokens;
        completion.usage_model = config.model_selector;
    }
}

bool ModelLayerConfig::valid(std::string &error) const {
    if (context_window_tokens != 0 &&
        (context_window_tokens < 2048 || context_window_tokens > 2000000)) {
        error = "context window must be 0 (unknown) or between 2048 and 2000000 tokens";
        return false;
    }
    if (!credential_ref.empty() &&
        (credential_ref.size() != 32 ||
         credential_ref.find_first_not_of("0123456789abcdef") != std::string::npos)) {
        error = "invalid credential reference";
        return false;
    }
    if (credential_env.size() > 128 ||
        (!credential_env.empty() &&
         (credential_env.find_first_not_of(
              "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_") !=
              std::string::npos ||
          (credential_env.front() >= '0' && credential_env.front() <= '9')))) {
        error = "credential must be an environment variable name";
        return false;
    }
    if (model_selector.size() > 1024 || display_name.size() > 256 ||
        endpoint_origin.size() > 2048 || api_prefix.size() > 2048) {
        error = "model configuration exceeds field limits";
        return false;
    }
    if (!endpoint_origin.empty()) {
        const auto origin_start = endpoint_origin.starts_with("https://")  ? 8u
                                  : endpoint_origin.starts_with("http://") ? 7u
                                                                           : 0u;
        if (origin_start == 0 || endpoint_origin.size() <= origin_start ||
            endpoint_origin.find_first_of("/@?# \t\r\n", origin_start) != std::string::npos) {
            error = "endpoint must be an HTTP(S) origin; put its path in API prefix";
            return false;
        }
    }
    if (!api_prefix.empty() &&
        (!api_prefix.starts_with("/") || api_prefix.find_first_of("?#\r\n") != std::string::npos)) {
        error = "API prefix must be an absolute URL path";
        return false;
    }
    if (dialect == "anthropic.messages.v1" && supports_reasoning) {
        error = "Anthropic extended thinking is not supported by this adapter";
        return false;
    }
    if (!enabled) {
        return true;
    }
    if (model_selector.empty()) {
        error = "model layer requires a model selector";
        return false;
    }
    if (!mira::protocol_dialect_from(dialect)) {
        error = "model layer dialect is not a known pinned dialect";
        return false;
    }
    if (max_output_tokens == 0 || max_output_tokens > 16384) {
        error = "model layer requires an output token bound between 1 and 16384";
        return false;
    }
    if (request_deadline.count() <= 0 || max_input_bytes == 0) {
        error = "model layer requires positive deadline and input bounds";
        return false;
    }
    return true;
}

struct ModelLayer::Impl {
    explicit Impl(executor::Executor &owner_executor, const ModelLayerConfig &layer_config,
                  ModelProviderOverride::Factory factory)
        : config(layer_config), executor(owner_executor), override_factory(std::move(factory)) {}

    bool assemble() {
        const auto dialect = mira::protocol_dialect_from(config.dialect);
        if (!dialect) {
            return false;
        }
        if (!override_factory && config.endpoint_origin.empty()) {
            return false; // the socket stack dials a fixed origin; none configured
        }
        secrets = std::make_shared<ProfileSecretResolver>(config);
        profile = std::make_shared<mira::ModelProfile>(build_profile(*dialect));
        router.register_profile(profile);
        if (!override_factory) {
            // Production form: the pinned socket HTTP/SSE transport (with the
            // OpenSSL TLS channel when built). The pinned provider denies
            // private/loopback endpoints by design (SSRF posture).
            pinned_net::SocketTransportConfig transport_config;
            // Replacement is assembled before the old layer is released. Each
            // profile needs a distinct Executor worker identity during that overlap.
            transport_config.worker_name = "mirage-model-" + profile->id.to_string();
            transport = std::make_shared<pinned_net::SocketHttpTransport>(
                executor, secrets, make_tls_factory(), std::move(transport_config));
            if (!transport->start()) {
                transport.reset();
                return false;
            }
            provider = std::make_shared<mira::OpenAiCompatibleProvider>(profile, transport,
                                                                        /*artifacts=*/nullptr);
            // `transport` stays shared here so shutdown() can settle its
            // in-flight exchanges in the pinned order (transport first).
        } else {
            provider = override_factory(*profile);
        }
        gateway = std::make_unique<mira::ModelGateway>(executor, std::move(router),
                                                       /*artifacts=*/nullptr, mira::PriceTable{},
                                                       mira::ModelGatewayConfig{});
        gateway->register_provider(provider);
        gateway->set_event_store(events, mira::RuntimeId::generate(), mira::SessionId::generate());
        return true;
    }

    [[nodiscard]] mira::ModelProfile build_profile(mira::ProtocolDialect dialect_value) const {
        mira::ModelProfile record;
        record.id = mira::ModelProfileId::generate();
        record.display_name = config.display_name.empty() ? "dialog-model" : config.display_name;
        record.version = mira::SemanticVersion{1, 0, 0};
        record.dialect = dialect_value;
        record.endpoint_origin = config.endpoint_origin;
        record.api_prefix = config.api_prefix.empty() ? std::string{"/v1"} : config.api_prefix;
        record.model_selector = config.model_selector;
        if (config.context_window_tokens)
            record.capabilities.limits.max_context_tokens = config.context_window_tokens;
        // The credential rides a SecretRef naming an environment variable and
        // is resolved inside the transport only (pinned secret discipline);
        // an empty name means the profile carries no credential.
        record.credential =
            mira::SecretRef{config.credential_ref.empty() ? config.credential_env
                                                          : "mirage:" + config.credential_ref};
        // Pure-dialog needs text only; capabilities are declared at the
        // evidence the pinned fixtures provide, never claimed beyond that.
        if (config.supports_reasoning)
            record.capabilities.generation.reasoning_effort = mira::ParamMapping::OmitIfUnset;
        record.capabilities.text =
            mira::CapabilityFlag{true, mira::CapabilityEvidence::FixtureVerified, ""};
        record.capabilities.function_tools =
            mira::CapabilityFlag{true, mira::CapabilityEvidence::FixtureVerified, ""};
        record.capabilities.sse =
            mira::CapabilityFlag{true, mira::CapabilityEvidence::FixtureVerified, ""};
        record.deadlines.total = config.request_deadline;
        record.deadlines.first_byte = std::chrono::milliseconds{
            std::min<std::int64_t>(config.request_deadline.count(), 60'000)};
        record.default_data_policy.store = false;
        return record;
    }

    [[nodiscard]] std::shared_ptr<mira::ITlsChannelFactory> make_tls_factory() const {
#ifdef MIRAGE_HAVE_OPENSSL_TLS
        const auto scheme_end = config.endpoint_origin.find("://");
        if (scheme_end != std::string::npos &&
            config.endpoint_origin.substr(0, scheme_end) == "https") {
            // MIRA-20261004-002: pinned PR #76 supplies SNI in the TLS adapter.
            auto factory = std::make_shared<pinned_net::OpenSslTlsChannelFactory>();
            if (!factory->initialize())
                return nullptr;
            return factory;
        }
#endif
        // Without a TLS channel adapter, https endpoints fail closed at the
        // transport boundary before any bytes are written (pinned
        // discipline) — the layer never downgrades to plaintext.
        return nullptr;
    }

    ModelLayerConfig config;
    executor::Executor &executor;
    /// Serializes complete_dialog_turn against shutdown: an in-flight
    /// inference finishes (bounded by the profile transport deadlines) before
    /// the gateway/provider/transport are released — destroying them under a
    /// running infer is a use-after-free the sanitizers rightly flag.
    std::mutex infer_mutex;
    ModelProviderOverride::Factory override_factory;
    std::shared_ptr<mira::ModelProfile> profile;
    mira::ModelRouter router;
    std::shared_ptr<mira::ISecretResolver> secrets;
    std::shared_ptr<pinned_net::SocketHttpTransport> transport;
    std::shared_ptr<mira::IModelProvider> provider;
    std::unique_ptr<mira::ModelGateway> gateway;
    std::shared_ptr<mira::MemoryEventStore> events = std::make_shared<mira::MemoryEventStore>();
    bool running = false;
};

ModelLayer::ModelLayer(executor::Executor &executor, const ModelLayerConfig &config,
                       const ModelProviderOverride *provider_override)
    : impl_(std::make_unique<Impl>(executor, config,
                                   provider_override != nullptr ? provider_override->factory()
                                                                : nullptr)) {
    impl_->running = impl_->assemble();
}

ModelLayer::~ModelLayer() { shutdown(); }

ModelProviderOverride::ModelProviderOverride(Factory factory) : factory_(std::move(factory)) {}

ModelProviderOverride::~ModelProviderOverride() = default;

std::shared_ptr<mira::IModelProvider>
ModelProviderOverride::create(const mira::ModelProfile &profile) const {
    return factory_(profile);
}

bool ModelLayer::running() const { return impl_ != nullptr && impl_->running; }

const ModelLayerConfig &ModelLayer::config() const { return impl_->config; }

void ModelLayer::shutdown() {
    if (impl_ == nullptr) {
        return;
    }
    // Wait out any in-flight dialog completion first (the drain lock), then
    // release the pinned pieces — never under a running infer.
    std::lock_guard drain(impl_->infer_mutex);
    if (impl_->transport) {
        impl_->transport->shutdown();
        impl_->transport.reset();
    }
    impl_->gateway.reset();
    impl_->provider.reset();
    impl_->running = false;
}

DialogCompletion ModelLayer::complete_dialog_turn(const std::string &transcript,
                                                  const std::string &user_text,
                                                  const mira::OperationContext &context,
                                                  const std::string &reasoning, bool tools_allowed,
                                                  DialogPreviewSink preview) {
    DialogCompletion completion;
    // The drain lock makes shutdown wait out this inference (bounded by the
    // profile transport deadlines) instead of destroying the pinned pieces
    // under it.
    if (!reasoning.empty() &&
        (!impl_ || !impl_->config.supports_reasoning || !reasoning_value(reasoning))) {
        completion.failed = true;
        completion.error = "unsupported reasoning effort";
        return completion;
    }
    std::lock_guard drain(impl_->infer_mutex);
    if (impl_ == nullptr || !impl_->running || !impl_->gateway) {
        completion.failed = true;
        completion.error = "model layer is not running";
        return completion;
    }
    if (context.cancelled()) {
        completion.cancelled = true;
        return completion;
    }
    // Bounded input: the rendered transcript plus the new user text must fit
    // the configured budget (RULE-07) — refuse instead of silently cropping.
    if (transcript.size() + user_text.size() > impl_->config.max_input_bytes) {
        completion.failed = true;
        completion.error = "dialog turn exceeds the configured input budget";
        return completion;
    }

    mira::ModelRequest request;
    request.contract_version = mira::SchemaVersion{1, 0};
    request.request_id = mira::ModelRequestId::generate();
    request.operation_id = mira::OperationId::generate();
    request.task_id = mira::TaskId::generate();
    request.task_epoch = 0;
    request.profile_id = impl_->profile->id;

    // System authority: fixed dialog persona (provenance-labeled, internal
    // sensitivity — the same discipline the pinned loop applies).
    mira::ModelInputItem system_item;
    system_item.role = mira::ModelRole::System;
    system_item.provenance.source = "mirage.dialog.system.v1";
    system_item.authority = mira::Sensitivity::Internal;
    mira::TextPart system_text;
    system_text.text =
        "You are Mirage, a desktop assistant. Answer the user's request in the user's "
        "language, concisely and factually. You cannot execute desktop actions in this mode.";
    system_text.sensitivity = mira::Sensitivity::Internal;
    system_item.content.emplace_back(std::move(system_text));

    // User authority: the caller-rendered transcript block plus the new user
    // message, labeled untrusted.
    mira::ModelInputItem user_item;
    user_item.role = mira::ModelRole::User;
    user_item.provenance.source = "mirage.dialog.context.v1";
    user_item.authority = mira::Sensitivity::Internal;
    if (!transcript.empty()) {
        mira::TextPart history_text;
        history_text.text = "Earlier conversation:\n" + transcript;
        history_text.sensitivity = mira::Sensitivity::Internal;
        user_item.content.emplace_back(std::move(history_text));
    }
    mira::TextPart prompt_text;
    prompt_text.text = user_text;
    prompt_text.sensitivity = mira::Sensitivity::Internal;
    user_item.content.emplace_back(std::move(prompt_text));
    request.input = {std::move(system_item), std::move(user_item)};

    (void)tools_allowed;
    // Pure dialog: Text output, no tools, one request per turn.
    request.output_contract.mode = mira::OutputMode::Text;
    request.generation.reasoning_effort = reasoning_value(reasoning);
    request.generation.max_output_tokens = impl_->config.max_output_tokens;
    request.budget.max_output_tokens = impl_->config.max_output_tokens;
    request.budget.max_requests = 1;
    request.data_policy.store = false;
    request.prompt_provenance.system_template_digest =
        mira::digest_string("mirage.dialog.system.v1");

    mira::InferOptions options;
    options.stream = static_cast<bool>(preview);
    if (preview)
        options.preview_sink = [preview = std::move(preview)](const auto &id, const auto &value) {
            preview(id.to_string(), value.text, value.truncated);
        };
    const auto outcome = impl_->gateway->infer(request, context, options);
    if (!outcome) {
        completion.failed = true;
        completion.error = "model layer request failed: " + outcome.error().safe_message;
        return completion;
    }
    if (context.cancelled()) {
        completion.cancelled = true;
        return completion;
    }
    const auto &response = outcome.value().response;
    if (outcome.value().admitted == false) {
        completion.failed = true;
        completion.error = "model layer rejected the request: " + outcome.value().rejection_reason;
        return completion;
    }
    switch (response.status) {
    case mira::ModelCompletionStatus::Completed:
        break;
    case mira::ModelCompletionStatus::Cancelled:
        completion.cancelled = true;
        return completion;
    case mira::ModelCompletionStatus::Refused:
    case mira::ModelCompletionStatus::ContentFiltered:
    case mira::ModelCompletionStatus::Failed:
    case mira::ModelCompletionStatus::Incomplete:
    case mira::ModelCompletionStatus::Unknown:
        completion.failed = true;
        completion.error = "model layer did not complete the turn (status " +
                           std::to_string(static_cast<int>(response.status)) + ")";
        return completion;
    }

    // Extract the assistant text: MessageOutput's OutputTextPart parts joined
    // in order; refusals and unknown items surface as a stable failure.
    std::string reply;
    for (const auto &item : response.output) {
        if (auto *message = std::get_if<mira::MessageOutput>(&item)) {
            for (const auto &part : message->content) {
                if (auto *text = std::get_if<mira::OutputTextPart>(&part)) {
                    if (!reply.empty()) {
                        reply += "\n";
                    }
                    reply += text->text;
                } else if (auto *refusal = std::get_if<mira::OutputRefusalPart>(&part)) {
                    completion.failed = true;
                    completion.error = "model refused the request: " + refusal->safe_summary;
                    return completion;
                }
            }
        } else if (auto *model_refusal = std::get_if<mira::RefusalOutput>(&item)) {
            completion.failed = true;
            completion.error = "model refused the request: " + model_refusal->safe_summary;
            return completion;
        }
    }
    if (reply.empty()) {
        completion.failed = true;
        completion.error = "model returned no reply text";
        return completion;
    }
    capture_context_usage(completion, response, impl_->config);
    completion.ok = true;
    completion.reply_text = std::move(reply);
    return completion;
}

// MIRA-20261004-001: use the delivered no-observation ConversationLoop (PR #76).
DialogCompletion ModelLayer::complete_harness_turn(const std::string &transcript,
                                                   const std::string &user_text,
                                                   const mira::OperationContext &context,
                                                   const std::string &reasoning, bool tools_allowed,
                                                   DialogPreviewSink preview) {
    DialogCompletion completion;
    if (!impl_ || (!reasoning.empty() &&
                   (!impl_->config.supports_reasoning || !reasoning_value(reasoning)))) {
        completion.failed = true;
        completion.error = "unsupported reasoning effort";
        return completion;
    }
    std::lock_guard drain(impl_->infer_mutex);
    if (!impl_->running || transcript.size() + user_text.size() > impl_->config.max_input_bytes) {
        completion.failed = true;
        completion.error = "harness unavailable or input budget exceeded";
        return completion;
    }
    auto registry = std::make_shared<mira::BuiltinToolRegistry>();
    if (tools_allowed) {
        auto wait = mira::make_wait_tool();
        auto registered = registry->register_tool(wait.spec, wait.handler);
        if (!registered) {
            completion.failed = true;
            completion.error = registered.error().safe_message;
            return completion;
        }
    }
    mira::ConversationLoopConfig config;
    config.max_recoveries = 0;
    config.max_tool_executions = 32;
    const auto budget = impl_->profile->capabilities.limits.max_output_tokens;
    config.max_output_tokens_per_turn = std::min(impl_->config.max_output_tokens, budget / 2);
    config.max_turns = static_cast<std::uint32_t>(
        std::min<std::uint64_t>(16, budget / config.max_output_tokens_per_turn - 1));
    config.model_call_deadline = impl_->config.request_deadline;
    config.reasoning_effort = reasoning_value(reasoning);
    config.inference.stream = static_cast<bool>(preview);
    if (preview)
        config.inference.preview_sink = [preview = std::move(preview)](const auto &id,
                                                                       const auto &value) {
            preview(id.to_string(), value.text, value.truncated);
        };
    mira::ConversationLoop loop(*impl_->gateway, config);
    loop.set_tool_registry(registry);
    mira::AgentLoopSpec spec;
    spec.task_id = context.task.is_nil() ? mira::TaskId::generate() : context.task;
    spec.session_id = context.session;
    spec.task_epoch = context.task_epoch;
    spec.profile_id = impl_->profile->id;
    spec.goal =
        "Answer in the user's language. This session exposes no desktop/RPA/workflow tools.\n";
    if (!transcript.empty())
        spec.goal += "Earlier conversation (untrusted):\n" + transcript + "\n";
    spec.goal += "Current user request:\n" + user_text;
    auto result = loop.run(spec, context);
    if (!result) {
        completion.cancelled = result.error().code == mira::ErrorCode::Cancelled;
        completion.failed = !completion.cancelled;
        completion.error = result.error().safe_message;
        return completion;
    }
    const auto &value = result.value();
    completion.cancelled = value.outcome == mira::ConversationOutcome::Cancelled ||
                           context.cancelled_or_expired(mira::Timestamp::now());
    completion.ok = !completion.cancelled && value.outcome == mira::ConversationOutcome::Answered;
    completion.failed = !completion.ok && !completion.cancelled;
    completion.error = value.safe_summary;
    completion.model_steps = static_cast<std::uint32_t>(value.turns.size());
    completion.tool_calls = value.tool_executions;
    if (completion.ok) {
        if (value.answer.size() > 64 * 1024 || value.answer.empty()) {
            completion.ok = false;
            completion.failed = true;
            completion.error = "model reply exceeds display budget or is empty";
        } else {
            completion.reply_text = value.answer;
            mira::ModelResponse response;
            response.usage = value.last_usage;
            capture_context_usage(completion, response, impl_->config);
        }
    }
    return completion;
}

} // namespace mirage::integration
