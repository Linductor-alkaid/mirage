#pragma once

#include <chrono>
#include <functional>
#include <memory>
#include <mirage/desktop/action_coordinator.hpp>
#include <mirage/desktop/input_session.hpp>

namespace kairo {
class Executor;
}
namespace mirage::integration::detail {
// Turn-owned facade. All platform work and authoritative held state live on
// one Executor blocking worker; the bounded FIFO never drops input commands.
class ControlledInputWorker {
  public:
    explicit ControlledInputWorker(kairo::Executor &);
    ~ControlledInputWorker();
    void stop();

  private:
    friend class ControlledInputSession;
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
class ControlledInputSession {
  public:
    using Factory = std::function<std::unique_ptr<desktop::InputSessionBackend>()>;
    struct Limits {
        std::chrono::milliseconds idle{desktop::input_session_budget::max_idle};
        std::chrono::milliseconds total{desktop::input_session_budget::max_session_total};
        std::string worker_name{}; // Optional external-owner identity; never model input.
    };
    ControlledInputSession(kairo::Executor &, std::shared_ptr<desktop::ActionCoordinator>,
                           desktop::CancelToken, Factory, Limits,
                           std::shared_ptr<ControlledInputWorker> = {});
    ~ControlledInputSession();
    ControlledInputSession(const ControlledInputSession &) = delete;
    ControlledInputSession &operator=(const ControlledInputSession &) = delete;
    desktop::InputSessionReceipt begin(std::uint64_t expected_epoch);
    desktop::InputSessionReceipt advance(std::uint64_t sequence, std::uint64_t version,
                                         desktop::InputSessionCommand, desktop::CancelToken = {});
    desktop::InputSessionReceipt observe();
    desktop::InputSessionReceipt release();
    desktop::InputSessionReceipt snapshot() const;
    std::uint64_t environment_epoch() const;
    void stop();

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace mirage::integration::detail
