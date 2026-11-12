#include <mirage/integration/visual_memory_write.hpp>

#include <mira/model_digest.hpp>

namespace mirage::integration {
namespace {
using Stage = VisualMemoryWriteStage;
mira::Error failed(mira::ErrorCode code, Stage stage, const char *reason,
                   const mira::OperationContext &context,
                   const std::optional<mira::ArtifactDescriptor> &artifact) {
    mira::Error result;
    result.code = code;
    result.domain = "mirage.visual_memory.write";
    result.domain_code = static_cast<std::int32_t>(stage);
    result.safe_message = reason;
    result.operation_id = context.operation;
    if (artifact)
        result.diagnostic_artifact = artifact->id;
    return result;
}
} // namespace

std::future<mira::Result<VisualMemoryWriteResult>>
schedule_visual_memory_write(mira::ContextMemorySupervisor &supervisor, mira::IEventStore &events,
                             mira::IArtifactStore &artifacts, mira::IMemory &memory,
                             mira::RuntimeId runtime, mira::MemoryId memory_id,
                             mira::MutationId mutation_id,
                             std::shared_ptr<const VisualActionEvidence> input,
                             VisualMemoryLimits limits, mira::OperationContext context) {
    return supervisor.submit<VisualMemoryWriteResult>(
        "visual_memory.write", mira::SupervisedOpClass::Critical,
        [&events, &artifacts, &memory, runtime, memory_id, mutation_id, input = std::move(input),
         limits, context = std::move(context)](
            mira::SupervisorToken token) mutable -> mira::Result<VisualMemoryWriteResult> {
            Stage stage = Stage::Preparation;
            std::optional<mira::ArtifactDescriptor> committed;
            const auto reject = [&](mira::ErrorCode code, const char *reason) {
                return failed(code, stage, reason, context, committed);
            };
            const auto original = context.cancellation_requested;
            context.cancellation_requested = [original, token] {
                return (original && original()) || token.stop_requested();
            };
            const auto boundary = [&]() -> mira::Result<void> {
                if (context.cancelled())
                    return reject(mira::ErrorCode::Cancelled, "visual_write_cancelled");
                if (context.expired(mira::Timestamp::now()))
                    return reject(mira::ErrorCode::DeadlineExceeded, "visual_write_deadline");
                return {};
            };
            try {
                if (!input || runtime.is_nil() || memory_id.is_nil() || mutation_id.is_nil())
                    return reject(mira::ErrorCode::InvalidArgument, "visual_write_identity");
                const auto bytes = prepare_visual_action_evidence(*input, limits, context);
                if (!bytes)
                    return reject(bytes.error().code, "visual_write_preparation_failed");
                stage = Stage::ArtifactWrite;
                if (const auto check = boundary(); !check)
                    return check.error();
                mira::ArtifactWriteSpec spec;
                spec.media_type = std::string(kVisualMemoryMediaType);
                spec.encoding = mira::ArtifactEncoding::Binary;
                spec.sensitivity = mira::Sensitivity::Sensitive;
                spec.content_schema = {1, 0};
                spec.max_bytes = bytes.value().size();
                auto writer = artifacts.begin(spec);
                if (!writer)
                    return reject(writer.error().code, "visual_write_artifact_begin_failed");
                if (const auto wrote = writer.value().write(bytes.value()); !wrote)
                    return reject(wrote.error().code, "visual_write_artifact_buffer_failed");
                if (const auto check = boundary(); !check)
                    return check.error();
                auto artifact = artifacts.commit(writer.value());
                if (!artifact)
                    return reject(artifact.error().code, "visual_write_artifact_commit_uncertain");
                committed = artifact.value();
                stage = Stage::ArtifactVerify;
                auto record =
                    make_visual_memory_record(memory_id, *committed, artifacts, limits, context);
                if (!record)
                    return reject(record.error().code, "visual_write_artifact_verify_failed");
                stage = Stage::ProvenanceCommit;
                if (const auto check = boundary(); !check)
                    return check.error();
                using Json = mira::JsonValue;
                const mira::EventPayload payload{
                    "mirage.visual_evidence_recorded.v1",
                    mira::canonical_json_string(Json{
                        Json::Object{{"schema", 1},
                                     {"operation", context.operation.to_string()},
                                     {"task_epoch", static_cast<std::int64_t>(context.task_epoch)},
                                     {"artifact_id", committed->id.to_string()},
                                     {"artifact_digest", committed->digest.to_string()}}}),
                    mira::EventClass::State};
                mira::AppendRequest request;
                request.event_id = input->provenance;
                request.runtime_id = runtime;
                request.session_id = context.session;
                request.task_id = context.task;
                request.payload = payload;
                request.required = mira::Durability::ProcessCrash;
                const auto appended = events.append(request);
                if (!appended)
                    return reject(appended.error().code, "visual_write_event_commit_uncertain");
                const auto &receipt = appended.value();
                if (receipt.event_id != request.event_id || !receipt.session_sequence ||
                    !receipt.task_sequence || !*receipt.task_sequence ||
                    !receipt.durable_watermark ||
                    (receipt.achieved != mira::Durability::ProcessCrash &&
                     receipt.achieved != mira::Durability::PowerLoss))
                    return reject(mira::ErrorCode::DataLoss, "visual_write_event_receipt_invalid");
                stage = Stage::ProvenanceRead;
                if (const auto check = boundary(); !check)
                    return check.error();
                const auto read = events.read({context.session, receipt.session_sequence - 1, 1});
                if (!read)
                    return reject(read.error().code, "visual_write_event_read_failed");
                if (read.value().events.size() != 1)
                    return reject(mira::ErrorCode::DataLoss, "visual_write_event_missing");
                const auto &event = read.value().events.front();
                if (event.event_id != request.event_id || event.runtime_id != runtime ||
                    event.session_id != context.session || event.task_id != request.task_id ||
                    event.session_sequence != receipt.session_sequence ||
                    event.task_sequence != receipt.task_sequence ||
                    event.payload.type != payload.type || event.payload.data != payload.data ||
                    event.payload.classification != payload.classification)
                    return reject(mira::ErrorCode::DataLoss,
                                  "visual_write_event_identity_mismatch");
                stage = Stage::MemoryApply;
                if (const auto check = boundary(); !check)
                    return check.error();
                mira::MemoryMutation mutation;
                mutation.id = mutation_id;
                mutation.scope = record.value().scope;
                mutation.proposed = std::move(record.value());
                mutation.evidence = mutation.proposed.provenance;
                const auto applied = memory.apply(mutation);
                if (!applied)
                    return reject(applied.error().code, "visual_write_memory_apply_uncertain");
                if (applied.value().record != memory_id || !applied.value().new_version ||
                    (applied.value().applied != mira::MemoryMutationType::Add &&
                     !(applied.value().applied == mira::MemoryMutationType::Noop &&
                       applied.value().idempotent_replay)))
                    return reject(mira::ErrorCode::DataLoss, "visual_write_memory_receipt_invalid");
                return VisualMemoryWriteResult{
                    *committed, receipt, applied.value(),
                    context.cancelled_or_expired(mira::Timestamp::now())};
            } catch (...) {
                return reject(mira::ErrorCode::Internal, "visual_write_exception_uncertain");
            }
        });
}
} // namespace mirage::integration
