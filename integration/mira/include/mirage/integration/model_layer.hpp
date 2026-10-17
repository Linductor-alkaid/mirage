#pragma once
#include <mirage/conversation.hpp>
#include <mirage/integration/desktop_tool_context.hpp>

#include <mira/model_contracts.hpp>

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace kairo {
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
    /// Single-request budget mirrored into the profile transport deadlines.
    std::chrono::milliseconds request_deadline{120'000};
    /// DEC-082: whole Agent task bound (0..10 minutes, exclusive lower bound).
    /// Independent of the per-model-call deadline; owned by Runtime Service.
    std::chrono::milliseconds harness_deadline{600'000};
    /// Declared per-request generation bound (0 = undeclared → the 32000
    /// default baseline applies; 1..65536 is used verbatim, never clipped —
    /// the ZCode-aligned declaration semantics). Harness also has a
    /// whole-loop token budget.
    std::uint64_t max_output_tokens = 0;
    std::uint64_t context_window_tokens = 0; ///< DEC-036: explicit window budget; 0 unknown
    /// Per-turn input text budget (user text plus rendered transcript).
    std::size_t max_input_bytes = 64ULL * 1024ULL;

    /// Explicit host declaration; default denies desktop images. Requires screen.capture
    /// permission.
    bool supports_images = false;

    /// DEC-088 capability 1 host switch: wire the capture-fed input-reuse
    /// identity seam (set_model_input_identity). Off keeps requests
    /// byte-identical to the pre-seam layer; acceptance A/B runs use it as
    /// the unannotated control.
    bool supply_input_reuse_identity = true;

    /// Diagnostics sink invoked once per provider-reported model response
    /// (usage only: token counts and quality, never request or reply
    /// content). Acceptance tooling and budget diagnostics aggregate these
    /// into per-request curves; unset keeps the layer silent.
    std::function<void(const mira::ModelUsage &)> usage_observer;

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

/// DEC-085: one classified tool failure class observed in a settled turn.
/// `code` is the stable IPC vocabulary shared with the `session.tool_error`
/// event; `message` is the user-facing composition; `count` counts the
/// failures of this tool within the current environment epoch, floored at 1
/// for an entry that only predates the current epoch (the wire contract
/// requires a positive count).
struct ToolFailure {
    std::string tool;
    /// "platform_limitation" | "permanent_failure" | "invalid_arguments" |
    /// "permission_denied" | "conditional_failure" | "transient_failure"
    std::string code;
    std::string message;
    std::uint32_t count = 0;
};

/// Outcome of one dialog inference (DEC-027). `ok` carries `reply_text`;
/// `failed` marks a settled turn with a stable `error` reason (safe for UI);
/// `cancelled` mirrors the caller's cancellation probe. Cumulative run
/// counters are uint64 (M9-03): a Continuous desktop task has no
/// accumulated step or tool cutoff, so the counts never saturate.
struct DialogCompletion {
    bool ok = false;
    bool failed = false;
    bool cancelled = false;
    std::string error;
    std::string reply_text;
    std::uint64_t model_steps = 0;
    std::uint64_t tool_calls = 0;
    std::optional<std::uint64_t> input_tokens; // final successful request, provider reported
    std::uint64_t context_window_tokens = 0;
    std::string usage_model;
    std::vector<conversation::Part> parts = {};
    std::uint64_t supplied_input_items = 0;
    std::uint64_t supplied_input_dropped = 0;
    std::uint64_t supply_degradations = 0;
    /// DEC-088: requests that carried the input-reuse annotation (third
    /// consecutive identical image supply); zero without an identity seam.
    std::uint64_t supplied_input_reuse_annotations = 0;
    /// DEC-088 capability 2/3: requests whose tool-history replay came from
    /// the Layer 0 compaction seam and requests where the seam degraded to
    /// the raw replay; zero without a declared context window.
    std::uint64_t history_transforms = 0;
    std::uint64_t history_transform_degradations = 0;
    /// DEC-085: classified tool failures of this session (bounded by the
    /// tracker's per-session tool budget); the runtime service projects them
    /// onto `session.tool_error` events.
    std::vector<ToolFailure> tool_failures = {};
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

/// Independent unvalidated, bounded thinking snapshot; consumers only validate/deliver.
/// Empty snapshots clear an attempt, and final canonical parts remain independent.
using DialogThinkingPreviewSink = DialogPreviewSink;

/// Bounded full process snapshot on the caller task; consumers only validate/deliver.
/// The service owns update sequences; final canonical parts are returned in completion.
using DialogProcessSink = std::function<void(const std::vector<conversation::Part> &)>;
// DEC-084: actual provider-call boundaries, independent of returned thinking text.
// Called on the existing serialized infer task. Must be bounded and non-throwing.
using DialogModelActivitySink = std::function<void(bool active)>;

/// The service-side model layer (DEC-027): assembles the pinned model stack —
/// ModelProfile + ModelRouter + OpenAiCompatibleProvider over the pinned
/// SocketHttpTransport (+ the OpenSSL TLS channel when the pinned adapter was
/// built; https endpoints fail closed without it, never downgrade) + the
/// ModelGateway. Pure dialog serves a bounded Text-mode request; the harness
/// uses the pinned ConversationLoop with injected desktop tools. Declared
/// image support enables authorized PNG captures and per-request ImagePart
/// supply (DEC-055), without a second agent loop.
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
    ModelLayer(kairo::Executor &executor, const ModelLayerConfig &config,
               const ModelProviderOverride *provider_override = nullptr,
               DesktopToolContext desktop = {});
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
                         bool tools_allowed = true, DialogPreviewSink preview = {},
                         DialogProcessSink process = {}, DialogModelActivitySink activity = {},
                         DialogThinkingPreviewSink thinking_preview = {});

    /// Bounded ConversationLoop; M7-02 installs the available desktop atoms
    /// from the injected context. tools_allowed=false retains observation
    /// atoms but omits mutations and wait. Pure dialog remains tool-free.
    /// supports_images enables display discovery and authorized capture;
    /// completion reports supply drops/degradations as failures.
    /// M7-08 fixes mutation postconditions before dispatch and observes after
    /// lease settlement. Unverified/uncertain effects prevent success; passing
    /// evidence establishes declared local conditions, not arbitrary intent.
    /// The caller bridges task cancellation into cancel for provider calls.
    DialogCompletion complete_harness_turn(
        const std::string &transcript, const std::string &user_text,
        const mira::OperationContext &context, const std::string &reasoning = "",
        bool tools_allowed = true, DialogPreviewSink preview = {}, DialogProcessSink process = {},
        const mirage::desktop::CancelToken &cancel = {}, DialogModelActivitySink activity = {},
        DialogThinkingPreviewSink thinking_preview = {});

    /// Ordered teardown: waits out any in-flight dialog completion (bounded
    /// by the profile transport deadlines), then settles the transport's
    /// in-flight exchanges and releases the pinned pieces. Idempotent.
    void shutdown();

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace mirage::integration
