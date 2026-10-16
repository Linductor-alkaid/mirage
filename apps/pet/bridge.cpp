#include "bridge.hpp"
#include "../common/session_run_worker.hpp"
#include <mirage/platform/frontend_process.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <eui/app.h>
#include <iostream>
#include <kairo/comm.hpp>
#include <kairo/executor.hpp>
#include <mirage/platform/pet_window.hpp>
#include <mirage/runtime/ipc/session_client.hpp>
#include <mirage/runtime/persistence/paths.hpp>
#include <mirage/runtime/persistence/store.hpp>

namespace mirage::pet_ui {
namespace ipc = runtime::ipc;
using namespace std::chrono_literals;

struct Bridge::Impl {
    explicit Impl(std::string endpoint)
        : client(std::make_shared<ipc::SessionClient>(std::move(endpoint))) {}
    kairo::Executor executor;
    ResourceLoader resources{executor,
                             [](const runtime::pet::Clip &clip, DecodedClip &out,
                                std::string &diagnostic, const std::function<bool()> &cancelled) {
                                 return decode_clip("assets/pet", clip, out, diagnostic, cancelled);
                             }};
    std::shared_ptr<ipc::SessionClient> client;
    kairo::comm::LatestMailbox<runtime::pet::Status> inbox{"mira-pet-status"};
    kairo::WorkerHandle worker;
    kairo::TimerHandle refresh;
    kairo::TimerHandle animation_wake;
    std::array<kairo::TimerSubmission<void>, 3>
        bubble_expiries; // UI-owned, bounded handle/future retention
    void consume_bubble_timers(bool drain = false) {
        for (auto &timer : bubble_expiries) {
            if (!timer.future.valid() || (!drain && timer.future.wait_for(std::chrono::milliseconds{
                                                        0}) != std::future_status::ready))
                continue;
            try {
                timer.future.get();
            } catch (const kairo::TaskCancelled &) {
            } // visible cooperative cancellation
            catch (const std::exception &error) {
                stale.store(true);
                std::cerr << "mira_pet: bubble timer failed: " << error.what() << '\n';
            }
        }
    }
    void cancel_bubble_timers() {
        for (auto &timer : bubble_expiries)
            if (timer.handle.valid())
                (void)timer.handle.cancel();
        consume_bubble_timers();
    }
    std::uint64_t consumed_status = 0, consumed_preferences = 0; // UI-owned
    kairo::comm::LatestMailbox<runtime::persistence::AppPreferences> appearance{"pet-appearance"};
    runtime::persistence::LocalStateStore preference_store{
        runtime::persistence::default_config_directory(), "preferences.json",
        runtime::persistence::kMaxAppPreferencesBytes};
    void load_preferences() {
        const auto stored = preference_store.load();
        if (stored.status == runtime::persistence::LoadStatus::Loaded)
            appearance.publish(runtime::persistence::decode_app_preferences(stored.body));
        else if (stored.status == runtime::persistence::LoadStatus::Absent)
            appearance.publish(runtime::persistence::AppPreferences{});
        else
            std::cerr << "mira_pet: appearance read failed: " << stored.error << '\n';
    }
    std::atomic_bool live{false}, stale{false}, refreshing{false}, animating{false}, visible{true};
    std::atomic_uint64_t event_seq{0};
    std::atomic_int64_t lost_since_ms{0};
    bool stopping = false;

    void publish(const runtime::pet::Status &status) {
        inbox.publish(status);
        if (visible.load())
            app::requestUpdate();
    }
    void check_hidden_exit() {
        const auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
                             std::chrono::steady_clock::now().time_since_epoch())
                             .count();
        auto since = lost_since_ms.load();
        // BUG-20261015-002: distinguish transport failure from sync staleness.
        // Only !live (worker thread stopped) indicates actual owner loss, not stale
        // (which just means the last poll failed — events may still be arriving).
        if (!live.load()) {
            if (!since) {
                (void)lost_since_ms.compare_exchange_strong(since, now);
                since = lost_since_ms.load();
            }
        } else {
            lost_since_ms.store(0);
            since = 0;
        }
        std::string diagnostic;
        if (!platform::pet_window::relay_hidden_exit(since && now - since >= 5000, diagnostic))
            std::cerr << "mira_pet: " << diagnostic << '\n';
    }
    bool arm_animation() {
        animation_wake = executor.submit_periodic(33, [this] {
            if (visible.load() && (animating.load() || !live.load() || stale.load() ||
                                   platform::pet_window::exit_requested()))
                app::requestUpdate();
        });
        return animation_wake.valid();
    }
    // REUSE-15: worker skeleton (cooperative wakeup, run episode) comes
    // from the shared apps/common base.
    class SessionWorker final : public mirage::apps::SessionRunWorker {
      public:
        SessionWorker(Impl &impl, std::shared_ptr<ipc::SessionClient> client)
            : mirage::apps::SessionRunWorker(std::move(client)), impl_(impl) {}
        void run(kairo::StopToken stop) override {
            std::string diagnostic;
            const auto exit_reason = drive(diagnostic);
            impl_.live.store(false);

            // DEC-094: consume and log the exit reason
            if (exit_reason == ipc::SessionClient::RunExit::ConnectionLost) {
                std::cerr << "mira_pet: IPC connection lost: " << diagnostic << '\n';
            }
            // RunExit::Stopped is normal shutdown, no warning needed

            if (!stop.stop_requested())
                app::requestUpdate();
        }

      private:
        Impl &impl_;
    };
};

Bridge::Bridge(std::string endpoint) : impl_(std::make_unique<Impl>(std::move(endpoint))) {}
Bridge::~Bridge() { shutdown(); }
bool Bridge::start(std::string &diagnostic) {
    auto &p = *impl_;
    kairo::ExecutorConfig config;
    config.min_threads = config.max_threads = 2;
    config.queue_capacity = config.max_in_flight_tasks = 8;
    if (!p.executor.initialize(config)) {
        diagnostic = "mira_pet Executor 初始化失败";
        return false;
    }
    p.load_preferences();
    p.client->set_event_sink([&p](const ipc::Event &event) {
        const auto previous = p.event_seq.exchange(event.seq);
        if (previous && event.seq != previous + 1)
            p.stale.store(true);
        if (const auto *updated = std::get_if<ipc::PetUpdatedEvent>(&event.payload))
            p.publish(updated->status);
    });
    if (!p.client->connect(500ms, diagnostic))
        return false;
    kairo::BlockingWorkerSpec worker;
    worker.name = "mira-pet-ipc";
    worker.config.thread_name = "mira-pet-ipc";
    worker.worker = std::make_unique<Impl::SessionWorker>(p, p.client);
    p.live.store(true);
    p.worker = p.executor.start_worker(std::move(worker));
    if (!p.worker.started()) {
        p.live.store(false);
        diagnostic = "mira_pet IPC worker admission failed: " + p.worker.start_result().message;
        return false;
    }
    const auto subscribed = p.client->call(ipc::SubscribeEventsRequest{false, true}, 1500ms).get();
    if (!subscribed.ok) {
        diagnostic = "mira_pet event subscription failed: " + subscribed.error.message;
        return false;
    }
    const auto initial = p.client->call(ipc::PetStatusRequest{}, 1500ms).get();
    const auto *status = std::get_if<runtime::pet::Status>(&initial.payload);
    if (!initial.ok || !status) {
        diagnostic = "mira_pet initial status unavailable";
        return false;
    }
    p.publish(*status);
    // M8-05: Reduce polling frequency — events provide real-time updates, periodic
    // status requests are only a fallback health check, not the primary source.
    p.refresh = p.executor.submit_periodic(30000, [&p] {
        p.check_hidden_exit();
        p.load_preferences();
        // BUG-20261015-002 partial fix: status request timeout should not close
        // the entire session while events are still arriving. This check ensures
        // we only attempt sync when not already refreshing (prevents overlap).
        if (!p.live.load() || p.refreshing.exchange(true))
            return;
        struct Release {
            std::atomic_bool &flag;
            ~Release() { flag.store(false); }
        } release{p.refreshing};
        ipc::Response response;
        try {
            response = p.client->call(ipc::PetStatusRequest{}, 3000ms).get();
        } catch (const std::exception &error) {
            // M8-05: sync failure != connection lost. Mark stale but do not propagate
            // exception — the event stream may still be healthy. The worker thread
            // monitors transport health independently.
            p.stale.store(true);
            if (p.visible.load())
                app::requestUpdate();
            std::cerr << "mira_pet: status sync failed (events may still be live): " << error.what()
                      << '\n';
            return; // do not throw — let events drive state if still arriving
        }
        if (const auto *snapshot = std::get_if<runtime::pet::Status>(&response.payload);
            response.ok && snapshot) {
            p.stale.store(false);
            p.publish(*snapshot);
        } else {
            p.stale.store(true);
            app::requestUpdate();
        }
    });
    if (!p.refresh.valid()) {
        diagnostic = "mira_pet refresh timer admission failed";
        return false;
    }
    if (!p.arm_animation()) {
        diagnostic = "mira_pet animation timer admission failed";
        return false;
    }
    const auto ready =
        p.client
            ->call(ipc::ProductControlRequest{"pet_ready", 0, 0, platform::current_process_id()},
                   1500ms)
            .get();
    if (!ready.ok) {
        diagnostic = "mira_pet readiness refused: " + ready.error.message;
        return false;
    }
    return true;
}
bool Bridge::latest(runtime::pet::Status &out) {
    auto &p = *impl_;
    p.consume_bubble_timers();
    std::uint64_t sequence = 0;
    const bool fresh = p.live.load() && !p.stale.load() &&
                       p.inbox.try_load_newer_than(p.consumed_status, out, sequence);
    if (fresh)
        p.consumed_status = sequence;
    return fresh;
}
bool Bridge::preferences(runtime::persistence::AppPreferences &out) {
    auto &p = *impl_;
    std::uint64_t sequence = 0;
    const bool fresh = p.appearance.try_load_newer_than(p.consumed_preferences, out, sequence);
    if (fresh)
        p.consumed_preferences = sequence;
    return fresh;
}
void Bridge::set_bubble_expiry(std::uint64_t remaining_ms) {
    auto &p = *impl_;
    p.cancel_bubble_timers();
    if (!remaining_ms || !p.visible.load() || p.stopping)
        return;
    auto slot = std::find_if(p.bubble_expiries.begin(), p.bubble_expiries.end(),
                             [](const auto &timer) { return !timer.future.valid(); });
    if (slot == p.bubble_expiries.end()) {
        p.stale.store(true);
        std::cerr << "mira_pet: bubble timer retention capacity exceeded\n";
        return;
    }
    *slot = p.executor.submit_delayed(
        static_cast<std::int64_t>(std::min<std::uint64_t>(5000, remaining_ms)),
        [] { app::requestUpdate(); });
    if (!slot->handle.valid()) {
        p.stale.store(true);
        std::cerr << "mira_pet: bubble expiry timer admission failed\n";
    }
}
bool Bridge::connected() const {
    // BUG-20261015-002: connected means transport is live, not sync-state fresh.
    // The event stream may be healthy even if the last poll was stale.
    return impl_->live.load();
}
ResourceLoader &Bridge::loader() { return impl_->resources; }
void Bridge::set_animating(bool value) { impl_->animating.store(value); }
void Bridge::set_visible(bool value) {
    auto &p = *impl_;
    if (p.stopping || p.visible.exchange(value) == value)
        return;
    if (p.animation_wake.valid())
        (void)p.animation_wake.cancel();
    p.cancel_bubble_timers();
    p.animation_wake = {};
    if (value && !p.arm_animation()) {
        p.stale.store(true);
        std::cerr << "mira_pet: restored animation timer admission failed\n";
    }
}
void Bridge::shutdown() {
    if (!impl_ || impl_->stopping)
        return;
    auto &p = *impl_;
    p.stopping = true;
    p.live.store(false);
    p.animating.store(false);
    if (p.animation_wake.valid())
        (void)p.animation_wake.cancel();
    p.cancel_bubble_timers();
    if (p.refresh.valid())
        (void)p.refresh.cancel();
    p.client->stop();
    if (p.worker.started())
        p.worker.stop();
    p.resources.stop();
    p.executor.shutdown(true);
    p.consume_bubble_timers(true);
}
} // namespace mirage::pet_ui
