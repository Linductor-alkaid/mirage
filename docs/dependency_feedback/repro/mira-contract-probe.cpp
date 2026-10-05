// Feedback evidence only; not a replacement runtime or a passing regression gate.
// MIRA-20260927-001, MIRA-20261004-001, MIRA-20261004-002.
#include <executor/executor.hpp>
#include <mira/adapters/net/openssl_tls.hpp>
#include <mira/agent_loop.hpp>
#include <mira/workflow_versioning.hpp>

#include <array>
#include <iostream>
#include <stdexcept>
#include <sys/socket.h>
#include <type_traits>
#include <unistd.h>

namespace {
void require(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
}
class TextEnvironment final : public mira::IEnvironment {
  public:
    bool screen_requested = false;
    mira::EnvironmentCapabilities capabilities() const override { return {}; }
    mira::Result<mira::Observation> observe(const mira::ObservationRequest &request,
                                            const mira::OperationContext &) override {
        screen_requested = request.required.screen;
        mira::Error error;
        error.code = mira::ErrorCode::UnsupportedCapability;
        error.safe_message = "screen unsupported";
        return error;
    }
    mira::Result<mira::ExecutionReceipt> execute(const mira::InputSequence &,
                                                 const mira::OperationContext &) override {
        throw std::runtime_error("unexpected desktop action");
    }
    mira::Result<void> interrupt(const mira::OperationContext &) override { return {}; }
};
void loop_probe(executor::Executor &owner) {
    auto environment = std::make_shared<TextEnvironment>();
    mira::ModelGateway gateway(owner, mira::ModelRouter{}, nullptr, mira::PriceTable{});
    mira::AgentLoop loop(environment, gateway);
    mira::AgentLoopSpec spec;
    spec.goal = "answer a text question without desktop access";
    mira::ModelDoneVerifier verifier;
    auto result = loop.run(spec, mira::make_control_context(), verifier);
    require(result && result.value().outcome == mira::LoopOutcome::Failed &&
                environment->screen_requested,
            "observation behavior changed; re-triage feedback");
    std::cout << "loop: required.screen=true; outcome=Failed before model routing\n";
    static_assert(!std::is_constructible_v<mira::ModelInputItem, mira::JsonValue>);
    std::cout << "tool input: JsonValue wire result is not a ModelInputItem\n";
}
void version_probe() {
    mira::WorkflowVersionHistory history;
    history.workflow_id = mira::WorkflowId::generate();
    mira::WorkflowVersionRecord draft;
    draft.content_digest = mira::digest_string("same workflow content");
    require(static_cast<bool>(mira::append_workflow_version(history, draft)), "append draft");
    auto published = draft;
    published.version.patch = 1;
    published.parent_digest = draft.content_digest;
    published.validation = mira::WorkflowValidationResult::DryRunPassed;
    published.validation_evidence = mira::digest_string("dry-run evidence");
    require(static_cast<bool>(mira::append_workflow_version(history, published)),
            "append validated");
    const auto resolved = mira::resolve_workflow_version(history, draft.content_digest);
    const auto latest = mira::latest_runnable_workflow_version(history);
    require(resolved && latest && !mira::workflow_version_is_runnable(resolved.value()) &&
                mira::workflow_version_is_runnable(latest.value()),
            "version behavior changed; re-triage feedback");
    std::cout << "workflow: digest resolves not_validated; latest runnable=dry_run_passed\n";
}
struct SocketPair {
    int fds[2]{-1, -1};
    ~SocketPair() {
        for (int fd : fds)
            if (fd >= 0)
                ::close(fd);
    }
};
void sni_probe() {
    SocketPair pair;
    require(::socketpair(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK, 0, pair.fds) == 0, "socketpair");
    mira::adapters::net::OpenSslTlsChannelFactory factory;
    require(factory.initialize(), "TLS factory initialization");
    auto channel = factory.create(pair.fds[0], "sni-probe.example", mira::TlsOptions{});
    require(static_cast<bool>(channel), "TLS channel");
    bool read = false, write = false;
    require(static_cast<bool>(channel.value()->handshake(read, write)), "emit ClientHello");
    std::array<unsigned char, 4096> bytes{};
    const auto received = ::recv(pair.fds[1], bytes.data(), bytes.size(), 0);
    require(received > 43 && bytes[0] == 22 && bytes[5] == 1, "TLS ClientHello record");
    const auto size = static_cast<std::size_t>(received);
    auto u16 = [&](std::size_t at) {
        require(at + 2 <= size, "truncated ClientHello length");
        return static_cast<std::size_t>((bytes[at] << 8U) | bytes[at + 1]);
    };
    std::size_t cursor = 43;
    cursor += 1 + bytes[cursor]; // Session ID.
    cursor += 2 + u16(cursor);   // Cipher suites.
    require(cursor < size, "compression offset");
    cursor += 1 + bytes[cursor];
    const auto end = cursor + 2 + u16(cursor);
    cursor += 2;
    require(end <= size, "extension length");
    bool sni = false;
    while (cursor < end) {
        require(cursor + 4 <= end, "extension header");
        const auto type = u16(cursor);
        const auto count = u16(cursor + 2);
        cursor += 4;
        require(cursor + count <= end, "extension body");
        sni = sni || type == 0;
        cursor += count;
    }
    require(!sni, "SNI now exists; re-triage feedback");
    std::cout << "openssl: DNS host supplied; ClientHello server_name extension absent\n";
}
} // namespace
int main() {
    executor::Executor owner;
    if (!owner.initialize_ex(executor::ExecutorConfig{}).ok)
        return 2;
    int status = 0;
    try {
        auto work = owner.submit_auto([&] {
            loop_probe(owner);
            version_probe();
            sni_probe();
        });
        work.get();
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        status = 2;
    }
    owner.shutdown(true);
    return status;
}
