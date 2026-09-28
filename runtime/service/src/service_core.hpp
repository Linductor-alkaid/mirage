#pragma once

#include <chrono>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include <executor/executor.hpp>
#include <executor/serial_execution_context.hpp>

#include <mirage/desktop/desktop_environment.hpp>
#include <mirage/desktop/visual_reference_registry.hpp>
#include <mirage/integration/desktop_atom_toolset.hpp>
#include <mirage/integration/model_layer.hpp>
#include <mirage/integration/session_journal.hpp>
#include <mirage/integration/workflow_event_bridge.hpp>
#include <mirage/runtime/mira_host.hpp>
#include <mirage/runtime/permission/permission.hpp>
#include <mirage/runtime/persistence/session_state.hpp>
#include <mirage/runtime/persistence/settings.hpp>
#include <mirage/runtime/persistence/store.hpp>

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
    /// Sessions the service knows (DEC-021): primary + session.open ones.
    SessionRegistry sessions;

    /// M1-07 recovery persistence; disabled until enable() puts a store in
    /// it. persist() is called on every driver settlement and at the end of
    /// the ordered teardown.
    RecoveryWriter recovery;

    /// Desktop surface the M1 task drivers act on (mirrors the environment
    /// wrapped by the binding handed to start(); null until then).
    std::shared_ptr<mirage::desktop::DesktopEnvironment> environment;

    /// Visual reference registry the desktop.observe face projects the
    /// visual generation from (DEC-026); null until configured — the visual
    /// component then fails closed instead of capturing (DEC-016).
    mirage::desktop::VisualReferenceRegistry *visual_registry = nullptr;

    /// RULE-05 gate judged before every desktop action (DEC-010); owned
    /// here so the drivers and future request paths share one policy and
    /// one confirmation hook.
    std::shared_ptr<mirage::runtime::permission::PermissionController> permission;

    /// Process-wide event broadcast point (DEC-012 decision 5); publishes
    /// come from the serial context and the task drivers, subscriptions are
    /// handed to the IPC loop.
    EventHub events;

    /// Conversation journal over the pinned event store (DEC-021): the
    /// service's only event storage, feeding the session.* history face and
    /// the session.message stream. Owned here so handlers (serial context)
    /// and drivers share one store.
    std::shared_ptr<mirage::integration::SessionJournal> journal;

    /// Workflow faces (DEC-023): the catalog and run registries are the
    /// service-side product index over the pinned library/run table, and the
    /// event bridge is the pinned workflow event stream's only translation
    /// point. Constructed with the service, so the workflow.* faces are
    /// always served; the bridge's sink feeds the EventHub broadcast.
    WorkflowRegistry workflows;
    WorkflowRunRegistry workflow_runs;
    std::shared_ptr<mirage::integration::WorkflowEventBridge> workflow_bridge;
    /// Desktop atom toolset (DEC-024): the BuiltIn registry workflow ToolCall
    /// steps dispatch through, projected by workflow.atom.catalog. Built at
    /// start() over the bound environment with the shared permission gate;
    /// empty until then.
    std::shared_ptr<mirage::integration::DesktopAtomToolset> workflow_tools;

    /// Model layer (DEC-027): the pinned model stack serving the
    /// session.chat dialog face. Constructed at start() only when
    /// ServiceConfig::model is enabled and valid; null otherwise (hello
    /// reports no `chat` capability and session.chat answers `unavailable`).
    std::unique_ptr<mirage::integration::ModelLayer> model_layer;
    /// The model layer's configuration mirror (DEC-027), engaged at start().
    mirage::integration::ModelLayerConfig model;
    /// Read-roots resource-scope mirror (M5-07 policy face): reported by
    /// policy.get, replaced by policy.set, applied to the bound provider at
    /// start (the embedding wires it into the environment).
    std::vector<std::string> read_roots;
    /// Settings write-back store (M5-07, DEC-011 Desktop Permissions entry):
    /// policy.set persists the merged document here. Null when write-back is
    /// disabled (tests).
    std::unique_ptr<mirage::runtime::persistence::LocalStateStore> settings_store;
    /// Session state store (M5-08, DEC-021 backlog ①): the conversation
    /// journal, dialog threads and session registry persist here across
    /// restarts. Null when disabled (tests).
    std::unique_ptr<mirage::runtime::persistence::LocalStateStore> session_state_store;
    /// Raw journal append inputs (DEC-021 hydration surface): shadow the
    /// journal's appends with the raw inputs so the persisted document can
    /// reproduce the projected view exactly (append_outcome composes its
    /// sentence from progress + steps; the projection alone would not
    /// round-trip). Same append discipline as the journal itself.
    std::mutex journal_raw_mutex;
    std::map<std::string, std::vector<mirage::runtime::persistence::PersistedJournalEntry>>
        journal_raw;
    /// Serializes session-state document writes from the serial context and
    /// worker threads (dialog settle, task settle).
    std::mutex session_state_mutex;
    /// Dialog threads (DEC-027): the per-session bounded turn logs, the
    /// snapshot face of session.chat_updated.
    DialogRegistry dialogs;

    std::string mirage_version;
    std::size_t max_steps_per_task = 64;
    std::size_t max_task_records = 256;
    /// Session registry capacity (DEC-021): session.open fails closed at the
    /// bound instead of growing without bound.
    std::size_t max_sessions = 16;
    /// Upper bound for one session.history response, regardless of the
    /// requested limit.
    std::size_t max_history_entries = 200;
    /// Workflow registry capacities (DEC-023): the catalog refuses new ids
    /// at the bound; the run registry evicts terminal entries before it
    /// refuses admission.
    std::size_t max_workflow_definitions = 128;
    std::size_t max_workflow_runs = 256;
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
