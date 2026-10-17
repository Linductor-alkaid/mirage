#include "../../integration/mira/src/desktop_action_verification.hpp"
#include "../../integration/mira/src/desktop_image_supply.hpp"
#include "../../integration/mirador/src/ppocr_pipeline.hpp"
#include "../support/xvfb_display.hpp"
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <memory>
#include <mira/model_digest.hpp>
#include <mirage/integration/fake_visual_backend.hpp>
#include <mirage/integration/model_layer.hpp>
#include <mirage/integration/visual_observation_pipeline.hpp>
#include <mirage/platform/linux/linux_desktop_environment.hpp>
#include <source_location>
#include <stdexcept>
#include <thread>

#include <X11/Xlib.h>
#include <X11/Xutil.h>

namespace d = mirage::desktop;
namespace i = mirage::integration;
namespace detail = i::detail;
using Json = mira::JsonValue;
namespace {
int checks = 0;
void check(bool value, const std::source_location where = std::source_location::current()) {
    ++checks;
    if (!value) {
        std::cerr << "X11 marks check at line " << where.line() << "\n";
        throw std::runtime_error("X11 marks check at line " + std::to_string(where.line()));
    }
}
mira::ToolExecutionRecord invoke(mira::BuiltinToolRegistry &registry, const std::string &name,
                                 const Json &args, const mira::OperationContext &ctx) {
    for (const auto &spec : registry.exposed_tools())
        if (spec.wire_name == name) {
            mira::ToolProposal call;
            call.tool_id = spec.tool_id;
            call.tool_version = spec.version;
            call.wire_name = name;
            call.has_side_effects = spec.has_side_effects;
            call.operation_id = mira::OperationId::generate();
            call.provider_call_id.value = name;
            call.arguments = args;
            call.arguments_digest = mira::canonical_json_digest(args);
            auto result = registry.execute(call, ctx);
            if (!result)
                std::cerr << result.error().safe_message << "\n";
            check(static_cast<bool>(result));
            return result.value();
        }
    throw std::runtime_error("X11 visual tool missing");
}
void capture_gate_negative(kairo::Executor &, Display *, Window,
                           const std::shared_ptr<d::DesktopEnvironment> &, bool);
void run() {
    check(XInitThreads() != 0);
    mirage::testing::XvfbDisplay server(mirage::testing::find_xvfb(), "200x140x24");
    Display *client = XOpenDisplay(server.display_name().c_str());
    check(client != nullptr);
    struct Close {
        Display *display;
        ~Close() { XCloseDisplay(display); }
    } close{client};
    const Window canvas =
        XCreateSimpleWindow(client, DefaultRootWindow(client), 20, 20, 160, 100, 0, 0, 0xffffff);
    XStoreName(client, canvas, "Before");
    XSelectInput(client, canvas, ButtonPressMask | ButtonReleaseMask);
    XMapWindow(client, canvas);
    XSetInputFocus(client, canvas, RevertToParent, CurrentTime);
    const auto gc = XCreateGC(client, canvas, 0, nullptr);
    XSetForeground(client, gc, 0x2563eb);
    XFillRectangle(client, canvas, gc, 20, 20, 60, 30);
    XFreeGC(client, gc);
    XSync(client, False);
    auto env = std::make_shared<mirage::platform::linux_backend::LinuxDesktopEnvironment>(
        std::vector<std::filesystem::path>{},
        mirage::platform::linux_backend::X11Options{.enabled = true,
                                                    .display = server.display_name()});
    check(env->screen() && env->window() && env->input() && !env->accessibility());
    const auto front = env->window()->front_window();
    check(front.ok && front.found);
    kairo::Executor executor;
    check(executor.initialize({}).ok);
    {
        i::FakeOcrConfig backend;
        backend.info.accepted_formats = {mirador::PixelFormat::kBgra8};
        backend.regions = {{{40, 40, 60, 30}, "fixture rectangle", 0.9f, {}}};
        i::FakeOcrBackend ocr(backend);
        d::VisualReferenceRegistry visual;
        i::VisualObservationPipelineConfig config;
        config.run_ocr = true;
        config.refresh_ocr = true;
        config.session.ocr_backend = &ocr;
        i::VisualObservationPipeline pipeline(executor, *env->screen(), visual, config);
        std::string failure;
        check(pipeline.start(failure));
        i::DesktopToolContext desktop;
        desktop.environment = env;
        desktop.actions = std::make_shared<d::ActionCoordinator>();
        desktop.gate = [](const auto &, const auto &, const auto &cancel) { return !cancel(); };
        desktop.mark = [&](const auto &crop, const auto &cancel, const auto &end) {
            return pipeline.refresh_marked(crop, cancel, end);
        };
        desktop.with_visual_reference = [&](const auto &scope, const auto &ref,
                                            const auto &action) {
            return visual.with_reference(scope, ref, action);
        };
        auto ctx = mira::make_control_context();
        ctx.task = mira::TaskId::generate();
        ctx.session = mira::SessionId::generate();
        ctx.task_epoch = 1;
        detail::DesktopImageTurn turn(std::make_shared<detail::DesktopImageSource>(), desktop, ctx,
                                      {});
        detail::DesktopActionVerification verification(desktop, ctx, {});
        mira::BuiltinToolRegistry registry;
        const i::DesktopAtomToolset::Decorator decorate = [&](const std::string &name,
                                                              mira::BuiltinToolHandler handler) {
            return [&, name, handler](const auto &args, const auto &operation) {
                return detail::DesktopActionVerification::mutation(name)
                           ? verification.execute(name, args, operation, handler)
                           : handler(args, operation);
            };
        };
        turn.register_tool(registry, decorate);
        verification.register_tool(registry, decorate);
        auto marked = invoke(registry, "desktop.vision.mark", Json{Json::Object{}}, ctx);
        check(!marked.failed);
        auto target =
            Json{Json::Object{{"window_id", front.window.id},
                              {"visual_scope", *marked.result.find("visual_scope")},
                              {"environment_epoch", *marked.result.find("environment_epoch")},
                              {"mark", std::int64_t{1}},
                              {"verify_kind", "window_title_equals"},
                              {"expected_title", "Clicked"}}};
        auto clicked = invoke(registry, "desktop.vision.click", target, ctx);
        if (clicked.failed)
            std::cerr << clicked.safe_error_summary << '\n';
        check(!clicked.failed);
        check(!*clicked.result.find("verified")->as_boolean());
        bool pressed = false, released = false;
        XSync(client, False);
        for (int n = 0; n < 32 && XPending(client); ++n) {
            XEvent event{};
            XNextEvent(client, &event);
            if (event.type == ButtonPress) {
                pressed =
                    event.xbutton.button == 1 && event.xbutton.x == 50 && event.xbutton.y == 35;
                XStoreName(client, canvas, "Clicked");
            }
            if (event.type == ButtonRelease)
                released = event.xbutton.button == 1;
        }
        XSync(client, False);
        check(pressed && released);
        auto verified =
            invoke(registry, "desktop.action.verify",
                   Json{Json::Object{{"action_id", *clicked.result.find("action_id")}}}, ctx);
        check(!verified.failed && *verified.result.find("verified")->as_boolean());
        check(verification.settle());
        check(invoke(registry, "desktop.vision.click", target, ctx)
                  .failed); // Old pixels/epoch retired.
        pipeline.stop();
    }
    capture_gate_negative(executor, client, canvas, env, true);
    capture_gate_negative(executor, client, canvas, env, false);
    executor.shutdown(true);
    XDestroyWindow(client, canvas);
}
struct CanvasScript final : mira::IModelProvider {
    mira::ModelProfile profile_;
    std::string window, expected;
    std::function<void()> service_canvas;
    std::size_t step = 0, images = 0;
    Json marks, action;
    bool saw_verified = false;
    bool geometry = false;
    std::string expected_template;
    int target_x = 0;
    int target_y = 0;
    std::size_t selected = 0;
    explicit CanvasScript(mira::ModelProfile profile) : profile_(std::move(profile)) {}
    const mira::ModelProfile &profile() const override { return profile_; }
    mira::Result<mira::ModelResponse> infer(const mira::ModelRequest &request,
                                            const mira::OperationContext &,
                                            const mira::ProviderInferOptions &) override {
        if (step == 2)
            service_canvas();
        for (const auto &item : request.input)
            for (const auto &part : item.content) {
                if (std::holds_alternative<mira::ImagePart>(part) && step == 1)
                    ++images;
                if (const auto *result = std::get_if<mira::ToolResultPart>(&part);
                    result && !result->failed) {
                    if (result->result.find("marks"))
                        marks = result->result;
                    if (result->result.find("action_id"))
                        action = result->result;
                    if (step == 3 && result->result.find("status") &&
                        result->result.find("verified") &&
                        result->result.find("verified")->as_boolean().value_or(false))
                        saw_verified = true;
                }
            }
        mira::ModelResponse response;
        response.request_id = request.request_id;
        response.operation_id = request.operation_id;
        response.profile_id = profile_.id;
        response.status = mira::ModelCompletionStatus::Completed;
        if (step < 3) {
            const std::string name = step == 0   ? "desktop.vision.mark"
                                     : step == 1 ? "desktop.vision.click"
                                                 : "desktop.action.verify";
            Json args{Json::Object{}};
            if (step == 1) {
                check(images == 1 && marks.find("marks") &&
                      !marks.find("marks")->as_array()->empty());
                const auto &candidates = *marks.find("marks")->as_array();
                if (!expected_template.empty()) {
                    std::size_t matches = 0;
                    for (std::size_t n = 0; n < candidates.size(); ++n)
                        if (*candidates[n].find("template_id")->as_string() == expected_template) {
                            selected = n;
                            ++matches;
                        }
                    check(matches == 1);
                } else if (geometry) {
                    std::size_t matches = 0;
                    for (std::size_t n = 0; n < candidates.size(); ++n) {
                        const auto &candidate = candidates[n];
                        const auto x = *candidate.find("x")->as_integer();
                        const auto y = *candidate.find("y")->as_integer();
                        const auto w = *candidate.find("width")->as_integer();
                        const auto h = *candidate.find("height")->as_integer();
                        if (x < target_x && x + w > target_x && y < target_y && y + h > target_y &&
                            w >= 55 && w <= 85 && h >= 55 && h <= 85) {
                            selected = n;
                            ++matches;
                        }
                    }
                    check(matches == 1);
                } else
                    check(candidates.size() == 1);
                std::cout << "TARGET " << mira::to_json_string(candidates[selected]) << '\n';
                args = Json{Json::Object{{"window_id", window},
                                         {"visual_scope", *marks.find("visual_scope")},
                                         {"environment_epoch", *marks.find("environment_epoch")},
                                         {"mark", *candidates[selected].find("mark")},
                                         {"verify_kind", "visual_text_exists"},
                                         {"expected_text", expected}}};
            }
            if (step == 2) {
                check(action.find("action_id"));
                args = Json{Json::Object{{"action_id", *action.find("action_id")}}};
            }
            const auto tool = std::find_if(request.tools.begin(), request.tools.end(),
                                           [&](const auto &t) { return t.wire_name == name; });
            check(tool != request.tools.end());
            mira::ToolCallOutput call;
            call.tool_id = tool->tool_id;
            call.provider_name = name;
            call.provider_call_id.value = "canvas-step-" + std::to_string(step);
            call.arguments = std::move(args);
            call.arguments_digest = mira::digest_string(mira::to_json_string(call.arguments));
            response.output.emplace_back(std::move(call));
        } else {
            mira::MessageOutput answer;
            answer.content.emplace_back(mira::OutputTextPart{"Canvas task completed", {}});
            response.output.emplace_back(std::move(answer));
        }
        ++step;
        return response;
    }
};
struct CanvasPixels {
    int width = 0, height = 0;
    std::vector<unsigned char> rgb;
};
CanvasPixels load_canvas(const std::string &path) {
    std::ifstream input(path, std::ios::binary);
    CanvasPixels p;
    std::string format;
    int maximum = 0;
    input >> format >> p.width >> p.height >> maximum;
    check(format == "P6" && p.width == 480 && p.height == 180 && maximum == 255);
    check(input.get() == '\n');
    p.rgb.resize(static_cast<std::size_t>(p.width * p.height * 3));
    check(static_cast<bool>(input.read(reinterpret_cast<char *>(p.rgb.data()),
                                       static_cast<std::streamsize>(p.rgb.size()))));
    check(input.peek() == std::char_traits<char>::eof());
    return p;
}
Pixmap paint_canvas(Display *display, Window canvas, const CanvasPixels &pixels,
                    Pixmap previous = None) {
    XImage *image = XCreateImage(display, DefaultVisual(display, DefaultScreen(display)), 24,
                                 ZPixmap, 0, nullptr, static_cast<unsigned>(pixels.width),
                                 static_cast<unsigned>(pixels.height), 32, 0);
    check(image != nullptr);
    image->data = static_cast<char *>(std::calloc(static_cast<std::size_t>(image->bytes_per_line),
                                                  static_cast<std::size_t>(pixels.height)));
    check(image->data != nullptr);
    for (int y = 0; y < pixels.height; ++y)
        for (int x = 0; x < pixels.width; ++x) {
            const auto offset = static_cast<std::size_t>((y * pixels.width + x) * 3);
            XPutPixel(image, x, y,
                      (static_cast<unsigned long>(pixels.rgb[offset]) << 16) |
                          (static_cast<unsigned long>(pixels.rgb[offset + 1]) << 8) |
                          pixels.rgb[offset + 2]);
        }
    // A WM may reparent/unmap the window after the first draw. A retained
    // background pixmap restores the public fixture on every exposure.
    const Pixmap background = XCreatePixmap(display, canvas, pixels.width, pixels.height, 24);
    check(background != None);
    const GC gc = XCreateGC(display, background, 0, nullptr);
    XPutImage(display, background, gc, image, 0, 0, 0, 0, static_cast<unsigned>(pixels.width),
              static_cast<unsigned>(pixels.height));
    XSetWindowBackgroundPixmap(display, canvas, background);
    XClearWindow(display, canvas);
    XFreeGC(display, gc);
    XDestroyImage(image);
    if (previous != None)
        XFreePixmap(display, previous);
    XSync(display, False);
    return background;
}
bool canvas_matches(Display *display, Window canvas, const CanvasPixels &pixels) {
    // Fixture-only readback from our own drawable. Product capture still uses
    // ScreenProvider's root pixels and never substitutes this image.
    XImage *image =
        XGetImage(display, canvas, 0, 0, pixels.width, pixels.height, AllPlanes, ZPixmap);
    if (image == nullptr)
        return false;
    bool matches = true;
    for (int y = 0; y < pixels.height && matches; ++y)
        for (int x = 0; x < pixels.width; ++x) {
            const auto offset = static_cast<std::size_t>((y * pixels.width + x) * 3);
            const auto expected = (static_cast<unsigned long>(pixels.rgb[offset]) << 16) |
                                  (static_cast<unsigned long>(pixels.rgb[offset + 1]) << 8) |
                                  pixels.rgb[offset + 2];
            if (XGetPixel(image, x, y) != expected) {
                matches = false;
                break;
            }
        }
    XDestroyImage(image);
    return matches;
}
bool frame_matches(const d::ImageFrame &frame, const CanvasPixels &pixels) {
    if (frame.format != d::ImageFormat::Bgra8 || frame.width != pixels.width ||
        frame.height != pixels.height ||
        frame.stride < static_cast<std::size_t>(pixels.width) * 4 ||
        frame.pixels.size() / frame.stride < static_cast<std::size_t>(pixels.height))
        return false;
    for (int y = 0; y < pixels.height; ++y)
        for (int x = 0; x < pixels.width; ++x) {
            const auto source = static_cast<std::size_t>((y * pixels.width + x) * 3);
            const auto captured = static_cast<std::size_t>(y) * frame.stride + x * 4;
            if (frame.pixels[captured] != pixels.rgb[source + 2] ||
                frame.pixels[captured + 1] != pixels.rgb[source + 1] ||
                frame.pixels[captured + 2] != pixels.rgb[source])
                return false;
        }
    return true;
}
void capture_gate_negative(kairo::Executor &executor, Display *client, Window canvas,
                           const std::shared_ptr<d::DesktopEnvironment> &env, bool permission) {
    Window other = None;
    if (!permission) {
        // A second public fixture takes focus. Never activate a user window.
        other =
            XCreateSimpleWindow(client, DefaultRootWindow(client), 0, 0, 64, 64, 0, 0, 0x333333);
        XStoreName(client, other, "Canvas focus-negative fixture");
        XMapRaised(client, other);
        XSync(client, False);
        const auto other_id = std::to_string(static_cast<unsigned long>(other));
        const auto end = std::chrono::steady_clock::now() + std::chrono::seconds{2};
        for (;;) {
            const auto activation = env->window()->activate(other_id);
            const auto front = env->window()->front_window();
            if (activation.ok && front.ok && front.found && front.window.id == other_id)
                break;
            check(std::chrono::steady_clock::now() < end);
            std::this_thread::sleep_for(std::chrono::milliseconds{20});
        }
    }
    int mark_calls = 0;
    d::VisualReferenceRegistry references;
    i::DesktopToolContext desktop;
    desktop.environment = env;
    desktop.actions = std::make_shared<d::ActionCoordinator>();
    desktop.capture_scope = std::to_string(static_cast<unsigned long>(canvas));
    desktop.gate = [permission](const auto &cap, const auto &, const auto &cancel) {
        return !cancel() && !(permission && cap == "screen.capture");
    };
    desktop.mark = [&](const auto &, const auto &, const auto &) {
        ++mark_calls;
        return i::EnvironmentVisualRefresh{};
    };
    desktop.with_visual_reference = [&](const auto &scope, const auto &ref, const auto &action) {
        return references.with_reference(scope, ref, action);
    };
    auto ctx = mira::make_control_context();
    ctx.task = mira::TaskId::generate();
    ctx.session = mira::SessionId::generate();
    ctx.task_epoch = 1;
    ctx.deadline = std::chrono::steady_clock::now() + std::chrono::seconds{5};
    const auto result =
        executor
            .submit_auto([&] {
                detail::DesktopImageTurn turn(std::make_shared<detail::DesktopImageSource>(),
                                              desktop, ctx, {});
                mira::BuiltinToolRegistry registry;
                turn.register_tool(registry, {}, false);
                auto rejected = invoke(registry, "desktop.vision.mark", Json{Json::Object{}}, ctx);
                check(!turn.supplied_image());
                return rejected;
            })
            .get();
    check(result.failed && mark_calls == 0);
    check(result.safe_error_summary.find(permission ? "mark capture denied"
                                                    : "capture scope window is not focused") !=
          std::string::npos);
    if (other != None) {
        XDestroyWindow(client, other);
        XSync(client, False);
    }
    XSync(client, False);
    for (int n = 0; n < 32 && XPending(client); ++n) {
        XEvent event{};
        XNextEvent(client, &event);
        check(event.type != ButtonPress && event.type != ButtonRelease);
    }
    check(XPending(client) == 0);
    std::cout << "NEGATIVE kind=" << (permission ? "permission" : "focus")
              << " failed=1 mark_calls=0 images=0 input_events=0 reason="
              << result.safe_error_summary << '\n';
}
void real_canvas(const std::string &manifest, const std::string &before, const std::string &after,
                 const std::string &expected, bool update, const std::string &side,
                 const std::string &template_id = {}) {
    const auto setting = [](const char *name) {
        const auto *value = std::getenv(name);
        return value ? std::string{value} : std::string{};
    };
    const bool live = setting("MIRAGE_CANVAS_LIVE") == "1";
    const auto requested_reasoning = setting("MIRAGE_CANVAS_REASONING");
    const auto reasoning =
        requested_reasoning.empty() ? std::string{"adaptive"} : requested_reasoning;
    const bool regional = setting("MIRAGE_CANVAS_ROI") == "1";
    // MIRAGE_CANVAS_HOST attaches the canvas to a real host display instead
    // of a private Xvfb; MIRAGE_CANVAS_POS moves the window origin;
    // MIRAGE_CANVAS_OCCLUDE maps a cover window so the guard must refuse.
    const std::string host = setting("MIRAGE_CANVAS_HOST");
    const bool operator_focus = !host.empty() && setting("MIRAGE_CANVAS_OPERATOR") == "1";
    const bool occluded = !setting("MIRAGE_CANVAS_OCCLUDE").empty();
    int origin_x = 20, origin_y = 20;
    if (const auto pos = setting("MIRAGE_CANVAS_POS"); !pos.empty())
        check(std::sscanf(pos.c_str(), "%d,%d", &origin_x, &origin_y) == 2);
    if (live) {
        check(reasoning == "adaptive" || reasoning == "none");
        check(!side.empty() && template_id.empty());
        check(setting("MIRAGE_CANVAS_ORIGIN") == "https://api.minimaxi.com" ||
              setting("MIRAGE_CANVAS_ORIGIN") == "https://api.minimax.io");
        check(!setting("MIRAGE_CANVAS_KEY").empty());
    }
    check(XInitThreads() != 0 && i::PpOcrPipeline::supported());
    const auto before_pixels = load_canvas(before), after_pixels = load_canvas(after);
    std::unique_ptr<mirage::testing::XvfbDisplay> server;
    std::string display_name = host;
    if (host.empty()) {
        server = std::make_unique<mirage::testing::XvfbDisplay>(
            mirage::testing::find_xvfb(), regional ? "3840x2160x24" : "520x220x24");
        display_name = server->display_name();
    }
    Display *client = XOpenDisplay(display_name.c_str());
    check(client != nullptr);
    Pixmap background = None;
    struct Close {
        Display *display;
        Pixmap &background;
        ~Close() {
            if (background != None)
                XFreePixmap(display, background);
            XCloseDisplay(display);
        }
    } close{client, background};
    const Window canvas = XCreateSimpleWindow(client, DefaultRootWindow(client), origin_x, origin_y,
                                              480, 180, 0, 0, 0xffffff);
    const std::string canvas_id = std::to_string(static_cast<unsigned long>(canvas));
    XStoreName(client, canvas, "Canvas");
    XSelectInput(client, canvas, ButtonPressMask | ButtonReleaseMask);
    XMapWindow(client, canvas);
    if (occluded) {
        const Window cover = XCreateSimpleWindow(client, DefaultRootWindow(client), origin_x,
                                                 origin_y, 480, 180, 0, 0, 0x333333);
        XMapWindow(client, cover);
        XRaiseWindow(client, cover);
    }
    // A real XWayland window manager can unmap the client window while it
    // builds its frame, so XSetInputFocus may legitimately raise BadMatch.
    // The Xlib default handler would abort the fixture before the honest
    // front-window check below; ignore the async error and let the provider
    // activate fallback decide whether the canvas really holds focus.
    XSync(client, False); // The canvas must be viewable before focusing.
    const auto previous_handler = XSetErrorHandler([](Display *, XErrorEvent *) { return 0; });
    XSetInputFocus(client, canvas, RevertToParent, CurrentTime);
    XSync(client, False);
    XSetErrorHandler(previous_handler);
    background = paint_canvas(client, canvas, before_pixels);
    auto env = std::make_shared<mirage::platform::linux_backend::LinuxDesktopEnvironment>(
        std::vector<std::filesystem::path>{},
        mirage::platform::linux_backend::X11Options{.enabled = true, .display = display_name});
    const bool has_a11y = env->accessibility() != nullptr;
    if (host.empty())
        check(!has_a11y);
    if (regional && host.empty()) {
        const auto displays = env->screen()->list_displays();
        check(displays.ok && displays.displays.front().geometry.width == 3840 &&
              displays.displays.front().geometry.height == 2160);
        const auto full = env->screen()->capture_display(displays.displays.front().id);
        check(!full.ok && full.error.code == "capture_too_large");
    }
    // Stacking-aware occlusion guard: only windows strictly ABOVE the
    // canvas's outermost frame can hide its pixels — the reparenting WM's
    // frame always overlaps its own client, and windows below it in the
    // root's bottom-to-top stacking order paint underneath. The guard uses
    // the window manager's actual placement — not the requested origin — so
    // the checked rectangle is the one clicks would really land in. The
    // guard message is contract; the offline negative case asserts it.
    d::WindowGeometry canvas_geometry{};
    ::Window canvas_anchor = canvas; // the canvas itself, or its WM frame
    {
        // Mutter reparents client windows into WM frames; measure the actual
        // placement through X directly, the same global-rect translation the
        // provider's own window_geometry uses, and walk to the outermost
        // ancestor below the root. Wait bounded for the compositor to make
        // the window viewable — never guess the requested rect.
        const auto placed_deadline = std::chrono::steady_clock::now() + std::chrono::seconds{2};
        for (;;) {
            XWindowAttributes attributes{};
            int placed_x = 0, placed_y = 0;
            ::Window child = None;
            if (XGetWindowAttributes(client, canvas, &attributes) &&
                attributes.map_state == IsViewable &&
                XTranslateCoordinates(client, canvas, DefaultRootWindow(client), 0, 0, &placed_x,
                                      &placed_y, &child)) {
                canvas_geometry = {placed_x, placed_y, attributes.width, attributes.height};
                break;
            }
            check(std::chrono::steady_clock::now() < placed_deadline);
            std::this_thread::sleep_for(std::chrono::milliseconds{50});
        }
        ::Window parent = None, *kids = nullptr;
        unsigned int count = 0;
        ::Window root = None;
        if (XQueryTree(client, canvas, &root, &parent, &kids, &count)) {
            if (kids != nullptr)
                XFree(kids);
            while (parent != root && parent != None) {
                canvas_anchor = parent;
                ::Window next = None, *below = nullptr;
                unsigned int below_count = 0;
                if (!XQueryTree(client, canvas_anchor, &root, &next, &below, &below_count))
                    break;
                if (below != nullptr)
                    XFree(below);
                parent = next;
            }
        }
    }
    {
        ::Window root = None, parent = None, *kids = nullptr;
        unsigned int count = 0;
        check(XQueryTree(client, DefaultRootWindow(client), &root, &parent, &kids, &count));
        std::size_t anchor_index = count; // anchor absent: nothing can be above it
        for (std::size_t i = 0; i < count; ++i)
            if (kids[i] == canvas_anchor) {
                anchor_index = i;
                break;
            }
        for (std::size_t i = anchor_index + 1; i < count; ++i) {
            XWindowAttributes attributes{};
            if (!XGetWindowAttributes(client, kids[i], &attributes) ||
                attributes.map_state != IsViewable)
                continue;
            int global_x = 0, global_y = 0;
            ::Window child = None;
            if (!XTranslateCoordinates(client, kids[i], root, 0, 0, &global_x, &global_y, &child))
                continue;
            const bool overlaps = global_x < canvas_geometry.x + canvas_geometry.width &&
                                  global_x + attributes.width > canvas_geometry.x &&
                                  global_y < canvas_geometry.y + canvas_geometry.height &&
                                  global_y + attributes.height > canvas_geometry.y;
            if (overlaps)
                throw std::runtime_error(
                    "Canvas window " + canvas_id + " is occluded by window " +
                    std::to_string(static_cast<unsigned long>(kids[i])) +
                    "; host visual evidence refuses to capture beyond the public canvas");
        }
        if (kids != nullptr)
            XFree(kids);
    }
    // Map/reparent has settled; repaint before asking the operator to inspect.
    XClearWindow(client, canvas);
    XSync(client, False);
    check(canvas_matches(client, canvas, before_pixels));
    if (const auto negative = setting("MIRAGE_CANVAS_NEGATIVE"); !negative.empty()) {
        check(!live && (negative == "permission" || negative == "focus"));
        kairo::Executor executor;
        check(executor.initialize({}).ok);
        capture_gate_negative(executor, client, canvas, env, negative == "permission");
        executor.shutdown(true);
        XDestroyWindow(client, canvas);
        return;
    }
    if (!host.empty())
        std::cerr
            << "Canvas public drawable matches fixture=1\n"
               "Canvas ready: two black squares should be visible. Click the blank centre "
               "once to focus, release the button, then leave the mouse and window untouched.\n";
    bool operator_released = !operator_focus;
    int preparation_events = 0;
    const auto preparation_ready = [&] {
        if (operator_focus) {
            for (; preparation_events < 32 && XPending(client); ++preparation_events) {
                XEvent event{};
                XNextEvent(client, &event);
                if (event.type == ButtonRelease && event.xbutton.button == 1 &&
                    std::abs(event.xbutton.x - 240) <= 40 && std::abs(event.xbutton.y - 90) <= 40)
                    operator_released = true;
            }
            check(XPending(client) == 0);
        }
        return operator_released;
    };
    auto front = env->window()->front_window();
    if (!(front.ok && front.found && front.window.id == canvas_id) || !preparation_ready()) {
        // A real window manager owns focus: XSetInputFocus alone cannot move
        // Wayland compositor focus onto the canvas, and another surface (or
        // the WM focus proxy) may legitimately be front. Provider activation
        // is the platform-correct path; the compositor applies it
        // asynchronously and keeps newly mapped XWayland windows unviewable
        // while it builds their frame, so re-attempt activation within the
        // same bounded grace period (the provider honestly reports
        // not_found until the window is viewable again). The final check
        // stays strict — without the canvas as the focused front window this
        // harness refuses to supply any pixels.
        // Host runs leave a bounded human grace period: mutter transfers
        // focus only on a real user click, so the documented flow is "the
        // operator clicks the popped-up canvas once"; Xvfb never waits.
        const auto activation_deadline =
            std::chrono::steady_clock::now() + std::chrono::seconds{host.empty() ? 2 : 15};
        auto activation = env->window()->activate(canvas_id);
        front = env->window()->front_window();
        while ((!(front.ok && front.found && front.window.title == "Canvas" &&
                  front.window.id == canvas_id) ||
                !preparation_ready()) &&
               std::chrono::steady_clock::now() < activation_deadline) {
            std::this_thread::sleep_for(std::chrono::milliseconds{50});
            activation = env->window()->activate(canvas_id);
            front = env->window()->front_window();
        }
        if (!(front.ok && front.found && front.window.title == "Canvas" &&
              front.window.id == canvas_id) &&
            !activation.ok)
            std::cerr << "Canvas activation request failed: " << activation.error.code << " "
                      << activation.error.message << "\n";
        // GNOME-style compositors deny EWMH activation demands for
        // background-launched XWayland windows while a Wayland surface holds
        // focus (source 0/1/2 and _NET_WM_USER_TIME all refuse). A real
        // pointer click inside the canvas is the click-to-focus path a user
        // session provides, so perform exactly one — through the same bounded
        // provider input path this harness is authorized for, at the canvas
        // centre the occlusion guard just verified unoccluded — then discard
        // the fixture-window events it produced so the scripted click
        // accounting stays exact. Xvfb never needs this.
        if (!(front.ok && front.found && front.window.title == "Canvas" &&
              front.window.id == canvas_id) &&
            !host.empty() && !operator_focus) {
            int center_x = 0, center_y = 0;
            Window child = None;
            check(XTranslateCoordinates(client, canvas, DefaultRootWindow(client), 240, 90,
                                        &center_x, &center_y, &child));
            d::InputLimits limits;
            limits.timeout = std::chrono::milliseconds{1000};
            const auto assisted = env->input()->click_at(center_x, center_y, "left", limits, {});
            if (assisted.ok) {
                XSync(client, False);
                for (int n = 0; n < 32 && XPending(client); ++n) {
                    XEvent event{};
                    XNextEvent(client, &event);
                }
                // The same human grace applies after the assist click: on a
                // real desktop the operator's physical click lands here.
                const auto focus_deadline =
                    std::chrono::steady_clock::now() + std::chrono::seconds{host.empty() ? 2 : 15};
                front = env->window()->front_window();
                while (!(front.ok && front.found && front.window.title == "Canvas" &&
                         front.window.id == canvas_id) &&
                       std::chrono::steady_clock::now() < focus_deadline) {
                    std::this_thread::sleep_for(std::chrono::milliseconds{50});
                    front = env->window()->front_window();
                }
            } else {
                std::cerr << "Canvas focus assist click failed: " << assisted.error.code << " "
                          << assisted.error.message << "\n";
            }
        }
    }
    if (!(front.ok && front.found && front.window.title == "Canvas" &&
          front.window.id == canvas_id))
        std::cerr << "Canvas activation refused by the host: ok=" << front.ok
                  << " found=" << front.found << " id='" << front.window.id << "' title='"
                  << front.window.title << "'\n";
    check(front.ok && front.found && front.window.title == "Canvas" &&
          front.window.id == canvas_id);
    check(preparation_ready());
    if (operator_focus)
        std::cerr << "Canvas operator focus release confirmed\n";
    // Anchors and click targets must track where the window manager really
    // put the canvas, not where it was requested; the guard already vetted
    // this exact rectangle, so require the two to agree.
    check(front.window.geometry.x == canvas_geometry.x &&
          front.window.geometry.y == canvas_geometry.y &&
          front.window.geometry.width == canvas_geometry.width &&
          front.window.geometry.height == canvas_geometry.height);
    const int placed_x = front.window.geometry.x, placed_y = front.window.geometry.y;
    const int target_x = side.empty() ? 0 : placed_x + (side == "left" ? 100 : 360);
    kairo::Executor executor;
    check(executor.initialize({}).ok);
    {
        std::string failure;
        auto pipeline = i::PpOcrPipeline::create(executor, *env->screen(), manifest, failure);
        if (!pipeline)
            std::cerr << failure << '\n';
        check(pipeline && pipeline->running());
        i::DesktopToolContext desktop;
        desktop.environment = env;
        desktop.actions = std::make_shared<d::ActionCoordinator>();
        desktop.gate = [](const auto &, const auto &, const auto &cancel) { return !cancel(); };
        // DEC-062: every supplied pixel is scoped to the public Canvas window.
        desktop.capture_scope = canvas_id;
        auto last_mark_time = std::chrono::steady_clock::time_point{};
        desktop.mark = [&](const auto &crop, const auto &cancel, const auto &end) {
            const auto fresh = pipeline->mark(crop, cancel, end);
            if (regional && fresh.analyzed) {
                check(fresh.capture_roi && fresh.frame.width == 480 && fresh.frame.height == 180);
                check(fresh.marked_frame.width == 480 && fresh.marked_frame.height == 180);
            }
            if (fresh.analyzed) {
                last_mark_time = std::chrono::steady_clock::now();
            }
            if (target_x && !live) {
                if (!fresh.analyzed || fresh.marks.size() < 2)
                    std::cerr << "Canvas geometry candidates=" << fresh.marks.size()
                              << " captured=" << fresh.captured
                              << " capture_error=" << fresh.capture_error.code << ":"
                              << fresh.capture_error.message
                              << " analysis_error=" << fresh.analysis_error.code << ":"
                              << fresh.analysis_error.message << "\n";
                check(fresh.analyzed && fresh.marks.size() >= 2);
                if (template_id.empty()) {
                    for (const auto &r : fresh.publish.snapshot.regions)
                        check(r.source == d::VisualRegionSource::kGeometry && r.text.empty() &&
                              r.template_id.empty());
                } else {
                    check(std::count_if(fresh.publish.snapshot.regions.begin(),
                                        fresh.publish.snapshot.regions.end(), [&](const auto &r) {
                                            return r.template_id == template_id;
                                        }) == 1);
                }
            }
            return fresh;
        };
        desktop.with_visual_reference = [&](const auto &scope, const auto &ref,
                                            const auto &action) {
            return pipeline->registry().with_reference(scope, ref, action);
        };
        std::function<void()> service_canvas;
        desktop.ocr = [&](const auto &cancel, const auto &end) {
            if (live && service_canvas)
                service_canvas();
            i::DesktopOcrResult read;
            read.capture_begin = std::chrono::steady_clock::now();
            const auto fresh = regional
                                   ? pipeline->refresh_region(front.window.geometry, cancel, end)
                                   : pipeline->refresh(true, cancel, end);
            if (regional && fresh.analyzed)
                check(fresh.capture_roi && fresh.frame.width == 480 && fresh.frame.height == 180);
            read.ok = fresh.captured && fresh.analyzed;
            read.cancelled = fresh.capture_cancelled || fresh.analysis_cancelled;
            read.snapshot = fresh.publish.snapshot;
            read.display_id = fresh.display_id;
            read.display_geometry = fresh.display_geometry;
            read.model_revision = pipeline->model_revision();
            read.backend_calls = pipeline->backend_calls();
            read.capture_end = std::chrono::steady_clock::now();
            return read;
        };
        bool pressed = false, released = false;
        std::shared_ptr<CanvasScript> script;
        std::size_t presses = 0, releases = 0, wrong_presses = 0;
        std::int64_t click_age_ms = -1;
        bool painted = false;
        service_canvas = [&] {
            XSync(client, False);
            std::int64_t anchor_x = target_x - placed_x, anchor_y = 90;
            if (!live) {
                const auto &mark = script->marks.find("marks")->as_array()->at(script->selected);
                anchor_x = *mark.find("x")->as_integer() + *mark.find("width")->as_integer() / 2 -
                           placed_x;
                anchor_y = *mark.find("y")->as_integer() + *mark.find("height")->as_integer() / 2 -
                           placed_y;
            }
            for (int n = 0; n < 32 && XPending(client); ++n) {
                XEvent event{};
                XNextEvent(client, &event);
                if (event.type == ButtonPress) {
                    ++presses;
                    const bool correct = event.xbutton.button == 1 &&
                                         std::abs(event.xbutton.x - anchor_x) <= 1 &&
                                         std::abs(event.xbutton.y - anchor_y) <= 1;
                    pressed = pressed || correct;
                    if (!correct)
                        ++wrong_presses;
                    click_age_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                       std::chrono::steady_clock::now() - last_mark_time)
                                       .count();
                }
                if (event.type == ButtonRelease) {
                    ++releases;
                    released = released || event.xbutton.button == 1;
                }
            }
            if (!live)
                check(pressed && released);
            if (update && pressed && released && !painted) {
                background = paint_canvas(client, canvas, after_pixels, background);
                // XSync completes server requests, not compositor presentation.
                // Establish the fixture's redraw precondition through root ROI
                // pixels before returning control to the model. These pixels are
                // discarded; product verification still captures fresh frames.
                // Bounded synchronous work inside the existing Executor turn.
                const auto begin = std::chrono::steady_clock::now();
                const auto end = begin + std::chrono::milliseconds{500};
                const d::CancelToken cancel{
                    [&] { return std::chrono::steady_clock::now() >= end; }};
                int attempts = 0;
                for (;;) {
                    check(!cancel.cancelled());
                    const auto current = env->window()->front_window();
                    check(current.ok && current.found && current.window.id == canvas_id &&
                          current.window.geometry.x == placed_x &&
                          current.window.geometry.y == placed_y &&
                          current.window.geometry.width == 480 &&
                          current.window.geometry.height == 180);
                    const auto captured = env->screen()->capture_roi(front.window.geometry,
                                                                     d::CaptureLimits{}, cancel);
                    ++attempts;
                    if (!captured.ok)
                        std::cerr << "Canvas presentation capture failed: " << captured.error.code
                                  << ":" << captured.error.message << '\n';
                    check(captured.ok);
                    if (frame_matches(captured.frame, after_pixels))
                        break;
                    std::this_thread::sleep_for(std::chrono::milliseconds{20});
                }
                std::cerr << "Canvas presentation matched attempts=" << attempts << " elapsed_ms="
                          << std::chrono::duration_cast<std::chrono::milliseconds>(
                                 std::chrono::steady_clock::now() - begin)
                                 .count()
                          << '\n';
                painted = true;
            }
        };
        i::ModelProviderOverride factory([&](const auto &profile) {
            script = std::make_shared<CanvasScript>(profile);
            script->window = front.window.id;
            script->expected = expected;
            script->geometry = target_x != 0;
            script->target_x = target_x;
            script->target_y = placed_y + 90;
            script->expected_template = template_id;
            script->service_canvas = service_canvas;
            return script;
        });
        i::ModelLayerConfig config;
        config.enabled = true;
        config.model_selector = live ? setting("MIRAGE_CANVAS_MODEL") : "canvas-script";
        config.dialect = "anthropic.messages.v1";
        config.supports_images = true;
        if (live) {
            config.endpoint_origin = setting("MIRAGE_CANVAS_ORIGIN");
            config.api_prefix = "/anthropic/v1";
            config.credential_env = "MIRAGE_CANVAS_KEY";
            config.request_deadline = std::chrono::seconds{30};
        }
        i::ModelLayer model(executor, config, live ? nullptr : &factory, desktop);
        check(model.running());
        // The operator's focus click belongs to fixture preparation. Retire
        // it before the model turn, never while counting autonomous input.
        // A held physical button or a flooded queue refuses admission.
        if (!host.empty()) {
            Window root = None, child = None;
            int root_x = 0, root_y = 0, local_x = 0, local_y = 0;
            unsigned int mask = 0;
            check(XQueryPointer(client, canvas, &root, &child, &root_x, &root_y, &local_x, &local_y,
                                &mask));
            check((mask & (Button1Mask | Button2Mask | Button3Mask | Button4Mask | Button5Mask)) ==
                  0);
            XSync(client, False);
            for (; preparation_events < 32 && XPending(client); ++preparation_events) {
                XEvent event{};
                XNextEvent(client, &event);
            }
            check(XPending(client) == 0);
            std::cerr << "Canvas preparation events retired=" << preparation_events << '\n';
        }
        auto ctx = mira::make_control_context();
        ctx.task = mira::TaskId::generate();
        ctx.session = mira::SessionId::generate();
        ctx.task_epoch = 1;
        ctx.deadline = std::chrono::steady_clock::now() + std::chrono::seconds{30};
        const auto started = std::chrono::steady_clock::now();
        struct ToolTiming {
            std::string id, tool, status;
            std::int64_t started_ms = -1, finished_ms = -1;
            std::int64_t mark_to_click_ms = -1, mark_ready_to_click_ms = -1;
        };
        std::vector<ToolTiming> timings;
        std::int64_t mark_started_ms = -1, mark_ready_ms = -1;
        // Diagnostic projection of the existing serialized process events;
        // no new task monitor, clock source, thread or model thinking trace.
        const auto process = [&](const auto &parts) {
            const auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
                                 std::chrono::steady_clock::now() - started)
                                 .count();
            for (const auto &part : parts) {
                if (part.kind != "tool")
                    continue;
                auto found = std::find_if(timings.begin(), timings.end(),
                                          [&](const auto &t) { return t.id == part.id; });
                if (found == timings.end() && part.status == "running") {
                    check(timings.size() < 32);
                    ToolTiming timing;
                    timing.id = part.id;
                    timing.tool = part.name;
                    timing.status = part.status;
                    timing.started_ms = now;
                    if (part.name == "desktop.vision.mark") {
                        mark_started_ms = now;
                        mark_ready_ms = -1;
                    }
                    if (part.name == "desktop.vision.click") {
                        timing.mark_to_click_ms = mark_started_ms < 0 ? -1 : now - mark_started_ms;
                        timing.mark_ready_to_click_ms =
                            mark_ready_ms < 0 ? -1 : now - mark_ready_ms;
                    }
                    timings.push_back(std::move(timing));
                } else if (found != timings.end() && found->finished_ms < 0 &&
                           part.status != "running" && part.status != "pending") {
                    found->finished_ms = now;
                    found->status = part.status;
                    if (part.name == "desktop.vision.mark" && part.status == "complete")
                        mark_ready_ms = now;
                }
            }
        };
        const auto prompt =
            live ? "只操作 Canvas 窗口（window_id=" + front.window.id +
                       ")。"
                       "先实际调用 desktop.vision.mark 查看编号图，选择黑色方框内白色交叉 X "
                       "图案的编号。"
                       "另一个方框只有一条白色斜线，禁止点击它。答案和位置只在图片里，禁止猜编号或"
                       "坐标。"
                       "然后实际调用 desktop.vision.click，使用本次 scope、epoch 和编号，"
                       "派发前固定 verify_kind=visual_text_exists、expected_text=Task saved。"
                       "最多点击一次，不要重试输入。随后调用 desktop.action.verify 使用返回的 "
                       "action_id "
                       "检查新观察，只在 verified=true 时报告完成，否则报告无法验证。"
                       "仅使用 mark、click、verify 三个工具，不要输出思考或说明，直接调用工具。"
                 : std::string{"Click the numbered target and verify the Canvas result"};
        const auto result = executor
                                .submit_auto([&] {
                                    auto completed = model.complete_harness_turn(
                                        "", prompt, ctx, live ? reasoning : "", true, {}, process);
                                    if (live)
                                        service_canvas();
                                    return completed;
                                })
                                .get();
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                                 std::chrono::steady_clock::now() - started)
                                 .count();
        if (!live && (result.ok != update || !pressed || !released || script->images != 1 ||
                      script->saw_verified != update)) {
            std::cerr << result.error << '\n'
                      << "FAILURE elapsed_ms=" << elapsed
                      << " backend_calls=" << pipeline->backend_calls() << " ok=" << result.ok
                      << " failed=" << result.failed << " pressed=" << pressed
                      << " released=" << released << " images=" << script->images
                      << " saw_verified=" << script->saw_verified << " presses=" << presses
                      << " releases=" << releases << " wrong_presses=" << wrong_presses << '\n';
            for (const auto &part : result.parts)
                if (part.name == "desktop.vision.click" || part.name == "desktop.action.verify")
                    std::cerr << "FAILURE_EVIDENCE " << part.name << " " << part.output << '\n';
        }
        if (!live) {
            check(result.ok == update && result.failed == !update);
            check(pressed && released && script->images == 1 && script->saw_verified == update);
            check(update || result.reply_text.find("completed") == std::string::npos);
        }
        const auto final_front = env->window()->front_window();
        check(final_front.ok && final_front.found && final_front.window.title == "Canvas" &&
              final_front.window.id == canvas_id);
        Json::Array tool_evidence;
        Json::Array timing_evidence;
        for (const auto &timing : timings) {
            check(timing.started_ms >= 0 &&
                  (timing.finished_ms == -1 || timing.finished_ms >= timing.started_ms));
            timing_evidence.emplace_back(
                Json::Object{{"tool", timing.tool},
                             {"status", timing.status},
                             {"started_ms", timing.started_ms},
                             {"finished_ms", timing.finished_ms},
                             {"mark_to_click_ms", timing.mark_to_click_ms},
                             {"mark_ready_to_click_ms", timing.mark_ready_to_click_ms}});
        }
        if (!live)
            check(timings.size() == 3);
        std::cout << "TIMINGS " << mira::to_json_string(Json{timing_evidence}) << '\n';
        for (const auto &part : result.parts) {
            if (live && part.kind == "tool")
                tool_evidence.emplace_back(Json::Object{{"tool", part.name},
                                                        {"input", part.input},
                                                        {"output", part.output},
                                                        {"status", part.status}});
            check(part.input.find(expected) == std::string::npos);
            if (part.name == "desktop.vision.click" || part.name == "desktop.action.verify")
                std::cout << "EVIDENCE " << part.name << " " << part.output << '\n';
        }
        if (live) {
            std::cout << "LIVE "
                      << mira::to_json_string(Json{Json::Object{
                             {"ok", result.ok},
                             {"failed", result.failed},
                             {"cancelled", result.cancelled},
                             {"error", result.error},
                             {"reasoning", reasoning},
                             {"timings", Json{std::move(timing_evidence)}},
                             {"answer", result.reply_text},
                             {"pressed", pressed},
                             {"released", released},
                             {"presses", static_cast<std::int64_t>(presses)},
                             {"releases", static_cast<std::int64_t>(releases)},
                             {"wrong_presses", static_cast<std::int64_t>(wrong_presses)},
                             {"elapsed_ms", elapsed},
                             {"click_age_ms", click_age_ms},
                             {"model_steps", static_cast<std::int64_t>(result.model_steps)},
                             {"tool_calls", static_cast<std::int64_t>(result.tool_calls)},
                             {"supplied_input_items",
                              static_cast<std::int64_t>(result.supplied_input_items)},
                             {"supplied_input_dropped",
                              static_cast<std::int64_t>(result.supplied_input_dropped)},
                             {"supply_degradations",
                              static_cast<std::int64_t>(result.supply_degradations)},
                             {"backend_calls",
                              static_cast<std::int64_t>(pipeline->backend_calls())},
                             {"display", host.empty() ? "private-xvfb" : host},
                             {"capture_scope", canvas_id},
                             {"origin", Json{Json::Object{{"x", std::int64_t{placed_x}},
                                                          {"y", std::int64_t{placed_y}}}}},
                             {"tools", std::move(tool_evidence)}}})
                      << '\n';
        }
        std::cout << "CANVAS update=" << update << " ok=" << result.ok << " pressed=" << pressed
                  << " released=" << released << (script ? " images=" : " supplied_items=")
                  << (script ? script->images : result.supplied_input_items)
                  << " backend_calls=" << pipeline->backend_calls()
                  << " geometry=" << (target_x != 0) << " template=" << template_id
                  << " title=Canvas window=" << canvas_id << " origin=" << placed_x << ","
                  << placed_y << " display=" << (host.empty() ? "private-xvfb" : host)
                  << " a11y=" << (has_a11y ? 1 : 0) << " roi_capture=" << regional << "\n";
        model.shutdown();
        pipeline->stop();
    }
    executor.shutdown(true);
    XDestroyWindow(client, canvas);
}
} // namespace
int main(int argc, char **argv) {
    try {
        if (argc == 1)
            run();
        else {
            check(argc == 6 || argc == 7 || argc == 8);
            check(std::string{argv[5]} == "update" || std::string{argv[5]} == "unchanged");
            std::string side;
            if (argc >= 7) {
                check(std::string{argv[6]} == "left" || std::string{argv[6]} == "right");
                side = argv[6];
            }
            real_canvas(argv[1], argv[2], argv[3], argv[4], std::string{argv[5]} == "update", side,
                        argc == 8 ? argv[7] : "");
        }
        std::cout << "X11 visual marks: " << checks << " checks passed\n";
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
