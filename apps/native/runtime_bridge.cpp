#include "runtime_bridge.hpp"
#include <atomic>
#include <future>
#include <kairo/comm.hpp>
#include <kairo/executor.hpp>
#include <kairo/serial_execution_context.hpp>
#include <mirage/runtime/ipc/endpoint.hpp>
#include <mirage/runtime/ipc/session_client.hpp>
#include <vector>

namespace mirage::native_ui {
namespace ipc = mirage::runtime::ipc;
struct RuntimeBridge::Impl {
    kairo::Executor executor;
    kairo::SerialExecutionContext serial;
    std::shared_ptr<ipc::SessionClient> client;
    std::shared_ptr<ipc::SessionClient> catalog_client;
    kairo::comm::MpscChannel<RuntimeMessage> inbox{{.capacity = 128, .name = "native-ui-events"}};
    std::atomic_bool live{false}, gap{false}, active{false};
    std::atomic_bool animating{false};
    kairo::TimerHandle activity_timer;
    std::uint64_t timer_failures = 0;
    std::function<void()> wake;
    kairo::WorkerHandle worker;
    kairo::WorkerHandle catalog_worker;
    std::vector<std::future<void>> calls; // UI-only ownership, capacity 16
    bool stopping = false;
    void post(RuntimeMessage message) {
        if (!inbox.try_send(std::move(message)))
            gap.store(true);
        wake(); // EUI documented atomic requestUiUpdate/platform wake boundary
    }
    class Worker final : public kairo::IBlockingIoWorker {
      public:
        explicit Worker(Impl &owner) : owner_(owner) {}
        void run(kairo::StopToken stop) override {
            std::string reason;
            if (!owner_.client->connect(std::chrono::milliseconds{500}, reason)) {
                owner_.post({RuntimeMessage::Kind::Lost, reason, 0, {}, {}});
                return;
            }
            if (stop.stop_requested()) {
                owner_.client->stop();
                return;
            }
            owner_.live.store(true);
            owner_.post({RuntimeMessage::Kind::Connected, {}, 0, {}, {}});
            (void)owner_.client->run(reason);
            owner_.live.store(false);
            if (!stop.stop_requested())
                owner_.post({RuntimeMessage::Kind::Lost, reason, 0, {}, {}});
        }
        void wakeup() noexcept override { owner_.client->stop(); }

      private:
        Impl &owner_;
    };
    class CatalogWorker final : public kairo::IBlockingIoWorker {
      public:
        explicit CatalogWorker(Impl &owner) : owner_(owner) {}
        void run(kairo::StopToken stop) override {
            std::string reason;
            if (!owner_.catalog_client->connect(std::chrono::milliseconds{500}, reason)) {
                owner_.post(
                    {RuntimeMessage::Kind::Diagnostic, "模型目录连接失败: " + reason, 0, {}, {}});
                return;
            }
            if (stop.stop_requested()) {
                owner_.catalog_client->stop();
                return;
            }
            (void)owner_.catalog_client->run(reason);
        }
        void wakeup() noexcept override { owner_.catalog_client->stop(); }

      private:
        Impl &owner_;
    };
};
RuntimeBridge::RuntimeBridge(std::function<void()> wake_ui, std::string endpoint)
    : impl_(std::make_unique<Impl>()) {
    auto &p = *impl_;
    p.wake = std::move(wake_ui);
    p.client = std::make_shared<ipc::SessionClient>(endpoint.empty() ? ipc::default_socket_path()
                                                                     : endpoint);
    p.catalog_client = std::make_shared<ipc::SessionClient>(p.client->address());
    p.client->set_event_sink([&p](const ipc::Event &event) {
        RuntimeMessage message;
        message.kind = RuntimeMessage::Kind::Event;
        message.event = event;
        p.post(std::move(message));
    });
    kairo::ExecutorConfig config;
    config.min_threads = config.max_threads = 2; // catalog has its own IPC session
    config.queue_capacity = 16;
    config.max_in_flight_tasks = 16;
    if (!p.executor.initialize(config)) {
        p.post({RuntimeMessage::Kind::Lost, "Executor 初始化失败", 0, {}, {}});
        return;
    }
    kairo::BlockingWorkerSpec spec;
    spec.name = "mirage-native-ipc";
    spec.config.thread_name = "mirage-native-ipc";
    spec.worker = std::make_unique<Impl::Worker>(p);
    p.worker = p.executor.start_worker(std::move(spec));
    if (!p.worker.started())
        p.post({RuntimeMessage::Kind::Lost,
                "IPC worker 提交被拒绝: " + p.worker.start_result().message,
                0,
                {},
                {}});
    kairo::BlockingWorkerSpec catalog_spec;
    catalog_spec.name = "mirage-native-catalog-ipc";
    catalog_spec.config.thread_name = "mirage-native-catalog-ipc";
    catalog_spec.worker = std::make_unique<Impl::CatalogWorker>(p);
    p.catalog_worker = p.executor.start_worker(std::move(catalog_spec));
    if (!p.catalog_worker.started())
        p.post({RuntimeMessage::Kind::Diagnostic,
                "模型目录连接未启动: " + p.catalog_worker.start_result().message,
                0,
                {},
                {}});
}
RuntimeBridge::~RuntimeBridge() { shutdown(); }
bool RuntimeBridge::call(ipc::Request request, std::string tag, std::uint64_t local_id) {
    auto &p = *impl_;
    for (auto it = p.calls.begin(); it != p.calls.end();) {
        if (it->wait_for(std::chrono::milliseconds{0}) == std::future_status::ready) {
            try {
                it->get();
            } catch (...) {
                p.post({RuntimeMessage::Kind::Lost, "IPC 任务执行失败或提交被拒绝", 0, {}, {}});
            }
            it = p.calls.erase(it);
        } else
            ++it;
    }
    if (p.stopping || !p.live.load() || p.calls.size() >= 16)
        return false;
    const bool catalog = std::holds_alternative<ipc::ListModelsRequest>(request);
    auto action = [&p, request = std::move(request), tag = std::move(tag), local_id, catalog] {
        const auto timeout =
            catalog ? std::chrono::milliseconds{25000} : std::chrono::milliseconds{4000};
        const auto response = (catalog ? p.catalog_client : p.client)->call(request, timeout).get();
        p.post({RuntimeMessage::Kind::Response, tag, local_id, response, {}});
    };
    if (catalog)
        p.calls.push_back(p.executor.submit_auto(std::move(action)));
    else
        p.calls.push_back(p.executor.submit_on(p.serial, std::move(action)));
    return true;
}
bool RuntimeBridge::load_attachment(const std::string &path, std::uint64_t session_id,
                                    std::uint64_t generation) {
    auto &p = *impl_;
    if (p.stopping || p.calls.size() >= 16)
        return false;
    p.calls.push_back(p.executor.submit_auto([&p, path, session_id, generation] {
        RuntimeMessage message;
        message.kind = RuntimeMessage::Kind::Attachment;
        message.local_id = session_id;
        message.attachment_generation = generation;
        message.attachment = read_text_attachment(path);
        p.post(std::move(message));
    }));
    return true;
}
bool RuntimeBridge::receive(RuntimeMessage &out) {
    auto &p = *impl_;
    if (p.activity_timer.valid()) {
        const auto status = p.executor.get_periodic_task_status(p.activity_timer.id());
        if (status && status->failed_count > p.timer_failures) {
            p.timer_failures = status->failed_count;
            p.post({RuntimeMessage::Kind::Diagnostic,
                    "等待状态刷新失败: " + status->last_error_message,
                    0,
                    {},
                    {}});
        }
    }
    for (auto it = p.calls.begin(); it != p.calls.end();) {
        if (it->wait_for(std::chrono::milliseconds{0}) == std::future_status::ready) {
            try {
                it->get();
            } catch (...) {
                p.post({RuntimeMessage::Kind::Lost, "IPC 任务执行失败或提交被拒绝", 0, {}, {}});
            }
            it = p.calls.erase(it);
        } else
            ++it;
    }
    return p.inbox.try_receive(out);
}
bool RuntimeBridge::connected() const { return impl_->live.load(); }
bool RuntimeBridge::take_gap() { return impl_->gap.exchange(false); }
void RuntimeBridge::set_activity(bool active, bool animating) {
    auto &p = *impl_;
    if (p.stopping || (p.active.load() == active && p.animating.load() == animating))
        return;
    p.active.store(active);
    p.animating.store(animating);
    if (p.activity_timer.valid())
        (void)p.activity_timer.cancel();
    p.activity_timer = {};
    if (!active && !animating) {
        return;
    }
    p.timer_failures = 0;
    p.activity_timer =
        p.executor.submit_periodic_cancellable(animating ? 16 : 100, [&p](kairo::StopToken stop) {
            if (!stop.stop_requested() && (p.active.load() || p.animating.load()))
                p.wake();
        });
    if (!p.activity_timer.valid())
        p.post({RuntimeMessage::Kind::Diagnostic, "等待状态刷新提交被拒绝", 0, {}, {}});
}

void RuntimeBridge::shutdown() {
    if (!impl_ || impl_->stopping)
        return;
    auto &p = *impl_;
    p.stopping = true;
    p.active.store(false);
    if (p.activity_timer.valid())
        (void)p.activity_timer.cancel();
    p.live.store(false);
    p.client->stop();
    p.catalog_client->stop();
    if (p.worker.started())
        p.worker.stop();
    if (p.catalog_worker.started())
        p.catalog_worker.stop();
    for (auto &future : p.calls) {
        try {
            future.get();
        } catch (...) {
            p.gap.store(true);
        }
    }
    p.calls.clear();
    p.serial.shutdown();
    p.executor.shutdown(true);
    p.inbox.close();
}
} // namespace mirage::native_ui
