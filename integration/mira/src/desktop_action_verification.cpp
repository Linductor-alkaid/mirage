#include "desktop_action_verification.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <mira/observation.hpp>
#include <stdexcept>

namespace mirage::integration::detail {
namespace {
using mira::JsonValue;
std::string field(const JsonValue &value, const char *name) {
    const auto *v = value.find(name);
    return v && v->as_string() ? *v->as_string() : "";
}
bool flag(const JsonValue &value, const char *name) {
    const auto *v = value.find(name);
    return v && v->as_boolean().value_or(false);
}
mira::Error error(mira::ErrorCode code, const char *message) {
    mira::Error e;
    e.code = code;
    e.domain = "mirage.desktop.verification";
    e.safe_message = message;
    return e;
}
bool same(const mirage::desktop::WindowGeometry &a, const mirage::desktop::WindowGeometry &b) {
    return a.x == b.x && a.y == b.y && a.width == b.width && a.height == b.height;
}
bool contains(const mirage::desktop::WindowGeometry &outer,
              const mirage::desktop::WindowGeometry &inner) {
    return outer.width > 0 && outer.height > 0 && inner.width > 0 && inner.height > 0 &&
           inner.x >= outer.x && inner.y >= outer.y &&
           std::int64_t{inner.x} + inner.width <= std::int64_t{outer.x} + outer.width &&
           std::int64_t{inner.y} + inner.height <= std::int64_t{outer.y} + outer.height;
}
} // namespace

DesktopActionVerification::DesktopActionVerification(DesktopToolContext desktop,
                                                     mira::OperationContext owner,
                                                     mirage::desktop::CancelToken cancel)
    : desktop_(std::move(desktop)), owner_(std::move(owner)),
      cancel_([owner = owner_, parent = std::move(cancel)] {
          return parent.cancelled() || owner.cancelled_or_expired(mira::Timestamp::now());
      }) {}
bool DesktopActionVerification::mutation(const std::string &name) {
    return name.starts_with("desktop.session.") || name == "desktop.vision.click_region" ||
           name == "desktop.vision.click" || name == "desktop.vision.scroll" ||
           name == "desktop.vision.drag" || name == "desktop.window.activate" ||
           name == "desktop.input.click" || name == "desktop.input.key" ||
           name == "desktop.input.type_text" || name == "desktop.application.launch" ||
           name == "desktop.application.open_uri";
}
JsonValue DesktopActionVerification::summary(const Record &r) const {
    return JsonValue{
        JsonValue::Object{{"action_id", r.id},
                          {"action", r.action},
                          {"window_id", r.window},
                          {"application_id", r.application_id},
                          {"status", r.status},
                          {"reason", r.reason},
                          {"verify_kind", r.kind},
                          {"verified", r.status == "verified"},
                          {"dispatched", r.dispatched},
                          {"side_effect_may_have_occurred", r.uncertain},
                          {"condition_digest", r.condition_digest.to_string()},
                          {"evidence_id", r.evidence},
                          {"observed_digest", r.observed_digest},
                          {"captured_at_ms", r.captured_at_ms},
                          {"visual_scope", r.visual_scope},
                          {"display_id", r.display_id},
                          {"model_revision", r.model_revision},
                          {"ocr_backend_calls", static_cast<std::int64_t>(r.backend_calls)},
                          {"environment_epoch", static_cast<std::int64_t>(r.epoch)}}};
}

bool DesktopActionVerification::observe_text(
    Record &r, const mirage::desktop::WindowGeometry &window,
    const mirage::desktop::CancelToken &bounded, std::chrono::steady_clock::time_point end,
    const mira::OperationContext &ctx, std::int64_t &matching_nodes, std::string &visual_digest) {
    const auto fail = [&](const char *reason) {
        r.reason = reason;
        return false;
    };
    if (visual_observations_ >= 64)
        return fail("visual_observation_budget_exceeded");
    ++visual_observations_;
    if (!desktop_.ocr || !desktop_.environment->screen())
        return fail("visual_unavailable");
    const AtomCancelProbe cancelled = [&] { return bounded.cancelled(); };
    const bool allowed =
        desktop_.authorize
            ? desktop_.authorize("screen.capture", "ocr:configured-display", cancelled,
                                 ctx.task.to_string(), ctx.operation.to_string())
            : desktop_.gate && desktop_.gate("screen.capture", "ocr:configured-display", cancelled);
    if (bounded.cancelled())
        return fail("cancelled_or_expired");
    if (!allowed)
        return fail("visual_permission_denied");
    const auto begin = std::chrono::steady_clock::now();
    DesktopOcrResult read;
    try {
        read = desktop_.ocr(bounded, end);
    } catch (...) {
        return fail("visual_analysis_failed");
    }
    const auto finish = std::chrono::steady_clock::now();
    if (bounded.cancelled() || read.cancelled)
        return fail("cancelled_or_expired");
    if (!read.ok)
        return fail("visual_analysis_failed");
    if (read.capture_begin < begin || read.capture_end < read.capture_begin ||
        read.capture_end > finish || read.capture_end > end ||
        read.backend_calls <= last_backend_calls_ ||
        std::find(visual_scopes_.begin(), visual_scopes_.end(), read.snapshot.scope_ref) !=
            visual_scopes_.end())
        return fail("visual_evidence_not_fresh");
    const auto identity = [](const std::string &s, std::size_t maximum) {
        return !s.empty() && s.size() <= maximum && mirage::desktop::is_valid_utf8(s);
    };
    if (!identity(read.snapshot.scope_ref, 128) || !identity(read.display_id, 256) ||
        !identity(read.model_revision, 128) || read.snapshot.regions.size() > 128 ||
        read.backend_calls > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()))
        return fail("visual_evidence_invalid");
    last_backend_calls_ = read.backend_calls;
    visual_scopes_.push_back(read.snapshot.scope_ref);
    if ((!r.model_revision.empty() &&
         (r.model_revision != read.model_revision || r.display_id != read.display_id ||
          !same(r.display_geometry, read.display_geometry))) ||
        !contains(read.display_geometry, window))
        return fail("visual_source_changed_or_clipped");
    JsonValue::Array facts;
    std::int64_t page_matches = 0, all_text_matches = 0;
    bool page_valid = false;
    std::vector<std::string> refs;
    for (const auto &region : read.snapshot.regions) {
        if (!identity(region.ref, 32) || region.text.size() > 1024 ||
            !mirage::desktop::is_valid_utf8(region.text) || !std::isfinite(region.confidence) ||
            region.confidence < 0 || region.confidence > 1 || region.bounds.width <= 0 ||
            region.bounds.height <= 0 ||
            std::find(refs.begin(), refs.end(), region.ref) != refs.end())
            return fail("visual_evidence_invalid");
        refs.push_back(region.ref);
        if (region.source == mirage::desktop::VisualRegionSource::kOcr &&
            region.confidence >= 0.8 && region.text == r.text && contains(window, region.bounds))
            ++matching_nodes;
        if (region.source == mirage::desktop::VisualRegionSource::kOcr &&
            contains(window, region.bounds)) {
            if (region.text == r.text)
                ++all_text_matches;
            if (region.text == r.page_text) {
                ++page_matches;
                page_valid = region.confidence >= 0.8;
            }
        }
        facts.emplace_back(JsonValue::Array{
            region.ref, static_cast<std::int64_t>(region.source),
            mira::digest_string(region.text).to_string(), region.confidence,
            std::int64_t{region.bounds.x}, std::int64_t{region.bounds.y},
            std::int64_t{region.bounds.width}, std::int64_t{region.bounds.height}});
    }
    if (!r.page_text.empty() &&
        (page_matches != 1 || !page_valid || all_text_matches != matching_nodes))
        return fail("visual_page_identity_changed");
    r.visual_scope = read.snapshot.scope_ref;
    r.display_id = read.display_id;
    r.model_revision = read.model_revision;
    r.display_geometry = read.display_geometry;
    r.backend_calls = read.backend_calls;
    visual_digest =
        mira::canonical_json_digest(
            JsonValue{JsonValue::Array{
                r.visual_scope, r.display_id, r.model_revision,
                static_cast<std::int64_t>(r.backend_calls), std::int64_t{read.display_geometry.x},
                std::int64_t{read.display_geometry.y}, std::int64_t{read.display_geometry.width},
                std::int64_t{read.display_geometry.height}, JsonValue{std::move(facts)}}})
            .to_string();
    return true;
}

void DesktopActionVerification::observe(Record &r, const mira::OperationContext &ctx) {
    if (!r.dispatched && !r.uncertain)
        return;
    r.status = r.uncertain ? "uncertain" : "unverified";
    r.evidence.clear();
    r.observed_digest.clear();
    r.captured_at_ms = 0;
    const auto begin = mira::Timestamp::now();
    const auto budget = (r.kind == "visual_text_exists" || r.kind == "display_text_exists" ||
                         r.kind.starts_with("shared_"))
                            ? std::chrono::seconds{5}
                            : std::chrono::seconds{1};
    const auto end =
        std::min({begin.monotonic + budget, owner_.deadline.value_or(begin.monotonic + budget),
                  ctx.deadline.value_or(begin.monotonic + budget)});
    const mirage::desktop::CancelToken bounded([this, end, ctx] {
        return cancel_.cancelled() || ctx.cancelled_or_expired(mira::Timestamp::now()) ||
               std::chrono::steady_clock::now() >= end;
    });
    try {
        if (bounded.cancelled()) {
            r.reason = "cancelled_or_expired";
            return;
        }
        if (!desktop_.actions || !desktop_.environment || r.epoch != desktop_.actions->epoch()) {
            r.reason = "environment_changed";
            return;
        }
        if (r.kind == "shared_frame_observed" || r.kind == "shared_text_exists") {
            if (!shared_observation_ || r.uncertain) {
                r.reason = "effect_uncertain";
                return;
            }
            auto local = ctx;
            local.deadline = end;
            auto proof = shared_observation_(r.kind, r.text, local);
            if (!proof || bounded.cancelled() || r.epoch != desktop_.actions->epoch()) {
                r.reason = "shared_observation_failed";
                if (!proof) {
                    // The receipt is the only place the model can read why the
                    // shared frame died; keep the underlying capture diagnostic
                    // visible instead of dropping it.
                    auto message = proof.error().safe_message;
                    message.erase(std::find_if(message.begin(), message.end(),
                                               [](char c) { return c == '\n' || c == '\r'; }),
                                  message.end());
                    if (message.size() > 160)
                        message.resize(160);
                    r.reason += ": " + message;
                }
                return;
            }
            const bool matched = flag(proof.value(), "matched");
            r.evidence = mira::ObservationId::generate().to_string();
            r.observed_digest = field(proof.value(), "digest");
            r.captured_at_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                   std::chrono::system_clock::now().time_since_epoch())
                                   .count();
            r.status = matched && !r.observed_digest.empty() ? "verified" : "unverified";
            r.reason = !matched                            ? "condition_not_matched"
                       : r.kind == "shared_frame_observed" ? "fresh_shared_frame_only"
                                                           : "shared_text_matched_only";
            return;
        }
        if (r.kind == "application_running") {
            auto *application = desktop_.environment->application();
            if (!application) {
                r.reason = "application_unavailable";
                return;
            }
            const auto queried = application->running_state(r.application_id, bounded);
            if (!queried.ok || bounded.cancelled() || r.epoch != desktop_.actions->epoch() ||
                queried.instance_id.size() > 256) {
                r.reason = "application_observation_failed";
                return;
            }
            r.evidence = mira::ObservationId::generate().to_string();
            r.observed_digest =
                mira::canonical_json_digest(
                    JsonValue{JsonValue::Array{r.application_id, queried.running,
                                               queried.instance_id, r.condition_digest.to_string(),
                                               static_cast<std::int64_t>(r.epoch)}})
                    .to_string();
            r.captured_at_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                   std::chrono::system_clock::now().time_since_epoch())
                                   .count();
            const bool matched = queried.running && !queried.instance_id.empty();
            // DEC-086: an uncertain dispatch is a fence against unprovable
            // effects, not against evidence — when its declared postcondition
            // is machine-verified afterwards, the record settles and the turn
            // can continue. Effects that stay unproven keep awaiting review.
            r.status = matched ? "verified" : r.uncertain ? "uncertain" : "unverified";
            r.reason = matched       ? (r.uncertain ? "application_running_confirmed"
                                                    : "application_running_only")
                       : r.uncertain ? "effect_uncertain"
                                     : "condition_not_matched";
            return;
        }
        if (!desktop_.environment->window()) {
            r.reason = "window_unavailable";
            return;
        }
        auto *window = desktop_.environment->window();
        const auto before = window->front_window(bounded);
        if (r.kind == "foreground_changed" || r.kind == "foreground_title_equals" ||
            r.kind == "display_text_exists") {
            if (!before.ok || (before.found && before.window.title.size() > (64u << 10))) {
                r.reason = "foreground_unavailable";
                return;
            }
            const auto equal_front = [&](const auto &a, const auto &b) {
                return a.found == b.found &&
                       (!a.found ||
                        (a.window.id == b.window.id && a.window.title == b.window.title &&
                         same(a.window.geometry, b.window.geometry)));
            };
            bool matched = r.kind == "foreground_changed"
                               ? !equal_front(before, r.initial_front)
                               : before.found && before.window.title == r.title;
            std::int64_t matching = 0;
            std::string visual;
            if (r.kind == "display_text_exists") {
                auto *screen = desktop_.environment->screen();
                const auto displays = screen->list_displays({}, bounded);
                if (!displays.ok || std::none_of(displays.displays.begin(), displays.displays.end(),
                                                 [&](const auto &d) {
                                                     return d.id == r.display_id &&
                                                            same(d.geometry, r.display_geometry);
                                                 })) {
                    r.reason = "visual_source_changed_or_clipped";
                    return;
                }
                const auto fixed_display = r.display_id;
                const auto fixed_geometry = r.display_geometry;
                if (!observe_text(r, fixed_geometry, bounded, end, ctx, matching, visual))
                    return;
                const auto after_displays = screen->list_displays({}, bounded);
                if (!after_displays.ok ||
                    std::none_of(after_displays.displays.begin(), after_displays.displays.end(),
                                 [&](const auto &d) {
                                     return d.id == fixed_display &&
                                            same(d.geometry, fixed_geometry);
                                 })) {
                    r.reason = "visual_source_changed_or_clipped";
                    return;
                }
                if (r.display_id != fixed_display || !same(r.display_geometry, fixed_geometry)) {
                    r.display_id = fixed_display;
                    r.display_geometry = fixed_geometry;
                    r.reason = "visual_source_changed_or_clipped";
                    return;
                }
                matched = matching == 1;
            }
            const auto after = window->front_window(bounded);
            if (!after.ok || !equal_front(before, after) || bounded.cancelled() ||
                r.epoch != desktop_.actions->epoch()) {
                r.reason = "observation_changed_or_expired";
                return;
            }
            r.evidence = mira::ObservationId::generate().to_string();
            r.observed_digest =
                mira::canonical_json_digest(
                    JsonValue{JsonValue::Array{before.found, before.window.id, before.window.title,
                                               std::int64_t{before.window.geometry.x},
                                               std::int64_t{before.window.geometry.y},
                                               std::int64_t{before.window.geometry.width},
                                               std::int64_t{before.window.geometry.height},
                                               r.condition_digest.to_string(), visual,
                                               static_cast<std::int64_t>(r.epoch)}})
                    .to_string();
            r.captured_at_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                   std::chrono::system_clock::now().time_since_epoch())
                                   .count();
            r.status = r.uncertain ? "uncertain" : matched ? "verified" : "unverified";
            r.reason = r.uncertain ? "effect_uncertain"
                       : matched   ? "condition_matched"
                                   : "condition_not_matched";
            return;
        }
        if (!before.ok || !before.found || before.window.id != r.window ||
            before.window.title.size() > (64u << 10) ||
            (!r.page_title.empty() && before.window.title != r.page_title)) {
            r.reason = "focus_changed_or_unavailable";
            return;
        }
        bool matched = r.kind == "window_focused";
        std::int64_t matching_nodes = 0;
        std::string visual_digest;
        if (r.kind == "window_title_equals")
            matched = before.window.title == r.title;
        if (r.kind == "semantic_node_exists") {
            auto *accessibility = desktop_.environment->accessibility();
            if (!accessibility) {
                r.reason = "semantic_unavailable";
                return;
            }
            const auto tree = accessibility->semantic_snapshot(r.window, {4096}, bounded);
            if (!tree.ok || tree.cancelled || tree.snapshot.nodes.size() > 4096) {
                r.reason = "semantic_unavailable";
                return;
            }
            if ((!r.application.empty() && tree.snapshot.application != r.application) ||
                (!r.page_title.empty() && tree.snapshot.window_title != r.page_title)) {
                r.reason = "knowledge_identity_mismatch";
                return;
            }
            const auto count =
                std::count_if(tree.snapshot.nodes.begin(), tree.snapshot.nodes.end(),
                              [&](const auto &n) { return n.role == r.role && n.name == r.name; });
            matched = count == 1;
            matching_nodes = count;
        }
        if (r.kind == "visual_text_exists") {
            if (!observe_text(r, before.window.geometry, bounded, end, ctx, matching_nodes,
                              visual_digest))
                return;
            matched = matching_nodes == 1;
        }
        const auto after = window->front_window(bounded);
        if (!after.ok || !after.found || after.window.id != r.window ||
            !same(before.window.geometry, after.window.geometry) ||
            before.window.title != after.window.title || bounded.cancelled() ||
            r.epoch != desktop_.actions->epoch()) {
            r.reason = "observation_changed_or_expired";
            return;
        }
        // Reuse the pinned freshness/quality evaluator; no atomic OS transaction claim.
        mira::Observation observed;
        observed.id = mira::ObservationId::generate();
        observed.session_id = owner_.session;
        observed.environment_epoch = r.epoch;
        observed.foreground.emplace();
        auto &component = *observed.foreground;
        component.environment_epoch = r.epoch;
        component.value.activity_name = before.window.title;
        component.value.sensitive = true;
        component.provenance.source = desktop_.environment->info().name;
        component.provenance.method =
            r.kind == "visual_text_exists" ? "post-action-fresh-ocr" : "post-action-window-query";
        component.capture.normalized_begin = begin;
        component.capture.normalized_end = mira::Timestamp::now();
        observed.aggregate_span = component.capture;
        mira::ObservationRequest request;
        request.mode = mira::ObservationMode::Verification;
        request.required.foreground = true;
        request.max_age = std::chrono::duration_cast<std::chrono::milliseconds>(budget);
        const auto evaluated =
            mira::evaluate_observation(observed, request, mira::Timestamp::now());
        if (!evaluated.satisfies_request || !evaluated.fresh_enough) {
            r.reason = "observation_invalid";
            return;
        }
        r.evidence = observed.id.to_string();
        r.observed_digest =
            mira::canonical_json_digest(
                JsonValue{JsonValue::Array{before.window.id, before.window.title,
                                           std::int64_t{before.window.geometry.x},
                                           std::int64_t{before.window.geometry.y},
                                           std::int64_t{before.window.geometry.width},
                                           std::int64_t{before.window.geometry.height},
                                           matching_nodes, r.condition_digest.to_string(),
                                           static_cast<std::int64_t>(r.epoch), visual_digest}})
                .to_string();
        r.captured_at_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                               std::chrono::system_clock::now().time_since_epoch())
                               .count();
        r.status = r.uncertain ? "uncertain" : matched ? "verified" : "unverified";
        r.reason = r.uncertain      ? "effect_uncertain"
                   : r.kind.empty() ? "condition_missing"
                   : matched        ? "condition_matched"
                                    : "condition_not_matched";
    } catch (...) {
        r.reason = "observation_failed";
    }
}

std::string DesktopActionVerification::failure_receipt(const Record &r, std::string message,
                                                       const std::string &invalid_field,
                                                       std::string code,
                                                       const std::string &refreshed_scope) {
    // Pinned ToolExecutor caps safe_error_summary at 512 bytes. Keep the
    // canonical receipt valid; the UI and action.verify get the full record.
    // 256 leaves room for the shared-input rejection's embedded observation
    // JSON (with refreshed_image_scope) while the >512 fallback below still
    // degrades oversized provider errors to the short pointer text.
    conversation::truncate_text(message, 256);
    JsonValue receipt{JsonValue::Object{{"schema", 2},
                                        {"status", r.status},
                                        {"verified", false},
                                        {"dispatched", r.dispatched},
                                        {"side_effect_may_have_occurred", r.uncertain},
                                        {"input_released", r.input_released},
                                        {"action_id", r.id},
                                        {"reason", r.reason},
                                        {"message", message}}};
    if (!invalid_field.empty())
        receipt.set("field", invalid_field);
    conversation::truncate_text(code, 64);
    if (!code.empty())
        receipt.set("error_code", code);
    if (!refreshed_scope.empty())
        receipt.set("refreshed_image_scope", refreshed_scope);
    auto encoded = mira::to_json_string(receipt);
    if (encoded.size() > 512) {
        // Escaping control characters can expand even a short provider error.
        receipt.set("message",
                    "Desktop action failed; inspect desktop.action.verify with action_id.");
        encoded = mira::to_json_string(receipt);
        if (encoded.size() > 512) {
            receipt.set("error_code", "provider_error");
            encoded = mira::to_json_string(receipt);
        }
    }
    return encoded;
}

mira::Result<JsonValue>
DesktopActionVerification::execute(const std::string &name, const JsonValue &args,
                                   const mira::OperationContext &ctx,
                                   const mira::BuiltinToolHandler &handler) {
    if (ctx.task != owner_.task || ctx.session != owner_.session ||
        ctx.task_epoch != owner_.task_epoch)
        return error(mira::ErrorCode::InvalidArgument, "desktop verification owner mismatch");
    if (cancel_.cancelled() || ctx.cancelled_or_expired(mira::Timestamp::now()))
        return error(mira::ErrorCode::Cancelled, "desktop verification cancelled");
    if (fenced_ && desktop_.actions && !desktop_.actions->input_released())
        return error(mira::ErrorCode::InvalidArgument,
                     "input_release_failed: human review required");
    if (canonical_failure_ || fenced_ ||
        (!records_.empty() && records_.back().status != "verified" &&
         !(records_.back().status == "rejected" && !records_.back().dispatched &&
           !records_.back().uncertain)))
        return error(mira::ErrorCode::InvalidArgument,
                     "verify the previous action; uncertain effects require human review");
    if (records_.size() >= 32)
        return error(mira::ErrorCode::ResourceExhausted, "desktop action budget exceeded");
    Record r;
    r.id = mira::OperationId::generate().to_string();
    r.action = name;
    r.window = field(args, "window_id");
    r.display_id = field(args, "display_id");
    const bool display_region =
        name == "desktop.vision.click_region" && field(args, "target_context") == "display";
    const bool shared_action = name.starts_with("desktop.session.");
    const bool application_action =
        name == "desktop.application.launch" || name == "desktop.application.open_uri";
    r.application_id = application_action ? field(args, "application_id") : "";
    r.kind = field(args, "verify_kind");
    r.title = field(args, "expected_title");
    r.role = field(args, "expected_role");
    r.name = field(args, "expected_name");
    r.text = field(args, "expected_text");
    r.application = field(args, "expected_application");
    r.page_title = field(args, "expected_page_title");
    r.page_text = field(args, "expected_page_text");
    if (application_action && r.kind.empty())
        r.kind = "application_running";
    if (r.kind.empty() && name == "desktop.window.activate")
        r.kind = "window_focused";
    const auto reject_before_dispatch =
        [&](const char *message, const std::string &invalid_field = {}) -> mira::Result<JsonValue> {
        r.status = "rejected";
        r.reason = "validation_failed_before_dispatch";
        r.input_released = true;
        auto failure = error(mira::ErrorCode::InvalidArgument, message);
        failure.safe_message = failure_receipt(r, message, invalid_field, {}, {});
        records_.push_back(std::move(r));
        return failure;
    };
    const auto valid_text = [](const std::string &s) {
        return s.size() <= 1024 && mirage::desktop::is_valid_utf8(s);
    };
    for (const auto *key :
         {"verify_kind", "expected_title", "expected_role", "expected_name", "expected_text"})
        if (const auto *value = args.find(key); value && !value->as_string())
            return reject_before_dispatch(
                "Postcondition field must be a string; omit unused fields.", key);
    if (application_action && (r.application_id.empty() || r.application_id.size() > 256 ||
                               !mirage::desktop::is_valid_utf8(r.application_id)))
        return reject_before_dispatch("application_id must be a nonempty bounded UTF-8 string.",
                                      "application_id");
    if (application_action &&
        (r.kind != "application_running" || !r.window.empty() || !r.application.empty() ||
         !r.page_title.empty() || !r.page_text.empty()))
        return reject_before_dispatch(
            "Application actions have only the fixed application_running condition.",
            "verify_kind");
    if (!application_action && !display_region && !shared_action &&
        (r.window.empty() || r.window.size() > 256 || !mirage::desktop::is_valid_utf8(r.window)))
        return reject_before_dispatch("window_id must be a nonempty bounded UTF-8 string.",
                                      "window_id");
    for (const auto *key : {"expected_title", "expected_role", "expected_name", "expected_text",
                            "expected_application", "expected_page_title", "expected_page_text"})
        if (!valid_text(field(args, key)))
            return reject_before_dispatch("Postcondition field exceeds its UTF-8/1024-byte limit.",
                                          key);
    if (shared_action && (!shared_observation_ ||
                          !(r.kind == "shared_frame_observed" || r.kind == "shared_text_exists")))
        return reject_before_dispatch(
            "Shared input requires shared_frame_observed or shared_text_exists.", "verify_kind");
    if (!(r.kind.empty() ||
          (shared_action &&
           (r.kind == "shared_frame_observed" || r.kind == "shared_text_exists")) ||
          r.kind == "window_focused" || r.kind == "window_title_equals" ||
          r.kind == "semantic_node_exists" || r.kind == "visual_text_exists" ||
          (application_action && r.kind == "application_running") ||
          r.kind == "foreground_changed" || r.kind == "foreground_title_equals" ||
          r.kind == "display_text_exists"))
        return reject_before_dispatch("Unsupported verify_kind; choose a declared condition.",
                                      "verify_kind");
    if (display_region && !(r.kind == "foreground_changed" || r.kind == "foreground_title_equals" ||
                            r.kind == "display_text_exists"))
        return reject_before_dispatch(
            "Display actions need an explicit foreground/display condition.", "verify_kind");
    for (const auto &[key, used] :
         {std::pair{"expected_title",
                    r.kind == "window_title_equals" || r.kind == "foreground_title_equals"},
          std::pair{"expected_role", r.kind == "semantic_node_exists"},
          std::pair{"expected_name", r.kind == "semantic_node_exists"},
          std::pair{"expected_text", r.kind == "visual_text_exists" ||
                                         r.kind == "display_text_exists" ||
                                         r.kind == "shared_text_exists"}})
        if (!used && !field(args, key).empty())
            return reject_before_dispatch(
                "Field is unused for this verify_kind; omit it. No action dispatched.", key);
    if ((r.kind == "window_title_equals" || r.kind == "foreground_title_equals") &&
        !args.find("expected_title"))
        return reject_before_dispatch("window_title_equals requires expected_title.",
                                      "expected_title");
    if (r.kind == "semantic_node_exists" && r.role.empty())
        return reject_before_dispatch("semantic_node_exists requires a nonempty expected_role.",
                                      "expected_role");
    if (r.kind == "semantic_node_exists" && !args.find("expected_name"))
        return reject_before_dispatch("semantic_node_exists requires expected_name.",
                                      "expected_name");
    if ((r.kind == "visual_text_exists" || r.kind == "display_text_exists" ||
         r.kind == "shared_text_exists") &&
        r.text.empty())
        return reject_before_dispatch("visual_text_exists requires nonempty expected_text.",
                                      "expected_text");
    if ((r.kind == "visual_text_exists" || r.kind == "display_text_exists") &&
        (!desktop_.ocr || !desktop_.actions || !desktop_.environment ||
         !desktop_.environment->screen() || !desktop_.environment->window()))
        return reject_before_dispatch(
            "visual postcondition unavailable; enable OCR or choose an available condition");
    r.condition_digest = mira::digest_string(
        mira::to_json_string(JsonValue{JsonValue::Array{r.kind, r.title, r.role, r.name, r.text}}));
    if (application_action)
        r.condition_digest = mira::canonical_json_digest(JsonValue{JsonValue::Array{
            r.kind, r.application_id, mira::digest_string(field(args, "uri")).to_string()}});
    if (!r.application.empty() || !r.page_title.empty())
        r.condition_digest = mira::canonical_json_digest(JsonValue{
            JsonValue::Array{r.condition_digest.to_string(), r.application, r.page_title}});
    if (!r.page_text.empty())
        r.condition_digest = mira::canonical_json_digest(
            JsonValue{JsonValue::Array{r.condition_digest.to_string(), r.page_text}});
    if (r.kind == "foreground_changed" || r.kind == "foreground_title_equals" ||
        r.kind == "display_text_exists") {
        const auto initial_end = std::min(
            ctx.deadline.value_or(std::chrono::steady_clock::now() + std::chrono::seconds{1}),
            std::chrono::steady_clock::now() + std::chrono::seconds{1});
        const auto bounded = mirage::desktop::CancelToken([this, ctx, initial_end] {
            return cancel_.cancelled() || ctx.cancelled_or_expired(mira::Timestamp::now()) ||
                   std::chrono::steady_clock::now() >= initial_end;
        });
        if (!desktop_.environment || !desktop_.environment->window())
            return reject_before_dispatch("Foreground condition unavailable.", "verify_kind");
        r.initial_front = desktop_.environment->window()->front_window(bounded);
        if (!r.initial_front.ok || bounded.cancelled() ||
            (r.initial_front.found && (r.initial_front.window.id.size() > 256 ||
                                       r.initial_front.window.title.size() > (64u << 10))))
            return reject_before_dispatch("Initial foreground unavailable.", "verify_kind");
        if (r.kind == "display_text_exists") {
            if (r.display_id.empty() || r.display_id.size() > 256)
                return reject_before_dispatch("display_text_exists requires display_id.",
                                              "display_id");
            const auto displays = desktop_.environment->screen()->list_displays({}, bounded);
            const auto found = std::find_if(displays.displays.begin(), displays.displays.end(),
                                            [&](const auto &d) { return d.id == r.display_id; });
            if (!displays.ok || found == displays.displays.end() || bounded.cancelled())
                return reject_before_dispatch("Display condition unavailable.", "display_id");
            r.display_geometry = found->geometry;
        }
        r.condition_digest = mira::canonical_json_digest(JsonValue{JsonValue::Array{
            r.condition_digest.to_string(), r.initial_front.found, r.initial_front.window.id,
            r.initial_front.window.title, std::int64_t{r.initial_front.window.geometry.x},
            std::int64_t{r.initial_front.window.geometry.y},
            std::int64_t{r.initial_front.window.geometry.width},
            std::int64_t{r.initial_front.window.geometry.height}, r.display_id,
            std::int64_t{r.display_geometry.x}, std::int64_t{r.display_geometry.y},
            std::int64_t{r.display_geometry.width}, std::int64_t{r.display_geometry.height}}});
    }
    auto result = [&]() -> mira::Result<JsonValue> {
        try {
            return handler(args, ctx);
        } catch (...) {
            fenced_ = true;
            r.uncertain = true;
            return error(mira::ErrorCode::PlatformError, "desktop tool handler failed");
        }
    }();
    if (result) {
        r.input_released = flag(result.value(), "input_released");
        r.dispatched = field(result.value(), "status") == "dispatched";
        if (!r.dispatched || !flag(result.value(), "input_released")) {
            const auto *effects = result.value().find("side_effect_may_have_occurred");
            r.uncertain = !effects || !effects->as_boolean() || *effects->as_boolean() ||
                          !flag(result.value(), "input_released");
            r.status = r.uncertain ? "uncertain" : "rejected";
            r.reason = "action_not_completed";
        }
    } else {
        const auto parsed = mira::parse_json(result.error().safe_message);
        r.input_released = parsed && flag(parsed.value(), "input_released");
        const auto *effects =
            parsed ? parsed.value().find("side_effect_may_have_occurred") : nullptr;
        r.uncertain = r.uncertain || !effects || !effects->as_boolean() || *effects->as_boolean() ||
                      !flag(parsed.value(), "input_released");
        r.status = r.uncertain ? "uncertain" : "rejected";
        r.reason = "action_failed";
    }
    fenced_ = fenced_ || r.uncertain;
    r.epoch = desktop_.actions ? desktop_.actions->epoch() : 0;
    observe(r, ctx);
    // At most one uncertain record can exist at a time (a new mutation needs
    // records_.back() verified or cleanly rejected), so machine evidence on
    // the newest record is exactly the fence's release condition (DEC-086).
    if (r.status == "verified")
        fenced_ = false;
    records_.push_back(std::move(r));
    const auto &record = records_.back();
    if (result) {
        auto object = *result.value().as_object();
        for (auto &[key, value] : object)
            if (key == "verified")
                value = JsonValue{record.status == "verified"};
        object.emplace_back("action_id", record.id);
        object.emplace_back("verification", summary(record));
        return JsonValue{std::move(object)};
    }
    auto failure = result.error();
    const auto provider_receipt = mira::parse_json(failure.safe_message);
    const auto provider_message =
        provider_receipt ? field(provider_receipt.value(), "message") : "";
    const auto provider_code =
        provider_receipt ? field(provider_receipt.value(), "error_code") : "";
    const auto provider_refreshed_scope =
        provider_receipt ? field(provider_receipt.value(), "refreshed_image_scope") : "";
    failure.safe_message =
        failure_receipt(record, provider_message.empty() ? failure.safe_message : provider_message,
                        {}, provider_code, provider_refreshed_scope);
    return failure;
}

mira::Result<JsonValue> DesktopActionVerification::recheck(const std::string &action_id,
                                                           const mira::OperationContext &ctx) {
    if (cancel_.cancelled() || ctx.cancelled_or_expired(mira::Timestamp::now()))
        return error(mira::ErrorCode::Cancelled, "desktop verification cancelled");
    if (ctx.task != owner_.task || ctx.session != owner_.session ||
        ctx.task_epoch != owner_.task_epoch || records_.empty() || action_id != records_.back().id)
        return error(mira::ErrorCode::InvalidArgument, "foreign or retired action identity");
    observe(records_.back(), ctx);
    // DEC-086: fresh machine evidence on the pending action releases the
    // mutation fence; the next gate check re-examines the record anyway.
    if (records_.back().status == "verified")
        fenced_ = false;
    return summary(records_.back());
}

void DesktopActionVerification::register_tool(mira::BuiltinToolRegistry &registry,
                                              const DesktopAtomToolset::Decorator &decorate) {
    if (!desktop_.environment || !desktop_.actions || !desktop_.environment->window())
        return;
    mira::BuiltinToolSpec spec;
    spec.wire_name = "desktop.action.verify";
    spec.description = "Read fresh evidence for the latest action_id and its fixed postcondition. "
                       "No input is repeated. Verify before another mutation or declaring success.";
    JsonValue::Object properties;
    properties.emplace_back(
        "action_id",
        JsonValue{JsonValue::Object{{"type", "string"}, {"maxLength", std::int64_t{128}}}});
    JsonValue::Object schema;
    schema.emplace_back("type", "object");
    schema.emplace_back("properties", JsonValue{std::move(properties)});
    schema.emplace_back("required", JsonValue{JsonValue::Array{"action_id"}});
    schema.emplace_back("additionalProperties", false);
    spec.parameters_schema = mira::JsonSchema{JsonValue{std::move(schema)}};
    mira::BuiltinToolHandler handler = [this](const auto &args,
                                              const auto &ctx) -> mira::Result<JsonValue> {
        return recheck(field(args, "action_id"), ctx);
    };
    if (decorate)
        handler = decorate(spec.wire_name, std::move(handler));
    if (!registry.register_tool(std::move(spec), std::move(handler)))
        throw std::logic_error("desktop verification registration rejected");
}
mira::Result<std::vector<mira::ModelInputItem>>
DesktopActionVerification::supply(const mira::AgentLoopSpec &spec) const {
    if (spec.task_id != owner_.task || spec.session_id != owner_.session ||
        spec.task_epoch != owner_.task_epoch || cancel_.cancelled())
        return error(mira::ErrorCode::Cancelled, "desktop verification supply invalidated");
    if (records_.empty())
        return std::vector<mira::ModelInputItem>{};
    JsonValue::Array results;
    for (const auto &record : records_)
        results.push_back(summary(record));
    mira::ModelInputItem item;
    item.role = mira::ModelRole::User;
    item.content.emplace_back(
        mira::TextPart{"Untrusted host postcondition evidence (local conditions only, not "
                       "proof of arbitrary task success): " +
                           mira::to_json_string(JsonValue{std::move(results)}),
                       mira::Sensitivity::Internal});
    return std::vector<mira::ModelInputItem>{std::move(item)};
}
bool DesktopActionVerification::no_effect_rejection() const {
    return !canonical_failure_ && !ocr_failed_ && !fenced_ && !records_.empty() &&
           records_.back().status == "rejected" &&
           std::all_of(records_.begin(), records_.end(), [](const auto &r) {
               return r.status == "verified" ||
                      (r.status == "rejected" && !r.dispatched && !r.uncertain && r.input_released);
           });
}
bool DesktopActionVerification::settle() {
    if (!records_.empty())
        observe(records_.back(), owner_);
    return !canonical_failure_ && !ocr_failed_ && !fenced_ &&
           (records_.empty() || records_.back().status == "verified") &&
           std::all_of(records_.begin(), records_.end(), [](const auto &r) {
               return r.status == "verified" ||
                      (r.status == "rejected" && !r.dispatched && !r.uncertain);
           });
}
void DesktopActionVerification::project(std::vector<conversation::Part> &parts) const {
    std::size_t total = 0;
    for (const auto &part : parts)
        total += part.text.size() + part.input.size() + part.output.size();
    for (auto &part : parts) {
        if (!mutation(part.name) || part.output.empty())
            continue;
        auto parsed = mira::parse_json(part.output);
        if (!parsed || !parsed.value().as_object())
            continue;
        const auto id = field(parsed.value(), "action_id");
        const auto record = std::find_if(records_.begin(), records_.end(),
                                         [&](const auto &r) { return r.id == id; });
        if (record == records_.end())
            continue;
        auto object = *parsed.value().as_object();
        if (!parsed.value().find("verification"))
            object.emplace_back("verification", summary(*record));
        if (!parsed.value().find("window_id"))
            object.emplace_back("window_id", record->window);
        for (auto &[key, value] : object) {
            if (key == "verification")
                value = summary(*record);
            if (key == "verified")
                value = JsonValue{record->status == "verified"};
        }
        total -= part.output.size();
        part.output = mira::to_json_string(JsonValue{std::move(object)});
        const auto available =
            total < conversation::max_content_bytes ? conversation::max_content_bytes - total : 0;
        part.truncated = conversation::truncate_text(
                             part.output, std::min(available, conversation::max_part_text)) ||
                         part.truncated;
        total += part.output.size();
    }
}
mira::Result<void> DesktopActionVerification::validate_request(const mira::ModelRequest &request,
                                                               const mira::OperationContext &ctx) {
    if (cancel_.cancelled() || ctx.cancelled_or_expired(mira::Timestamp::now()))
        return error(mira::ErrorCode::Cancelled, "desktop verification request cancelled");
    if (request.task_id != owner_.task || request.task_epoch != owner_.task_epoch ||
        ctx.task != owner_.task || ctx.session != owner_.session ||
        ctx.task_epoch != owner_.task_epoch)
        return error(mira::ErrorCode::InvalidArgument,
                     "desktop verification request owner mismatch");
    // Canonical history is authoritative even when the UI projection has hit its budget.
    // Re-derived per request against the current record states (DEC-086): a
    // failed mutation receipt stays disqualifying only while its record is
    // neither machine-verified nor a clean pre-dispatch rejection — the same
    // acceptance settle() applies. Unknown failures never enter records_ and
    // always fail the check, so a hidden failure still fences the turn.
    canonical_failure_ = false;
    for (const auto &item : request.input)
        for (const auto &part : item.content)
            if (const auto *call = std::get_if<mira::ToolCallPart>(&part);
                call && (mutation(call->wire_name) || call->wire_name == "desktop.action.verify" ||
                         call->wire_name == "desktop.vision.ocr" ||
                         call->wire_name == "desktop.vision.mark"))
                for (const auto &result_item : request.input)
                    for (const auto &result_part : result_item.content)
                        if (const auto *result = std::get_if<mira::ToolResultPart>(&result_part);
                            result && result->provider_call_id == call->provider_call_id) {
                            if (call->wire_name == "desktop.vision.ocr" ||
                                call->wire_name == "desktop.vision.mark")
                                // Later successful fresh vision replaces a failed read-only
                                // attempt; it does not alter any mutation fence.
                                ocr_failed_ = result->failed;
                            else if (result->failed) {
                                // Trust only this turn's own no-effect receipt. Unknown or
                                // unverified failures remain fenced even outside UI projection.
                                const auto parsed = mira::parse_json(result->safe_error_summary);
                                const auto &receipt = parsed ? parsed.value() : result->result;
                                const auto id = field(receipt, "action_id");
                                const auto known = std::find_if(
                                    records_.begin(), records_.end(), [&](const auto &r) {
                                        return r.id == id && r.action == call->wire_name;
                                    });
                                if (known == records_.end() ||
                                    !(known->status == "verified" ||
                                      (known->status == "rejected" && !known->dispatched &&
                                       !known->uncertain)))
                                    canonical_failure_ = true;
                            }
                        }
    return {};
}
} // namespace mirage::integration::detail
