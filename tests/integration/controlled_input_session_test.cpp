#include "../../integration/mira/src/controlled_input_session.hpp"
#include "../support/test.hpp"
#include <atomic>
#include <kairo/executor.hpp>
#include <stdexcept>
#include <thread>

namespace d = mirage::desktop;
using Session = mirage::integration::detail::ControlledInputSession;
namespace {
struct Facts {
    std::atomic<bool> physical_during{false};
    std::atomic<int> device_fault{0};
    std::atomic<int> presses{0}, releases{0}, delay_ms{0};
    std::atomic<bool> fail_release{false}, fail_check{false}, throw_apply{false}, down{false},
        physical{false};
};
class Backend final : public d::InputSessionBackend {
    std::shared_ptr<Facts> facts_;

  public:
    explicit Backend(std::shared_ptr<Facts> facts) : facts_(std::move(facts)) {}
    d::InputCapabilitySet capabilities() const override {
        return d::InputCapabilitySet{}
            .with(d::InputCapability::software_injection)
            .with(d::InputCapability::smooth_move)
            .with(d::InputCapability::cross_observation_hold)
            .with(d::InputCapability::timed_press)
            .with(d::InputCapability::independent_modifiers)
            .with(d::InputCapability::hold_capture);
    }
    d::ProviderError check() override {
        if (facts_->device_fault)
            return {facts_->device_fault == 1 ? "input_device_changed" : "input_device_lost",
                    "fixture device fault"};
        if (facts_->physical)
            return {"physical_takeover", "fixture physical input"};
        return facts_->fail_check.load()
                   ? d::ProviderError{"target_changed", "fixture target changed"}
                   : d::ProviderError{};
    }
    d::InputSessionDelivery apply(const d::InputSessionCommand &command,
                                  const d::CancelToken &cancel) override {
        ++facts_->presses;
        facts_->down = true;
        if (facts_->physical_during)
            return {false,
                    false,
                    true,
                    {"physical_takeover", "fixture physical input during dispatch"}};
        if (facts_->throw_apply)
            throw std::runtime_error("injected platform failure");
        const auto end =
            std::chrono::steady_clock::now() + std::chrono::milliseconds{facts_->delay_ms.load()};
        while (std::chrono::steady_clock::now() < end) {
            if (cancel.cancelled())
                return {false, true, true, {"cancelled", "fixture command cancelled"}};
            std::this_thread::sleep_for(std::chrono::milliseconds{1});
        }
        if (command.kind == d::InputSessionCommand::Kind::key_up ||
            command.kind == d::InputSessionCommand::Kind::button_up ||
            command.kind == d::InputSessionCommand::Kind::key_tap ||
            command.kind == d::InputSessionCommand::Kind::button_tap) {
            ++facts_->releases;
            facts_->down = false;
        }
        return {true, false, true, {}};
    }
    d::InputOutcome release() override {
        if (facts_->down.exchange(false))
            ++facts_->releases;
        const bool ok = !facts_->fail_release;
        return {ok, false, {}, facts_->presses > 0, ok};
    }
};
d::InputSessionCommand down() {
    d::InputSessionCommand command;
    command.kind = d::InputSessionCommand::Kind::key_down;
    command.key_name = "shift";
    return command;
}
void delay(kairo::Executor &executor, int ms) {
    auto wait = executor.submit_delayed(ms, [] {});
    wait.future.get();
}
void wait_terminal(kairo::Executor &executor, Session &session) {
    for (int i = 0; i < 50 && !session.snapshot().release_confirmed &&
                    session.snapshot().state != d::InputSessionState::fenced;
         ++i)
        delay(executor, 5);
}
void normal(kairo::Executor &executor) {
    auto owner = std::make_shared<d::ActionCoordinator>();
    auto facts = std::make_shared<Facts>();
    Session session(executor, owner, {}, [facts] { return std::make_unique<Backend>(facts); }, {});
    const auto ready = session.begin(owner->epoch());
    MIRAGE_CHECK(ready.state == d::InputSessionState::ready);
    MIRAGE_CHECK(!owner->try_begin({}));
    const auto held = session.advance(1, ready.state_version, down());
    MIRAGE_CHECK(held.outcome == d::InputSessionOutcome::holding &&
                 held.held_keys == std::vector<std::string>{"shift"});
    MIRAGE_CHECK(!held.release_confirmed && facts->down);
    const auto stale = session.advance(1, ready.state_version, down());
    MIRAGE_CHECK(stale.error.code == "stale_input_command" && facts->presses == 1);
    const auto observed = session.observe();
    MIRAGE_CHECK(observed.state == d::InputSessionState::awaiting_feedback);
    MIRAGE_CHECK(observed.idle_budget_ms_remaining <= held.idle_budget_ms_remaining);
    auto up = down();
    up.kind = d::InputSessionCommand::Kind::key_up;
    const auto released_key = session.advance(2, observed.state_version, up);
    MIRAGE_CHECK(released_key.held_keys.empty() &&
                 released_key.outcome == d::InputSessionOutcome::delivered);
    const auto closed = session.release();
    MIRAGE_CHECK(closed.release_confirmed && closed.held_keys.empty() &&
                 closed.held_buttons.empty() && owner->input_released());
    MIRAGE_CHECK(session.release().release_confirmed);
    MIRAGE_CHECK(session.advance(3, closed.state_version, down()).release_confirmed);
    MIRAGE_CHECK(facts->presses == 2 && facts->releases == 1);
    session.stop();
}
void endings(kairo::Executor &executor) {
    for (int fault = 0; fault < 8; ++fault) {
        auto owner = std::make_shared<d::ActionCoordinator>();
        auto facts = std::make_shared<Facts>();
        d::CancelToken cancel;
        Session session(
            executor, owner, cancel, [facts] { return std::make_unique<Backend>(facts); },
            fault == 0
                ? Session::Limits{std::chrono::milliseconds{35}, std::chrono::milliseconds{500}}
                : Session::Limits{});
        const auto ready = session.begin(owner->epoch());
        const auto held = session.advance(1, ready.state_version, down());
        MIRAGE_CHECK(held.outcome == d::InputSessionOutcome::holding);
        if (fault == 0) {
            delay(executor, 15);
            const auto observed = session.observe();
            MIRAGE_CHECK(observed.idle_budget_ms_remaining < 30);
            delay(executor, 30);
        }
        if (fault == 1)
            cancel.request_cancel();
        if (fault == 2)
            owner->takeover();
        if (fault == 3)
            facts->fail_check = true;
        if (fault == 4)
            session.stop();
        if (fault == 5) {
            facts->fail_release = true;
            cancel.request_cancel();
        }
        if (fault == 6)
            owner->invalidate_authority();
        if (fault == 7)
            facts->physical = true;
        wait_terminal(executor, session);
        const auto result = session.snapshot();
        MIRAGE_CHECK(!facts->down && facts->releases == 1);
        MIRAGE_CHECK(result.release_confirmed == (fault != 5));
        if (fault == 5) {
            MIRAGE_CHECK(result.state == d::InputSessionState::fenced);
            MIRAGE_CHECK(!owner->resume());
            MIRAGE_CHECK(!owner->try_begin({}));
        } else
            MIRAGE_CHECK(result.state == d::InputSessionState::released);
        if (fault == 7) {
            MIRAGE_CHECK(result.cancelled && !owner->try_begin({}));
            MIRAGE_CHECK(owner->resume());
        }
        session.stop();
    }
}
void faults(kairo::Executor &executor) {
    for (int fault = 0; fault < 5; ++fault) {
        auto owner = std::make_shared<d::ActionCoordinator>();
        auto facts = std::make_shared<Facts>();
        facts->throw_apply = fault == 0;
        Session session(executor, owner, {},
                        [facts, fault]() -> std::unique_ptr<d::InputSessionBackend> {
                            if (fault == 1)
                                throw std::runtime_error("factory");
                            if (fault == 2)
                                return {};
                            return std::make_unique<Backend>(facts);
                        },
                        {});
        if (fault == 4)
            owner->takeover();
        const auto ready = session.begin(fault == 3 ? owner->epoch() + 1 : owner->epoch());
        if (fault == 0) {
            const auto result = session.advance(1, ready.state_version, down());
            MIRAGE_CHECK(result.release_confirmed && !facts->down && facts->releases == 1);
        } else
            MIRAGE_CHECK(ready.release_confirmed && !ready.error.code.empty() &&
                         facts->presses == 0);
        session.stop();
    }
    Session::Limits limits;
    limits.worker_name = "controlled-input-admission-fixture";
    auto first_owner = std::make_shared<d::ActionCoordinator>();
    auto first_facts = std::make_shared<Facts>();
    Session first(
        executor, first_owner, {}, [first_facts] { return std::make_unique<Backend>(first_facts); },
        limits);
    MIRAGE_CHECK(first.begin(first_owner->epoch()).state == d::InputSessionState::ready);
    auto owner = std::make_shared<d::ActionCoordinator>();
    auto facts = std::make_shared<Facts>();
    Session rejected(
        executor, owner, {}, [facts] { return std::make_unique<Backend>(facts); }, limits);
    MIRAGE_CHECK(rejected.begin(owner->epoch()).error.code == "input_session_rejected");
    MIRAGE_CHECK(owner->input_released() && facts->presses == 0);
    first.stop();
}
void bounded_work(kairo::Executor &executor) {
    auto owner = std::make_shared<d::ActionCoordinator>();
    auto facts = std::make_shared<Facts>();
    auto session = std::make_shared<Session>(
        executor, owner, d::CancelToken{}, [facts] { return std::make_unique<Backend>(facts); },
        Session::Limits{});
    const auto ready = session->begin(owner->epoch());
    facts->delay_ms = 200;
    std::vector<std::future<d::InputSessionReceipt>> requests;
    for (int n = 0; n < 24; ++n)
        requests.push_back(executor.submit_auto(
            [session, ready] { return session->advance(1, ready.state_version, down()); }));
    int refused = 0;
    for (auto &request : requests)
        refused += request.get().error.code == "input_session_unavailable";
    MIRAGE_CHECK(refused > 0 && facts->presses == 1);
    MIRAGE_CHECK(session->release().release_confirmed && facts->releases == 1);
    session->stop();
    // Total budget is independent from renewed command/observation budgets.
    Session total(executor, owner, {}, [facts] { return std::make_unique<Backend>(facts); },
                  {std::chrono::milliseconds{200}, std::chrono::milliseconds{35}});
    facts->delay_ms = 0;
    auto state = total.begin(owner->epoch());
    state = total.advance(1, state.state_version, down());
    delay(executor, 15);
    state = total.observe();
    MIRAGE_CHECK(state.idle_budget_ms_remaining < 30);
    delay(executor, 30);
    wait_terminal(executor, total);
    MIRAGE_CHECK(total.snapshot().error.code == "input_session_timeout");
    total.stop();
    // Reaching the mutation budget closes through cleanup, never wraps counters.
    Session budget(executor, owner, {}, [facts] { return std::make_unique<Backend>(facts); }, {});
    state = budget.begin(owner->epoch());
    auto tap = down();
    tap.kind = d::InputSessionCommand::Kind::key_tap;
    for (std::uint64_t n = 1; n <= 64; ++n)
        state = budget.advance(n, state.state_version, tap);
    MIRAGE_CHECK(state.commands_remaining == 0 && state.command_sequence == 64);
    state = budget.advance(65, state.state_version, tap);
    MIRAGE_CHECK(state.release_confirmed && state.error.code == "result_too_large");
    budget.stop();
    // Execution cancellation also reaches owned-key cleanup, not only idle cancellation.
    d::CancelToken stop;
    Session running(executor, owner, stop, [facts] { return std::make_unique<Backend>(facts); },
                    {});
    facts->delay_ms = 1000;
    state = running.begin(owner->epoch());
    auto cancellation = executor.submit_delayed(25, [stop]() mutable { stop.request_cancel(); });
    state = running.advance(1, state.state_version, down());
    cancellation.future.get();
    MIRAGE_CHECK(state.release_confirmed && state.cancelled && !facts->down);
    running.stop();
}

void command_boundaries(kairo::Executor &executor) {
    auto owner = std::make_shared<d::ActionCoordinator>();
    auto facts = std::make_shared<Facts>();
    Session session(executor, owner, {}, [facts] { return std::make_unique<Backend>(facts); }, {});
    auto state = session.begin(owner->epoch());
    for (const char *name : {"shift", "ctrl", "alt", "meta"}) {
        auto command = down();
        command.key_name = name;
        state = session.advance(state.command_sequence + 1, state.state_version, command);
    }
    MIRAGE_CHECK(state.held_keys.size() == 4 && facts->presses == 4);
    auto tap = down();
    tap.kind = d::InputSessionCommand::Kind::key_tap;
    tap.key_name = "a";
    state = session.advance(5, state.state_version, tap);
    MIRAGE_CHECK(state.release_confirmed && state.error.code == "result_too_large" &&
                 facts->presses == 4);
    session.stop();
    Session cancelled(executor, owner, {}, [facts] { return std::make_unique<Backend>(facts); },
                      {});
    state = cancelled.begin(owner->epoch());
    d::CancelToken stop;
    stop.request_cancel();
    state = cancelled.advance(1, state.state_version, down(), stop);
    MIRAGE_CHECK(state.cancelled && state.release_confirmed && state.command_sequence == 0 &&
                 facts->presses == 4);
    cancelled.stop();
    facts->physical_during = true;
    Session physical(executor, owner, {}, [facts] { return std::make_unique<Backend>(facts); }, {});
    state = physical.begin(owner->epoch());
    state = physical.advance(1, state.state_version, down());
    MIRAGE_CHECK(state.cancelled && state.release_confirmed && !facts->down);
    MIRAGE_CHECK(!owner->try_begin({}) && owner->resume());
    physical.stop();
}

void planned_press_budget(kairo::Executor &executor) {
    for (const bool move : {false, true}) {
        auto owner = std::make_shared<d::ActionCoordinator>();
        auto facts = std::make_shared<Facts>();
        Session session(executor, owner, {}, [facts] { return std::make_unique<Backend>(facts); },
                        {std::chrono::milliseconds{500}, std::chrono::milliseconds{500}});
        auto state = session.begin(owner->epoch());
        auto press = down();
        press.kind = d::InputSessionCommand::Kind::key_tap;
        press.dwell = std::chrono::milliseconds{1100};
        if (move) {
            press = {};
            press.kind = d::InputSessionCommand::Kind::smooth_move;
            press.x = press.y = 100;
            press.duration = std::chrono::milliseconds{600};
        }
        state = session.advance(1, state.state_version, press);
        MIRAGE_CHECK(state.release_confirmed && state.error.code == "input_session_timeout");
        MIRAGE_CHECK(facts->presses == 0 && state.command_sequence == 0);
        session.stop();
    }
}

void stop_during_command(kairo::Executor &executor) {
    auto owner = std::make_shared<d::ActionCoordinator>();
    auto facts = std::make_shared<Facts>();
    facts->delay_ms = 1000;
    auto session = std::make_shared<Session>(
        executor, owner, d::CancelToken{}, [facts] { return std::make_unique<Backend>(facts); },
        Session::Limits{});
    const auto state = session->begin(owner->epoch());
    const auto started = std::chrono::steady_clock::now();
    auto work = executor.submit_auto(
        [session, state] { return session->advance(1, state.state_version, down()); });
    auto stopped = executor.submit_delayed(25, [session] { session->stop(); });
    const auto result = work.get();
    stopped.future.get();
    MIRAGE_CHECK(result.cancelled && result.release_confirmed && !facts->down);
    MIRAGE_CHECK(std::chrono::steady_clock::now() - started < std::chrono::milliseconds{500});
}

void worker_reuse(kairo::Executor &executor) {
    auto worker = std::make_shared<mirage::integration::detail::ControlledInputWorker>(executor);
    auto owner = std::make_shared<d::ActionCoordinator>();
    auto facts = std::make_shared<Facts>();
    const auto before = executor.get_blocking_io_worker_list().size();
    for (int n = 0; n < 20; ++n) {
        Session session(
            executor, owner, {}, [facts] { return std::make_unique<Backend>(facts); }, {}, worker);
        auto state = session.begin(owner->epoch());
        MIRAGE_CHECK(state.state == d::InputSessionState::ready && state.held_keys.empty());
        state = session.advance(1, state.state_version, down());
        MIRAGE_CHECK(state.outcome == d::InputSessionOutcome::holding);
        MIRAGE_CHECK(session.release().release_confirmed);
        session.stop();
        MIRAGE_CHECK(executor.get_blocking_io_worker_list().size() == before + 1);
    }
    worker->stop();
}
void device_endings(kairo::Executor &executor) {
    for (const bool lost : {false, true}) {
        auto owner = std::make_shared<d::ActionCoordinator>();
        auto facts = std::make_shared<Facts>();
        Session session(executor, owner, {}, [facts] { return std::make_unique<Backend>(facts); },
                        {});
        auto state = session.begin(owner->epoch());
        state = session.advance(1, state.state_version, down());
        MIRAGE_CHECK(state.outcome == d::InputSessionOutcome::holding);
        facts->fail_release = lost;
        facts->device_fault = lost ? 2 : 1;
        wait_terminal(executor, session);
        state = session.snapshot();
        MIRAGE_CHECK(state.release_confirmed == !lost && state.cancelled == lost);
        MIRAGE_CHECK(state.error.code == (lost ? "input_release_failed" : "input_device_changed"));
        MIRAGE_CHECK(state.held_keys.empty() == !lost);
        MIRAGE_CHECK(owner->input_released() == !lost);
        if (lost)
            MIRAGE_CHECK(!owner->resume() && !owner->try_begin({}));
        MIRAGE_CHECK(session.advance(2, state.state_version, down()).state == state.state);
        session.stop();
    }
}

} // namespace
int main() {
    kairo::Executor executor;
    kairo::ExecutorConfig config;
    config.min_threads = config.max_threads = 16;
    MIRAGE_CHECK(executor.initialize(config).ok);
    try {
        normal(executor);
        endings(executor);
        faults(executor);
        bounded_work(executor);
        worker_reuse(executor);
        command_boundaries(executor);
        planned_press_budget(executor);
        stop_during_command(executor);
        device_endings(executor);
    } catch (const std::exception &e) {
        std::fprintf(stderr, "controlled input: %s\n", e.what());
        executor.shutdown(true);
        return 1;
    }
    executor.shutdown(true);
    return mirage::testing::finish("controlled_input_session");
}
