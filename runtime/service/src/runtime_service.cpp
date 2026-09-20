#include <mirage/runtime/runtime_service.hpp>

#include <mirage/runtime/ipc/endpoint.hpp>
#include <mirage/runtime/ipc/framing.hpp>
#include <mirage/runtime/ipc/protocol.hpp>
#include <mirage/runtime/ipc/stream.hpp>
#include <mirage/runtime/persistence/paths.hpp>
#include <mirage/runtime/persistence/recovery.hpp>
#include <mirage/runtime/persistence/store.hpp>

#include "service_core.hpp"
#include "service_loop.hpp"
#include "task_driver.hpp"

#include <executor/blocking_io.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <future>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace mirage::runtime {
namespace {

using detail::progress_name;

constexpr const char *kServiceName = "mirage-runtime";
constexpr std::size_t kMaxGoalBytes = 8 * 1024;
constexpr std::size_t kMaxArgumentBytes = 4 * 1024;

bool terminal_progress(TaskProgress progress) {
    return progress == TaskProgress::Completed || progress == TaskProgress::Failed ||
           progress == TaskProgress::Cancelled;
}

} // namespace

ServiceInfo runtime_service_info() {
    // The service owns the process's Executor and the hosted pinned runtime
    // and outlives any GUI (design doc section 12, EXEC-01, DEC-007).
    return {kServiceName, true};
}

struct RuntimeService::Impl {
    enum class Lifecycle { New, Running, Terminal };

    ServiceConfig config;
    std::string socket_path;
    /// Fail-closed hook used when the config carries no confirmation
    /// handler (DEC-010): headless M1 services reject every Confirm rule.
    permission::DenyAllConfirmation fail_closed_confirmation;
    std::shared_ptr<detail::ServiceCore> core = std::make_shared<detail::ServiceCore>();
    /// Non-owning observer of the loop object; the executor's blocking
    /// worker owns the loop itself. Valid from a successful start_worker()
    /// until the lifecycle leaves Running (the run()/destructor path joins
    /// the loop before touching anything else).
    detail::ServiceLoop *loop = nullptr;
    ipc::IpcListener listener;
    executor::WorkerHandle loop_worker;
    std::promise<void> loop_done;
    std::future<void> loop_done_future;
    std::atomic<Lifecycle> lifecycle{Lifecycle::New};
    int shutdown_fd = -1;
    ServiceRunReport run_report;

    explicit Impl(ServiceConfig service_config)
        : config(std::move(service_config)),
          socket_path(config.socket_path.empty() ? ipc::default_socket_path()
                                                 : config.socket_path) {
        core->mirage_version = config.mirage_version;
        core->max_steps_per_task = config.max_steps_per_task;
        core->max_task_records = config.max_task_records;
        core->max_result_bytes = config.max_result_bytes;
        core->step_timeout = config.step_timeout;
        core->command_wait = config.command_wait;
        {
            std::lock_guard lock(core->registry.mutex);
            core->registry.capacity = config.max_task_records;
        }
        // One controller for the whole service (DEC-010): the configured
        // policy plus the configured hook, or the fail-closed default.
        core->permission = std::make_shared<permission::PermissionController>(
            config.permission_policy,
            config.confirmation ? *config.confirmation : fail_closed_confirmation);
        loop_done_future = loop_done.get_future();
    }

    ipc::ServiceIdentity identity() const {
        ipc::ServiceIdentity result;
        result.name = kServiceName;
        result.mirage_version = config.mirage_version.empty() ? "unknown" : config.mirage_version;
        result.mira_core_version = mira_core_version_string();
        result.host_status = host_status_name(core->host.status());
        result.protocol = ipc::kProtocolVersion;
        // DEC-012: an event-capable service always advertises the member.
        result.events = true;
        return result;
    }

    // --- response helpers (loop thread via posted payloads) ---------------

    void respond(std::uint64_t connection_id, std::uint64_t correlation_id,
                 ipc::ResponsePayload payload) {
        ipc::Response response;
        response.ok = true;
        response.id = correlation_id;
        response.payload = std::move(payload);
        if (loop != nullptr) {
            loop->post_response(connection_id, ipc::encode_response(response));
        }
    }

    void fail(std::uint64_t connection_id, std::uint64_t correlation_id, std::string code,
              std::string message, bool close_after = false) {
        ipc::Response response;
        response.ok = false;
        response.id = correlation_id;
        response.error = {std::move(code), std::move(message)};
        if (loop == nullptr) {
            return;
        }
        const std::string payload = ipc::encode_response(response);
        if (close_after) {
            loop->post_response_and_close(connection_id, payload);
        } else {
            loop->post_response(connection_id, payload);
        }
    }

    /// Host status transitions publish into the hub (DEC-012 decision 5):
    /// the LatestMailbox keeps the newest status for subscribe-time seeds
    /// and the change enters the Topic publish path. Bounded and
    /// thread-safe; also legal after the executor drained (teardown), where
    /// subscribers no longer exist but the mailbox stays truthful.
    void publish_host_status(HostStatus status) {
        ipc::HostStatusEvent event;
        event.status = host_status_name(status);
        core->events.publish_host_status(std::move(event));
    }

    // --- request handling --------------------------------------------------

    /// Loop thread: pushes the frame through the service's serial context so
    /// every request (and every host operation it performs) runs serialized
    /// on one thread, and waits for the bounded handler to finish.
    void handle_frame(std::uint64_t connection_id, const std::string &payload) {
        auto handled = core->executor.submit_on(core->serial, [this, connection_id, payload] {
            process_request(connection_id, payload);
        });
        try {
            handled.get();
        } catch (const std::exception &) {
            fail(connection_id, 0, "internal", "request was not admitted by the service runtime",
                 true);
        }
    }

    /// Serial thread: decode and dispatch one request.
    void process_request(std::uint64_t connection_id, const std::string &payload) {
        ipc::RequestDecode decoded = ipc::decode_request(payload);
        if (!decoded.ok) {
            fail(connection_id, 0, "protocol_error", decoded.error, true);
            return;
        }
        const std::uint64_t correlation_id = decoded.id;
        if (auto *request = std::get_if<ipc::HelloRequest>(&decoded.body)) {
            (void)request;
            respond(connection_id, correlation_id, identity());
            return;
        }
        if (auto *request = std::get_if<ipc::SubmitTaskRequest>(&decoded.body)) {
            handle_submit(connection_id, correlation_id, std::move(*request));
            return;
        }
        if (auto *request = std::get_if<ipc::ListTasksRequest>(&decoded.body)) {
            (void)request;
            handle_list(connection_id, correlation_id);
            return;
        }
        if (auto *request = std::get_if<ipc::InspectTaskRequest>(&decoded.body)) {
            handle_inspect(connection_id, correlation_id, std::move(request->task_id));
            return;
        }
        if (auto *request = std::get_if<ipc::CancelTaskRequest>(&decoded.body)) {
            handle_cancel(connection_id, correlation_id, std::move(request->task_id));
            return;
        }
        if (auto *request = std::get_if<ipc::ShutdownRequest>(&decoded.body)) {
            (void)request;
            respond(connection_id, correlation_id, ipc::ShutdownAccepted{});
            // The response above is queued; the loop flushes it on its
            // ordered exit (DEC-007 service.shutdown semantics).
            if (loop) {
                loop->stop_serving();
            }
            return;
        }
        if (auto *request = std::get_if<ipc::SubscribeEventsRequest>(&decoded.body)) {
            (void)request;
            handle_subscribe(connection_id, correlation_id);
            return;
        }
        if (auto *request = std::get_if<ipc::UnsubscribeEventsRequest>(&decoded.body)) {
            (void)request;
            handle_unsubscribe(connection_id, correlation_id);
            return;
        }
        fail(connection_id, correlation_id, "unsupported", "unknown request");
    }

    std::chrono::milliseconds
    effective_step_timeout(const std::optional<std::chrono::milliseconds> &requested) const {
        const std::chrono::milliseconds raw = requested.value_or(config.step_timeout);
        return std::clamp<std::chrono::milliseconds>(raw, std::chrono::milliseconds{1},
                                                     config.step_timeout);
    }

    void handle_submit(std::uint64_t connection_id, std::uint64_t correlation_id,
                       ipc::SubmitTaskRequest request) {
        if (request.goal.empty()) {
            fail(connection_id, correlation_id, "invalid_argument",
                 "task.submit requires a non-empty 'goal'");
            return;
        }
        if (request.goal.size() > kMaxGoalBytes) {
            fail(connection_id, correlation_id, "invalid_argument",
                 "goal exceeds " + std::to_string(kMaxGoalBytes) + " bytes");
            return;
        }
        if (request.steps.size() > core->max_steps_per_task) {
            fail(connection_id, correlation_id, "invalid_argument",
                 "task declares more than " + std::to_string(core->max_steps_per_task) + " steps");
            return;
        }
        for (const auto &step : request.steps) {
            if (step.argument.size() > kMaxArgumentBytes) {
                fail(connection_id, correlation_id, "invalid_argument",
                     "step argument exceeds " + std::to_string(kMaxArgumentBytes) + " bytes");
                return;
            }
        }
        const std::chrono::milliseconds step_timeout = effective_step_timeout(request.step_timeout);

        // On the serial thread: direct host call is the owner discipline.
        const TaskSubmissionResult submission = core->host.submit_task(request.goal);
        if (!submission.ok) {
            fail(connection_id, correlation_id, submission.error.code, submission.error.message);
            return;
        }
        {
            std::lock_guard lock(core->registry.mutex);
            if (core->registry.full()) {
                // Explicit bounded-registry rejection; the just-admitted
                // pinned task is rolled back instead of leaking (RULE-07).
                core->host.cancel_task(submission.task);
                fail(connection_id, correlation_id, "invalid_state",
                     "task registry at capacity (" + std::to_string(core->registry.capacity) + ")");
                return;
            }
            detail::TaskRecord record;
            record.id = submission.task.id;
            record.goal = request.goal;
            record.step_timeout = step_timeout;
            record.steps.reserve(request.steps.size());
            for (const auto &step : request.steps) {
                detail::StepRecord entry;
                entry.spec = step;
                record.steps.push_back(std::move(entry));
            }
            core->registry.tasks.emplace(record.id, std::move(record));
        }
        auto driver_submission = core->executor.submit_cancellable(
            [core = core, id = submission.task.id](executor::StopToken stop) {
                detail::run_driver(stop, core, id);
            });
        {
            std::lock_guard lock(core->drivers_mutex);
            core->drivers.emplace(submission.task.id, std::move(driver_submission));
        }
        // Creation event (DEC-012 decision 3); published before the
        // acknowledgement so a subscriber never observes the ack for a task
        // whose created event is still queued behind serial work.
        detail::publish_task_updated(core, submission.task.id);
        respond(connection_id, correlation_id, ipc::TaskSubmitted{submission.task.id});
    }

    void handle_list(std::uint64_t connection_id, std::uint64_t correlation_id) {
        // Copy the fields the summary needs while holding the registry lock:
        // the blocking task_view call below must run outside the lock, and a
        // bare TaskRecord pointer would race mark_driver_done's
        // final_progress write once the lock is dropped (TSAN, CI run
        // 35504750823).
        struct ListedTask {
            std::string id;
            std::string goal;
            std::string final_progress;
        };
        std::vector<ListedTask> tasks;
        {
            std::lock_guard lock(core->registry.mutex);
            tasks.reserve(core->registry.tasks.size());
            for (const auto &entry : core->registry.tasks) {
                tasks.push_back({entry.first, entry.second.goal, entry.second.final_progress});
            }
        }
        ipc::TaskList list;
        list.tasks.reserve(tasks.size());
        for (auto &task : tasks) {
            ipc::TaskSummary summary;
            if (!task.final_progress.empty()) {
                // Settled (this run or hydrated from the M1-07 recovery
                // file): the recorded terminal name is the truth.
                summary.progress = task.final_progress;
            } else {
                const TaskViewResult view = core->host.task_view(TaskIdentity{task.id});
                summary.progress = view.ok ? progress_name(view.view.progress) : "Unknown";
            }
            summary.id = std::move(task.id);
            summary.goal = std::move(task.goal);
            list.tasks.push_back(std::move(summary));
        }
        respond(connection_id, correlation_id, std::move(list));
    }

    void handle_inspect(std::uint64_t connection_id, std::uint64_t correlation_id,
                        std::string task_id) {
        std::optional<detail::TaskRecord> snapshot;
        {
            std::lock_guard lock(core->registry.mutex);
            if (auto entry = core->registry.tasks.find(task_id);
                entry != core->registry.tasks.end()) {
                snapshot = entry->second;
            }
        }
        if (!snapshot) {
            fail(connection_id, correlation_id, "not_found", "unknown task id");
            return;
        }
        ipc::InspectTask inspect;
        inspect.id = snapshot->id;
        inspect.goal = snapshot->goal;
        if (!snapshot->final_progress.empty()) {
            // Settled (this run or hydrated from the M1-07 recovery file):
            // the recorded terminal name and outcome are the truth.
            inspect.progress = snapshot->final_progress;
            inspect.has_success = snapshot->has_success;
            inspect.success = snapshot->success;
        } else {
            const TaskViewResult view = core->host.task_view(TaskIdentity{snapshot->id});
            inspect.progress = view.ok ? progress_name(view.view.progress) : "Unknown";
            if (view.ok && terminal_progress(view.view.progress)) {
                inspect.has_success = true;
                inspect.success = view.view.success;
            }
        }
        inspect.steps.reserve(snapshot->steps.size());
        for (std::size_t index = 0; index < snapshot->steps.size(); ++index) {
            const detail::StepRecord &record = snapshot->steps[index];
            ipc::StepView step;
            step.index = static_cast<int>(index);
            step.kind = ipc::step_kind_name(record.spec.kind);
            step.status = record.status;
            step.operation_id = record.operation_id;
            step.permission = record.permission;
            step.ok = record.ok;
            step.exit_code = record.exit_code;
            step.result = record.result;
            step.result_truncated = record.result_truncated;
            step.error = record.error;
            inspect.steps.push_back(std::move(step));
        }
        respond(connection_id, correlation_id, std::move(inspect));
    }

    /// Serial thread: cooperative task cancellation (M1-05 cancellation
    /// path). The order matters: the desktop cancel token goes first so an
    /// in-flight provider action ends at its next observation point, then
    /// the driver's executor stop token stops between-step progress, and
    /// the pinned cancel settles the task — the pinned state stays
    /// authoritative and a terminal task is never revived.
    void handle_cancel(std::uint64_t connection_id, std::uint64_t correlation_id,
                       std::string task_id) {
        bool known = false;
        bool from_recovery = false;
        {
            std::lock_guard lock(core->registry.mutex);
            auto entry = core->registry.tasks.find(task_id);
            known = entry != core->registry.tasks.end();
            if (known) {
                from_recovery = entry->second.from_recovery;
                if (!from_recovery) {
                    entry->second.cancel.request_cancel();
                }
            }
        }
        if (!known) {
            fail(connection_id, correlation_id, "not_found", "unknown task id");
            return;
        }
        if (from_recovery) {
            // The task settled in an earlier service era (M1-07); this
            // service's pinned instance has no counterpart to cancel, and
            // a terminal task is never revived.
            fail(connection_id, correlation_id, "invalid_state",
                 "task belongs to a previous service run and is already "
                 "settled");
            return;
        }
        {
            std::lock_guard lock(core->drivers_mutex);
            if (auto driver = core->drivers.find(task_id);
                driver != core->drivers.end() && driver->second.handle.valid()) {
                core->executor.request_task_cancel(driver->second.handle);
            }
        }
        const HostOutcome cancelled = core->host.cancel_task(TaskIdentity{task_id});
        if (!cancelled.ok) {
            fail(connection_id, correlation_id, cancelled.error.code, cancelled.error.message);
            return;
        }
        ipc::TaskCancelled acknowledgement;
        acknowledgement.task_id = task_id;
        const TaskViewResult view = core->host.task_view(TaskIdentity{task_id});
        acknowledgement.progress = view.ok ? progress_name(view.view.progress) : "Unknown";
        // Cancellation admission is a progress advance (DEC-012 decision 3):
        // subscribers see "Cancelling" (or the terminal state the pinned
        // cancel already settled under) before the ack leaves.
        detail::publish_task_updated(core, task_id);
        respond(connection_id, correlation_id, std::move(acknowledgement));
    }

    /// Serial thread: attach the connection to the service event stream
    /// (DEC-012). The bounded per-connection queue lives in the returned
    /// subscription; the loop drains it after the responses of each pass.
    /// The current host status seeds the stream (seq 1) so a fresh
    /// subscriber starts from the live state, mirroring the frontend mock.
    void handle_subscribe(std::uint64_t connection_id, std::uint64_t correlation_id) {
        if (loop == nullptr) {
            fail(connection_id, correlation_id, "internal", "service loop is not running");
            return;
        }
        auto subscription = core->events.subscribe(config.event_queue_capacity);
        std::optional<ipc::EventPayload> seed;
        if (auto status = core->events.current_host_status()) {
            seed = ipc::EventPayload{std::move(*status)};
        }
        respond(connection_id, correlation_id, ipc::ShutdownAccepted{});
        loop->post_attach_events(connection_id, std::move(subscription), std::move(seed));
    }

    /// Serial thread: drop the connection's subscription; idempotent.
    void handle_unsubscribe(std::uint64_t connection_id, std::uint64_t correlation_id) {
        if (loop == nullptr) {
            fail(connection_id, correlation_id, "internal", "service loop is not running");
            return;
        }
        loop->post_detach_events(connection_id);
        respond(connection_id, correlation_id, ipc::ShutdownAccepted{});
    }

    // --- lifecycle ---------------------------------------------------------

    void request_loop_stop() {
        if (lifecycle.load(std::memory_order_acquire) != Lifecycle::Running) {
            return;
        }
        // Within the Running lifecycle the loop object is alive (it dies
        // only during teardown, which runs after the loop exited).
        if (loop != nullptr) {
            loop->stop_serving();
        } else if (loop_worker.started()) {
            loop_worker.request_stop();
        }
    }

    void teardown() {
        // Ordered shutdown (AGENTS.md rule 7): producers are already stopped
        // (loop exited, listener closed by run()). Recover the blocking
        // worker, cancel drivers and tasks, drain the executor, then release
        // the hosted pinned runtime on this (non-worker) thread.
        if (loop_worker.started()) {
            loop_worker.stop();
        }
        loop = nullptr;
        std::vector<executor::TaskHandle> handles;
        std::vector<std::future<void>> driver_futures;
        {
            std::lock_guard lock(core->drivers_mutex);
            handles.reserve(core->drivers.size());
            driver_futures.reserve(core->drivers.size());
            for (auto &[id, submission] : core->drivers) {
                handles.push_back(submission.handle);
                driver_futures.push_back(std::move(submission.future));
            }
            core->drivers.clear();
        }
        for (const auto &handle : handles) {
            core->executor.request_task_cancel(handle);
        }
        std::vector<std::string> ids;
        {
            std::lock_guard lock(core->registry.mutex);
            ids = core->registry.ids();
        }
        for (const auto &id : ids) {
            // End in-flight desktop actions promptly: the desktop cancel
            // tokens break provider calls out of their budgets, the executor
            // stop tokens stop the drivers between steps.
            {
                std::lock_guard lock(core->registry.mutex);
                if (auto entry = core->registry.tasks.find(id);
                    entry != core->registry.tasks.end()) {
                    entry->second.cancel.request_cancel();
                }
            }
            // By-value id capture: a timed-out wait abandons the future and
            // the closure may run after this loop iteration ended.
            auto cancelled = core->executor.submit_on(core->serial, [core = core, id] {
                return core->host.cancel_task(TaskIdentity{id});
            });
            try {
                if (cancelled.valid() &&
                    cancelled.wait_for(core->command_wait) == std::future_status::ready) {
                    cancelled.get();
                }
            } catch (const std::exception &) {
                // Terminal or already cancelled tasks surface pinned
                // rejections here; the drain below settles the rest.
            }
        }
        core->executor.shutdown(true);
        for (auto &future : driver_futures) {
            try {
                if (future.valid()) {
                    future.get();
                }
            } catch (const std::exception &) {
            }
        }
        // M1-07: drivers settled (or gave up on) every task above; the
        // final snapshot captures the exact terminal state this era leaves
        // behind, including tasks cancelled by the teardown itself.
        if (config.persist_recovery_state) {
            core->recovery.persist(core->registry);
        }
        // Host status transitions after the loop is gone have no live
        // subscribers, but the hub's LatestMailbox stays truthful for the
        // record (and for any hub-observing diagnostics).
        publish_host_status(HostStatus::Stopping);
        const ShutdownResult host_shutdown = core->host.shutdown();
        publish_host_status(host_shutdown.ok && host_shutdown.report.clean ? HostStatus::Stopped
                                                                           : HostStatus::Failed);
        core->serial.shutdown();
        run_report.clean = host_shutdown.ok && host_shutdown.report.clean;
        if (!run_report.clean) {
            run_report.diagnostic =
                host_shutdown.ok ? host_shutdown.report.diagnostic : host_shutdown.error.message;
        }
        run_report.host_shutdown = host_shutdown.report;
        lifecycle.store(Lifecycle::Terminal, std::memory_order_release);
    }
};

RuntimeService::RuntimeService(ServiceConfig config)
    : impl_(std::make_unique<Impl>(std::move(config))) {}

RuntimeService::~RuntimeService() {
    if (impl_->lifecycle.load(std::memory_order_acquire) == Impl::Lifecycle::Running) {
        impl_->request_loop_stop();
        impl_->loop_done_future.wait();
        impl_->listener.close();
        impl_->teardown();
    }
}

const std::string &RuntimeService::socket_path() const { return impl_->socket_path; }

void RuntimeService::request_shutdown() { impl_->request_loop_stop(); }

void RuntimeService::register_shutdown_fd(int fd) { impl_->shutdown_fd = fd; }

ipc::ServiceIdentity RuntimeService::identity() const { return impl_->identity(); }

HostOutcome
RuntimeService::start(std::shared_ptr<mirage::integration::DesktopEnvironmentBinding> binding) {
    HostOutcome outcome;
    if (impl_->lifecycle.load(std::memory_order_acquire) != Impl::Lifecycle::New) {
        outcome.error = {"invalid_state", "service already started or terminal"};
        return outcome;
    }
    if (!binding) {
        outcome.error = {"invalid_argument", "binding is null"};
        impl_->lifecycle.store(Impl::Lifecycle::Terminal, std::memory_order_release);
        return outcome;
    }
    if (impl_->config.step_timeout <= std::chrono::milliseconds::zero() ||
        impl_->config.max_steps_per_task == 0 || impl_->config.max_task_records == 0 ||
        impl_->config.max_result_bytes == 0 || impl_->config.event_queue_capacity == 0) {
        outcome.error = {"invalid_argument", "service config limits are empty"};
        impl_->lifecycle.store(Impl::Lifecycle::Terminal, std::memory_order_release);
        return outcome;
    }

    // M1-07 recovery persistence: point the writer at the configured (or
    // default) state directory, then hydrate the terminal records of the
    // previous service era into the registry before any new task can be
    // admitted. A broken recovery file degrades loudly (writer records the
    // reason on stderr) instead of failing the start.
    if (impl_->config.persist_recovery_state) {
        const std::filesystem::path directory = impl_->config.recovery_directory.empty()
                                                    ? persistence::default_state_directory()
                                                    : impl_->config.recovery_directory;
        impl_->core->recovery.enable(
            persistence::LocalStateStore(directory, "task-recovery.json",
                                         persistence::kMaxRecoveryFileBytes),
            impl_->config.mirage_version, impl_->config.max_result_bytes);
        const std::size_t recovered =
            impl_->core->recovery.hydrate(impl_->core->registry, impl_->config.max_task_records);
        if (recovered > 0) {
            std::cerr << "mirage-service: recovered " << recovered << " task(s) from "
                      << (directory / "task-recovery.json").string() << '\n';
        }
    }

    impl_->core->environment = binding->bound_environment();
    if (!impl_->core->environment) {
        outcome.error = {"invalid_argument", "binding does not expose a mirage desktop "
                                             "environment; the M1 service cannot drive tasks"};
        impl_->lifecycle.store(Impl::Lifecycle::Terminal, std::memory_order_release);
        return outcome;
    }

    executor::ExecutorConfig executor_config;
    if (impl_->config.executor_threads > 0) {
        executor_config.max_threads = impl_->config.executor_threads;
    }
    const auto initialized = impl_->core->executor.initialize_ex(executor_config);
    if (!initialized.ok) {
        outcome.error = {"internal", "executor initialization failed: " + initialized.message};
        impl_->lifecycle.store(Impl::Lifecycle::Terminal, std::memory_order_release);
        return outcome;
    }

    impl_->publish_host_status(HostStatus::Starting);
    const HostOutcome hosted = impl_->core->host.start(binding);
    if (!hosted.ok) {
        impl_->publish_host_status(HostStatus::Failed);
        impl_->core->executor.shutdown(false);
        outcome.error = hosted.error;
        impl_->lifecycle.store(Impl::Lifecycle::Terminal, std::memory_order_release);
        return outcome;
    }
    impl_->publish_host_status(HostStatus::Running);

    std::string diagnostic;
    impl_->listener = ipc::IpcListener::bind(impl_->socket_path, diagnostic);
    if (!impl_->listener.valid()) {
        impl_->core->host.shutdown();
        impl_->publish_host_status(impl_->core->host.status());
        impl_->core->executor.shutdown(false);
        outcome.error = {"internal", std::move(diagnostic)};
        impl_->lifecycle.store(Impl::Lifecycle::Terminal, std::memory_order_release);
        return outcome;
    }

    detail::ServiceLoop::Dependencies dependencies;
    dependencies.listen_fd = impl_->listener.handle();
    dependencies.max_connections = impl_->config.max_connections;
    dependencies.on_frame = [raw = impl_.get()](std::uint64_t connection_id, std::string payload) {
        raw->handle_frame(connection_id, payload);
    };
    dependencies.on_exit = [raw = impl_.get()] { raw->loop_done.set_value(); };
    auto owned_loop = std::make_unique<detail::ServiceLoop>(std::move(dependencies));
    if (impl_->shutdown_fd >= 0) {
        owned_loop->register_shutdown_fd(impl_->shutdown_fd);
    }
    impl_->loop = owned_loop.get();

    executor::BlockingWorkerSpec spec;
    spec.name = "mirage-ipc-loop";
    spec.config.thread_name = "mirage-ipc-loop";
    // The executor's blocking worker owns the loop from here on; Impl keeps
    // only the observer pointer above.
    spec.worker = std::move(owned_loop);
    impl_->loop_worker = impl_->core->executor.start_worker(std::move(spec));
    if (!impl_->loop_worker.started()) {
        impl_->loop = nullptr;
        impl_->listener.close();
        impl_->core->host.shutdown();
        impl_->publish_host_status(impl_->core->host.status());
        impl_->core->executor.shutdown(false);
        outcome.error = {"internal", "blocking worker start failed: " +
                                         impl_->loop_worker.start_result().message};
        impl_->lifecycle.store(Impl::Lifecycle::Terminal, std::memory_order_release);
        return outcome;
    }
    impl_->lifecycle.store(Impl::Lifecycle::Running, std::memory_order_release);
    outcome.ok = true;
    return outcome;
}

ServiceRunReport RuntimeService::run() {
    ServiceRunReport report;
    if (impl_->lifecycle.load(std::memory_order_acquire) != Impl::Lifecycle::Running) {
        report.diagnostic = "service is not running";
        return report;
    }
    // Blocks until the loop exits (shutdown requested via IPC, signal fd,
    // request_shutdown or executor stop), then tears down on this thread.
    impl_->loop_done_future.get();
    impl_->listener.close();
    impl_->teardown();
    return impl_->run_report;
}

} // namespace mirage::runtime
