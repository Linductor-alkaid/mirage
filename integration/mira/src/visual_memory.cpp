#include <mirage/integration/visual_memory.hpp>

#include <algorithm>
#include <limits>
#include <mira/model_digest.hpp>
#include <span>

namespace mirage::integration {
namespace {
using Json = mira::JsonValue;
using Clock = std::chrono::system_clock;
constexpr std::size_t kManifestBytes = 8192;
constexpr std::string_view kStatementPrefix = "visual_action_evidence=";
mira::Error error(mira::ErrorCode code, const char *reason) {
    mira::Error result;
    result.code = code;
    result.domain = "mirage.visual_memory";
    result.safe_message = reason;
    return result;
}
mira::Error invalid(const char *reason) { return error(mira::ErrorCode::InvalidArgument, reason); }
bool limits_valid(const VisualMemoryLimits &l) {
    return l.max_frame_bytes && l.max_frame_bytes <= (8u << 20) && l.max_artifact_bytes > 8 &&
           l.max_artifact_bytes <= (16u << 20) && l.max_pair_span.count() > 0 &&
           l.max_capture_age.count() > 0 && l.max_pair_span <= l.max_capture_age &&
           l.max_capture_age <= std::chrono::seconds{120};
}
mira::Result<void> live(const mira::OperationContext &ctx) {
    if (ctx.cancelled())
        return error(mira::ErrorCode::Cancelled, "visual_evidence_cancelled");
    if (ctx.expired(mira::Timestamp::now()))
        return error(mira::ErrorCode::DeadlineExceeded, "visual_evidence_deadline");
    if (ctx.task.is_nil())
        return invalid("visual_evidence_missing_task");
    return {};
}
std::int64_t ms(Clock::time_point time) {
    return std::chrono::duration_cast<std::chrono::milliseconds>(time.time_since_epoch()).count();
}
Clock::time_point from_ms(std::int64_t value) {
    return Clock::time_point{
        std::chrono::duration_cast<Clock::duration>(std::chrono::milliseconds{value})};
}
bool text_valid(const std::string &text) {
    return !text.empty() && text.size() <= 1024 && desktop::is_valid_utf8(text) &&
           std::none_of(text.begin(), text.end(),
                        [](unsigned char c) { return c < 32 || c == 127; });
}
bool keys(const Json &v, std::initializer_list<std::string_view> expected) {
    const auto *object = v.as_object();
    if (!object || object->size() != expected.size())
        return false;
    return std::all_of(expected.begin(), expected.end(),
                       [&](auto key) { return v.find(key) != nullptr; });
}
std::string string(const Json &v, const char *key) {
    const auto *item = v.find(key);
    return item && item->as_string() ? *item->as_string() : "";
}
std::int64_t integer(const Json &v, const char *key) {
    const auto *item = v.find(key);
    return item && item->is_integer() ? item->as_integer().value_or(-1) : -1;
}
bool id_valid(const std::string &value) {
    const auto id = mira::Id128::parse(value);
    return id && !id->is_nil() && id->to_string() == value;
}
mira::Result<std::vector<std::byte>> tight_pixels(const desktop::ImageFrame &f,
                                                  const VisualMemoryLimits &l,
                                                  const mira::OperationContext &ctx) {
    if (f.format != desktop::ImageFormat::Bgra8 || f.width <= 0 || f.height <= 0)
        return invalid("visual_evidence_invalid_frame");
    const auto row = static_cast<std::uint64_t>(f.width) * 4;
    const auto height = static_cast<std::size_t>(f.height);
    if (f.pixels.size() > l.max_frame_bytes || row > f.stride ||
        f.stride > l.max_frame_bytes / height || f.pixels.size() != f.stride * height)
        return invalid("visual_evidence_invalid_frame_buffer");
    std::vector<std::byte> result;
    result.reserve(static_cast<std::size_t>(row) * height);
    for (std::size_t y = 0; y < height; ++y) {
        if (const auto checked = live(ctx); !checked)
            return checked.error();
        const auto line =
            std::as_bytes(std::span(f.pixels).subspan(y * f.stride, static_cast<std::size_t>(row)));
        result.insert(result.end(), line.begin(), line.end());
    }
    return result;
}
Json frame_manifest(const desktop::ImageFrame &frame, std::span<const std::byte> bytes,
                    std::size_t offset) {
    return Json{Json::Object{{"width", frame.width},
                             {"height", frame.height},
                             {"offset", static_cast<std::int64_t>(offset)},
                             {"byte_size", static_cast<std::int64_t>(bytes.size())},
                             {"digest", mira::digest_bytes(bytes).to_string()}}};
}
mira::Result<desktop::ImageFrame> read_frame(const Json &v, std::span<const std::byte> pixels,
                                             std::size_t offset, const VisualMemoryLimits &l) {
    if (!keys(v, {"width", "height", "offset", "byte_size", "digest"}))
        return invalid("visual_evidence_frame_manifest");
    const auto w = integer(v, "width"), h = integer(v, "height");
    const auto size = integer(v, "byte_size");
    if (w <= 0 || h <= 0 || w > std::numeric_limits<std::int32_t>::max() ||
        h > std::numeric_limits<std::int32_t>::max() ||
        static_cast<std::uint64_t>(w) > l.max_frame_bytes / 4 / static_cast<std::uint64_t>(h) ||
        size != w * h * 4 || integer(v, "offset") != static_cast<std::int64_t>(offset) ||
        offset > pixels.size() || static_cast<std::uint64_t>(size) > pixels.size() - offset)
        return invalid("visual_evidence_frame_bounds");
    const auto bytes = pixels.subspan(offset, static_cast<std::size_t>(size));
    if (mira::digest_bytes(bytes).to_string() != string(v, "digest"))
        return error(mira::ErrorCode::DataLoss, "visual_evidence_frame_digest");
    desktop::ImageFrame result;
    result.width = static_cast<std::int32_t>(w);
    result.height = static_cast<std::int32_t>(h);
    result.stride = static_cast<std::size_t>(w) * 4;
    result.pixels.resize(bytes.size());
    std::transform(bytes.begin(), bytes.end(), result.pixels.begin(),
                   [](std::byte b) { return std::to_integer<std::uint8_t>(b); });
    return result;
}
mira::Result<VisualMemoryReview> read_artifact(const mira::ArtifactDescriptor &a,
                                               const mira::IArtifactStore &store,
                                               const VisualMemoryLimits &l,
                                               const mira::OperationContext &ctx) {
    if (const auto checked = live(ctx); !checked)
        return checked.error();
    if (!limits_valid(l) || a.id.is_nil() || a.media_type != kVisualMemoryMediaType ||
        a.encoding != mira::ArtifactEncoding::Binary ||
        a.content_schema != mira::SchemaVersion{1, 0} ||
        a.sensitivity != mira::Sensitivity::Sensitive || a.byte_size <= 8 ||
        a.byte_size > l.max_artifact_bytes)
        return invalid("visual_evidence_artifact_descriptor");
    const auto opened = store.open(a);
    if (!opened)
        return error(opened.error().code, "visual_evidence_unavailable");
    const auto bytes = std::span(opened.value().bytes());
    if (bytes.size() != a.byte_size || mira::digest_bytes(bytes) != a.digest)
        return error(mira::ErrorCode::DataLoss, "visual_evidence_artifact_digest");
    std::uint64_t length = 0;
    for (unsigned i = 0; i < 8; ++i)
        length |= static_cast<std::uint64_t>(std::to_integer<unsigned>(bytes[i])) << (8 * i);
    if (!length || length > kManifestBytes || length > bytes.size() - 8)
        return invalid("visual_evidence_manifest_budget");
    const std::string encoded(reinterpret_cast<const char *>(bytes.data() + 8),
                              static_cast<std::size_t>(length));
    const auto parsed = mira::parse_json(encoded);
    if (!parsed || mira::canonical_json_string(parsed.value()) != encoded)
        return invalid("visual_evidence_manifest_json");
    const auto &m = parsed.value();
    if (!keys(m, {"schema", "task", "session", "operation", "provenance", "task_epoch",
                  "application", "page", "recorded_at_ms", "expires_at_ms", "pair_span_ms",
                  "format", "outcome", "sensitivity", "before", "after"}) ||
        integer(m, "schema") != 1 || string(m, "format") != "bgra8888" ||
        string(m, "sensitivity") != mira::sensitivity_name(a.sensitivity) ||
        string(m, "outcome") != "not_verified" || !id_valid(string(m, "task")) ||
        !id_valid(string(m, "session")) || !id_valid(string(m, "operation")) ||
        !id_valid(string(m, "provenance")) || integer(m, "task_epoch") <= 0 ||
        string(m, "task") != ctx.task.to_string() || !text_valid(string(m, "application")) ||
        !text_valid(string(m, "page")) || integer(m, "pair_span_ms") < 0 ||
        integer(m, "pair_span_ms") > l.max_pair_span.count())
        return invalid("visual_evidence_manifest_identity");
    const auto recorded = integer(m, "recorded_at_ms"), expiry = integer(m, "expires_at_ms");
    const auto max_time = ms(Clock::time_point::max());
    if (recorded <= 0 || expiry <= recorded || expiry > max_time || recorded > ms(Clock::now()) ||
        expiry <= ms(Clock::now()))
        return error(mira::ErrorCode::StaleObservation, "visual_evidence_expired_or_future");
    const auto pixels = bytes.subspan(8 + static_cast<std::size_t>(length));
    auto before = read_frame(*m.find("before"), pixels, 0, l);
    if (!before)
        return before.error();
    auto after = read_frame(*m.find("after"), pixels, before.value().pixels.size(), l);
    if (!after)
        return after.error();
    if (before.value().pixels.size() + after.value().pixels.size() != pixels.size())
        return invalid("visual_evidence_trailing_bytes");
    if (const auto checked = live(ctx); !checked)
        return checked.error();
    if (expiry <= ms(Clock::now()))
        return error(mira::ErrorCode::StaleObservation, "visual_evidence_expired");
    return VisualMemoryReview{m, std::move(before.value()), std::move(after.value())};
}
} // namespace

mira::Result<std::vector<std::byte>>
prepare_visual_action_evidence(const VisualActionEvidence &input, const VisualMemoryLimits &l,
                               const mira::OperationContext &ctx) {
    if (const auto checked = live(ctx); !checked)
        return checked.error();
    const auto now = std::chrono::steady_clock::now();
    if (!limits_valid(l) || input.task != ctx.task || input.session != ctx.session ||
        input.operation != ctx.operation || input.session.is_nil() || input.operation.is_nil() ||
        input.provenance.is_nil() || !input.task_epoch || input.task_epoch != ctx.task_epoch ||
        input.sensitivity != mira::Sensitivity::Sensitive ||
        input.task_epoch > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()) ||
        !text_valid(input.application) || !text_valid(input.page) || ms(input.recorded_at) <= 0 ||
        ms(input.recorded_at) > ms(Clock::now()) || ms(input.expires_at) <= ms(input.recorded_at) ||
        input.expires_at <= Clock::now())
        return invalid("visual_evidence_owner_or_limits");
    if (!input.before.authority_epoch || !input.before.environment_epoch ||
        input.before.authority_epoch != input.after.authority_epoch ||
        input.before.environment_epoch != input.after.environment_epoch ||
        input.before.captured_at.time_since_epoch().count() <= 0 ||
        input.after.captured_at < input.before.captured_at || input.after.captured_at > now ||
        now - input.before.captured_at > l.max_capture_age ||
        input.after.captured_at - input.before.captured_at > l.max_pair_span)
        return error(mira::ErrorCode::StaleObservation, "visual_evidence_source_or_time");
    auto before = tight_pixels(input.before.image, l, ctx);
    if (!before)
        return before.error();
    auto after = tight_pixels(input.after.image, l, ctx);
    if (!after)
        return after.error();
    const auto span = std::chrono::duration_cast<std::chrono::milliseconds>(
        input.after.captured_at - input.before.captured_at);
    const auto manifest = mira::canonical_json_string(Json{Json::Object{
        {"schema", 1},
        {"task", input.task.to_string()},
        {"session", input.session.to_string()},
        {"operation", input.operation.to_string()},
        {"provenance", input.provenance.to_string()},
        {"task_epoch", static_cast<std::int64_t>(input.task_epoch)},
        {"application", input.application},
        {"page", input.page},
        {"recorded_at_ms", ms(input.recorded_at)},
        {"expires_at_ms", ms(input.expires_at)},
        {"pair_span_ms", span.count()},
        {"format", "bgra8888"},
        {"outcome", "not_verified"},
        {"sensitivity", mira::sensitivity_name(input.sensitivity)},
        {"before", frame_manifest(input.before.image, before.value(), 0)},
        {"after", frame_manifest(input.after.image, after.value(), before.value().size())}}});
    const auto size = 8 + manifest.size() + before.value().size() + after.value().size();
    if (manifest.size() > kManifestBytes || size > l.max_artifact_bytes)
        return error(mira::ErrorCode::ResourceExhausted, "visual_evidence_artifact_budget");
    std::vector<std::byte> result;
    result.reserve(size);
    for (unsigned n = 0; n < 8; ++n)
        result.push_back(
            static_cast<std::byte>((static_cast<std::uint64_t>(manifest.size()) >> (8 * n)) & 255));
    const auto encoded = std::as_bytes(std::span(manifest));
    result.insert(result.end(), encoded.begin(), encoded.end());
    result.insert(result.end(), before.value().begin(), before.value().end());
    result.insert(result.end(), after.value().begin(), after.value().end());
    if (const auto checked = live(ctx); !checked)
        return checked.error();
    if (std::chrono::steady_clock::now() - input.before.captured_at > l.max_capture_age ||
        input.expires_at <= Clock::now())
        return error(mira::ErrorCode::StaleObservation,
                     "visual_evidence_expired_during_preparation");
    return result;
}

mira::Result<mira::MemoryRecord> make_visual_memory_record(mira::MemoryId id,
                                                           const mira::ArtifactDescriptor &artifact,
                                                           const mira::IArtifactStore &store,
                                                           const VisualMemoryLimits &l,
                                                           const mira::OperationContext &ctx) {
    const auto decoded = read_artifact(artifact, store, l, ctx);
    if (!decoded)
        return decoded.error();
    const auto &m = decoded.value().manifest;
    if (id.is_nil() || string(m, "session") != ctx.session.to_string() ||
        string(m, "operation") != ctx.operation.to_string() ||
        integer(m, "task_epoch") != static_cast<std::int64_t>(ctx.task_epoch))
        return invalid("visual_evidence_original_owner_required");
    mira::MemoryRecord result;
    result.id = id;
    result.scope = {mira::MemoryScopeKind::Task, ctx.task.to_string(), std::nullopt};
    result.kind = mira::MemoryKind::Episode;
    result.statement = std::string(kStatementPrefix) + artifact.digest.to_string();
    result.evidence = mira::ArtifactRef{artifact.id, artifact.digest, artifact.byte_size,
                                        artifact.media_type, artifact.sensitivity};
    result.validity.valid_from = from_ms(integer(m, "recorded_at_ms"));
    result.validity.valid_until = from_ms(integer(m, "expires_at_ms"));
    result.recorded_at = result.validity.valid_from;
    result.expires_at = result.validity.valid_until;
    result.provenance = {*mira::EventId::parse(string(m, "provenance"))};
    result.verification = mira::MemoryVerification::Observed;
    result.confidence = 1.0F;
    result.sensitivity = artifact.sensitivity;
    if (const auto valid = result.validate(); !valid)
        return invalid("visual_evidence_memory_record_invalid");
    if (const auto policy = mira::check_memory_kind_policy(result); !policy)
        return invalid("visual_evidence_memory_policy");
    return result;
}

mira::Result<VisualMemoryReview> review_visual_memory(const mira::MemoryRecord &record,
                                                      const mira::IArtifactStore &store,
                                                      const VisualMemoryLimits &l,
                                                      const mira::OperationContext &ctx) {
    if (record.scope !=
            mira::MemoryScope{mira::MemoryScopeKind::Task, ctx.task.to_string(), std::nullopt} ||
        record.id.is_nil() || record.statement.size() != kStatementPrefix.size() + 64 ||
        record.provenance.size() != 1 || !record.validate() ||
        record.kind != mira::MemoryKind::Episode || record.status != mira::MemoryStatus::Active ||
        record.source_namespace || record.verification != mira::MemoryVerification::Observed ||
        !record.evidence || record.sensitivity != record.evidence->sensitivity ||
        record.statement != std::string(kStatementPrefix) + record.evidence->digest.to_string())
        return invalid("visual_evidence_record_identity");
    const auto &e = *record.evidence;
    const mira::ArtifactDescriptor artifact{
        e.id,          e.digest, e.byte_size, e.media_type, mira::ArtifactEncoding::Binary,
        e.sensitivity, {1, 0}};
    auto result = read_artifact(artifact, store, l, ctx);
    if (!result)
        return result.error();
    const auto &m = result.value().manifest;
    if (record.recorded_at != from_ms(integer(m, "recorded_at_ms")) ||
        record.validity.valid_from != record.recorded_at ||
        record.expires_at != std::optional{from_ms(integer(m, "expires_at_ms"))} ||
        record.validity.valid_until != record.expires_at || record.provenance.size() != 1 ||
        record.provenance[0].to_string() != string(m, "provenance"))
        return invalid("visual_evidence_record_provenance");
    return result;
}
} // namespace mirage::integration
