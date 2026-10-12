#include "bridge.hpp"

#include <atomic>
#include <chrono>
#include <eui/app.h>
#include <kairo/comm.hpp>
#include <kairo/executor.hpp>
#include <mirage/runtime/ipc/session_client.hpp>

namespace mirage::pet_ui {
namespace ipc = runtime::ipc;
using namespace std::chrono_literals;

struct Bridge::Impl {
    explicit Impl(std::string endpoint)
        : client(std::make_shared<ipc::SessionClient>(std::move(endpoint))) {}
    kairo::Executor executor;
    std::shared_ptr<ipc::SessionClient> client;
    kairo::comm::LatestMailbox<runtime::pet::Status> inbox{"mira-pet-status"};
    kairo::WorkerHandle worker;
    kairo::TimerHandle refresh;
    kairo::TimerHandle animation_wake;
    std::atomic_bool live{false}, stale{false}, refreshing{false}, animating{false};
    std::atomic_uint64_t event_seq{0};
    bool stopping = false;

    void publish(const runtime::pet::Status &status) {
        inbox.publish(status);
        app::requestUpdate();
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
    p.worker = p.executor.start_worker(std::move(worker));
    if (!p.worker.started()) {
        diagnostic = "mira_pet IPC worker admission failed: " + p.worker.start_result().message;
        return false;
    }
    p.live.store(true);
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
        if (!p.live.load() || p.refreshing.exchange(true))
            return;
        struct Release {
            std::atomic_bool &flag;
            ~Release() { flag.store(false); }
        } release{p.refreshing};
        const auto response = p.client->call(ipc::PetStatusRequest{}, 1500ms).get();
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
    p.animation_wake = p.executor.submit_periodic(50, [&p] {
        if (p.animating.load() || !p.live.load() || p.stale.load())
            app::requestUpdate();
    });
    if (!p.animation_wake.valid()) {
        diagnostic = "mira_pet animation timer admission failed";
        return false;
    }
    return true;
}
bool Bridge::latest(runtime::pet::Status &out) {
    auto &p = *impl_;
    return p.live.load() && !p.stale.load() && p.inbox.try_load(out);
}
bool Bridge::connected() const { return impl_->live.load() && !impl_->stale.load(); }
void Bridge::set_animating(bool value) { impl_->animating.store(value); }
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
    p.executor.shutdown(true);
}
} // namespace mirage::pet_ui
