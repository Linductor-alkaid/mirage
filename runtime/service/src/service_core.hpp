#pragma once

#include <chrono>
#include <map>
#include <memory>
#include <mutex>
#include <string>

#include <executor/executor.hpp>
#include <executor/serial_execution_context.hpp>

#include <mirage/desktop/desktop_environment.hpp>
#include <mirage/runtime/mira_host.hpp>
#include <mirage/runtime/permission/permission.hpp>

#include "event_hub.hpp"
#include "recovery_writer.hpp"
#include "task_registry.hpp"

namespace mirage::runtime::detail {

/// State shared by the service loop, the request handlers and the task
/// drivers. Owned by a shared_ptr so a driver task finishing during drain
/// can never touch freed state; the RuntimeService::Impl holds one
/// reference for the service lifetime.
struct ServiceCore {
    /// The process's only Executor instance (EXEC-01): the service owns it,
    /// initializes it in start() and shuts it down in the ordered teardown.
    executor::Executor executor;
    /// Serialization context for every MiraHost operation; the host's
    /// single-owner discipline is expressed through it.
    executor::SerialExecutionContext serial;
    MiraHost host;
    TaskRegistry registry;

    /// M1-07 recovery persistence; disabled until enable() puts a store in
    /// it. persist() is called on every driver settlement and at the end of
    /// the ordered teardown.
    RecoveryWriter recovery;

    /// Desktop surface the M1 task drivers act on (mirrors the environment
    /// wrapped by the binding handed to start(); null until then).
    std::shared_ptr<mirage::desktop::DesktopEnvironment> environment;

    /// RULE-05 gate judged before every desktop action (DEC-010); owned
    /// here so the drivers and future request paths share one policy and
    /// one confirmation hook.
    std::shared_ptr<mirage::runtime::permission::PermissionController> permission;

    /// Process-wide event broadcast point (DEC-012 decision 5); publishes
    /// come from the serial context and the task drivers, subscriptions are
    /// handed to the IPC loop.
    EventHub events;

    std::string mirage_version;
    std::size_t max_steps_per_task = 64;
    std::size_t max_task_records = 256;
    std::size_t max_result_bytes = 8192;
    std::chrono::milliseconds step_timeout{30000};
    std::chrono::milliseconds command_wait{4000};

    /// Driver handles per active task (guarded by its own mutex; the
    /// registry mutex is never held while touching executor types).
    std::mutex drivers_mutex;
    std::map<std::string, executor::TaskSubmission<void>> drivers;

    ServiceCore() = default;
    ServiceCore(const ServiceCore &) = delete;
    ServiceCore &operator=(const ServiceCore &) = delete;
};

} // namespace mirage::runtime::detail
