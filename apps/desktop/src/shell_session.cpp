#include "shell_session.hpp"

namespace mirage::desktop_shell {
namespace {

/// The Executor blocking worker adapter around the session's run loop. The
/// loop's own stop discipline is cooperative (a readiness slice), so
/// wakeup() only flips the client's stop flag and the loop returns within
/// one slice — the bounded external wait the blocking-I/O card requires.
class LoopWorker final : public executor::IBlockingIoWorker {
  public:
    LoopWorker(std::shared_ptr<ipc::SessionClient> session,
               std::function<void(const std::string &)> on_exit)
        : session_(std::move(session)), on_exit_(std::move(on_exit)) {}

    void run(executor::StopToken /*stop_token*/) override {
        std::string diagnostic;
        (void)session_->run(diagnostic); // the exit kind is folded into the diagnostic
        on_exit_(diagnostic);
    }

    void wakeup() noexcept override { session_->stop(); }

  private:
    std::shared_ptr<ipc::SessionClient> session_;
    std::function<void(const std::string &)> on_exit_;
};

} // namespace

ShellSession::ShellSession(executor::Executor &executor, std::string address, EventFn on_event,
                           LostFn on_lost)
    : executor_(executor), address_(std::move(address)), on_event_(std::move(on_event)),
      on_lost_(std::move(on_lost)) {}

ShellSession::~ShellSession() { shutdown(); }

void ShellSession::set_forwarders(EventFn on_event, LostFn on_lost) {
    std::lock_guard<std::mutex> guard(mutex_);
    on_event_ = std::move(on_event);
    on_lost_ = std::move(on_lost);
}

bool ShellSession::ensure_connected() {
    std::lock_guard<std::mutex> guard(mutex_);
    if (stopping_) {
        return false;
    }
    if (session_ && worker_.started() && session_->connected()) {
        return true;
    }
    tear_down_locked();

    auto next = std::make_shared<ipc::SessionClient>(address_);
    std::string diagnostic;
    if (!next->connect(kConnectDeadline, diagnostic)) {
        return false;
    }
    session_ = next;
    // Events arrive on the run-loop thread; forward them outside the lock
    // (the forwarder contract: post-only, never reenters this class).
    next->set_event_sink([this](const ipc::Event &event) {
        EventFn forward;
        {
            std::lock_guard<std::mutex> guard(mutex_);
            forward = on_event_;
        }
        if (forward) {
            forward(ipc::encode_event(event));
        }
    });
    executor::BlockingWorkerSpec spec;
    spec.name = "mirage-shell-ipc";
    spec.config.thread_name = "mirage-shell-ipc";
    spec.worker = std::make_unique<LoopWorker>(
        session_, [this](const std::string &reason) { handle_loop_exit(reason); });
    worker_ = executor_.start_worker(std::move(spec));
    if (!worker_.started()) {
        // Admission is not execution (the Executor integration discipline):
        // a failed worker start is surfaced here, not silently retried.
        session_.reset();
        return false;
    }
    return true;
}

bool ShellSession::connected() const {
    std::lock_guard<std::mutex> guard(mutex_);
    return session_ && !stopping_ && session_->connected();
}

std::future<ipc::Response> ShellSession::call(const ipc::Request &body,
                                              std::chrono::milliseconds timeout) {
    if (!ensure_connected()) {
        std::promise<ipc::Response> refused;
        std::future<ipc::Response> future = refused.get_future();
        ipc::Response failed;
        failed.ok = false;
        failed.error = {"unavailable",
                        "no live session at '" + address_ + "' (service unreachable)"};
        refused.set_value(std::move(failed));
        return future;
    }
    // The shared pointer keeps the client alive even if a concurrent
    // reconnect replaces the member; a dead session resolves the future
    // with "unavailable" (the SessionClient contract).
    std::shared_ptr<ipc::SessionClient> session;
    {
        std::lock_guard<std::mutex> guard(mutex_);
        session = session_;
    }
    return session->call(body, timeout);
}

void ShellSession::shutdown() {
    std::lock_guard<std::mutex> guard(mutex_);
    stopping_ = true;
    tear_down_locked();
}

void ShellSession::tear_down_locked() {
    if (worker_.started()) {
        // Requests stop, wakes the run loop and joins it; the worker owns a
        // shared reference to the session, so the join is use-after-free safe.
        worker_.stop();
    }
    worker_ = executor::WorkerHandle{};
    session_.reset();
}

void ShellSession::handle_loop_exit(const std::string &diagnostic) {
    std::lock_guard<std::mutex> guard(mutex_);
    if (stopping_) {
        return; // deliberate shutdown is not a loss to report
    }
    if (on_lost_) {
        on_lost_(diagnostic); // forwarder contract: posts, never reenters
    }
}

} // namespace mirage::desktop_shell
