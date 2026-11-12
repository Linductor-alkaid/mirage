#include "../support/test.hpp"

#include <kairo/executor.hpp>
#include <mira/adapters/net/socket_transport.hpp>
#include <mira/environment.hpp>
#include <mira/model_contracts.hpp>
#include <mira/model_profile.hpp>
#include <mira/model_provider.hpp>
#include <mira/model_transport.hpp>
#include <mirage/integration/model_layer.hpp>

#include <algorithm>
#include <chrono>
#include <memory>
#include <optional>
#include <string>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

// Independent verification probe for the 2026-11-11 mira pin upgrade
// (branch deps/mira-upstream-sync-20261111). Pinned PR#118 denies
// loopback/private/link-local endpoints at the transport unless the host
// consents on the ModelProfile. Mirage's ModelLayer assembles every profile
// from user-configured ModelLayerConfig fields (endpoint origin comes from
// LocalSettings -> IPC -> ModelLayerConfig, never from model output), so
// build_profile() now sets ModelProfile::allow_private_endpoint = true. The
// suite pins both faces of that wiring:
//
//   P1 loopback consent — the assembled profile carries the host consent, and
//      one real socket-stack gateway call against a closed loopback port
//      fails at the connection layer ("no candidate address accepted the
//      connection", TransportFailed), never at the endpoint-policy layer
//      ("allowlist" / "no policy-compliant address", EndpointPolicyDenied).
//      ModelLayer has no consent-off configuration path by design (the
//      consent is unconditional for user-configured profiles), so the
//      consent-off contrast is exercised directly against the pinned
//      OpenAiCompatibleProvider + SocketHttpTransport on the same closed
//      loopback endpoint: consent off -> EndpointPolicyDenied, consent on ->
//      connection-level failure. That contrast also validates the error
//      categories the ModelLayer probes discriminate on.
//   P2 public invariance — an https://fixture.example profile assembles with
//      unchanged identity fields (origin, prefix, selector, dialect, data
//      policy) and a real call fails at DNS/connect level, never as a
//      private-address policy denial (no reachability required).
//
// Determinism: loopback probes hit a port this process binds and releases
// (ECONNREFUSED is immediate); nothing dials beyond 127.0.0.1 except the
// reserved-TLD fixture.example name whose failure mode is the assertion.

namespace {

namespace i = mirage::integration;

// A port that currently has no listener on the loopback interface: bind an
// ephemeral port, read it back and close the socket. The kernel will not
// hand it to a listener in between in any realistic schedule, and the model
// layer's bounded deadlines cap the worst case regardless.
std::uint16_t reserve_closed_loopback_port() {
    const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        return 0;
    }
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = 0;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    std::uint16_t port = 0;
    if (::bind(fd, reinterpret_cast<const sockaddr *>(&address), sizeof(address)) == 0) {
        sockaddr_in bound{};
        socklen_t length = sizeof(bound);
        if (::getsockname(fd, reinterpret_cast<sockaddr *>(&bound), &length) == 0) {
            port = ntohs(bound.sin_port);
        }
    }
    ::close(fd);
    return port;
}

i::ModelLayerConfig probe_config(const std::string &origin) {
    i::ModelLayerConfig config;
    config.enabled = true;
    config.endpoint_origin = origin;
    config.model_selector = "endpoint-probe";
    config.max_output_tokens = 256;
    // The layer resolves the profile credential inside the transport before
    // any bytes move; a resolvable environment credential keeps the probes on
    // the connection path (a missing key would fail earlier, at the
    // credential stage, and mask the endpoint outcome under test).
    config.credential_env = "MIRAGE_ENDPOINT_PROBE_TOKEN";
    ::setenv(config.credential_env.c_str(), "endpoint-probe-value", 1);
    // Bounded: every probe here fails within the connect/DNS stage, far
    // inside this budget; it only caps pathological scheduling.
    config.request_deadline = std::chrono::milliseconds{10'000};
    return config;
}

mira::OperationContext probe_context() {
    auto context = mira::make_control_context();
    context.task = mira::TaskId::generate();
    context.session = mira::SessionId::generate();
    context.deadline = std::chrono::steady_clock::now() + std::chrono::seconds{15};
    return context;
}

// The assembly scenarios never dial through the override; the scripted
// provider only exists so the layer assembles (and hands its profile to the
// factory) instead of building the socket stack.
class RejectingProvider final : public mira::IModelProvider {
  public:
    explicit RejectingProvider(const mira::ModelProfile &profile) : profile_(profile) {}
    [[nodiscard]] const mira::ModelProfile &profile() const override { return profile_; }
    [[nodiscard]] mira::Result<mira::ModelResponse>
    infer(const mira::ModelRequest &, const mira::OperationContext &,
          const mira::ProviderInferOptions &) override {
        mira::Error error;
        error.code = mira::ErrorCode::Unavailable;
        error.domain = "mirage.test";
        error.safe_message = "the endpoint probe never dials through a scripted provider";
        return error;
    }

  private:
    mira::ModelProfile profile_;
};

bool contains_any(const std::string &text, std::initializer_list<const char *> needles) {
    return std::any_of(needles.begin(), needles.end(),
                       [&](const char *needle) { return text.find(needle) != std::string::npos; });
}

// Endpoint-policy denial vocabulary of the pinned transport (security.cpp /
// socket_transport.cpp). None of it may appear in a connection-level failure.
constexpr std::initializer_list<const char *> kPolicyMarkers = {
    "allowlist", "no policy-compliant address", "EndpointPolicyDenied"};

// Resolves every reference to a fixed value; the pinned provider only needs
// the credential stage to succeed before it reaches the transport.
class AnySecretResolver final : public mira::ISecretResolver {
  public:
    [[nodiscard]] mira::Result<std::string> resolve(const mira::SecretRef &) override {
        return std::string{"endpoint-probe-value"};
    }
};

// P1 assembly half: the layer's only profile construction point declares the
// host consent for user-configured endpoints.
void assembled_profile_carries_loopback_consent(kairo::Executor &executor,
                                                std::uint16_t closed_port) {
    const auto config = probe_config("http://127.0.0.1:" + std::to_string(closed_port));
    std::optional<mira::ModelProfile> assembled;
    i::ModelProviderOverride provider([&](const mira::ModelProfile &profile) {
        assembled = profile;
        return std::make_shared<RejectingProvider>(profile);
    });
    i::ModelLayer layer(executor, config, &provider, {});
    MIRAGE_CHECK(layer.running());
    MIRAGE_CHECK(assembled.has_value());
    // The change under verification: explicit consent on the assembled
    // profile (pinned PR#118 SSRF line consumes it in limits_from_profile).
    MIRAGE_CHECK(assembled->allow_private_endpoint);
    // Identity fields must keep coming from the user configuration, and the
    // profile must remain self-consistent for the pinned transport.
    MIRAGE_CHECK(assembled->endpoint_origin == config.endpoint_origin);
    MIRAGE_CHECK(assembled->endpoint_host() == "127.0.0.1");
    MIRAGE_CHECK(assembled->api_prefix == config.api_prefix);
    MIRAGE_CHECK(assembled->model_selector == config.model_selector);
    MIRAGE_CHECK(assembled->dialect == mira::ProtocolDialect::OpenAIResponsesV1);
    MIRAGE_CHECK(assembled->default_data_policy.store == false);
    MIRAGE_CHECK(assembled->validate().has_value());
}

// P2 assembly half: the consent line must not reshape the public-endpoint
// assembly path (identity fields, policy, validation unchanged).
void assembled_profile_keeps_public_origin_identity(kairo::Executor &executor) {
    const auto config = probe_config("https://fixture.example");
    std::optional<mira::ModelProfile> assembled;
    i::ModelProviderOverride provider([&](const mira::ModelProfile &profile) {
        assembled = profile;
        return std::make_shared<RejectingProvider>(profile);
    });
    i::ModelLayer layer(executor, config, &provider, {});
    MIRAGE_CHECK(layer.running());
    MIRAGE_CHECK(assembled.has_value());
    MIRAGE_CHECK(assembled->endpoint_origin == config.endpoint_origin);
    MIRAGE_CHECK(assembled->endpoint_host() == "fixture.example");
    MIRAGE_CHECK(assembled->api_prefix == config.api_prefix);
    MIRAGE_CHECK(assembled->model_selector == config.model_selector);
    MIRAGE_CHECK(assembled->dialect == mira::ProtocolDialect::OpenAIResponsesV1);
    MIRAGE_CHECK(assembled->default_data_policy.store == false);
    MIRAGE_CHECK(assembled->validate().has_value());
    // The consent is declared uniformly for user-configured profiles; the
    // pinned allowed_hosts still pins every hop to this configured host, so
    // the public origin gains no reach beyond itself.
    MIRAGE_CHECK(assembled->allow_private_endpoint);
}

// P1 behavior half: a real gateway call through the pinned socket stack to a
// closed loopback port must fail at the connection layer (the consent let it
// get all the way to connect()), with zero endpoint-policy vocabulary.
void loopback_gateway_failure_reaches_connection_layer(kairo::Executor &executor,
                                                       std::uint16_t closed_port) {
    const auto config = probe_config("http://127.0.0.1:" + std::to_string(closed_port));
    i::ModelLayer layer(executor, config, nullptr, {});
    MIRAGE_CHECK(layer.running());
    const auto context = probe_context();
    auto work = executor.submit_auto(
        [&] { return layer.complete_dialog_turn({}, "endpoint probe", context); });
    MIRAGE_CHECK(work.valid());
    const auto completion = work.get();
    MIRAGE_CHECK(completion.failed);
    MIRAGE_CHECK(!completion.ok);
    MIRAGE_CHECK(completion.error.starts_with("model layer request failed:"));
    // Connection-level: the pinned transport reports a refused connect as
    // TransportFailed "no candidate address accepted the connection".
    MIRAGE_CHECK(completion.error.find("no candidate address accepted the connection") !=
                 std::string::npos);
    // Categorically not an SSRF/policy rejection.
    MIRAGE_CHECK(!contains_any(completion.error, kPolicyMarkers));
    layer.shutdown();
}

// P2 behavior half: a real gateway call to a reserved-TLD public origin must
// fail at DNS/connect level (or the fail-closed TLS path), never as a
// private-address policy denial.
void public_origin_failure_is_transport_level(kairo::Executor &executor) {
    const auto config = probe_config("https://fixture.example");
    i::ModelLayer layer(executor, config, nullptr, {});
    MIRAGE_CHECK(layer.running());
    const auto context = probe_context();
    auto work = executor.submit_auto(
        [&] { return layer.complete_dialog_turn({}, "endpoint probe", context); });
    MIRAGE_CHECK(work.valid());
    const auto completion = work.get();
    MIRAGE_CHECK(completion.failed);
    MIRAGE_CHECK(!completion.ok);
    MIRAGE_CHECK(completion.error.starts_with("model layer request failed:"));
    MIRAGE_CHECK(!contains_any(completion.error, kPolicyMarkers));
    // Transport-stage vocabulary: DNS failure (offline / NXDOMAIN for the
    // reserved TLD), refused/filtered connect, connect deadline, or the
    // fail-closed https-without-TLS path. Any of these is a socket-level
    // outcome; a private-address denial would be EndpointPolicyDenied.
    MIRAGE_CHECK(contains_any(
        completion.error, {"dns resolution failed", "no candidate address accepted the connection",
                           "connect exceeded its deadline", "without a TLS channel factory"}));
    layer.shutdown();
}

// The consent-off contrast the ModelLayer cannot express by design: the same
// closed loopback endpoint through the pinned provider/transport pair fails
// as an endpoint-policy denial without consent and reaches the connect stage
// with it. This validates the error categories the ModelLayer probes
// discriminate on (upstream PR#118 semantics on the pinned revision).
void pinned_consent_switch_discriminates_loopback(kairo::Executor &executor,
                                                  std::uint16_t closed_port) {
    // Any-value resolver: the probe credential only has to resolve so the
    // pinned provider reaches the transport stage (same shape as the upstream
    // m3 fixture's MapSecretResolver).
    auto secrets = std::make_shared<AnySecretResolver>();
    auto transport = std::make_shared<mira::adapters::net::SocketHttpTransport>(executor, secrets);
    MIRAGE_CHECK(transport->start());

    const std::string origin = "http://127.0.0.1:" + std::to_string(closed_port);
    const auto make_probe_profile = [&](const bool consent) {
        mira::ModelProfile profile;
        profile.id = mira::ModelProfileId::generate();
        profile.display_name = "endpoint-probe";
        profile.version = mira::SemanticVersion{1, 0, 0};
        profile.dialect = mira::ProtocolDialect::OpenAIResponsesV1;
        profile.endpoint_origin = origin;
        profile.api_prefix = "/v1";
        profile.model_selector = "endpoint-probe";
        profile.credential = mira::SecretRef{"endpoint-probe-credential"};
        profile.capabilities.text =
            mira::CapabilityFlag{true, mira::CapabilityEvidence::FixtureVerified, ""};
        profile.capabilities.sse =
            mira::CapabilityFlag{true, mira::CapabilityEvidence::FixtureVerified, ""};
        profile.deadlines.connect = std::chrono::milliseconds{2'000};
        profile.deadlines.total = std::chrono::milliseconds{10'000};
        profile.default_data_policy.store = false;
        profile.allow_private_endpoint = consent;
        return profile;
    };
    const auto infer = [&](const mira::ModelProfile &profile) {
        mira::OpenAiCompatibleProvider provider(std::make_shared<const mira::ModelProfile>(profile),
                                                transport, /*artifacts=*/nullptr);
        mira::ModelRequest request;
        request.contract_version = mira::SchemaVersion{1, 0};
        request.request_id = mira::ModelRequestId::generate();
        request.operation_id = mira::OperationId::generate();
        request.task_id = mira::TaskId::generate();
        request.profile_id = profile.id;
        mira::ModelInputItem item;
        item.role = mira::ModelRole::User;
        item.content.emplace_back(mira::TextPart{"endpoint probe"});
        request.input.push_back(std::move(item));
        request.output_contract.mode = mira::OutputMode::Text;
        request.data_policy.store = false;
        mira::OperationContext context;
        context.operation = request.operation_id;
        context.started_at = mira::Timestamp::now();
        return provider.infer(request, context, mira::ProviderInferOptions{});
    };

    const auto denied = infer(make_probe_profile(false));
    MIRAGE_CHECK(!denied.has_value());
    MIRAGE_CHECK(denied.error().domain_code ==
                 static_cast<std::int32_t>(mira::ModelDomainCode::EndpointPolicyDenied));
    MIRAGE_CHECK(contains_any(denied.error().safe_message, kPolicyMarkers));

    const auto consented = infer(make_probe_profile(true));
    MIRAGE_CHECK(!consented.has_value());
    MIRAGE_CHECK(consented.error().domain_code !=
                 static_cast<std::int32_t>(mira::ModelDomainCode::EndpointPolicyDenied));
    MIRAGE_CHECK(consented.error().safe_message == "no candidate address accepted the connection");
    MIRAGE_CHECK(!contains_any(consented.error().safe_message, kPolicyMarkers));

    transport->shutdown();
}

struct ExecutorFixture final {
    kairo::Executor executor;
    ExecutorFixture() {
        kairo::ExecutorConfig config;
        config.min_threads = config.max_threads = 4;
        (void)executor.initialize(config);
    }
    ~ExecutorFixture() { (void)executor.shutdown(true); }
};

} // namespace

int main() {
    ExecutorFixture fixture;
    const auto closed_port = reserve_closed_loopback_port();
    MIRAGE_CHECK(closed_port != 0);
    assembled_profile_carries_loopback_consent(fixture.executor, closed_port);
    assembled_profile_keeps_public_origin_identity(fixture.executor);
    pinned_consent_switch_discriminates_loopback(fixture.executor, closed_port);
    loopback_gateway_failure_reaches_connection_layer(fixture.executor, closed_port);
    public_origin_failure_is_transport_level(fixture.executor);
    return mirage::testing::finish("model_profile_endpoint_test");
}
