#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace executor {
class Executor;
}

namespace mira {
class IModelProvider;
struct ModelProfile;
struct OperationContext;
} // namespace mira

namespace mirage::integration {

/// Configuration of the service-side model layer (DEC-027, M5-06; DEC-025
/// backlog item 3). Mirrored onto `ServiceConfig::model` (a plain struct, no
/// pinned types cross the runtime public header). `enabled = false` keeps the
/// dialog face dark: `session.chat` answers the stable `unavailable` error
/// and hello reports no `chat` capability (the `permissions`-bit precedent
/// for equipment-dependent faces).
struct ModelLayerConfig {
    bool enabled = false;
    bool supports_reasoning = false;
    /// Pinned wire dialect of the profile (pinned protocol_dialect_from
    /// vocabulary): "openai.responses.v1" or "openai.chat-completions.v1".
    std::string dialect = "openai.responses.v1";
    std::string display_name = "dialog-model";
    /// Fixed provider origin, e.g. "http://127.0.0.1:8080" or
    /// "https://api.example.com". Mappers never accept model-supplied URLs.
    std::string endpoint_origin;
    /// Dialect API prefix, e.g. "/v1".
    std::string api_prefix = "/v1";
    /// Model alias sent on the wire.
    std::string model_selector;
    /// Environment variable carrying the API key; resolved through
    /// SecretRef at the transport boundary only (never logged, never stored
    /// in events). Empty means the profile carries no credential.
    std::string credential_env;
    std::string credential_ref;
    std::function<std::optional<std::string>(const std::string &)> credential_lookup;
    /// Whole-request budget mirrored into the profile transport deadlines.
    std::chrono::milliseconds request_deadline{120'000};
    /// Per-request generation bound (1..16384); harness also has a whole-loop token budget.
    std::uint64_t max_output_tokens = 2048;
    std::uint64_t context_window_tokens = 0; ///< DEC-036: explicit window budget; 0 unknown
    /// Per-turn input text budget (user text plus rendered transcript).
    std::size_t max_input_bytes = 64ULL * 1024ULL;

    /// Fail-closed validation for an enabled layer: a known dialect and a
    /// model selector are mandatory; the endpoint origin is required only
    /// when the pinned socket stack is assembled (an override provider does
    /// not dial any origin). Disabled layers still validate bounded field syntax.
    [[nodiscard]] bool valid(std::string &error) const;
};

/// Composer choices are derived from documented model capabilities (DEC-046).
struct ReasoningOption {
    std::string value;
    std::string label;
};
[[nodiscard]] std::vector<ReasoningOption>
reasoning_options(const std::string &dialect, const std::string &model, bool declared = false);

/// Outcome of one dialog inference (DEC-027). `ok` carries `reply_text`;
/// `failed` marks a settled turn with a stable `error` reason (safe for UI);
/// `cancelled` mirrors the caller's cancellation probe.
struct DialogCompletion {
    bool ok = false;
    bool failed = false;
    bool cancelled = false;
    std::string error;
    std::string reply_text;
    std::uint32_t model_steps = 0;
    std::uint32_t tool_calls = 0;
    std::optional<std::uint64_t> input_tokens; // final successful request, provider reported
    std::uint64_t context_window_tokens = 0;
    std::string usage_model;
};

/// Opaque carrier for a caller-supplied pinned model provider (DEC-027 test
/// and extension seam): keeps the pinned type out of the runtime public
/// header while letting an embedding (ServiceConfig, tests) replace the
/// socket-backed provider with a scripted one. Nullptr inside means "build
/// the pinned socket stack normally".
class ModelProviderOverride {
  public:
    /// Builds the scripted provider bound to exactly the profile the layer
    /// assembled (the gateway routes by profile id, so the provider must
    /// expose the same identity).
    using Factory =
        std::function<std::shared_ptr<mira::IModelProvider>(const mira::ModelProfile &profile)>;
    explicit ModelProviderOverride(Factory factory);
    ~ModelProviderOverride();
    ModelProviderOverride(const ModelProviderOverride &) = delete;
    ModelProviderOverride &operator=(const ModelProviderOverride &) = delete;

    [[nodiscard]] std::shared_ptr<mira::IModelProvider>
    create(const mira::ModelProfile &profile) const;

    [[nodiscard]] const Factory &factory() const { return factory_; }

  private:
    Factory factory_;
};

/// Full transient snapshot, at most 16KiB. Invoked on the transport context;
/// consumers perform bounded delivery only. Canonical results settle separately.
using DialogPreviewSink =
    std::function<void(const std::string &request_id, const std::string &text, bool truncated)>;

/// The service-side model layer (DEC-027): assembles the pinned model stack —
/// ModelProfile + ModelRouter + OpenAiCompatibleProvider over the pinned
/// SocketHttpTransport (+ the OpenSSL TLS channel when the pinned adapter was
/// built; https endpoints fail closed without it, never downgrade) + the
/// ModelGateway — and serves one bounded pure-dialog completion per call:
/// a Text-mode ModelRequest with the dialog system prompt, the caller-rendered
/// transcript block and the new user text. No tools, no desktop observation —
/// the desktop agent loop stays a separate pinned surface (DEC-008 step 2's
/// pure-dialog form).
///
/// Threading: constructed and shut down by the service owner (the same
/// Executor that backs the transport's blocking I/O workers, EXEC-01);
/// complete_dialog_turn runs on the caller's bounded task. Secrets resolve
/// only inside the transport. The layer keeps no dialog state — the
/// session-facing turn log lives in the service.
class ModelLayer {
  public:
    /// Assembles the pinned socket-backed provider (production form). When
    /// `provider_override` is set, the socket stack is skipped and the
    /// scripted provider serves the gateway instead (tests; the pinned
    /// production provider denies private/loopback endpoints by design).
    ModelLayer(executor::Executor &executor, const ModelLayerConfig &config,
               const ModelProviderOverride *provider_override = nullptr);
    ~ModelLayer();
    ModelLayer(const ModelLayer &) = delete;
    ModelLayer &operator=(const ModelLayer &) = delete;

    [[nodiscard]] bool running() const;
    [[nodiscard]] const ModelLayerConfig &config() const;

    /// One bounded dialog completion. `transcript` is the caller-rendered
    /// earlier-conversation block (may be empty); `user_text` is the new
    /// user message. Bounded by the context deadline / cancellation probe
    /// and the profile transport deadlines.
    DialogCompletion
    complete_dialog_turn(const std::string &transcript, const std::string &user_text,
                         const mira::OperationContext &context, const std::string &reasoning = "",
                         bool tools_allowed = true, DialogPreviewSink preview = {});

    // MIRA-20261004-001: bounded conversational harness, no desktop observation.
    DialogCompletion
    complete_harness_turn(const std::string &transcript, const std::string &user_text,
                          const mira::OperationContext &context, const std::string &reasoning = "",
                          bool tools_allowed = true, DialogPreviewSink preview = {});

    /// Ordered teardown: waits out any in-flight dialog completion (bounded
    /// by the profile transport deadlines), then settles the transport's
    /// in-flight exchanges and releases the pinned pieces. Idempotent.
    void shutdown();

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace mirage::integration
