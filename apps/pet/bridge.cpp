#include "bridge.hpp"
#include <mirage/platform/frontend_process.hpp>

#include <atomic>
#include <chrono>
#include <eui/app.h>
#include <iostream>
#include <kairo/comm.hpp>
#include <kairo/executor.hpp>
#include <mirage/platform/pet_window.hpp>
#include <mirage/runtime/ipc/session_client.hpp>

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
        if (!live.load() || stale.load()) {
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
        animation_wake = executor.submit_periodic(50, [this] {
            if (visible.load() && (animating.load() || !live.load() || stale.load() ||
                                   platform::pet_window::exit_requested()))
                app::requestUpdate();
        });
        return animation_wake.valid();
    }
    class SessionWorker final : public kairo::IBlockingIoWorker {
      public:
        explicit SessionWorker(Impl &impl) : impl_(impl) {}
        void run(kairo::StopToken stop) override {
            std::string diagnostic;
            (void)impl_.client->run(diagnostic);
            impl_.live.store(false);
            if (!stop.stop_requested())
                app::requestUpdate();
        }
        void wakeup() noexcept override { impl_.client->stop(); }

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
    worker.worker = std::make_unique<Impl::SessionWorker>(p);
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
    p.refresh = p.executor.submit_periodic(2000, [&p] {
        p.check_hidden_exit();
        if (!p.live.load() || p.refreshing.exchange(true))
            return;
        struct Release {
            std::atomic_bool &flag;
            ~Release() { flag.store(false); }
        } release{p.refreshing};
        ipc::Response response;
        try {
            response = p.client->call(ipc::PetStatusRequest{}, 1500ms).get();
        } catch (const std::exception &error) {
            p.stale.store(true);
            if (p.visible.load())
                app::requestUpdate();
            std::cerr << "mira_pet: status refresh failed: " << error.what() << '\n';
            throw; // keep the periodic failure visible to the Executor owner
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
    return p.live.load() && !p.stale.load() && p.inbox.try_load(out);
}
bool Bridge::connected() const { return impl_->live.load() && !impl_->stale.load(); }
ResourceLoader &Bridge::loader() { return impl_->resources; }
void Bridge::set_animating(bool value) { impl_->animating.store(value); }
void Bridge::set_visible(bool value) {
    auto &p = *impl_;
    if (p.stopping || p.visible.exchange(value) == value)
        return;
    if (p.animation_wake.valid())
        (void)p.animation_wake.cancel();
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
    if (p.refresh.valid())
        (void)p.refresh.cancel();
    p.client->stop();
    if (p.worker.started())
        p.worker.stop();
    p.resources.stop();
    p.executor.shutdown(true);
}
} // namespace mirage::pet_ui
