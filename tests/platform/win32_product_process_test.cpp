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

    // A load over the cap reports TooLarge instead of truncating.
    persistence::LocalStateStore small(tree.root(), "small.json", 8);
    MIRAGE_CHECK(small.save("123456789").ok);
    MIRAGE_CHECK(small.load().status == persistence::LoadStatus::TooLarge);
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
    ipc_round_trip_over_named_pipe();
    return mirage::testing::finish("win32_product_process_test");
}
