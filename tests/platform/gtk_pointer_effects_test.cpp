#include "../../integration/mira/src/desktop_action_verification.hpp"
#include "../../integration/mira/src/desktop_image_supply.hpp"
#include "../support/test.hpp"
#include "../support/xvfb_display.hpp"
#include <chrono>
#include <cstdlib>
#include <kairo/executor.hpp>
#include <limits>
#include <mira/json.hpp>
#include <mira/model_digest.hpp>
#include <mirage/desktop/action_coordinator.hpp>
#include <mirage/integration/fake_visual_backend.hpp>
#include <mirage/integration/visual_observation_pipeline.hpp>
#include <mirage/platform/linux/linux_desktop_environment.hpp>
#include <poll.h>
#include <stdexcept>

// Xlib macros follow pinned C++ headers.
#include "../support/x11_pointer_diagnostics.hpp"
#include <X11/Xlib.h>
#include <X11/extensions/XTest.h>

namespace d = mirage::desktop;
namespace i = mirage::integration;
using Json = mira::JsonValue;
namespace {
class Application {
    pid_t pid_ = -1;
    int input_ = -1, output_ = -1;

  public:
    Json initial;
    Application(const std::string &display, const char *python, const char *script,
                const char *mode, bool operator_mode = false) {
        int commands[2]{-1, -1}, responses[2]{-1, -1};
        const auto close_pipes = [&] {
            for (const auto fd : {commands[0], commands[1], responses[0], responses[1]})
                if (fd >= 0)
                    ::close(fd);
        };
        if (::pipe(commands) || ::pipe(responses)) {
            close_pipes();
            throw std::runtime_error("GTK fixture pipe failed");
        }
        pid_ = ::fork();
        if (pid_ < 0) {
            close_pipes();
            throw std::runtime_error("GTK fixture spawn failed");
        }
        if (pid_ == 0) {
            ::dup2(commands[0], STDIN_FILENO);
            ::dup2(responses[1], STDOUT_FILENO);
            for (const auto fd : {commands[0], commands[1], responses[0], responses[1]})
                ::close(fd);
            ::setenv("DISPLAY", display.c_str(), 1);
            if (operator_mode) {
                ::execl(python, python, script, mode, "--operator", static_cast<char *>(nullptr));
                _exit(127);
            }
            ::execl(python, python, script, mode, static_cast<char *>(nullptr));
            _exit(127);
        }
        ::close(commands[0]);
        ::close(responses[1]);
        input_ = commands[1];
        output_ = responses[0];
        try {
            initial = receive();
        } catch (...) {
            stop();
            throw;
        }
    }
    Application(int input, int output) {
        input_ = ::fcntl(input, F_DUPFD_CLOEXEC, 3);
        output_ = ::fcntl(output, F_DUPFD_CLOEXEC, 3);
        if (input_ < 0 || output_ < 0) {
            stop();
            throw std::runtime_error("host fixture pipe duplication failed");
        }
        try {
            initial = state();
        } catch (...) {
            stop();
            throw;
        }
    }
    int input_fd() const { return input_; }
    int output_fd() const { return output_; }
    ~Application() { stop(); }
    void stop() noexcept {
        if (pid_ > 0) {
            ::kill(pid_, SIGTERM);
            ::waitpid(pid_, nullptr, 0);
            pid_ = -1;
        }
        if (input_ >= 0)
            ::close(input_);
        if (output_ >= 0)
            ::close(output_);
        input_ = output_ = -1;
    }
    Json receive() {
        const auto end = std::chrono::steady_clock::now() + std::chrono::seconds{5};
        std::string line;
        while (line.size() < 4096 && std::chrono::steady_clock::now() < end) {
            pollfd fd{output_, POLLIN, 0};
            if (::poll(&fd, 1, 100) < 0)
                throw std::runtime_error("GTK fixture poll failed");
            if (!(fd.revents & (POLLIN | POLLHUP)))
                continue;
            char c = 0;
            if (::read(output_, &c, 1) != 1)
                throw std::runtime_error("GTK fixture exited without state");
            if (c == '\n') {
                auto result = mira::parse_json(line);
                if (!result || !result.value().as_object())
                    throw std::runtime_error("invalid GTK state");
                return std::move(result.value());
            }
            line += c;
        }
        throw std::runtime_error("GTK fixture state deadline/budget exceeded");
    }
    Json state(bool repaint = false) {
        const std::string_view command =
            repaint ? "{\"op\":\"repaint\"}\n" : "{\"op\":\"state\"}\n";
        if (::write(input_, command.data(), command.size()) != static_cast<ssize_t>(command.size()))
            throw std::runtime_error("GTK fixture command failed");
        auto response = receive();
        if (!response.find("kind") || !response.find("kind")->as_string() ||
            *response.find("kind")->as_string() != "state")
            throw std::runtime_error("unexpected fixture state packet");
        return response;
    }
};
double number(const Json &value, const char *key) {
    const auto *field = value.find(key);
    if (!field)
        throw std::runtime_error("missing GTK state field");
    if (field->as_integer())
        return static_cast<double>(*field->as_integer());
    if (field->as_number())
        return *field->as_number();
    throw std::runtime_error("invalid GTK state number");
}
d::PointerPoint point(const d::PointerActionScope &scope, const Json &area) {
    return {scope.geometry.x + static_cast<int>(number(area, "x") + number(area, "width") / 2),
            scope.geometry.y + static_cast<int>(number(area, "y") + number(area, "height") / 2)};
}
unsigned int mask(Display *display) {
    Window root = 0, child = 0;
    int rx = 0, ry = 0, wx = 0, wy = 0;
    unsigned int result = 0;
    MIRAGE_CHECK(XQueryPointer(display, DefaultRootWindow(display), &root, &child, &rx, &ry, &wx,
                               &wy, &result));
    return result;
}
mira::ToolExecutionRecord invoke(mira::BuiltinToolRegistry &registry, const char *name,
                                 const Json &arguments, const mira::OperationContext &ctx) {
    for (const auto &spec : registry.exposed_tools()) {
        if (spec.wire_name != name)
            continue;
        mira::ToolProposal proposal;
        proposal.tool_id = spec.tool_id;
        proposal.tool_version = spec.version;
        proposal.wire_name = name;
        proposal.has_side_effects = spec.has_side_effects;
        proposal.operation_id = mira::OperationId::generate();
        proposal.provider_call_id.value = "gtk-fixture";
        proposal.arguments = arguments;
        proposal.arguments_digest = mira::canonical_json_digest(arguments);
        auto result = registry.execute(proposal, ctx);
        if (!result) {
            mira::ToolExecutionRecord failed;
            failed.failed = true;
            failed.safe_error_summary = result.error().safe_message;
            return failed;
        }
        return result.value();
    }
    throw std::runtime_error("GTK fixture tool unavailable");
}
struct HostFixture {
    std::string display, scenario;
    int input = -1, output = -1;
    Window window = None;
    bool diagnostics = false;
    int cancel_distance = 8;
};
void effects(kairo::Executor &executor, const char *python, const char *script, bool helpers,
             bool resource_extension, bool scaled, const HostFixture &host) {
    std::unique_ptr<mirage::testing::XvfbDisplay> server;
    if (host.scenario.empty())
        server = std::make_unique<mirage::testing::XvfbDisplay>(
            mirage::testing::find_xvfb(), scaled ? "1600x1200x24" : "800x600x24",
            resource_extension);
    const auto display_name = server ? server->display_name() : host.display;
    mirage::platform::linux_backend::LinuxDesktopEnvironment env(
        {}, {.enabled = true, .display = display_name});
    Display *client = XOpenDisplay(display_name.c_str());
    if (!client)
        throw std::runtime_error("GTK fixture X client unavailable");
    int extension_opcode = 0, extension_event = 0, extension_error = 0;
    const bool has_resource = XQueryExtension(client, "X-Resource", &extension_opcode,
                                              &extension_event, &extension_error);
    MIRAGE_CHECK(has_resource == resource_extension);
    struct Close {
        Display *display;
        ~Close() { XCloseDisplay(display); }
    } close{client};
    for (const auto *mode :
         {"scroll", "scroll-blocked", "splitter", "splitter-blocked", "drag-cancel", "native-dnd",
          "native-blocked", "native-cancel", "native-focus", "native-overlay-above",
          "native-overlay-below", "pixel-change"}) {
        if (!host.scenario.empty() && host.scenario != mode)
            continue;
        auto application = host.scenario.empty()
                               ? std::make_unique<Application>(display_name, python, script, mode)
                               : std::make_unique<Application>(host.input, host.output);
        auto &app = *application;
        const auto window = static_cast<Window>(number(app.initial, "window"));
        if (host.scenario.empty()) {
            XSetInputFocus(client, window, RevertToParent, CurrentTime);
            XSync(client, False);
        } else if (window != host.window ||
                   !app.initial.find("operator_release")->as_boolean().value_or(false))
            throw std::runtime_error("host fixture window/release mismatch");
        const auto front = env.window()->front_window({});
        MIRAGE_CHECK(front.ok && front.found);
        if (!front.ok || !front.found || front.window.id != std::to_string(window))
            throw std::runtime_error("fixture is not the foreground window; no input attempted");
        const auto scale = static_cast<int>(number(app.initial, "scale"));
        if (scale < 1 || scale > 4 ||
            front.window.geometry.width != number(app.initial, "pixel_width") ||
            front.window.geometry.height != number(app.initial, "pixel_height"))
            throw std::runtime_error("fixture physical geometry mismatch; no input attempted");
        const d::PointerActionScope scope{front.window.id, front.window.geometry};
        const std::string scenario = mode;
        auto owner = std::make_shared<d::ActionCoordinator>();
        const auto &rows = *app.initial.find("rows")->as_array();
        const bool native = scenario.starts_with("native-");
        const auto from = scenario.starts_with("scroll")
                              ? point(scope, *app.initial.find("scroll_target"))
                          : native ? point(scope, rows[0])
                                   : point(scope, *app.initial.find("handle"));
        const auto to =
            native ? point(scope, rows[3]) : d::PointerPoint{from.x + 60 * scale, from.y};
        std::unique_ptr<mirage::testing::X11PointerDiagnostics> diagnostics;
        if (host.diagnostics)
            diagnostics = std::make_unique<mirage::testing::X11PointerDiagnostics>(client, window);
        bool sampled_press = false, sampled_motion = false, sampled_late = false;
        // This X client is separate from the GTK source connection, even
        // though the driver and its adversarial windows share one PID.
        XSetWindowAttributes overlay_attributes{};
        overlay_attributes.override_redirect = True;
        const auto adversary =
            XCreateWindow(client, DefaultRootWindow(client), scope.geometry.x, scope.geometry.y,
                          scope.geometry.width, scope.geometry.height, 0, CopyFromParent,
                          InputOutput, CopyFromParent, CWOverrideRedirect, &overlay_attributes);
        d::CancelToken stop;
        bool cancelled_held = false, interrupted = false;
        d::CancelToken cancel{[&] {
            Window root = None, child = None;
            int rx = 0, ry = 0, wx = 0, wy = 0;
            unsigned int held = 0;
            if (!XQueryPointer(client, DefaultRootWindow(client), &root, &child, &rx, &ry, &wx, &wy,
                               &held))
                return false;
            if (scenario == "drag-cancel" && (held & Button1Mask)) {
                cancelled_held = true;
                stop.request_cancel();
            }
            if (diagnostics && (held & Button1Mask)) {
                if (!sampled_press) {
                    sampled_press = true;
                    diagnostics->sample("press");
                }
                if (!sampled_motion && ry > from.y) {
                    sampled_motion = true;
                    diagnostics->sample("first-motion");
                }
                if (!sampled_late && ry >= from.y + 50 * scale) {
                    sampled_late = true;
                    diagnostics->sample("late-motion");
                }
            }
            const int interrupt_distance =
                !host.scenario.empty() && scenario == "native-cancel" ? host.cancel_distance : 50;
            if (helpers && !interrupted && native && (held & Button1Mask) &&
                ry >= from.y + interrupt_distance * scale && scenario != "native-dnd" &&
                scenario != "native-blocked") {
                interrupted = true;
                if (scenario == "native-cancel") {
                    cancelled_held = true;
                    stop.request_cancel();
                } else if (scenario == "native-focus") {
                    XMapWindow(client, adversary);
                    XSetInputFocus(client, adversary, RevertToParent, CurrentTime);
                } else {
                    XMapWindow(client, adversary);
                    XWindowChanges changes{};
                    changes.sibling = child;
                    changes.stack_mode = scenario == "native-overlay-below" ? Below : Above;
                    XConfigureWindow(client, adversary, CWSibling | CWStackMode, &changes);
                }
                XSync(client, False);
            }
            return stop.cancelled();
        }};
        // Candidate fixture isolates input compatibility; real native toolkit
        // effect is the independent oracle. Do not claim OCR localization.
        i::FakeOcrConfig candidates;
        candidates.info.accepted_formats = {mirador::PixelFormat::kBgra8};
        for (const auto target : {from, to})
            candidates.regions.push_back(
                {{static_cast<float>(target.x - scope.geometry.x - 2),
                  static_cast<float>(target.y - scope.geometry.y - 8), 4, 16},
                 "target",
                 0.9f,
                 {}});
        i::FakeOcrBackend ocr(candidates);
        d::VisualReferenceRegistry visual;
        i::VisualObservationPipelineConfig config;
        config.source_id = "gtk-pointer-" + scenario;
        config.roi_capture = config.run_ocr = config.refresh_ocr = true;
        config.session.ocr_backend = &ocr;
        i::VisualObservationPipeline pipeline(executor, *env.screen(), visual, config);
        std::string error;
        if (!pipeline.start(error))
            throw std::runtime_error("GTK fixture visual owner failed");
        i::DesktopToolContext tools;
        tools.environment = std::shared_ptr<d::DesktopEnvironment>(&env, [](auto *) {});
        tools.actions = owner;
        tools.capture_scope = scope.window_id;
        tools.gate = [](const auto &, const auto &, const auto &probe) { return !probe(); };
        d::ImageFrame host_source;
        tools.mark = [&](const auto &crop, const auto &token, const auto &deadline) {
            auto result = pipeline.refresh_marked(crop, token, deadline);
            if (!host.scenario.empty() || scenario == "pixel-change")
                host_source = result.frame;
            if (scenario == "pixel-change")
                (void)app.state(true);
            return result;
        };
        tools.with_visual_reference = [&](const auto &s, const auto &ref, const auto &action) {
            return visual.with_reference(s, ref, action);
        };
        tools.with_visual_references = [&](const auto &s, const auto &a, const auto &b,
                                           const auto &action) {
            return visual.with_references(s, a, b, action);
        };
        auto ctx = mira::make_control_context();
        ctx.task = mira::TaskId::generate();
        ctx.session = mira::SessionId::generate();
        ctx.task_epoch = 1;
        ctx.deadline = std::chrono::steady_clock::now() + std::chrono::seconds{30};
        i::detail::DesktopActionVerification verification(tools, ctx, cancel);
        i::detail::DesktopImageTurn turn(std::make_shared<i::detail::DesktopImageSource>(), tools,
                                         ctx, cancel);
        mira::BuiltinToolRegistry registry;
        turn.register_tool(registry, [&](const auto &name, mira::BuiltinToolHandler handler) {
            return [&, name, handler](const auto &args, const auto &operation) {
                return i::detail::DesktopActionVerification::mutation(name)
                           ? verification.execute(name, args, operation, handler)
                           : handler(args, operation);
            };
        });
        verification.register_tool(registry, {});
        const auto marked = invoke(registry, "desktop.vision.mark", Json{Json::Object{}}, ctx);
        if (marked.failed || !marked.result.find("marks"))
            throw std::runtime_error("GTK fixture mark failed: " + marked.safe_error_summary);
        MIRAGE_CHECK(marked.result.find("marks")->as_array()->size() == 2);
        const std::string expected_title = (host.scenario.empty() ? "" : "Mirage M7-09 | ") +
                                           std::string{scenario.starts_with("scroll") ? "Scrolled"
                                                       : native                       ? "Dropped"
                                                                                      : "Resized"};
        Json::Object arguments{{"visual_scope", *marked.result.find("visual_scope")},
                               {"environment_epoch", *marked.result.find("environment_epoch")},
                               {"window_id", scope.window_id},
                               {"mark", std::int64_t{1}},
                               {"verify_kind", "window_title_equals"},
                               {"expected_title", expected_title}};
        const bool scroll = scenario.starts_with("scroll");
        if (scroll) {
            arguments.emplace_back("horizontal_steps", std::int64_t{2});
            arguments.emplace_back("vertical_steps", std::int64_t{3});
        } else
            arguments.emplace_back("destination_mark", std::int64_t{2});
        const auto dispatched =
            invoke(registry, scroll ? "desktop.vision.scroll" : "desktop.vision.drag",
                   Json{arguments}, ctx);
        std::fprintf(stdout, "GTK_DISPATCH %s failed=%d %s\n", mode, dispatched.failed,
                     mira::to_json_string(dispatched.result).c_str());
        if (diagnostics) {
            diagnostics->sample("after-action");
            const auto report = diagnostics->report();
            MIRAGE_CHECK(report.find("prepared")->as_boolean().value_or(false));
            MIRAGE_CHECK(report.find("samples")->as_array()->size() <= 4);
            std::fprintf(stdout, "GTK_POINTER_DIAGNOSTIC %s %s\n", mode,
                         mira::to_json_string(report).c_str());
        }
        if ((!host.scenario.empty() || scenario == "pixel-change") && dispatched.failed &&
            dispatched.safe_error_summary.find("stale_observation") != std::string::npos) {
            const auto now = env.screen()->capture_roi(scope.geometry, {}, {});
            if (now.ok && now.frame.width == host_source.width &&
                now.frame.height == host_source.height && now.frame.stride == host_source.stride &&
                now.frame.pixels.size() == host_source.pixels.size()) {
                int left = now.frame.width, top = now.frame.height, right = -1, bottom = -1;
                std::size_t changed = 0;
                for (int y = 0; y < now.frame.height; ++y)
                    for (int x = 0; x < now.frame.width; ++x) {
                        const auto offset = static_cast<std::size_t>(y) * now.frame.stride +
                                            static_cast<std::size_t>(x) * 4;
                        if (!std::equal(now.frame.pixels.begin() + offset,
                                        now.frame.pixels.begin() + offset + 4,
                                        host_source.pixels.begin() + offset)) {
                            ++changed;
                            left = std::min(left, x);
                            top = std::min(top, y);
                            right = std::max(right, x);
                            bottom = std::max(bottom, y);
                        }
                    }
                std::fprintf(stdout, "GTK_PIXEL_CHANGE %s count=%zu bounds=%d,%d,%d,%d\n", mode,
                             changed, left, top, right, bottom);
                if (scenario == "pixel-change")
                    MIRAGE_CHECK(changed > 0 && left >= 0 && top >= 0);
            }
        }
        auto state = app.state();
        const auto end = std::chrono::steady_clock::now() + std::chrono::seconds{2};
        for (int attempt = 0; attempt < 32 && std::chrono::steady_clock::now() < end; ++attempt) {
            if ((scenario == "scroll" && number(state, "horizontal") > 0 &&
                 number(state, "vertical") > 0) ||
                (scenario == "splitter" &&
                 number(state, "position") > number(app.initial, "position")) ||
                (scenario == "native-dnd" && state.find("title")->as_string() &&
                 *state.find("title")->as_string() == expected_title))
                break;
            state = app.state();
        }
        const bool physical_released = !(mask(client) & Button1Mask);
        const bool owner_released = owner->input_released();
        MIRAGE_CHECK(physical_released && owner_released);
        std::fprintf(stdout, "GTK_RELEASE %s physical=%d owner=%d\n", mode, physical_released,
                     owner_released);
        if (scenario == "pixel-change") {
            MIRAGE_CHECK(dispatched.failed && dispatched.safe_error_summary.find(
                                                  "stale_observation") != std::string::npos);
            MIRAGE_CHECK(dispatched.safe_error_summary.find(
                             "\"side_effect_may_have_occurred\":false") != std::string::npos);
            MIRAGE_CHECK(app.initial.find("position")->as_integer() ==
                         state.find("position")->as_integer());
        } else if (scenario == "drag-cancel") {
            MIRAGE_CHECK(cancelled_held && dispatched.failed);
            MIRAGE_CHECK(number(state, "position") == number(app.initial, "position"));
        } else if (native && scenario != "native-blocked" &&
                   (!helpers || scenario != "native-dnd")) {
            const auto expected_error = !helpers                      ? "target_obscured"
                                        : scenario == "native-cancel" ? "cancelled"
                                        : scenario == "native-focus"  ? "focus_changed"
                                                                      : "target_obscured";
            MIRAGE_CHECK(dispatched.failed &&
                         dispatched.safe_error_summary.find(expected_error) != std::string::npos);
            if (helpers) {
                MIRAGE_CHECK(interrupted);
                MIRAGE_CHECK(number(*state.find("drag_signals"), "begin") == 1);
                if (scenario == "native-cancel")
                    MIRAGE_CHECK(cancelled_held);
            } else
                MIRAGE_CHECK(mira::to_json_string(*state.find("order")) ==
                             mira::to_json_string(*app.initial.find("order")));
        } else {
            MIRAGE_CHECK(!dispatched.failed);
            if (scenario == "native-dnd") {
                const Json expected{Json::Array{"Bravo", "Charlie", "Alpha", "Delta", "Echo"}};
                MIRAGE_CHECK(mira::to_json_string(*state.find("order")) ==
                             mira::to_json_string(expected));
                MIRAGE_CHECK(number(*state.find("drag_signals"), "drop") == 1 &&
                             number(*state.find("drag_signals"), "failed") == 0);
            }
            if (scenario == "native-blocked")
                MIRAGE_CHECK(mira::to_json_string(*state.find("order")) ==
                             mira::to_json_string(*app.initial.find("order")));
            if (scenario == "scroll")
                MIRAGE_CHECK(number(state, "horizontal") > 0 && number(state, "vertical") > 0);
            if (scenario == "scroll-blocked")
                MIRAGE_CHECK(number(state, "horizontal") == number(app.initial, "horizontal") &&
                             number(state, "vertical") == number(app.initial, "vertical"));
            if (scenario == "splitter")
                MIRAGE_CHECK(number(state, "position") >= number(app.initial, "position") + 40);
            if (scenario == "splitter-blocked")
                MIRAGE_CHECK(number(state, "position") == number(app.initial, "position"));
        }
        const bool expected_success =
            scenario == "scroll" || scenario == "splitter" || (helpers && scenario == "native-dnd");
        if (!dispatched.failed) {
            Json verify_args{Json::Object{{"action_id", *dispatched.result.find("action_id")}}};
            const auto verified = invoke(registry, "desktop.action.verify", verify_args, ctx);
            MIRAGE_CHECK(!verified.failed && verified.result.find("verified") &&
                         verified.result.find("verified")->as_boolean().value_or(false) ==
                             expected_success);
        }
        const bool settled = verification.settle();
        MIRAGE_CHECK(settled == expected_success);
        MIRAGE_CHECK(invoke(registry, scroll ? "desktop.vision.scroll" : "desktop.vision.drag",
                            Json{arguments}, ctx)
                         .failed);
        pipeline.stop();
        XDestroyWindow(client, adversary);
        XSync(client, False);
        std::fprintf(stdout, "GTK_EFFECT %s %s\n", mode, mira::to_json_string(state).c_str());
        std::fprintf(stdout, "GTK_ACTION %s verified=%d %s\n", mode, settled,
                     dispatched.safe_error_summary.c_str());
        std::fflush(stdout);
    }
}
void handshake(kairo::Executor &executor, const char *python, const char *script, bool helpers,
               bool scaled) {
    if (!helpers)
        throw std::runtime_error("private host handshake requires XRes-enabled build");
    mirage::testing::XvfbDisplay server(mirage::testing::find_xvfb(), "1600x1200x24");
    Display *client = XOpenDisplay(server.display_name().c_str());
    if (!client)
        throw std::runtime_error("handshake X client unavailable");
    struct Close {
        Display *display;
        ~Close() { XCloseDisplay(display); }
    } close{client};
    for (const auto *mode : {"scroll", "native-dnd", "native-blocked", "native-cancel"}) {
        Application app(server.display_name(), python, script, mode, true);
        const auto window = static_cast<Window>(number(app.initial, "window"));
        XSetInputFocus(client, window, RevertToParent, CurrentTime);
        Window child = None;
        int origin_x = 0, origin_y = 0;
        MIRAGE_CHECK(XTranslateCoordinates(client, window, DefaultRootWindow(client), 0, 0,
                                           &origin_x, &origin_y, &child));
        const d::PointerActionScope scope{std::to_string(window),
                                          {origin_x, origin_y,
                                           static_cast<int>(number(app.initial, "pixel_width")),
                                           static_cast<int>(number(app.initial, "pixel_height"))}};
        const auto target = point(scope, *app.initial.find("operator_target"));
        MIRAGE_CHECK(XTestFakeMotionEvent(client, -1, target.x, target.y, CurrentTime));
        MIRAGE_CHECK(XTestFakeButtonEvent(client, Button1, True, CurrentTime));
        MIRAGE_CHECK(XTestFakeButtonEvent(client, Button1, False, CurrentTime));
        XSync(client, False);
        const auto armed = app.receive();
        MIRAGE_CHECK(armed.find("operator_release")->as_boolean().value_or(false));
        MIRAGE_CHECK(!(mask(client) & Button1Mask));
        const HostFixture fixture{server.display_name(), mode,   app.input_fd(),
                                  app.output_fd(),       window, true,
                                  scaled ? 50 : 8};
        effects(executor, python, script, helpers, true, false, fixture);
    }
}
void click_sequence_effects(const char *python, const char *script) {
    mirage::testing::XvfbDisplay server(mirage::testing::find_xvfb(), "800x600x24");
    mirage::platform::linux_backend::LinuxDesktopEnvironment env(
        {}, {.enabled = true, .display = server.display_name()});
    Display *client = XOpenDisplay(server.display_name().c_str());
    if (!client)
        throw std::runtime_error("click sequence X client unavailable");
    struct Close {
        Display *display;
        ~Close() { XCloseDisplay(display); }
    } close{client};
    for (const auto count : {1u, 2u, 3u, 5u, d::max_click_count}) {
        Application app(server.display_name(), python, script, "click-sequence");
        const auto window = static_cast<Window>(number(app.initial, "window"));
        XSetInputFocus(client, window, RevertToParent, CurrentTime);
        XSync(client, False);
        const auto front = env.window()->front_window({});
        if (!front.ok || !front.found || front.window.id != std::to_string(window))
            throw std::runtime_error("click sequence fixture focus mismatch");
        const d::PointerActionScope scope{front.window.id, front.window.geometry};
        const auto at = point(scope, app.initial.find("rows")->as_array()->front());
        const auto result = env.input()->click_sequence_at(at.x, at.y, "left", count, {}, {});
        MIRAGE_CHECK(result.ok && result.input_released);
        Json state;
        const auto end = std::chrono::steady_clock::now() + std::chrono::seconds{1};
        do {
            (void)::poll(nullptr, 0, 10);
            state = app.state();
        } while (number(*state.find("click_events"), "releases") < count &&
                 std::chrono::steady_clock::now() < end);
        MIRAGE_CHECK(number(*state.find("click_events"), "presses") == count);
        MIRAGE_CHECK(number(*state.find("click_events"), "releases") == count);
        if (count <= 2)
            MIRAGE_CHECK(number(state, "row_activations") == (count == 2 ? 1 : 0));
        if (count == 3)
            MIRAGE_CHECK(number(*state.find("click_events"), "triples") == 1);
        MIRAGE_CHECK(!(mask(client) & Button1Mask));
        std::fprintf(stderr, "GTK click_count=%u presses=%.0f releases=%.0f row_activations=%.0f\n",
                     count, number(*state.find("click_events"), "presses"),
                     number(*state.find("click_events"), "releases"),
                     number(state, "row_activations"));
    }
}
} // namespace
int main(int argc, char **argv) {
    if (argc != 3 && argc != 4)
        return 2;
    const std::string_view option = argc == 4 ? argv[3] : "";
    const bool clicks_only = option == "--click-sequence-only" || option == "--double-click-only";
    const bool private_handshake = option == "--handshake" || option == "--scaled-handshake";
    const bool scaled = option == "--scaled" || option == "--scaled-handshake";
    const bool resource_extension = option != "--without-xres";
    HostFixture host;
    if (option.starts_with("--host=")) {
        host.scenario = option.substr(7);
        if (host.scenario != "scroll" && host.scenario != "native-dnd" &&
            host.scenario != "native-blocked" && host.scenario != "native-cancel")
            return 2;
        const auto *display = std::getenv("DISPLAY"), *session = std::getenv("XDG_SESSION_TYPE");
        if (!display || !session || std::string_view{session} != "x11" || display[0] != ':')
            return 2;
        host.display = display;
        const auto integer = [](const char *key) -> unsigned long {
            const auto *value = std::getenv(key);
            char *end = nullptr;
            const auto parsed = value ? std::strtoul(value, &end, 10) : 0;
            if (!value || !value[0] || !end || *end || !parsed)
                throw std::runtime_error("invalid host fixture descriptor/window");
            return parsed;
        };
        try {
            const auto input = integer("MIRAGE_GTK_HOST_INPUT_FD");
            const auto output = integer("MIRAGE_GTK_HOST_OUTPUT_FD");
            if (input < 3 || output < 3 || input > std::numeric_limits<int>::max() ||
                output > std::numeric_limits<int>::max() || input == output)
                return 2;
            host.input = static_cast<int>(input);
            host.output = static_cast<int>(output);
            host.window = integer("MIRAGE_GTK_HOST_WINDOW");
            host.diagnostics = std::getenv("MIRAGE_GTK_HOST_DIAGNOSTICS") != nullptr;
            if (std::getenv("MIRAGE_GTK_HOST_CANCEL_DISTANCE")) {
                const auto distance = integer("MIRAGE_GTK_HOST_CANCEL_DISTANCE");
                if (distance != 8 && distance != 50)
                    return 2;
                host.cancel_distance = static_cast<int>(distance);
            }
        } catch (const std::exception &error) {
            std::fprintf(stderr, "%s\n", error.what());
            return 2;
        }
    } else if (!option.empty() && !clicks_only && option != "--strict" &&
               option != "--without-xres" && !scaled && !private_handshake)
        return 2;
#ifdef MIRAGE_TEST_X11_DRAG_HELPERS
    const bool helpers = option != "--strict" && resource_extension;
#else
    const bool helpers = false;
#endif
    if (!host.scenario.empty() && !helpers)
        return 2;
    if (scaled)
        ::setenv("GDK_SCALE", "2", 1);
    kairo::Executor executor;
    ::signal(SIGPIPE, SIG_IGN);
    MIRAGE_CHECK(executor.initialize({}).ok);
    try {
        executor
            .submit_auto([&] {
                if (clicks_only)
                    click_sequence_effects(argv[1], argv[2]);
                else if (private_handshake)
                    handshake(executor, argv[1], argv[2], helpers, scaled);
                else
                    effects(executor, argv[1], argv[2], helpers, resource_extension, scaled, host);
            })
            .get();
        executor.shutdown(true);
    } catch (const std::exception &error) {
        std::fprintf(stderr, "GTK pointer fixture: %s\n", error.what());
        executor.shutdown(true);
        return 1;
    }
    return mirage::testing::finish("gtk_pointer_effects");
}
