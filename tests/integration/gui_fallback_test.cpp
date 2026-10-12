#include "../../integration/mira/src/desktop_action_verification.hpp"
#include "../../integration/mira/src/desktop_image_supply.hpp"
#include "../support/fake_desktop_environment.hpp"
#include "../support/provider_tool_name.hpp"
#include <atomic>
#include <iostream>
#include <kairo/comm/mailbox.hpp>
#include <kairo/executor.hpp>
#include <mirage/integration/model_layer.hpp>
#include <source_location>
#ifdef MIRAGE_GUI_X11
#include "../support/xvfb_display.hpp"
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <mirage/platform/linux/linux_desktop_environment.hpp>
#include <poll.h>
#endif

namespace d = mirage::desktop;
namespace i = mirage::integration;
namespace detail = i::detail;
using Json = mira::JsonValue;
namespace {
std::atomic_int checks{0};
void check(bool v, const std::source_location at = std::source_location::current()) {
    ++checks;
    if (!v)
        throw std::runtime_error("GUI fallback check at " + std::to_string(at.line()));
}
std::string field(const Json &v, const char *key) {
    const auto *p = v.find(key);
    return p && p->as_string() ? *p->as_string() : "";
}
mira::OperationContext owner() {
    auto c = mira::make_control_context();
    c.task = mira::TaskId::generate();
    c.session = mira::SessionId::generate();
    c.task_epoch = 7;
    c.deadline = std::chrono::steady_clock::now() + std::chrono::seconds{20};
    return c;
}
// Application shortcuts and semantic access are deliberately unavailable.
class Environment final : public d::DesktopEnvironment {
  public:
    std::shared_ptr<d::DesktopEnvironment> delegate;
    d::InputProvider *override_input = nullptr;
    d::EnvironmentInfo info() const override { return {"GUI-only", "test"}; }
    d::ScreenProvider *screen() override { return delegate->screen(); }
    d::WindowProvider *window() override { return delegate->window(); }
    d::InputProvider *input() override {
        return override_input ? override_input : delegate->input();
    }
};
class FaultInput final : public d::InputProvider {
  public:
    d::InputProvider *delegate = nullptr;
    int calls = 0, mode = 0;
    std::function<void()> after;
    bool complete_actions_supported() const override { return true; }
    d::InputOutcome click_at(std::int32_t x, std::int32_t y, const d::MouseButton &b,
                             const d::InputLimits &l, const d::CancelToken &c) override {
        ++calls;
        if (mode == 1)
            throw std::runtime_error("injected pointer exception");
        auto out = delegate->click_at(x, y, b, l, c);
        if (after)
            after();
        if (mode == 2) {
            auto stopping = c;
            stopping.request_cancel();
        }
        if (mode == 3) {
            out.ok = false;
            out.error = {"io_error", "uncertain"};
            out.side_effect_may_have_occurred = true;
        }
        return out;
    }
    d::InputOutcome inject_key(const d::KeySym &k, bool pressed, const d::InputLimits &l,
                               const d::CancelToken &c) override {
        return delegate->inject_key(k, pressed, l, c);
    }
    d::InputOutcome type_text(const std::string &s, const d::InputLimits &l,
                              const d::CancelToken &c) override {
        return delegate->type_text(s, l, c);
    }
    d::InputOutcome pointer_move(std::int32_t x, std::int32_t y, const d::InputLimits &l,
                                 const d::CancelToken &c) override {
        return delegate->pointer_move(x, y, l, c);
    }
    d::InputOutcome pointer_button(const d::MouseButton &b, bool pressed, const d::InputLimits &l,
                                   const d::CancelToken &c) override {
        return delegate->pointer_button(b, pressed, l, c);
    }
    d::PointerQueryOutcome pointer_position(const d::CancelToken &c) override {
        return delegate->pointer_position(c);
    }
};
mira::ToolExecutionRecord invoke(mira::BuiltinToolRegistry &r, const std::string &name,
                                 const Json &args, const mira::OperationContext &ctx) {
    for (const auto &spec : r.exposed_tools()) {
        if (spec.wire_name != name)
            continue;
        mira::ToolProposal p;
        p.tool_id = spec.tool_id;
        p.tool_version = spec.version;
        p.wire_name = name;
        p.has_side_effects = spec.has_side_effects;
        p.operation_id = mira::OperationId::generate();
        p.provider_call_id.value = "gui-call";
        p.arguments = args;
        p.arguments_digest = mira::canonical_json_digest(args);
        auto result = r.execute(p, ctx);
        if (!result) {
            mira::ToolExecutionRecord out;
            out.failed = true;
            out.safe_error_summary = result.error().safe_message;
            return out;
        }
        return result.value();
    }
    throw std::runtime_error("missing GUI tool " + name);
}
struct Fixture {
    std::shared_ptr<mirage::testing::FakeDesktopEnvironment> fake =
        std::make_shared<mirage::testing::FakeDesktopEnvironment>();
    std::shared_ptr<Environment> env = std::make_shared<Environment>();
    FaultInput input;
    i::DesktopToolContext tools;
    mira::OperationContext ctx = owner();
    d::CancelToken cancel;
    std::unique_ptr<detail::DesktopImageTurn> image;
    std::unique_ptr<detail::DesktopActionVerification> verifier;
    mira::BuiltinToolRegistry registry;
    bool deny = false;
    std::function<void()> on_authorize;
    explicit Fixture(bool mutations = true, std::string scope = {}) {
        fake->windows = {{"w1", "Editor", {100, 100, 100, 100}, true}};
        fake->displays = {{"d1", {0, 0, 400, 300}, true}};
        env->delegate = fake;
        input.delegate = fake->input();
        env->override_input = &input;
        tools.environment = env;
        tools.actions = std::make_shared<d::ActionCoordinator>();
        tools.capture_scope = std::move(scope);
        tools.gate = [&](const auto &cap, const auto &, const auto &) {
            if (cap == "input.inject" && on_authorize)
                on_authorize();
            return cap != "input.inject" || !deny;
        };
        verifier = std::make_unique<detail::DesktopActionVerification>(tools, ctx, cancel);
        image = std::make_unique<detail::DesktopImageTurn>(
            std::make_shared<detail::DesktopImageSource>(), tools, ctx, cancel);
        auto decorate = [&](const auto &name, mira::BuiltinToolHandler handler) {
            return mira::BuiltinToolHandler{[&, name, handler](const auto &args, const auto &c) {
                return detail::DesktopActionVerification::mutation(name)
                           ? verifier->execute(name, args, c, handler)
                           : handler(args, c);
            }};
        };
        image->register_tool(registry, decorate, mutations);
        verifier->register_tool(registry, {});
    }
    Json capture(std::string source = "display", std::string id = "d1", bool supply = true) {
        auto result = invoke(registry, "desktop.screen.capture",
                             Json{Json::Object{{"source", source}, {"source_id", id}}}, ctx);
        check(!result.failed);
        if (supply) {
            mira::AgentLoopSpec spec;
            spec.task_id = ctx.task;
            spec.session_id = ctx.session;
            spec.task_epoch = ctx.task_epoch;
            auto items = image->supply(spec);
            check(static_cast<bool>(items));
            mira::ModelRequest request;
            request.task_id = ctx.task;
            request.task_epoch = ctx.task_epoch;
            request.input = items.value();
            check(static_cast<bool>(image->validate_request(request, ctx)));
        }
        return result.result;
    }
    Json args(const Json &meta, bool window = false) {
        return Json{
            Json::Object{{"image_scope", *meta.find("image_scope")},
                         {"environment_epoch", *meta.find("environment_epoch")},
                         {"target_context", window ? "window" : "display"},
                         {window ? "window_id" : "display_id", window ? "w1" : "d1"},
                         {"x", window ? 110 : 20},
                         {"y", window ? 110 : 20},
                         {"width", 20},
                         {"height", 20},
                         {"verify_kind", window ? "window_focused" : "foreground_changed"}}};
    }
    mira::ToolExecutionRecord click(const Json &a) {
        return invoke(registry, "desktop.vision.click_region", a, ctx);
    }
};
void contracts(kairo::Executor &executor) {
    for (const auto &mode :
         {"good",      "none",           "unseen",           "pixels",       "focus",
          "display",   "deny",           "takeover",         "cancel",       "throw",
          "uncertain", "cancel_after",   "bounds",           "outside",      "window",
          "repeat",    "progress_false", "unrelated",        "owner",        "deadline",
          "zero",      "wrong_display",  "permission_focus", "capture_fail", "count"}) {
        Fixture f;
        if (std::string{mode} == "none")
            f.fake->windows.clear();
        const auto meta = f.capture("display", "d1", std::string{mode} != "unseen");
        auto args = f.args(meta, std::string{mode} == "window" || std::string{mode} == "outside");
        f.input.after = [&] {
            f.fake->windows = {{"browser", "Browser", {10, 10, 200, 100}, true}};
        };
        const std::string m = mode;
        if (m == "pixels")
            f.fake->set_pixel_value(1);
        if (m == "focus")
            f.fake->windows.front().title = "Changed";
        if (m == "display")
            f.fake->displays.front().geometry.x = 1;
        if (m == "deny")
            f.deny = true;
        if (m == "takeover")
            f.on_authorize = [&] { f.tools.actions->takeover(); };
        if (m == "cancel")
            f.cancel.request_cancel();
        if (m == "throw")
            f.input.mode = 1;
        if (m == "uncertain")
            f.input.mode = 3;
        if (m == "cancel_after")
            f.input.mode = 2;
        if (m == "bounds")
            args.set("x", 399);
        if (m == "outside")
            args.set("x", 20);
        if (m == "window" || m == "progress_false")
            f.input.after = {};
        if (m == "repeat") {
            const auto repeated = f.capture();
            check(field(meta, "image_scope") != field(repeated, "image_scope"));
        }
        if (m == "unrelated") {
            f.fake->capture_frame_edit = [](auto &frame) {
                if (frame.width == 400)
                    frame.pixels.front() = 0;
            };
        }
        if (m == "owner")
            f.ctx.session = mira::SessionId::generate();
        if (m == "deadline")
            f.ctx.deadline = std::chrono::steady_clock::now();
        if (m == "zero")
            args.set("width", 0);
        if (m == "wrong_display")
            args.set("display_id", "another");
        if (m == "permission_focus")
            f.on_authorize = [&] { f.fake->windows.clear(); };
        if (m == "capture_fail")
            f.fake->failures.capture_error = true;
        if (m == "count") {
            Json latest;
            for (int n = 0; n < 3; ++n)
                latest = f.capture();
            args = f.args(latest);
        }
        const auto result = f.click(args);
        if (m == "good" || m == "none" || m == "window" || m == "unrelated") {
            check(!result.failed && f.input.calls == 1);
            check(result.result.find("verified")->as_boolean().value_or(false));
            check(f.verifier->settle());
        } else if (m == "progress_false") {
            check(!result.failed && !result.result.find("verified")->as_boolean().value_or(true));
            check(!f.verifier->settle());
            check(f.click(args).failed && f.input.calls == 1);
        } else {
            check(result.failed);
            check(f.input.calls ==
                  (m == "throw" || m == "uncertain" || m == "cancel_after" ? 1 : 0));
            if (f.input.calls)
                check(f.click(args).failed && f.input.calls == 1);
        }
        check(f.tools.actions->input_released() || m == "throw");
    }
    { // Downscaled preview -> source pixels, including negative desktop origin.
        Fixture f;
        f.fake->displays.front().geometry = {-1000, -200, 3200, 2000};
        auto args = f.args(f.capture());
        args.set("x", 100);
        args.set("y", 200);
        args.set("width", 20);
        args.set("height", 10);
        f.input.after = [&] { f.fake->windows.clear(); };
        check(!f.click(args).failed);
        check(f.fake->pointer.x == -780 && f.fake->pointer.y == 210);
    }
    { // A screenshot of a background window is observable but cannot authorize input.
        Fixture f;
        f.fake->windows.push_back({"w2", "Background", {10, 10, 50, 50}, false});
        const auto meta = f.capture("window", "w2");
        check(!meta.find("region_actionable")->as_boolean().value_or(true));
        check(meta.find("image_scope") == nullptr);
    }
    { // A slow model does not invalidate an unchanged locally revalidated intent.
        Fixture f;
        const auto args = f.args(f.capture());
        executor.submit_delayed(5100, [] {}).future.get();
        f.input.after = [&] { f.fake->windows.clear(); };
        check(!f.click(args).failed);
    }
    {
        Fixture f(false);
        const auto specs = f.registry.exposed_tools();
        check(std::none_of(specs.begin(), specs.end(),
                           [](const auto &s) { return s.has_side_effects; }));
    }
    {
        Fixture f(true, "w1");
        check(invoke(f.registry, "desktop.screen.capture",
                     Json{Json::Object{{"source", "display"}, {"source_id", "d1"}}}, f.ctx)
                  .failed);
        auto args = f.args(f.capture("window", "w1"));
        args.set("x", 150);
        check(f.click(args).failed && f.input.calls == 0);
    }
    { // Explicit display OCR can verify menus outside the old foreground window.
        Fixture f;
        std::uint64_t calls = 0;
        f.tools.ocr = [&](const auto &, const auto &) {
            i::DesktopOcrResult out;
            out.capture_begin = std::chrono::steady_clock::now();
            out.ok = true;
            out.backend_calls = ++calls;
            out.display_id = "d1";
            out.display_geometry = {0, 0, 400, 300};
            out.model_revision = "fixture";
            out.snapshot.scope_ref = "scope" + std::to_string(calls);
            out.snapshot.regions = {
                {"@v1", d::VisualRegionSource::kOcr, {10, 10, 50, 20}, "Menu", "", 0.99}};
            out.capture_end = std::chrono::steady_clock::now();
            return out;
        };
        detail::DesktopActionVerification verifier(f.tools, f.ctx, {});
        Json args{Json::Object{{"target_context", "display"},
                               {"display_id", "d1"},
                               {"verify_kind", "display_text_exists"},
                               {"expected_text", "Menu"}}};
        int dispatches = 0;
        auto handler = [&](const auto &, const auto &) -> mira::Result<Json> {
            ++dispatches;
            return Json{Json::Object{{"status", "dispatched"}, {"input_released", true}}};
        };
        check(static_cast<bool>(
            verifier.execute("desktop.vision.click_region", args, f.ctx, handler)));
        check(verifier.settle() && calls == 2 && dispatches == 1);
        f.fake->displays.front().geometry.x = 1;
        check(!verifier.settle());
    }
}

struct Script final : mira::IModelProvider {
    mira::ModelProfile p;
    std::string display;
    int step = 0;
    std::function<void(int)> wait;
    explicit Script(mira::ModelProfile profile, std::string id)
        : p(std::move(profile)), display(std::move(id)) {}
    const mira::ModelProfile &profile() const override { return p; }
    mira::Result<mira::ModelResponse> infer(const mira::ModelRequest &request,
                                            const mira::OperationContext &,
                                            const mira::ProviderInferOptions &) override {
        check(std::none_of(request.tools.begin(), request.tools.end(), [](const auto &s) {
            return s.wire_name == "desktop_application_launch" ||
                   s.wire_name == "desktop_application_open_uri";
        }));
        if (wait)
            wait(step);
        Json capture, front, action;
        bool saw_image = false;
        for (const auto &item : request.input)
            for (const auto &part : item.content) {
                saw_image = saw_image || std::holds_alternative<mira::ImagePart>(part);
                if (const auto *r = std::get_if<mira::ToolResultPart>(&part); r && !r->failed) {
                    if (r->result.find("image_scope"))
                        capture = r->result;
                    if (r->result.find("observation_scope"))
                        front = r->result;
                    if (r->result.find("action_id"))
                        action = r->result;
                }
            }
        mira::ModelResponse response;
        response.request_id = request.request_id;
        response.operation_id = request.operation_id;
        response.profile_id = p.id;
        response.status = mira::ModelCompletionStatus::Completed;
        std::string name;
        Json args{Json::Object{}};
        if (step == 0) {
            name = "desktop.screen.capture";
            args = Json{Json::Object{{"source", "display"}, {"source_id", display}}};
        } else if (step == 1) {
            check(saw_image);
            name = "desktop.vision.click_region";
            args = Json{Json::Object{{"image_scope", *capture.find("image_scope")},
                                     {"environment_epoch", *capture.find("environment_epoch")},
                                     {"target_context", "display"},
                                     {"display_id", display},
                                     {"x", 20},
                                     {"y", 20},
                                     {"width", 40},
                                     {"height", 40},
                                     {"verify_kind", "foreground_title_equals"},
                                     {"expected_title", "Browser"}}};
        } else if (step == 2 || step == 9) {
            name = "desktop.action.verify";
            args.set("action_id", *action.find("action_id"));
        } else if (step == 3 || step == 5 || step == 7) {
            name = "desktop.window.front";
        } else if (step == 4 || step == 6 || step == 8) {
            name = step == 6 ? "desktop.input.type_text" : "desktop.input.key";
            args = Json{Json::Object{
                {"window_id", *front.find("id")},
                {"observation_scope", *front.find("observation_scope")},
                {"environment_epoch", *front.find("environment_epoch")},
                {"verify_kind", step == 8 ? "window_title_equals" : "window_focused"}}};
            args.set(step == 6 ? "text" : "key", step == 4   ? "ctrl+l"
                                                 : step == 6 ? "https://example.test/"
                                                             : "enter");
            if (step == 8)
                args.set("expected_title", "Loaded:https://example.test/");
        } else if (step == 10) {
            name = "desktop.screen.capture";
            args = Json{Json::Object{{"source", "window"}, {"source_id", *front.find("id")}}};
        } else {
            check(saw_image);
            mira::MessageOutput message;
            message.content.emplace_back(mira::OutputTextPart{"Fixture navigation verified", {}});
            response.output.emplace_back(std::move(message));
            ++step;
            return response;
        }
        const auto spec =
            std::find_if(request.tools.begin(), request.tools.end(), [&](const auto &s) {
                return s.wire_name == mirage::testing::fixture_tool_name(name);
            });
        check(spec != request.tools.end());
        mira::ToolCallOutput call;
        call.tool_id = spec->tool_id;
        call.provider_name = spec->wire_name;
        call.provider_call_id.value = "gui-" + std::to_string(step);
        call.arguments = args;
        call.arguments_digest = mira::digest_string(mira::to_json_string(args));
        response.output.emplace_back(std::move(call));
        ++step;
        return response;
    }
};
#ifdef MIRAGE_GUI_X11
void real_navigation(kairo::Executor &executor) {
    check(XInitThreads() != 0);
    mirage::testing::XvfbDisplay xvfb(mirage::testing::find_xvfb());
    Display *display = XOpenDisplay(xvfb.display_name().c_str());
    check(display != nullptr);
    const auto root = DefaultRootWindow(display);
    const auto launcher = XCreateSimpleWindow(display, root, 10, 10, 70, 70, 0, 0, 0x111111);
    const auto browser = XCreateSimpleWindow(display, root, 100, 100, 400, 200, 0, 0, 0xffffff);
    XSelectInput(display, launcher, ButtonPressMask | ButtonReleaseMask);
    XSelectInput(display, browser, KeyPressMask | KeyReleaseMask);
    const auto title = [&](Window w, const std::string &t) {
        XStoreName(display, w, t.c_str());
        XChangeProperty(display, w, XInternAtom(display, "_NET_WM_NAME", False),
                        XInternAtom(display, "UTF8_STRING", False), 8, PropModeReplace,
                        reinterpret_cast<const unsigned char *>(t.data()),
                        static_cast<int>(t.size()));
        XSync(display, False);
    };
    title(launcher, "Launcher");
    title(browser, "Browser");
    XMapWindow(display, launcher);
    XSetInputFocus(display, root, RevertToParent, CurrentTime);
    XSync(display, False);
    auto platform = std::make_shared<mirage::platform::linux_backend::LinuxDesktopEnvironment>(
        std::vector<std::filesystem::path>{},
        mirage::platform::linux_backend::X11Options{true, xvfb.display_name()});
    auto env = std::make_shared<Environment>();
    env->delegate = platform;
    const auto displays = env->screen()->list_displays();
    check(displays.ok && displays.displays.size() == 1);
    i::DesktopToolContext tools;
    tools.environment = env;
    tools.actions = std::make_shared<d::ActionCoordinator>();
    tools.gate = [](const auto &, const auto &, const auto &) { return true; };
    kairo::comm::LatestMailbox<int> stage;
    check(stage.try_publish(0));
    i::ModelProviderOverride provider([&](const auto &profile) {
        auto script = std::make_shared<Script>(profile, displays.displays.front().id);
        script->wait = [&](int n) {
            const int expected = n == 2 ? 1 : n == 5 ? 2 : n == 9 ? 3 : 0;
            const auto end = std::chrono::steady_clock::now() + std::chrono::seconds{2};
            int value = 0;
            while (expected && std::chrono::steady_clock::now() < end &&
                   (!stage.try_load(value) || value < expected))
                (void)::poll(nullptr, 0, 1);
            check(!expected || value >= expected);
        };
        return script;
    });
    i::ModelLayerConfig config;
    config.enabled = true;
    config.model_selector = "GUI-fixture";
    config.supports_images = true;
    config.max_output_tokens = 256;
    i::ModelLayer layer(executor, config, &provider, tools);
    const auto ctx = owner();
    auto work = executor.submit_auto(
        [&] { return layer.complete_harness_turn("", "Open via GUI and enter URL", ctx); });
    std::string url;
    bool address_focus = false;
    int clicks = 0;
    while (work.wait_for(std::chrono::milliseconds{0}) != std::future_status::ready) {
        if (!XPending(display)) {
            (void)::poll(nullptr, 0, 1);
            continue;
        }
        XEvent event{};
        XNextEvent(display, &event);
        if (event.type == ButtonRelease && event.xbutton.window == launcher) {
            ++clicks;
            XMapWindow(display, browser);
            XSetInputFocus(display, browser, RevertToParent, CurrentTime);
            XSync(display, False);
            check(stage.try_publish(1));
        }
        if (event.type == KeyPress && event.xkey.window == browser) {
            char buffer[32]{};
            KeySym key{};
            const int n = XLookupString(&event.xkey, buffer, sizeof(buffer), &key, nullptr);
            if ((event.xkey.state & ControlMask) && key == XK_l) {
                address_focus = true;
                url.clear();
                check(stage.try_publish(2));
            } else if (key == XK_Return && address_focus) {
                title(browser, "Loaded:" + url);
                check(stage.try_publish(3));
            } else if (address_focus && n > 0 && !(event.xkey.state & ControlMask))
                url.append(buffer, static_cast<std::size_t>(n));
        }
    }
    const auto result = work.get();
    if (!result.ok)
        std::cerr << result.error << '\n';
    check(result.ok && clicks == 1 && address_focus && url == "https://example.test/");
    const auto front = env->window()->front_window();
    check(front.ok && front.found && front.window.title == "Loaded:https://example.test/");
    char keys[32]{};
    XQueryKeymap(display, keys);
    check(std::all_of(std::begin(keys), std::end(keys), [](char c) { return c == 0; }));
    Window pointer_root{}, child{};
    int rx = 0, ry = 0, wx = 0, wy = 0;
    unsigned mask = 0;
    check(XQueryPointer(display, root, &pointer_root, &child, &rx, &ry, &wx, &wy, &mask));
    check((mask & (Button1Mask | Button2Mask | Button3Mask)) == 0);
    check(result.supplied_input_items > 0 && result.supplied_input_dropped == 0);
    layer.shutdown();
    env.reset();
    platform.reset();
    XDestroyWindow(display, browser);
    XDestroyWindow(display, launcher);
    XCloseDisplay(display);
    std::cout
        << "Real X11 GUI-only launcher -> Ctrl+L -> URL -> Enter -> fresh page fixture passed\n";
}
#endif
} // namespace
int main() {
    try {
        kairo::Executor executor;
        kairo::ExecutorConfig options;
        options.min_threads = options.max_threads = 3;
        check(static_cast<bool>(executor.initialize(options)));
        contracts(executor);
#ifdef MIRAGE_GUI_X11
        real_navigation(executor);
#endif
        executor.shutdown(true);
        std::cout << checks.load() << " GUI fallback checks passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
