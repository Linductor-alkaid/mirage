#include "recovery_writer.hpp"

#include <iostream>
#include <utility>
#include <vector>

#include <mirage/runtime/ipc/protocol.hpp>

namespace mirage::runtime::detail {
namespace {

/// Converts a registry step record into its persisted form. `result` is
/// already capped by the driver (max_result_bytes), so the persist path
/// never carries unbounded payloads.
persistence::RecoveryStep to_recovery_step(const StepRecord &record) {
    persistence::RecoveryStep step;
    step.kind = ipc::step_kind_name(record.spec.kind);
    step.argument = record.spec.argument;
    step.status = record.status;
    step.operation_id = record.operation_id;
    step.permission = record.permission;
    step.ok = record.ok;
    step.exit_code = record.exit_code;
    step.result = record.result;
    step.result_truncated = record.result_truncated;
    step.error = record.error;
    return step;
}

/// Converts one settled registry task into its persisted form; nullopt for
/// records that never reached a terminal progress name (cannot happen for
/// driver-settled tasks after the M1-07 hook, defensive all the same).
std::optional<persistence::RecoveryTask> to_recovery_task(const TaskRecord &record) {
    if (record.final_progress.empty()) {
        return std::nullopt;
    }
    persistence::RecoveryTask task;
    task.id = record.id;
    task.goal = record.goal;
    task.progress = record.final_progress;
    task.has_success = record.has_success;
    task.success = record.success;
    task.steps.reserve(record.steps.size());
    for (const StepRecord &step : record.steps) {
        task.steps.push_back(to_recovery_step(step));
    }
    return task;
}

} // namespace

void RecoveryWriter::enable(persistence::LocalStateStore store, std::string mirage_version,
                            std::size_t max_result_bytes) {
    std::lock_guard lock(mutex_);
    store_ = std::move(store);
    mirage_version_ = std::move(mirage_version);
    max_result_bytes_ = max_result_bytes;
}

bool RecoveryWriter::enabled() const {
    std::lock_guard lock(mutex_);
    return store_.has_value();
}

void RecoveryWriter::record_error(const std::string &reason) {
    // Callers hold the writer mutex.
    last_error_ = reason;
    if (!error_reported_) {
        error_reported_ = true;
        // The service runs in the foreground (DEC-007); stderr is the M1
        // observability surface for recovery problems. Loud once, no spam.
        std::cerr << "mirage-service: recovery state problem: " << reason << '\n';
    }
}

std::string RecoveryWriter::last_error() {
    std::lock_guard lock(mutex_);
    return last_error_;
}

void RecoveryWriter::persist(TaskRegistry &registry) {
    std::lock_guard writer_lock(mutex_);
    if (!store_) {
        return;
    }
    // Snapshot under the registry lock, write after releasing it: the file
    // I/O must never extend the registry's critical section.
    persistence::RecoveryState state;
    {
        std::lock_guard registry_lock(registry.mutex);
        state.tasks.reserve(registry.tasks.size());
        for (const auto &[id, record] : registry.tasks) {
            (void)id;
            if (auto task = to_recovery_task(record)) {
                state.tasks.push_back(std::move(*task));
            }
        }
    }
    state.mirage_version = mirage_version_;
    state.saved_at = persistence::utc_timestamp_now();
    const persistence::SaveResult saved = store_->save(persistence::encode_recovery(state));
    if (!saved.ok) {
        record_error("cannot persist " + store_->file_path().string() + ": " + saved.error);
    }
}

std::size_t RecoveryWriter::hydrate(TaskRegistry &registry, std::size_t capacity) {
    std::lock_guard writer_lock(mutex_);
    if (!store_) {
        return 0;
    }
    const persistence::LoadResult loaded = store_->load();
    if (loaded.status == persistence::LoadStatus::Absent) {
        return 0;
    }
    if (loaded.status == persistence::LoadStatus::TooLarge) {
        record_error("recovery file exceeds the byte budget at " + store_->file_path().string());
        return 0;
    }
    if (loaded.status == persistence::LoadStatus::IoError) {
        record_error("cannot read " + store_->file_path().string() + ": " + loaded.error);
        return 0;
    }
    const persistence::RecoveryDecode decoded = persistence::decode_recovery(loaded.body);
    if (!decoded.ok) {
        record_error("cannot decode " + store_->file_path().string() + ": " + decoded.error);
        return 0;
    }
    std::size_t hydrated = 0;
    {
        std::lock_guard registry_lock(registry.mutex);
        for (const persistence::RecoveryTask &task : decoded.state.tasks) {
            if (registry.tasks.size() >= capacity) {
                record_error("recovery file holds more tasks than the "
                             "registry capacity (" +
                             std::to_string(capacity) +
                             "); the rest are "
                             "dropped");
                break;
            }
            TaskRecord record;
            record.id = task.id;
            record.goal = task.goal;
            record.driver_done = true;
            record.final_progress = task.progress;
            record.from_recovery = true;
            record.has_success = task.has_success;
            record.success = task.success;
            record.steps.reserve(task.steps.size());
            bool steps_usable = true;
            for (const persistence::RecoveryStep &step : task.steps) {
                StepRecord entry;
                const auto kind = ipc::step_kind_from_name(step.kind);
                if (!kind) {
                    // decode_recovery already validates the vocabulary;
                    // this guards against drift between the two modules.
                    steps_usable = false;
                    break;
                }
                entry.spec.kind = *kind;
                entry.spec.argument = step.argument;
                static const char *kStatuses[] = {step_status::kPending, step_status::kRunning,
                                                  step_status::kOk,      step_status::kFailed,
                                                  step_status::kSkipped, step_status::kCancelled};
                for (const char *candidate : kStatuses) {
                    if (step.status == candidate) {
                        entry.status = candidate;
                        break;
                    }
                }
                static const char *kPermissions[] = {"", "allowed", "confirmed", "denied",
                                                     "confirmation_rejected"};
                for (const char *candidate : kPermissions) {
                    if (step.permission == candidate) {
                        entry.permission = candidate;
                        break;
                    }
                }
                entry.operation_id = step.operation_id;
                entry.ok = step.ok;
                entry.exit_code = step.exit_code;
                entry.result = step.result;
                entry.result_truncated = step.result_truncated;
                entry.error = step.error;
                record.steps.push_back(std::move(entry));
            }
            if (!steps_usable) {
                record_error("recovery file carries an unknown step kind in "
                             "task " +
                             task.id);
                return 0;
            }
            ++hydrated;
            std::string id = record.id;
            registry.tasks.emplace(std::move(id), std::move(record));
        }
    }
    return hydrated;
}

} // namespace mirage::runtime::detail
