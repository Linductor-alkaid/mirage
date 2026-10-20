#include "../../integration/mira/src/desktop_action_verification.hpp"
#include "../support/fake_desktop_environment.hpp"
#include <iostream>
#include <limits>
#include <mira/model_digest.hpp>
#include <source_location>
#include <stdexcept>

namespace d = mirage::desktop;
namespace i = mirage::integration;
using Json = mira::JsonValue;
namespace {
int checks = 0;
void check(bool value, const std::source_location where = std::source_location::current()) {
    ++checks;
    if (!value)
        throw std::runtime_error("visual condition check at " + std::to_string(where.line()));
}
std::string field(const Json &value, const char *name) {
    const auto *v = value.find(name);
    return v && v->as_string() ? *v->as_string() : "";
}
struct Fixture {
    std::shared_ptr<mirage::testing::FakeDesktopEnvironment> env =
        std::make_shared<mirage::testing::FakeDesktopEnvironment>();
    i::DesktopToolContext desktop;
    mira::OperationContext ctx = mira::make_control_context();
    d::CancelToken cancel;
    int calls = 0, inputs = 0, permissions = 0;
    bool allow = true;
    std::string mode;
    std::function<void(i::DesktopOcrResult &)> edit;
    Fixture() {
        env->windows = {{"w1", "Canvas", {-100, -40, 100, 80}, true},
                        {"w2", "Other", {100, -40, 100, 80}, false}};
        desktop.environment = env;
        desktop.actions = std::make_shared<d::ActionCoordinator>();
        ctx.task = mira::TaskId::generate();
        ctx.session = mira::SessionId::generate();
        ctx.task_epoch = 3;
        desktop.authorize = [&](const auto &cap, const auto &resource, const auto &probe,
                                const auto &task, const auto &operation) {
            ++permissions;
            check(cap == "screen.capture" && resource == "ocr:configured-display");
            check(task == ctx.task.to_string() && operation == ctx.operation.to_string());
            return allow && !probe();
        };
        desktop.ocr = [&](const auto &, const auto &end) {
            check(end.has_value());
            i::DesktopOcrResult r;
            r.capture_begin = std::chrono::steady_clock::now();
            r.ok = true;
            r.display_id = "d1";
            r.display_geometry = {-200, -80, 400, 200};
            r.model_revision = "verified-model-revision";
            r.backend_calls = ++calls;
            r.snapshot.scope_ref = "@vs" + std::to_string(calls);
            r.snapshot.regions = {
                {"@v1", d::VisualRegionSource::kOcr, {-90, -30, 60, 20}, "Task saved", {}, 0.98}};
            r.capture_end = std::chrono::steady_clock::now();
            if (edit)
                edit(r);
            return r;
        };
    }
    Json args() const {
        return Json{Json::Object{{"window_id", "w1"},
                                 {"verify_kind", "visual_text_exists"},
                                 {"expected_text", "Task saved"}}};
    }
    mira::BuiltinToolHandler handler() {
        return [&](const auto &, const auto &) -> mira::Result<Json> {
            ++inputs;
            return Json{Json::Object{
                {"status", "dispatched"}, {"input_released", true}, {"verified", false}}};
        };
    }
};
void scenarios() {
    for (const std::string mode :
         {"good",     "wrong",         "duplicate",     "outside",  "clipped",   "low",
          "detector", "denied",        "exception",     "failed",   "cancelled", "takeover",
          "focus",    "move",          "title",         "old",      "future",    "no_calls",
          "no_scope", "no_revision",   "too_many",      "bad_utf8", "long_text", "nan",
          "bad_box",  "duplicate_ref", "window_clipped"}) {
        Fixture f;
        f.allow = mode != "denied";
        f.edit = [&](auto &r) {
            if (mode == "wrong")
                r.snapshot.regions[0].text = "Not saved";
            if (mode == "duplicate") {
                r.snapshot.regions.push_back(r.snapshot.regions[0]);
                r.snapshot.regions[1].ref = "@v2";
            }
            if (mode == "outside")
                r.snapshot.regions[0].bounds.x = 110;
            if (mode == "clipped")
                r.snapshot.regions[0].bounds.x = -110;
            if (mode == "low")
                r.snapshot.regions[0].confidence = 0.79;
            if (mode == "detector")
                r.snapshot.regions[0].source = d::VisualRegionSource::kDetector;
            if (mode == "exception")
                throw std::runtime_error("injected");
            if (mode == "failed")
                r.ok = false;
            if (mode == "cancelled")
                f.cancel.request_cancel();
            if (mode == "takeover")
                f.desktop.actions->takeover();
            if (mode == "focus") {
                f.env->windows[0].focused = false;
                f.env->windows[1].focused = true;
            }
            if (mode == "move")
                ++f.env->windows[0].geometry.x;
            if (mode == "title")
                f.env->windows[0].title += "Changed";
            if (mode == "old")
                r.capture_begin -= std::chrono::seconds{1};
            if (mode == "future")
                r.capture_end += std::chrono::seconds{1};
            if (mode == "no_calls")
                r.backend_calls = 0;
            if (mode == "no_scope")
                r.snapshot.scope_ref.clear();
            if (mode == "no_revision")
                r.model_revision.clear();
            if (mode == "too_many")
                r.snapshot.regions.resize(129);
            if (mode == "bad_utf8")
                r.snapshot.regions[0].text = "\xff";
            if (mode == "long_text")
                r.snapshot.regions[0].text = std::string(1025, 'x');
            if (mode == "nan")
                r.snapshot.regions[0].confidence = std::numeric_limits<double>::quiet_NaN();
            if (mode == "bad_box")
                r.snapshot.regions[0].bounds.width = 0;
            if (mode == "duplicate_ref")
                r.snapshot.regions.push_back(r.snapshot.regions[0]);
            if (mode == "window_clipped")
                r.display_geometry.width = 150;
        };
        i::detail::DesktopActionVerification verify(f.desktop, f.ctx, f.cancel);
        const auto result = verify.execute("desktop.vision.click", f.args(), f.ctx, f.handler());
        check(static_cast<bool>(result));
        const auto &evidence = *result.value().find("verification");
        if (mode == "good") {
            check(field(evidence, "status") == "verified");
            check(!field(evidence, "evidence_id").empty());
            check(field(evidence, "visual_scope") == "@vs1");
            check(verify.settle());
            check(f.calls == 1);
        } else {
            check(field(evidence, "status") == "unverified");
            check(!verify.settle());
            auto dependent = f.args();
            dependent.set("depends_on", Json::Array{*result.value().find("action_id")});
            check(!verify.execute("desktop.vision.click", dependent, f.ctx, f.handler()));
        }
        check(f.inputs == 1);
        check(mira::to_json_string(evidence).find("Task saved") == std::string::npos);
    }
}
void revalidation() {
    for (const std::string mode :
         {"scope", "calls", "model", "display", "geometry", "text", "permission"}) {
        Fixture f;
        i::detail::DesktopActionVerification verify(f.desktop, f.ctx, {});
        const auto first = verify.execute("desktop.input.key", f.args(), f.ctx, f.handler());
        check(static_cast<bool>(first));
        f.edit = [&](auto &r) {
            if (mode == "scope")
                r.snapshot.scope_ref = "@vs1";
            if (mode == "calls")
                r.backend_calls = 1;
            if (mode == "model")
                r.model_revision = "changed";
            if (mode == "display")
                r.display_id = "d2";
            if (mode == "geometry")
                ++r.display_geometry.x;
            if (mode == "text")
                r.snapshot.regions.clear();
        };
        if (mode == "permission")
            f.allow = false;
        check(verify.settle()); // No automatic OCR at settlement.
        check(static_cast<bool>(verify.recheck(field(first.value(), "action_id"), f.ctx)));
        check(!verify.settle());
        check(f.inputs == 1);
    }
    Fixture f;
    i::detail::DesktopActionVerification verify(f.desktop, f.ctx, {});
    auto first = verify.execute("desktop.input.key", f.args(), f.ctx, f.handler());
    check(static_cast<bool>(first));
    mira::BuiltinToolRegistry registry;
    verify.register_tool(registry, {});
    const auto spec = registry.exposed_tools().front();
    const auto reread = [&](Json args, mira::OperationContext ctx) {
        mira::ToolProposal call;
        call.tool_id = spec.tool_id;
        call.tool_version = spec.version;
        call.wire_name = spec.wire_name;
        call.operation_id = mira::OperationId::generate();
        call.provider_call_id.value = "verify";
        call.arguments = std::move(args);
        call.arguments_digest = mira::canonical_json_digest(call.arguments);
        return registry.execute(call, ctx);
    };
    const Json args{Json::Object{{"action_id", *first.value().find("action_id")}}};
    f.edit = [](auto &r) { r.snapshot.regions[0].text = "Not saved"; };
    auto wrong = reread(args, f.ctx);
    check(wrong && !wrong.value().failed && field(wrong.value().result, "status") == "unverified");
    f.edit = {};
    auto good = reread(args, f.ctx);
    check(good && !good.value().failed && field(good.value().result, "status") == "verified");
    check(f.inputs == 1 && f.calls == 3);
    auto changed = *args.as_object();
    changed.emplace_back("expected_text", "Not saved");
    auto replacement = reread(Json{std::move(changed)}, f.ctx);
    check(replacement && replacement.value().failed && f.calls == 3);
    auto expired = f.ctx;
    expired.deadline = std::chrono::steady_clock::now() - std::chrono::seconds{1};
    check(!reread(args, expired));
    check(f.inputs == 1 && f.calls == 3);
    for (int n = 3; n < 64; ++n) {
        const auto proof = reread(args, f.ctx);
        check(proof && field(proof.value().result, "status") == "verified");
    }
    check(verify.settle());
    check(f.calls == 64 && f.inputs == 1);
    auto limited = reread(args, f.ctx);
    check(limited &&
          field(limited.value().result, "reason") == "visual_observation_budget_exceeded");
    check(!verify.settle());
    // A takeover/resume is an authority barrier. A retired proof cannot be
    // revived by starting a new evidence budget; a new action must admit.
    f.desktop.actions->takeover();
    check(f.desktop.actions->resume());
    auto reopened = reread(args, f.ctx);
    check(reopened && reopened.value().failed &&
          reopened.value().safe_error_summary.find("authority changed") != std::string::npos);
    check(f.calls == 64);
    i::detail::DesktopActionVerification fresh(f.desktop, f.ctx, {});
    const auto admitted = fresh.execute("desktop.input.key", f.args(), f.ctx, f.handler());
    check(admitted && field(*admitted.value().find("verification"), "status") == "verified");
    check(f.calls == 65 && f.inputs == 2);
}
void invalid_conditions() {
    for (int mode = 0; mode < 6; ++mode) {
        Fixture f;
        auto args = *f.args().as_object();
        if (mode == 0)
            args.back().second = Json{""};
        if (mode == 1)
            args.back().second = Json{std::string(1025, 'x')};
        if (mode == 2)
            args.back().second = Json{"\xff"};
        if (mode == 3)
            args[1].second = Json{"window_focused"};
        if (mode == 4)
            f.desktop.ocr = {};
        if (mode == 5)
            args.pop_back();
        i::detail::DesktopActionVerification verify(f.desktop, f.ctx, {});
        check(!verify.execute("desktop.input.key", Json{std::move(args)}, f.ctx, f.handler()));
        check(f.inputs == 0 && f.calls == 0);
    }
    Fixture f;
    f.ctx.deadline = std::chrono::steady_clock::now() - std::chrono::seconds{1};
    i::detail::DesktopActionVerification verify(f.desktop, f.ctx, {});
    check(!verify.execute("desktop.input.key", f.args(), f.ctx, f.handler()));
    check(f.inputs == 0 && f.calls == 0);
}
void local_scope() {
    Fixture f;
    f.edit = [&](auto &r) {
        r.snapshot.regions[0].confidence = 0.8;
        r.snapshot.regions.push_back(r.snapshot.regions[0]);
        r.snapshot.regions[1].ref = "@v2";
        r.snapshot.regions[1].bounds.x = 110; // Same text in another window.
    };
    i::detail::DesktopActionVerification verify(f.desktop, f.ctx, {});
    auto result = verify.execute("desktop.input.key", f.args(), f.ctx, f.handler());
    check(result && field(*result.value().find("verification"), "status") == "verified");
    check(verify.settle());
    check(f.calls == 1 && f.inputs == 1);
}
} // namespace
int main() {
    try {
        scenarios();
        revalidation();
        invalid_conditions();
        local_scope();
        std::cout << "visual text verification: " << checks << " checks passed\n";
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
