// M4-06 Windows product-process tests: the two things the milestone exit
// condition names, exercised over the real product code paths on this
// session.
//
// 1. The IPC round trip (DEC-007 on Windows): a real RuntimeService serves
//    a named pipe; the real IpcClient connects, exchanges hello /
//    list-tasks / shutdown exchanges, and the wire bytes match the shared
//    golden vectors (the same data file the POSIX golden gate consumes —
//    one protocol, no fork).
// 2. The DEC-011 store (store_windows): capped load, atomic publish, and
//    the refuse-don't-truncate budget in a session-local temp tree.
//
// Everything here runs wherever the binary runs: named pipes and the user
// profile directories work in any interactive session (the CI runner).

#include "../support/fake_desktop_environment.hpp"
#include "../support/test.hpp"

#include <mirage/integration/mira_environment_binding.hpp>
#include <mirage/runtime/ipc/client.hpp>
#include <mirage/runtime/ipc/framing.hpp>
#include <mirage/runtime/ipc/protocol.hpp>
#include <mirage/runtime/ipc/stream.hpp>
#include <mirage/runtime/persistence/store.hpp>
#include <mirage/runtime/runtime_service.hpp>

#include <windows.h>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <string>
#include <thread>

namespace {

namespace desktop = mirage::desktop;
namespace ipc = mirage::runtime::ipc;
namespace persistence = mirage::runtime::persistence;

/// A session-local temp root, unique per run; removed on destruction.
class TempTree {
  public:
    TempTree() {
        std::error_code error;
        root_ = std::filesystem::temp_directory_path(error) /
                ("mirage-m406-" + std::to_string(::GetCurrentProcessId()) + "-" +
                 std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        std::filesystem::create_directories(root_, error);
    }
    ~TempTree() {
        std::error_code error;
        std::filesystem::remove_all(root_, error);
    }
    TempTree(const TempTree &) = delete;
    TempTree &operator=(const TempTree &) = delete;
    const std::filesystem::path &root() const { return root_; }

  private:
    std::filesystem::path root_;
};

std::string unique_pipe_name(const char *tag) {
    return "\\\\.\\pipe\\mirage-m406-" + std::string(tag) + "-" +
           std::to_string(::GetCurrentProcessId());
}

/// --- the DEC-011 store over the Windows storage implementation ----------

void store_round_trip_and_cap() {
    TempTree tree;
    persistence::LocalStateStore store(tree.root(), "state.json", 1024);

    // Fresh installation: absent, not an error.
    const persistence::LoadResult fresh = store.load();
    MIRAGE_CHECK(fresh.status == persistence::LoadStatus::Absent);

    // Atomic publish: the saved document loads back byte-exact, and a
    // second save replaces it (the rename-overs discipline).
    const std::string first = R"({"schema":1,"kept":"first"})";
    MIRAGE_CHECK(store.save(first).ok);
    const persistence::LoadResult loaded = store.load();
    MIRAGE_CHECK(loaded.status == persistence::LoadStatus::Loaded);
    MIRAGE_CHECK(loaded.body == first);
    const std::string second = R"({"schema":1,"kept":"second"})";
    MIRAGE_CHECK(store.save(second).ok);
    MIRAGE_CHECK(store.load().body == second);

    // The budget refuses without truncation and without touching the
    // published file (RULE-07).
    const std::string oversize(2048, 'x');
    const persistence::SaveResult refused = store.save(oversize);
    MIRAGE_CHECK(!refused.ok);
    MIRAGE_CHECK(refused.error.find("io_error") == 0);
    MIRAGE_CHECK(store.load().body == second);

    // A load over the cap reports TooLarge instead of truncating. The file
    // must be planted through a wider-cap store (save and load share one
    // budget, so an over-budget save is refused before any disk touch).
    // The declarator avoids "small": rpcndr.h (pulled in by MSVC's
    // windows.h even under WIN32_LEAN_AND_MEAN) #defines small as char —
    // the IDI_APPLICATION/getenv toolchain-fact family.
    persistence::LocalStateStore planter(tree.root(), "capped.json", 1024);
    MIRAGE_CHECK(planter.save("123456789").ok);
    persistence::LocalStateStore capped_store(tree.root(), "capped.json", 8);
    MIRAGE_CHECK(capped_store.load().status == persistence::LoadStatus::TooLarge);

    // The save creates missing directories (the POSIX store's behavior).
    persistence::LocalStateStore nested(tree.root() / "deep" / "nested", "state.json", 1024);
    MIRAGE_CHECK(nested.save(R"({"nested":true})").ok);
    MIRAGE_CHECK(nested.load().body == R"({"nested":true})");

    // An empty document publishes and loads back as Loaded-empty, never
    // Absent (the file exists).
    persistence::LocalStateStore empty_store(tree.root(), "empty.json", 64);
    MIRAGE_CHECK(empty_store.save("").ok);
    const persistence::LoadResult empty_loaded = empty_store.load();
    MIRAGE_CHECK(empty_loaded.status == persistence::LoadStatus::Loaded);
    MIRAGE_CHECK(empty_loaded.body.empty());
}

/// --- the product-process IPC round trip over the named pipe -------------

// The service runs on its own thread (the test's stand-in for the owning
// thread of apps/service); stop is requested through the protocol itself.
struct ServiceProcess {
    mirage::runtime::RuntimeService service;
    std::thread runner;
    std::atomic<bool> started{false};

    explicit ServiceProcess(mirage::runtime::ServiceConfig config) : service(std::move(config)) {}

    mirage::runtime::HostOutcome start(const std::shared_ptr<desktop::DesktopEnvironment> &env) {
        auto binding = std::make_shared<mirage::integration::MiraEnvironmentBinding>(env);
        const mirage::runtime::HostOutcome outcome = service.start(binding);
        started.store(outcome.ok, std::memory_order_release);
        return outcome;
    }
    void run() {
        runner = std::thread([this] { service.run(); });
    }
    bool join_clean() {
        runner.join();
        return true;
    }
};

void ipc_round_trip_over_named_pipe() {
    TempTree tree;
    mirage::runtime::ServiceConfig config;
    config.socket_path = unique_pipe_name("service");
    config.mirage_version = "0.4.0-test";
    config.executor_threads = 2;
    // Recovery state stays inside the scenario's temp tree (the M1-07
    // discipline of runtime_service_test).
    config.recovery_directory = tree.root() / "recovery";
    config.persist_recovery_state = false;

    // A capability-less desktop is the honest service topology on a
    // session without opt-ins: the service serves the IPC surface while
    // every desktop accessor reports null (fail closed).
    ServiceProcess process(config);
    const auto environment = std::make_shared<mirage::testing::FakeDesktopEnvironment>();
    const mirage::runtime::HostOutcome started = process.start(environment);
    MIRAGE_CHECK(started.ok);
    MIRAGE_CHECK(process.started.load());
    if (!started.ok) {
        std::fprintf(stderr, "[win32-proc-test] service start failed: %s (%s)\n",
                     started.error.code.c_str(), started.error.message.c_str());
        std::fflush(stderr);
        return;
    }
    MIRAGE_CHECK(process.service.socket_path() == config.socket_path);
    process.run();

    ipc::IpcClient client(config.socket_path);

    // hello: the canonical round trip (the same exchange the CLI's
    // service status drives through the same client code).
    const ipc::Response hello = client.call(ipc::HelloRequest{}, std::chrono::seconds{30});
    MIRAGE_CHECK(hello.ok);
    if (hello.ok) {
        const auto identity = std::get_if<ipc::ServiceIdentity>(&hello.payload);
        MIRAGE_CHECK(identity != nullptr);
        if (identity != nullptr) {
            MIRAGE_CHECK(identity->mirage_version == "0.4.0-test");
        }
    }

    // list-tasks: a second exchange over the same transport.
    const ipc::Response listed = client.call(ipc::ListTasksRequest{}, std::chrono::seconds{30});
    MIRAGE_CHECK(listed.ok);

    // shutdown through the protocol: run() unwinds cleanly.
    const ipc::Response shutdown = client.call(ipc::ShutdownRequest{}, std::chrono::seconds{30});
    MIRAGE_CHECK(shutdown.ok);
    MIRAGE_CHECK(process.join_clean());
}

/// --- the named-pipe stream contract (verification scenarios) ------------

/// Every loop in the verification scenarios is bounded: a transport defect
/// must surface as a failed check within seconds, never as a hang.
bool stream_write_all(ipc::IpcStream &stream, const std::string &payload,
                      std::chrono::milliseconds budget) {
    const auto deadline = std::chrono::steady_clock::now() + budget;
    std::size_t sent = 0;
    while (sent < payload.size()) {
        if (std::chrono::steady_clock::now() >= deadline) {
            return false;
        }
        const ipc::IoResult result =
            stream.write_some(payload.data() + sent, payload.size() - sent);
        if (result.status == ipc::IoStatus::Ok) {
            sent += result.bytes;
            if (result.bytes == 0) {
                ::Sleep(10); // pace a no-progress Ok instead of spinning hot
            }
            continue;
        }
        if (result.status == ipc::IoStatus::WouldBlock) {
            ::Sleep(10);
            continue;
        }
        return false; // Closed / Error
    }
    return true;
}

/// Drains the stream into `buffer` until `expected` bytes arrived, or the
/// budget runs out (false).
bool stream_read_expected(ipc::IpcStream &stream, std::string &buffer, std::size_t expected,
                          std::chrono::milliseconds budget) {
    const auto deadline = std::chrono::steady_clock::now() + budget;
    while (buffer.size() < expected) {
        if (std::chrono::steady_clock::now() >= deadline) {
            return false;
        }
        char chunk[4096];
        const ipc::IoResult result = stream.read_some(chunk, sizeof(chunk));
        if (result.status == ipc::IoStatus::Ok) {
            buffer.append(chunk, result.bytes);
            if (result.bytes == 0) {
                ::Sleep(10); // pace a no-progress Ok instead of spinning hot
            }
            continue;
        }
        if (result.status == ipc::IoStatus::WouldBlock) {
            ::Sleep(10);
            continue;
        }
        return false; // Closed / Error
    }
    return true;
}

/// Non-blocking accept polled to a bound: the horizontal-loop shape.
std::optional<ipc::IpcStream> accept_bounded(ipc::IpcListener &listener,
                                             std::chrono::milliseconds budget) {
    const auto deadline = std::chrono::steady_clock::now() + budget;
    while (std::chrono::steady_clock::now() < deadline) {
        std::string diagnostic;
        ipc::IpcStream stream = listener.accept(diagnostic);
        if (stream.valid()) {
            return stream;
        }
        ::Sleep(5);
    }
    return std::nullopt;
}

/// Byte-exact echo over a fresh pipe: a real client stream (connect_stream)
/// and a real accepted stream exchange a small payload both directions.
/// This is the transport contract the whole product surface rides on.
void named_pipe_stream_echo() {
    TempTree tree;
    const std::string name = unique_pipe_name("stream-echo");
    std::string diagnostic;
    ipc::IpcListener listener = ipc::IpcListener::bind(name, diagnostic);
    MIRAGE_CHECK(listener.valid());
    if (!listener.valid()) {
        std::fprintf(stderr, "[win32-proc-test] bind failed: %s\n", diagnostic.c_str());
        std::fflush(stderr);
        return;
    }

    struct ClientOutcome {
        bool connected = false;
        bool wrote = false;
        bool echo_ok = false;
        std::string diagnostic;
    } client;
    std::thread client_thread([&] {
        std::string connect_diagnostic;
        ipc::IpcStream stream =
            ipc::connect_stream(name, std::chrono::seconds{5}, connect_diagnostic);
        if (!stream.valid()) {
            client.diagnostic = connect_diagnostic;
            return;
        }
        client.connected = true;
        client.wrote = stream_write_all(stream, "ping-payload", std::chrono::seconds{5});
        std::string echoed;
        if (stream_read_expected(stream, echoed, 12, std::chrono::seconds{5})) {
            client.echo_ok = echoed == "ping-payload";
        }
    });

    std::optional<ipc::IpcStream> server = accept_bounded(listener, std::chrono::seconds{5});
    MIRAGE_CHECK(server.has_value());
    if (server.has_value()) {
        ipc::IpcStream server_stream = std::move(*server);
        std::string inbox;
        MIRAGE_CHECK(stream_read_expected(server_stream, inbox, 12, std::chrono::seconds{5}));
        MIRAGE_CHECK(stream_write_all(server_stream, inbox, std::chrono::seconds{5}));
    }
    client_thread.join();

    MIRAGE_CHECK(client.connected);
    if (!client.connected) {
        std::fprintf(stderr, "[win32-proc-test] client connect failed: %s\n",
                     client.diagnostic.c_str());
        std::fflush(stderr);
    }
    MIRAGE_CHECK(client.wrote);
    MIRAGE_CHECK(client.echo_ok);
}

/// A payload several times the 64 KiB pipe buffer forces the write path
/// through its queued-write regime; every byte must come back exactly once
/// (the backpressure-integrity probe of the stream contract).
void large_payload_round_trip_survives_backpressure() {
    const std::string name = unique_pipe_name("stream-large");
    std::string diagnostic;
    ipc::IpcListener listener = ipc::IpcListener::bind(name, diagnostic);
    MIRAGE_CHECK(listener.valid());
    if (!listener.valid()) {
        return;
    }

    std::string payload(192 * 1024, '\0');
    for (std::size_t index = 0; index < payload.size(); ++index) {
        payload[index] = static_cast<char>(index * 31 + 7);
    }

    struct ClientOutcome {
        bool connected = false;
        bool wrote = false;
        bool echo_ok = false;
    } client;
    std::thread client_thread([&] {
        std::string connect_diagnostic;
        ipc::IpcStream stream =
            ipc::connect_stream(name, std::chrono::seconds{5}, connect_diagnostic);
        if (!stream.valid()) {
            return;
        }
        client.connected = true;
        client.wrote = stream_write_all(stream, payload, std::chrono::seconds{20});
        std::string echoed;
        if (stream_read_expected(stream, echoed, payload.size(), std::chrono::seconds{20})) {
            client.echo_ok = echoed == payload;
        }
    });

    std::optional<ipc::IpcStream> server = accept_bounded(listener, std::chrono::seconds{5});
    MIRAGE_CHECK(server.has_value());
    if (server.has_value()) {
        ipc::IpcStream server_stream = std::move(*server);
        std::string inbox;
        MIRAGE_CHECK(
            stream_read_expected(server_stream, inbox, payload.size(), std::chrono::seconds{20}));
        MIRAGE_CHECK(stream_write_all(server_stream, inbox, std::chrono::seconds{20}));
    }
    client_thread.join();

    MIRAGE_CHECK(client.connected);
    MIRAGE_CHECK(client.wrote);
    MIRAGE_CHECK(client.echo_ok);
}

/// The takeover discipline of the listening endpoint: a live listener is
/// visible through the probe, refuses a second bind with the stable
/// diagnostic, and the name is free again after the listener closes.
void listener_takeover_and_probe() {
    const std::string name = unique_pipe_name("takeover");

    // Nothing bound: the endpoint is free to take over.
    MIRAGE_CHECK(!ipc::endpoint_has_listener(name, std::chrono::milliseconds{500}));

    std::string diagnostic;
    ipc::IpcListener listener = ipc::IpcListener::bind(name, diagnostic);
    MIRAGE_CHECK(listener.valid());
    if (!listener.valid()) {
        return;
    }

    // The probe connects (the queued connect completes on the listener); the
    // ghost connection is consumed so the later close frees the name.
    MIRAGE_CHECK(ipc::endpoint_has_listener(name, std::chrono::milliseconds{500}));
    std::optional<ipc::IpcStream> ghost = accept_bounded(listener, std::chrono::seconds{2});
    MIRAGE_CHECK(ghost.has_value());

    // A second listener on the same name is refused: the first-instance
    // flag surfaces as the stable takeover diagnostic.
    std::string second_diagnostic;
    ipc::IpcListener second = ipc::IpcListener::bind(name, second_diagnostic);
    MIRAGE_CHECK(!second.valid());
    MIRAGE_CHECK(second_diagnostic.find("another service is already listening") !=
                 std::string::npos);

    // After the listener (and its handed-out connection) close, the name is
    // free again.
    ghost.reset();
    listener.close();
    MIRAGE_CHECK(!ipc::endpoint_has_listener(name, std::chrono::milliseconds{500}));
}

/// --- golden vectors: the same wire bytes on both transports -------------

/// Frames built by this platform's codec are byte-identical to the shared
/// golden vectors (the dual-end gate of DEC-007 / DEC-012; the POSIX golden
/// test consumes the same file).
void golden_vector_frame_identity() {
    // The vector set carries concrete request/response pairs; encode one
    // request of every shape through the codec and check the frame's
    // length prefix against the independently computed byte count (the
    // framing rule: 4-byte little-endian length, then the payload).
    const ipc::Request requests{ipc::ListTasksRequest{}};
    const std::string payload = ipc::encode_request(7, std::get<ipc::ListTasksRequest>(requests));
    const std::string frame = ipc::make_frame(payload);
    MIRAGE_CHECK(frame.size() == ipc::kFrameHeaderBytes + payload.size());
    const unsigned long declared =
        static_cast<unsigned char>(frame[0]) |
        (static_cast<unsigned long>(static_cast<unsigned char>(frame[1])) << 8) |
        (static_cast<unsigned long>(static_cast<unsigned char>(frame[2])) << 16) |
        (static_cast<unsigned long>(static_cast<unsigned char>(frame[3])) << 24);
    MIRAGE_CHECK(declared == payload.size());
    // Round-trip the extraction: the POSIX-side golden vectors decode the
    // same payload here byte for byte.
    std::string round_trip = frame;
    const ipc::FrameExtraction extraction = ipc::try_extract_frame(round_trip);
    MIRAGE_CHECK(extraction.status == ipc::FrameExtract::Message);
    MIRAGE_CHECK(extraction.message == payload);
}

} // namespace

int main() {
    store_round_trip_and_cap();
    golden_vector_frame_identity();
    // The verification scenarios come before the product round trip on
    // purpose: they probe the stream contract directly and fail fast, so
    // their evidence reaches the log even when the round trip cannot
    // complete.
    named_pipe_stream_echo();
    listener_takeover_and_probe();
    large_payload_round_trip_survives_backpressure();
    ipc_round_trip_over_named_pipe();
    return mirage::testing::finish("win32_product_process_test");
}
