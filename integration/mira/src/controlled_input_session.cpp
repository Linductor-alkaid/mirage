#include "controlled_input_session.hpp"

#include <algorithm>
#include <condition_variable>
#include <future>
#include <kairo/comm/channel.hpp>
#include <kairo/comm/mailbox.hpp>
#include <kairo/executor.hpp>
#include <mira/core_contracts.hpp>
#include <mirage/conversation.hpp>
#include <mutex>
#include <stdexcept>
#include <utility>

namespace mirage::integration::detail {
namespace {
using namespace desktop;
using Clock = std::chrono::steady_clock;
struct Request {
    enum class Kind { command, observe, release } kind = Kind::observe;
    std::uint64_t sequence = 0, version = 0;
    InputSessionCommand command;
    CancelToken cancel;
    std::promise<InputSessionReceipt> done;
};
struct Projection {
    InputSessionReceipt receipt;
    Clock::time_point expires{};
    std::uint64_t epoch = 0;
};
struct Core {
    kairo::comm::MpscChannel<std::shared_ptr<Request>> requests{kairo::comm::ChannelOptions{
        8, kairo::comm::DropPolicy::RejectNewest, true, "controlled-input-commands"}};
    kairo::comm::LatestMailbox<Projection> projection{"controlled-input"};
    // Only a wait adapter; payload/state communication is owned by comm above.
    std::mutex wait_mutex;
    std::condition_variable wake;
    std::shared_ptr<ActionCoordinator> actions;
    ActionCoordinator::Lease lease;
    CancelToken cancel;
    kairo::StopToken worker_stop;
    ControlledInputSession::Factory factory;
    ControlledInputSession::Limits limits;
    std::promise<InputSessionReceipt> ready;
    InputSessionReceipt state;
    std::uint64_t authority = 0;
    Clock::time_point began{}, last_command{};
    std::unique_ptr<InputSessionBackend> backend;
    bool terminal() const {
        return state.state == InputSessionState::released ||
               state.state == InputSessionState::fenced;
    }
    void step(InputSessionEvent event) {
        const auto next = advance_input_session(state.state, event);
        if (!next)
            throw std::logic_error("invalid controlled input lifecycle transition");
        state.state = *next;
    }
    void publish() {
        conversation::truncate_text(state.error.code, 64);
        conversation::truncate_text(state.error.message, 160);
        ++state.state_version;
        state.commands_remaining = input_session_budget::max_session_commands -
                                   static_cast<std::uint32_t>(state.command_sequence);
        const auto now = Clock::now();
        state.idle_budget_ms_remaining =
            terminal()
                ? 0
                : std::max<std::int64_t>(
                      0, std::chrono::duration_cast<std::chrono::milliseconds>(
                             std::min(last_command + limits.idle, began + limits.total) - now)
                             .count());
        projection.publish(Projection{state,
                                      std::min(last_command + limits.idle, began + limits.total),
                                      actions ? actions->epoch() : 0});
    }
    void close(ProviderError reason = {}, bool cancelled = false) {
        if (terminal())
            return;
        step(InputSessionEvent::release_requested);
        state.cancelled |= cancelled;
        if (!reason.code.empty())
            state.error = std::move(reason);
        InputOutcome released;
        released.ok = released.input_released = true;
        try {
            if (backend)
                released = backend->release();
        } catch (...) {
            released.ok = released.input_released = false;
        }
        state.side_effect_may_have_occurred |= released.side_effect_may_have_occurred;
        state.release_confirmed = released.ok && released.input_released;
        step(state.release_confirmed ? InputSessionEvent::release_confirmed
                                     : InputSessionEvent::release_failed);
        state.outcome = state.release_confirmed ? InputSessionOutcome::released
                                                : InputSessionOutcome::uncertain;
        if (state.release_confirmed) {
            state.held_keys.clear();
            state.held_buttons.clear();
        }
        if (!state.release_confirmed) {
            state.error = {"input_release_failed", "Owned input release could not be confirmed"};
            lease.release_failed();
        }
        backend.reset();
        factory = {}; // Do not retain a past turn's environment or task closure.
        lease = {};
        requests.close();
        publish();
    }
    void fail(ProviderError error, bool cancelled = false) {
        const bool takeover =
            error.code == "physical_takeover" || error.code == "physical_input_unknown" ||
            error.code == "input_state_changed" ||
            (error.code == "physical_input_busy" && state.side_effect_may_have_occurred);
        cancelled |= error.code == "cancelled";
        if (takeover)
            actions->takeover();
        close(std::move(error), cancelled || takeover);
    }
    bool guard() {
        if (terminal())
            return false;
        const auto now = Clock::now();
        if (worker_stop.stop_requested() || cancel.cancelled() ||
            actions->authority_epoch() != authority) {
            close({"cancelled", "Input session cancelled or authority changed"}, true);
            return false;
        }
        if (now >= began + limits.total || now >= last_command + limits.idle) {
            close({"input_session_timeout", "Input session lifetime or idle budget expired"});
            return false;
        }
        if (const auto error = backend->check(); !error.code.empty()) {
            fail(error);
            return false;
        }
        return true;
    }
    void execute(Request &request) {
        if (request.kind == Request::Kind::release)
            close();
        else if (request.kind == Request::Kind::command && request.cancel.cancelled())
            close({"cancelled", "Queued input command cancelled before dispatch"}, true);
        else if (guard()) {
            if (request.kind == Request::Kind::observe) {
                if (state.state == InputSessionState::ready)
                    step(InputSessionEvent::admit);
                step(state.state == InputSessionState::awaiting_feedback
                         ? InputSessionEvent::observe_repeat
                         : InputSessionEvent::observe);
                publish(); // Observation never renews the idle deadline.
            } else {
                if (request.sequence != state.command_sequence + 1 ||
                    request.version != state.state_version) {
                    auto rejected = state;
                    rejected.error = {"stale_input_command",
                                      "Use the latest session version and next sequence"};
                    request.done.set_value(std::move(rejected));
                    return;
                }
                const auto &c = request.command;
                const bool key = c.kind == InputSessionCommand::Kind::key_down ||
                                 c.kind == InputSessionCommand::Kind::key_up ||
                                 c.kind == InputSessionCommand::Kind::key_tap;
                auto &held = key ? state.held_keys : state.held_buttons;
                const auto &name = key ? c.key_name : c.button;
                const auto found = std::find(held.begin(), held.end(), name);
                const bool down = c.kind == InputSessionCommand::Kind::key_down ||
                                  c.kind == InputSessionCommand::Kind::button_down;
                const bool tap = c.kind == InputSessionCommand::Kind::key_tap ||
                                 c.kind == InputSessionCommand::Kind::button_tap;
                const bool up = c.kind == InputSessionCommand::Kind::key_up ||
                                c.kind == InputSessionCommand::Kind::button_up;
                InputSessionCommandContext context{
                    backend->capabilities(),
                    static_cast<std::uint32_t>(state.held_keys.size() + state.held_buttons.size()),
                    static_cast<std::uint32_t>(state.command_sequence)};
                auto error = validate_input_session_command(c, context);
                if (!error && (down || tap) &&
                    context.held_inputs >= input_session_budget::max_held_inputs)
                    error = ProviderError{"result_too_large", "Held input budget exhausted"};
                if (!error && tap && c.dwell.count() > 0 &&
                    Clock::now() + c.dwell >=
                        std::min(last_command + limits.idle, began + limits.total))
                    error = ProviderError{"input_session_timeout",
                                          "Planned press exceeds the remaining input budget"};
                if (!error &&
                    (((down || tap) && found != held.end()) || (up && found == held.end())))
                    error = ProviderError{"invalid_input_transition",
                                          "Input is already held or was not owned"};
                if (error)
                    close(*error);
                else {
                    if (state.state == InputSessionState::ready)
                        step(InputSessionEvent::admit);
                    step(InputSessionEvent::command_admitted);
                    ++state.command_sequence; // Never replay a possibly delivered command.
                    const CancelToken bounded([this, &request] {
                        return worker_stop.stop_requested() || request.cancel.cancelled() ||
                               cancel.cancelled() || actions->authority_epoch() != authority ||
                               Clock::now() >= began + limits.total ||
                               Clock::now() >= last_command + limits.idle;
                    });
                    // Conservatively project possible ownership before dispatch, including
                    // an exception or ambiguous delivery. Confirmed cleanup clears it.
                    if (down || tap)
                        held.push_back(name);
                    const auto delivered = backend->apply(c, bounded);
                    state.side_effect_may_have_occurred |= delivered.side_effect_may_have_occurred;
                    if (!delivered.ok)
                        fail(delivered.error, delivered.cancelled);
                    else {
                        if (up)
                            held.erase(found);
                        if (tap)
                            std::erase(held, name);
                        step(InputSessionEvent::command_settled);
                        last_command = Clock::now();
                        state.outcome = state.held_keys.empty() && state.held_buttons.empty()
                                            ? InputSessionOutcome::delivered
                                            : InputSessionOutcome::holding;
                        publish();
                    }
                }
            }
        }
        request.done.set_value(state);
    }
};
using Binding = kairo::comm::LatestMailbox<std::shared_ptr<Core>>;
class Worker final : public kairo::IBlockingIoWorker {
  public:
    explicit Worker(std::shared_ptr<Binding> binding) : binding_(std::move(binding)) {}
    void run(kairo::StopToken stop) override {
        std::shared_ptr<Core> core;
        if (!binding_->try_load(core) || !core)
            return;
        auto &c = *core;
        c.worker_stop = stop;
        bool ready = false;
        try {
            c.began = c.last_command = Clock::now();
            c.backend = c.factory();
            if (!c.backend ||
                !c.backend->capabilities().has(InputCapability::cross_observation_hold) ||
                !c.backend->capabilities().has(InputCapability::hold_capture))
                c.close({"unsupported_platform",
                         "Controlled input and capture are not declared by this backend"});
            else if (c.guard())
                c.publish();
            c.ready.set_value(c.state);
            ready = true;
            while (!c.terminal() && !stop.stop_requested()) {
                std::shared_ptr<Request> request;
                if (c.requests.try_receive(request)) {
                    try {
                        c.execute(*request);
                    } catch (...) {
                        c.close({"input_session_failed", "Controlled input command failed"});
                        request->done.set_value(c.state);
                    }
                } else {
                    if (!c.guard())
                        break;
                    std::unique_lock lock(c.wait_mutex);
                    c.wake.wait_for(lock, std::chrono::milliseconds{4}, [&] {
                        return stop.stop_requested() || !c.requests.empty() ||
                               c.requests.is_closed();
                    });
                }
            }
            c.close({"cancelled", "Input session stopped"}, true);
        } catch (...) {
            c.close({"input_session_failed", "Controlled input worker failed"});
            if (!ready)
                c.ready.set_value(c.state);
        }
        std::shared_ptr<Request> request;
        while (!c.requests.is_drained()) {
            if (c.requests.try_receive(request))
                request->done.set_value(c.state);
        }
    }
    void wakeup() noexcept override {
        std::shared_ptr<Core> core;
        if (binding_->try_load(core) && core)
            core->wake.notify_all();
    }

  private:
    std::shared_ptr<Binding> binding_;
};
} // namespace

struct ControlledInputWorker::Impl {
    kairo::Executor &executor;
    std::shared_ptr<Binding> binding = std::make_shared<Binding>("controlled-input-binding");
    kairo::WorkerHandle handle;
    std::string name;
    bool registered = false;
};
ControlledInputWorker::ControlledInputWorker(kairo::Executor &executor)
    : impl_(std::make_unique<Impl>(
          Impl{executor, std::make_shared<Binding>("controlled-input-binding"), {}, {}, false})) {}
ControlledInputWorker::~ControlledInputWorker() {
    stop();
    impl_->binding->publish(std::shared_ptr<Core>{});
}
void ControlledInputWorker::stop() {
    if (impl_->registered)
        impl_->handle.stop();
}

struct ControlledInputSession::Impl {
    kairo::Executor &executor;
    std::shared_ptr<Core> core;
    std::shared_ptr<ControlledInputWorker> worker;
    bool begun = false;
    InputSessionReceipt request(std::shared_ptr<Request> message) {
        auto future = message->done.get_future();
        if (!begun || !core->requests.try_send(message)) {
            auto receipt = snapshot();
            if (receipt.state != InputSessionState::released &&
                receipt.state != InputSessionState::fenced)
                receipt.error = {"input_session_unavailable", "Input command queue is unavailable"};
            message->done.set_value(receipt);
            return future.get();
        }
        core->wake.notify_one();
        return future.get();
    }
    InputSessionReceipt snapshot() const {
        Projection result;
        core->projection.try_load(result);
        if (result.receipt.state != InputSessionState::released &&
            result.receipt.state != InputSessionState::fenced)
            result.receipt.idle_budget_ms_remaining = std::max<std::int64_t>(
                0,
                std::chrono::duration_cast<std::chrono::milliseconds>(result.expires - Clock::now())
                    .count());
        return result.receipt;
    }
};
ControlledInputSession::ControlledInputSession(kairo::Executor &executor,
                                               std::shared_ptr<ActionCoordinator> actions,
                                               CancelToken cancel, Factory factory, Limits limits,
                                               std::shared_ptr<ControlledInputWorker> worker)
    : impl_(std::make_unique<Impl>(Impl{
          executor, std::make_shared<Core>(),
          worker ? std::move(worker) : std::make_shared<ControlledInputWorker>(executor), false})) {
    impl_->core->actions = std::move(actions);
    impl_->core->cancel = std::move(cancel);
    impl_->core->factory = std::move(factory);
    impl_->core->limits = std::move(limits);
}
ControlledInputSession::~ControlledInputSession() { stop(); }
InputSessionReceipt ControlledInputSession::begin(std::uint64_t epoch) {
    auto &i = *impl_;
    if (i.begun)
        return i.snapshot();
    i.begun = true;
    auto &c = *i.core;
    const auto reject = [&](ProviderError error) {
        c.state.error = std::move(error);
        c.close(c.state.error);
        return c.state;
    };
    if (!epoch || !c.actions || !c.factory || c.limits.idle.count() <= 0 ||
        c.limits.total.count() <= 0 || c.limits.idle > input_session_budget::max_idle ||
        c.limits.total > input_session_budget::max_session_total)
        return reject({"invalid_argument", "Input session owner or budgets are invalid"});
    c.lease = c.actions->try_begin(c.cancel, epoch);
    if (!c.lease)
        return reject(c.lease.error());
    c.authority = c.actions->authority_epoch();
    auto &worker = *i.worker->impl_;
    if (&worker.executor != &i.executor)
        return reject({"invalid_argument", "Input worker belongs to another Executor"});
    std::shared_ptr<Core> previous;
    if (worker.binding->try_load(previous) && previous) {
        Projection projection;
        if (previous->projection.try_load(projection) &&
            projection.receipt.state != InputSessionState::released &&
            projection.receipt.state != InputSessionState::fenced)
            return reject({"desktop_busy", "Input worker belongs to another active session"});
    }
    if (worker.registered)
        worker.handle.stop();
    worker.binding->publish(i.core);
    bool started = false;
    if (worker.registered) {
        started = i.executor.start_blocking_io_worker(worker.name).ok;
    } else {
        kairo::BlockingWorkerSpec spec;
        spec.name = c.limits.worker_name.empty()
                        ? "controlled-input-" + mira::OperationId::generate().to_string()
                        : c.limits.worker_name;
        worker.name = spec.name;
        spec.config.thread_name = "controlled-input";
        spec.worker = std::make_unique<Worker>(worker.binding);
        worker.handle = i.executor.start_worker(std::move(spec));
        started = worker.handle.started();
        worker.registered = started;
    }
    if (!started)
        return reject({"input_session_rejected", "Executor rejected input worker"});
    return c.ready.get_future().get();
}
InputSessionReceipt ControlledInputSession::advance(std::uint64_t sequence, std::uint64_t version,
                                                    InputSessionCommand command,
                                                    CancelToken cancel) {
    auto request = std::make_shared<Request>();
    request->kind = Request::Kind::command;
    request->sequence = sequence;
    request->version = version;
    request->command = std::move(command);
    request->cancel = std::move(cancel);
    return impl_->request(std::move(request));
}
InputSessionReceipt ControlledInputSession::observe() {
    return impl_->request(std::make_shared<Request>());
}
InputSessionReceipt ControlledInputSession::release() {
    auto request = std::make_shared<Request>();
    request->kind = Request::Kind::release;
    auto receipt = impl_->request(std::move(request));
    if (receipt.error.code == "input_session_unavailable") {
        stop();
        receipt = snapshot();
    }
    return receipt;
}
InputSessionReceipt ControlledInputSession::snapshot() const { return impl_->snapshot(); }
std::uint64_t ControlledInputSession::environment_epoch() const {
    Projection projection;
    impl_->core->projection.try_load(projection);
    return projection.epoch;
}
void ControlledInputSession::stop() {
    std::shared_ptr<Core> active;
    auto &worker = *impl_->worker->impl_;
    if (worker.registered && worker.binding->try_load(active) && active == impl_->core)
        worker.handle.stop();
}
} // namespace mirage::integration::detail
