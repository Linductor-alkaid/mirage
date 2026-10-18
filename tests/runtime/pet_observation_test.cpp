#include "../../runtime/service/src/desktop_observer.hpp"
#include "../support/fake_desktop_environment.hpp"
#include "../support/ipc_io.hpp"
#include <atomic>
#include <mirage/integration/mira_environment_binding.hpp>
#include <mirage/runtime/ipc/client.hpp>
#include <mirage/runtime/runtime_service.hpp>
#include <poll.h>
#include <stdexcept>

using namespace std::chrono_literals;
namespace desktop = mirage::desktop;
namespace runtime = mirage::runtime;
namespace detail = runtime::detail;
namespace ipc = runtime::ipc;
namespace test = mirage::testing;
namespace {
enum class Fault {
    None,
    Admission,
    Before,
    After,
    Restore,
    ThrowVerify,
    ThrowRestore,
    ThrowAdmission,
    NoLease
};
struct State {
    std::atomic_bool hidden{false}, violation{false}, expect_hidden{true}, provider_throws{false};
    std::atomic_bool passive{false};
    std::atomic_int admitted{0}, restored{0}, fronts{0}, captures{0}, authorizations{0};
    std::atomic<Fault> fault{Fault::None};
    std::function<void()> on_release; // pure synchronous helper fixtures only
};
class Lease final : public desktop::PresentationLease {
  public:
    explicit Lease(State &state) : state_(state), excluded_(!state.passive.load()) {
        state_.hidden = excluded_;
    }
    bool excluded() const override { return excluded_; }
    ~Lease() override {
        try {
            (void)release();
        } catch (...) {
        }
    }
    desktop::ProviderError verify(const desktop::CancelToken &cancel) override {
        ++verified_;
        if (cancel.cancelled())
            return {"cancelled", "fixture cancelled"};
        const auto fault = state_.fault.load();
        if (fault == Fault::ThrowVerify)
            throw std::runtime_error("fixture verifier exception");
        if (fault == Fault::Before || (fault == Fault::After && verified_ > 1))
            return {"pet_exclusion_failed", "fixture changed shape"};
        return {};
    }
    desktop::ProviderError release() override {
        if (done_)
            return {};
        done_ = true;
        ++state_.restored;
        state_.hidden = false;
        if (state_.on_release)
            state_.on_release();
        if (state_.fault == Fault::ThrowRestore)
            throw std::runtime_error("fixture restore exception");
        return state_.fault == Fault::Restore
                   ? desktop::ProviderError{"pet_restore_failed", "fixture"}
                   : desktop::ProviderError{};
    }

  private:
    State &state_;
    bool excluded_;
    int verified_ = 0;
    bool done_ = false;
};
desktop::PresentationExclusion acquire(State &state, const desktop::CancelToken &cancel) {
    ++state.admitted;
    if (cancel.cancelled())
        return {{}, {"cancelled", "fixture cancelled"}};
    if (state.fault == Fault::ThrowAdmission)
        throw std::runtime_error("fixture admission exception");
    if (state.fault == Fault::Admission)
        return {{}, {"pet_exclusion_unavailable", "fixture"}};
    if (state.fault == Fault::NoLease)
        return {};
    return {std::make_unique<Lease>(state), {}};
}
class Environment final : public desktop::DesktopEnvironment {
  public:
    class Windows final : public desktop::WindowProvider {
      public:
        explicit Windows(Environment &environment) : environment_(environment) {}
        desktop::WindowListOutcome list_windows(const desktop::WindowListLimits &limits,
                                                const desktop::CancelToken &cancel) override {
            return environment_.inner.window()->list_windows(limits, cancel);
        }
        desktop::WindowQueryOutcome front_window(const desktop::CancelToken &cancel) override {
            auto &state = environment_.state;
            ++state.fronts;
            if (state.expect_hidden && !state.hidden)
                state.violation = true;
            if (state.provider_throws)
                throw std::runtime_error("fixture provider exception");
            auto result = environment_.inner.window()->front_window(cancel);
            if (on_front)
                on_front();
            return result;
        }
        desktop::WindowActionOutcome activate(const std::string &id,
                                              const desktop::CancelToken &cancel) override {
            return environment_.inner.window()->activate(id, cancel);
        }
        std::function<void()> on_front;

      private:
        Environment &environment_;
    };
    explicit Environment(State &value) : state(value), windows(*this) {
        inner.windows = {{"w1", "Fixture target", {0, 0, 300, 200}, true}};
        desktop::SemanticNode node;
        node.ref = "@e1";
        node.role = "button";
        node.name = "Target";
        inner.snapshots["w1"] = {"Fixture", "Fixture target", {node}};
    }
    desktop::EnvironmentInfo info() const override {
        return {"pet-observation-fixture", "private"};
    }
    desktop::WindowProvider *window() override { return &windows; }
    desktop::InputProvider *input() override { return inner.input(); }
    desktop::AccessibilityProvider *accessibility() override { return inner.accessibility(); }
    State &state;
    test::FakeDesktopEnvironment inner;
    Windows windows;
};
class Pipeline final : public mirage::integration::EnvironmentVisualPipeline {
  public:
    explicit Pipeline(State &state) : state_(state) {}
    bool running() const override { return enabled; }
    desktop::VisualReferenceRegistry &registry() override { return registry_; }
    desktop::VisualPublishOutcome publish(const std::string &text) {
        desktop::VisualSnapshot snapshot;
        desktop::VisualRegionEntry region;
        region.text = text;
        region.bounds = {0, 0, 20, 20};
        snapshot.regions.push_back(region);
        return registry_.publish(snapshot);
    }
    mirage::integration::EnvironmentVisualRefresh
    refresh(bool analyze, const desktop::CancelToken &cancel,
            const std::optional<std::chrono::steady_clock::time_point> &deadline) override {
        ++state_.captures;
        if ((state_.expect_hidden && !state_.hidden) || !analyze || !deadline || cancel.cancelled())
            state_.violation = true;
        if (on_capture)
            on_capture();
        if (throws)
            throw std::runtime_error("fixture capture exception");
        mirage::integration::EnvironmentVisualRefresh result;
        if (fails) {
            result.capture_error = {"capture_failed", "fixture"};
            return result;
        }
        result.captured = result.analyzed = true;
        result.publish = publish("fresh unobstructed target");
        last_scope = result.publish.snapshot.scope_ref;
        return result;
    }
    bool enabled = true, fails = false, throws = false;
    std::string last_scope;
    std::function<void()> on_capture;

  private:
    State &state_;
    desktop::VisualReferenceRegistry registry_;
};
void helper_cases() {
    State state;
    Environment environment(state);
    Pipeline pipeline(state);
    desktop::ActionCoordinator actions;
    auto old = pipeline.publish("cached pet content");
    MIRAGE_CHECK(old.ok);
    detail::DesktopObservationSource source{environment};
    source.pipeline = &pipeline;
    source.registry = &pipeline.registry();
    source.actions = &actions;
    bool allowed = true;
    source.authorize_visual = [&](const auto &) {
        ++state.authorizations;
        if (state.hidden)
            state.violation = true;
        return allowed;
    };
    source.exclude_pet = [&](const auto &cancel) { return acquire(state, cancel); };
    const auto read = [&](bool semantic = true, bool visual = true,
                          const desktop::CancelToken &cancel = {}) {
        return detail::read_desktop_observation(source, semantic, visual, cancel,
                                                std::chrono::steady_clock::now() + 5s);
    };
    auto result = read();
    MIRAGE_CHECK(result.ok && !state.hidden && !state.violation);
    MIRAGE_CHECK(result.observation.visual_snapshot_ref == pipeline.last_scope);
    MIRAGE_CHECK(result.observation.visual_snapshot_ref != old.snapshot.scope_ref);
    MIRAGE_CHECK(result.observation.visual_snapshot.regions.front().text ==
                 "fresh unobstructed target");
    MIRAGE_CHECK(state.admitted == 1 && state.restored == 1 && state.captures == 1);
    const auto captures = state.captures.load(), authorizations = state.authorizations.load();
    result = read(true, false);
    MIRAGE_CHECK(result.ok && state.captures == captures && state.authorizations == authorizations);
    MIRAGE_CHECK(result.observation.visual_snapshot_ref.empty() && !state.hidden);
    const auto admitted = state.admitted.load(), fronts = state.fronts.load();
    allowed = false;
    result = read();
    MIRAGE_CHECK(!result.ok && result.error.code == "permission_denied");
    MIRAGE_CHECK(state.admitted == admitted && state.fronts == fronts && !state.hidden);
    allowed = true;
    source.pipeline = nullptr;
    result = read();
    MIRAGE_CHECK(!result.ok &&
                 result.error.message.find("requires_fresh_visual") != std::string::npos);
    MIRAGE_CHECK(state.admitted == admitted && state.fronts == fronts);
    source.exclude_pet = {};
    state.expect_hidden = false;
    result = read();
    MIRAGE_CHECK(result.ok && result.observation.visual_snapshot_ref == pipeline.last_scope);
    source.exclude_pet = [&](const auto &cancel) { return acquire(state, cancel); };
    source.pipeline = &pipeline;
    state.expect_hidden = true;
    pipeline.enabled = false;
    MIRAGE_CHECK(!read().ok && state.admitted == admitted);
    pipeline.enabled = true;
    for (const auto fault :
         {Fault::Admission, Fault::Before, Fault::After, Fault::Restore, Fault::ThrowVerify,
          Fault::ThrowRestore, Fault::ThrowAdmission, Fault::NoLease}) {
        state.fault = fault;
        const auto previous = state.fronts.load();
        result = read();
        MIRAGE_CHECK(!result.ok && result.error.code == "unavailable" && !state.hidden);
        if (fault == Fault::Admission || fault == Fault::Before || fault == Fault::ThrowVerify ||
            fault == Fault::ThrowAdmission || fault == Fault::NoLease)
            MIRAGE_CHECK(state.fronts == previous);
    }
    state.fault = Fault::None;
    pipeline.fails = true;
    MIRAGE_CHECK(!read().ok && !state.hidden);
    pipeline.fails = false;
    pipeline.throws = true;
    MIRAGE_CHECK(!read().ok && !state.hidden);
    pipeline.throws = false;
    state.provider_throws = true;
    MIRAGE_CHECK(!read().ok && !state.hidden);
    state.provider_throws = false;
    desktop::CancelToken cancelled;
    cancelled.request_cancel();
    const auto before_cancel = state.admitted.load();
    MIRAGE_CHECK(!read(true, true, cancelled).ok && state.admitted == before_cancel);
    result = detail::read_desktop_observation(source, true, true, {},
                                              std::chrono::steady_clock::now() - 1ms);
    MIRAGE_CHECK(!result.ok && state.admitted == before_cancel);
    const auto expires_during_capture = std::chrono::steady_clock::now() + 50ms;
    const auto restored_before_timeout = state.restored.load();
    pipeline.on_capture = [&] {
        while (std::chrono::steady_clock::now() < expires_during_capture)
            (void)::poll(nullptr, 0, 1);
    };
    result = detail::read_desktop_observation(source, true, true, {}, expires_during_capture);
    MIRAGE_CHECK(!result.ok && result.error.message.find("expired") != std::string::npos &&
                 !state.hidden && state.restored == restored_before_timeout + 1);
    pipeline.on_capture = {};
    desktop::CancelToken in_capture;
    pipeline.on_capture = [&] { in_capture.request_cancel(); };
    MIRAGE_CHECK(!read(true, true, in_capture).ok && !state.hidden);
    pipeline.on_capture = {};
    desktop::CancelToken in_assembly;
    environment.windows.on_front = [&] { in_assembly.request_cancel(); };
    MIRAGE_CHECK(!read(true, false, in_assembly).ok && !state.hidden);
    environment.windows.on_front = [&] { actions.takeover(); };
    result = read();
    MIRAGE_CHECK(!result.ok &&
                 result.error.message.find("desktop_epoch_changed") != std::string::npos);
    MIRAGE_CHECK(actions.resume());
    environment.windows.on_front = [&] { (void)pipeline.publish("another producer"); };
    result = read();
    MIRAGE_CHECK(!result.ok &&
                 result.error.message.find("visual_snapshot_changed") != std::string::npos);
    environment.windows.on_front = {};
    state.on_release = [&] { (void)pipeline.publish("replacement during restoration"); };
    result = read();
    MIRAGE_CHECK(!result.ok &&
                 result.error.message.find("visual_snapshot_changed") != std::string::npos);
    state.on_release = [&] { actions.takeover(); };
    result = read();
    MIRAGE_CHECK(!result.ok &&
                 result.error.message.find("desktop_epoch_changed") != std::string::npos);
    MIRAGE_CHECK(actions.resume());
    state.on_release = {};
    result = read();
    MIRAGE_CHECK(result.ok && !state.hidden && !state.violation);
    // DEC-106: a really excluded lease must not carry the passive notice.
    MIRAGE_CHECK(result.observation.environment_state.find("pet_presentation") ==
                 std::string::npos);
    state.passive = true;
    state.expect_hidden = false;
    const auto before_passive = state.captures.load();
    result = read();
    MIRAGE_CHECK(result.ok && !state.hidden && !state.violation &&
                 state.captures == before_passive + 1);
    MIRAGE_CHECK(result.observation.environment_state.find("pet_presentation=not_excluded") !=
                     std::string::npos &&
                 !result.observation.visual_snapshot_ref.empty());
    allowed = false;
    MIRAGE_CHECK(!read().ok && !state.hidden && state.captures == before_passive + 1);
}
class Tray final : public desktop::TrayCarrier {
  public:
    RunReport run(const desktop::TrayCarrierContext &context,
                  const std::function<bool()> &stop) override {
        if (context.on_ready)
            context.on_ready();
        while (!stop())
            (void)::poll(nullptr, 0, 5);
        return {true, {}};
    }
    void wakeup() override {}
};
class Pet final : public desktop::FrontendProcess {
  public:
    explicit Pet(State &state) : state_(state) {}
    bool open(const std::string &, std::string &) override {
        pid_ = 777;
        return true;
    }
    std::int64_t pid() override { return pid_.load(); }
    bool stop(std::string &) override {
        pid_ = 0;
        return true;
    }
    desktop::PresentationExclusion
    exclude_presentation(const desktop::CancelToken &cancel) override {
        return acquire(state_, cancel);
    }

  private:
    State &state_;
    std::atomic_int64_t pid_{0};
};
void ipc_cases() {
    test::TempDir directory;
    State state;
    auto environment = std::make_shared<Environment>(state);
    desktop::VisualReferenceRegistry registry;
    desktop::VisualSnapshot cached;
    MIRAGE_CHECK(registry.publish(cached).ok);
    runtime::ServiceConfig config;
    config.socket_path = directory.root() / "pet-observation.sock";
    config.persist_recovery_state = config.persist_session_state = config.persist_settings = false;
    config.open_frontend = false;
    config.pet = std::make_shared<Pet>(state);
    config.tray_carrier = std::make_shared<Tray>();
    config.visual_registry = &registry;
    runtime::RuntimeService service(config);
    const auto started =
        service.start(std::make_shared<mirage::integration::MiraEnvironmentBinding>(environment));
    MIRAGE_CHECK(started.ok);
    if (!started.ok)
        return;
    ipc::IpcClient client(config.socket_path);
    MIRAGE_CHECK(client.call(ipc::ProductControlRequest{"pet_ready", 0, 0, 777}, 3s).ok);
    auto response = client.call(ipc::DesktopObserveRequest{true, false}, 3s);
    MIRAGE_CHECK(response.ok && std::holds_alternative<ipc::ObservationView>(response.payload));
    MIRAGE_CHECK(state.admitted == 1 && state.restored == 1 && !state.hidden && !state.violation);
    if (const auto *view = std::get_if<ipc::ObservationView>(&response.payload)) {
        MIRAGE_CHECK(view->active_window == "Fixture target" && view->semantic.has_value());
        MIRAGE_CHECK(!view->visual_snapshot_ref.has_value());
    }
    const auto before = state.admitted.load();
    response = client.call(ipc::DesktopObserveRequest{true, true}, 3s);
    MIRAGE_CHECK(!response.ok && response.error.code == "unavailable");
    MIRAGE_CHECK(response.error.message.find("requires_fresh_visual") != std::string::npos);
    MIRAGE_CHECK(state.admitted == before); // no cached generation is treated as clean
    for (const auto fault : {Fault::Admission, Fault::Before, Fault::After, Fault::Restore}) {
        state.fault = fault;
        response = client.call(ipc::DesktopObserveRequest{false, false}, 3s);
        MIRAGE_CHECK(!response.ok && response.error.code == "unavailable" && !state.hidden);
        MIRAGE_CHECK(client.call(ipc::HelloRequest{}, 3s).ok);
    }
    state.fault = Fault::None;
    state.provider_throws = true;
    response = client.call(ipc::DesktopObserveRequest{}, 3s);
    MIRAGE_CHECK(!response.ok && !state.hidden);
    state.provider_throws = false;
    MIRAGE_CHECK(client.call(ipc::DesktopObserveRequest{}, 3s).ok && !state.violation);
    // Every successful/failed request released its owner lease: stop and a new
    // generation can be admitted instead of remaining stuck behind the guard.
    MIRAGE_CHECK(client.call(ipc::ProductControlRequest{"pet_stop", 0, 0, 0}, 3s).ok);
    const auto deadline = std::chrono::steady_clock::now() + 3s;
    bool stopped = false;
    while (!stopped && std::chrono::steady_clock::now() < deadline) {
        const auto status = client.call(ipc::ProductControlRequest{}, 3s);
        if (const auto *value = std::get_if<ipc::ProductState>(&status.payload))
            stopped = value->pet_pid == 0;
        if (!stopped)
            (void)::poll(nullptr, 0, 1);
    }
    MIRAGE_CHECK(stopped);
    MIRAGE_CHECK(client.call(ipc::ProductControlRequest{"pet_start", 0, 0, 0}, 3s).ok);
    MIRAGE_CHECK(client.call(ipc::ProductControlRequest{"pet_ready", 0, 0, 777}, 3s).ok);
    response = client.call(ipc::DesktopObserveRequest{}, 3s);
    MIRAGE_CHECK(response.ok && !state.hidden);
    if (const auto *view = std::get_if<ipc::ObservationView>(&response.payload))
        MIRAGE_CHECK(view->environment_state.find("pet_presentation") == std::string::npos);
    state.passive = true;
    state.expect_hidden = false;
    response = client.call(ipc::DesktopObserveRequest{true, false}, 3s);
    MIRAGE_CHECK(response.ok && !state.hidden && !state.violation);
    if (const auto *view = std::get_if<ipc::ObservationView>(&response.payload))
        MIRAGE_CHECK(view->environment_state.find("pet_presentation=not_excluded") !=
                     std::string::npos);
    service.request_shutdown();
    MIRAGE_CHECK(service.run().clean);
}
} // namespace
int main() {
    helper_cases();
    ipc_cases();
    return test::finish("pet_observation_test");
}
