#include "../../integration/mira/src/desktop_action_verification.hpp"
#include "../../integration/mira/src/desktop_image_supply.hpp"
#include "../support/test.hpp"
#include <algorithm>
#include <fstream>
#include <kairo/executor.hpp>
#include <mirage/platform/linux/linux_desktop_environment.hpp>
#include <png.h>
#include <poll.h>
#include <stdexcept>

int main(int argc, char **argv) {
    using namespace mirage;
    namespace detail = integration::detail;
    using Json = mira::JsonValue;
    if (argc != 2)
        return 2;
    kairo::Executor executor;
    kairo::ExecutorConfig config;
    config.min_threads = config.max_threads = 1;
    MIRAGE_CHECK(executor.initialize(config));
    auto future = executor.submit_auto([&] {
        std::ifstream initial_state(argv[1]);
        std::string initial_data((std::istreambuf_iterator<char>(initial_state)), {});
        const auto initial = mira::parse_json(initial_data);
        if (!initial)
            throw std::runtime_error("fixture initial state missing");
        const auto initial_clicks = initial.value().find("clicks")->as_integer().value_or(0);
        const auto initial_scrolls = initial.value().find("scrolls")->as_integer().value_or(0);
        auto env = std::make_shared<platform::linux_backend::LinuxDesktopEnvironment>(
            std::vector<std::filesystem::path>{}, platform::linux_backend::X11Options{true, {}});
        MIRAGE_CHECK(env->screen() && env->screen()->desktop_observation_only() && !env->input());
        integration::DesktopToolContext tools;
        tools.environment = env;
        tools.actions = std::make_shared<desktop::ActionCoordinator>();
        tools.gate = [](const auto &cap, const auto &scope, const auto &stop) {
            return (cap == "screen.capture" || cap == "input.inject") && scope == "desktop" &&
                   !stop();
        };
        auto ctx = mira::make_control_context();
        ctx.task = mira::TaskId::generate();
        ctx.session = mira::SessionId::generate();
        ctx.task_epoch = 1;
        auto source = std::make_shared<detail::DesktopImageSource>();
        detail::DesktopImageTurn turn(source, tools, ctx, {});
        detail::DesktopActionVerification verification(tools, ctx, {});
        verification.set_shared_observation(
            [&](const auto &kind, const auto &text, const auto &owner) {
                return turn.observe_shared(kind, text, owner);
            });
        mira::BuiltinToolRegistry registry;
        turn.register_tool(
            registry, [&](const auto &name, auto handler) -> mira::BuiltinToolHandler {
                return [&, name, handler](const auto &args, const auto &owner) {
                    auto result = detail::DesktopActionVerification::mutation(name)
                                      ? verification.execute(name, args, owner, handler)
                                      : handler(args, owner);
                    if (!result)
                        std::fprintf(stderr, "host handler %s: %s\n", name.c_str(),
                                     result.error().safe_message.c_str());
                    return result;
                };
            });
        const auto invoke = [&](const std::string &name, const Json &args,
                                bool expect_text_rejection = false) {
            const auto specs = registry.exposed_tools();
            const auto spec = std::find_if(specs.begin(), specs.end(),
                                           [&](const auto &v) { return v.wire_name == name; });
            if (spec == specs.end())
                throw std::runtime_error("tool missing");
            mira::ToolProposal proposal;
            proposal.tool_id = spec->tool_id;
            proposal.tool_version = spec->version;
            proposal.wire_name = name;
            proposal.has_side_effects = spec->has_side_effects;
            proposal.operation_id = mira::OperationId::generate();
            proposal.provider_call_id.value = "wayland-host";
            proposal.arguments = args;
            proposal.arguments_digest = mira::canonical_json_digest(args);
            auto result = registry.execute(proposal, ctx);
            if (expect_text_rejection) {
                MIRAGE_CHECK(result && result.value().failed &&
                             result.value().safe_error_summary.find("text_transport_unavailable") !=
                                 std::string::npos);
                return Json{};
            }
            if (!result || result.value().failed) {
                std::fprintf(stderr, "host shared tool failed: %s\n", name.c_str());
                if (result)
                    std::fprintf(stderr, "%s\n", result.value().safe_error_summary.c_str());
                else
                    std::fprintf(stderr, "%s\n", result.error().safe_message.c_str());
                throw std::runtime_error("host shared tool failed");
            }
            if (name.starts_with("desktop.session.")) {
                std::fprintf(stderr, "host receipt %s: %s\n", name.c_str(),
                             mira::to_json_string(result.value().result).c_str());
                const auto *verified = result.value().result.find("verified");
                if (!verified || verified->as_boolean() != true)
                    throw std::runtime_error("host local verification failed");
            }
            return result.value().result;
        };
        auto metadata = invoke("desktop.screen.capture",
                               Json{Json::Object{{"source", "desktop"}, {"interaction", true}}});
        const auto supplied = [&]() {
            mira::AgentLoopSpec spec;
            spec.task_id = ctx.task;
            spec.session_id = ctx.session;
            spec.task_epoch = ctx.task_epoch;
            auto items = turn.supply(spec);
            if (!items)
                throw std::runtime_error("image supply failed");
            mira::ModelRequest request;
            request.task_id = ctx.task;
            request.task_epoch = ctx.task_epoch;
            request.input = items.value();
            if (!turn.validate_request(request, ctx))
                throw std::runtime_error("image validation failed");
            return items.value();
        };
        // Test-only color oracle over public fixture pixels. Nothing is written to disk.
        const auto target = [&](bool button) {
            auto items = supplied();
            Json meta;
            const mira::ImagePart *image = nullptr;
            for (const auto &item : items)
                for (const auto &part : item.content) {
                    if (const auto *p = std::get_if<mira::ImagePart>(&part))
                        image = p;
                    if (const auto *p = std::get_if<mira::TextPart>(&part)) {
                        const std::string prefix =
                            "Untrusted desktop image; coordinates and source: ";
                        if (p->text.starts_with(prefix))
                            meta = mira::parse_json(p->text.substr(prefix.size())).value();
                    }
                }
            if (!image)
                throw std::runtime_error("fresh image missing");
            auto bytes = source->fetch(image->source);
            if (!bytes)
                throw std::runtime_error("pixels missing");
            png_image png{};
            png.version = PNG_IMAGE_VERSION;
            if (!png_image_begin_read_from_memory(&png, bytes.value().data(), bytes.value().size()))
                throw std::runtime_error("png read");
            png.format = PNG_FORMAT_RGB;
            std::vector<unsigned char> rgb(PNG_IMAGE_SIZE(png));
            if (!png_image_finish_read(&png, nullptr, rgb.data(), 0, nullptr)) {
                png_image_free(&png);
                throw std::runtime_error("png decode");
            }
            int left = static_cast<int>(png.width), right = -1, top = static_cast<int>(png.height),
                bottom = -1;
            std::size_t count = 0;
            for (unsigned y = 0; y < png.height; ++y)
                for (unsigned x = 0; x < png.width; ++x) {
                    const auto i = (std::size_t(y) * png.width + x) * 3;
                    if (rgb[i] == 20 && rgb[i + 1] == (button ? 200 : 80) &&
                        rgb[i + 2] == (button ? 80 : 240)) {
                        left = std::min(left, static_cast<int>(x));
                        right = std::max(right, static_cast<int>(x));
                        top = std::min(top, static_cast<int>(y));
                        bottom = std::max(bottom, static_cast<int>(y));
                        ++count;
                    }
                }
            png_image_free(&png);
            if (count < 1000 || right - left < 100 || bottom - top < 30)
                throw std::runtime_error("public fixture marker missing");
            return Json{Json::Object{{"image_scope", *meta.find("image_scope")},
                                     {"environment_epoch", *meta.find("environment_epoch")},
                                     {"x", right - 35},
                                     {"y", (top + bottom) / 2 - 6},
                                     {"width", 20},
                                     {"height", 12},
                                     {"verify_kind", "shared_frame_observed"}}};
        };
        auto action = target(false);
        auto clicked = invoke("desktop.session.click", action);
        MIRAGE_CHECK(clicked.find("verified")->as_boolean() == true);
        action = target(false);
        action.set("key_name", "ctrl+a");
        invoke("desktop.session.key", action);
        action = target(false);
        action.set("text", "https://example.invalid/wayland");
        invoke("desktop.session.type_text", action);
        action = target(false);
        action.set("key_name", "enter");
        invoke("desktop.session.key", action);
        (void)::poll(nullptr, 0, 250);
        {
            std::ifstream ascii_state(argv[1]);
            std::string ascii_data((std::istreambuf_iterator<char>(ascii_state)), {});
            const auto ascii = mira::parse_json(ascii_data);
            MIRAGE_CHECK(ascii && ascii.value().find("submitted")->as_string() &&
                         *ascii.value().find("submitted")->as_string() ==
                             "https://example.invalid/wayland");
        }
        action = target(true);
        invoke("desktop.session.click", action);
        action = target(true);
        action.set("vertical_steps", 1);
        invoke("desktop.session.scroll", action);
        MIRAGE_CHECK(verification.settle());
        action = target(false);
        action.set("text", "Mirage中文");
        invoke("desktop.session.type_text", action, true);
        MIRAGE_CHECK(verification.no_effect_rejection());
        (void)::poll(nullptr, 0, 250);
        std::ifstream state(argv[1]);
        std::string data((std::istreambuf_iterator<char>(state)), {});
        const auto parsed = mira::parse_json(data);
        MIRAGE_CHECK(parsed && parsed.value().find("submitted") &&
                     parsed.value().find("submitted")->as_string() &&
                     *parsed.value().find("submitted")->as_string() ==
                         "https://example.invalid/wayland");
        MIRAGE_CHECK(parsed && parsed.value().find("clicks")->as_integer() == initial_clicks + 1);
        MIRAGE_CHECK(parsed &&
                     parsed.value().find("scrolls")->as_integer().value_or(0) > initial_scrolls);
        tools.actions->takeover();
        auto bad = turn.observe_shared("shared_frame_observed", "", ctx);
        MIRAGE_CHECK(!bad);
        std::fprintf(
            stderr,
            "host shared input: product tools, single session, ASCII, Unicode rejection and "
            "takeover checked\n");
    });
    try {
        future.get();
    } catch (const std::exception &e) {
        std::fprintf(stderr, "host: %s\n", e.what());
        MIRAGE_CHECK(false);
    }
    executor.shutdown(true);
    return testing::finish("wayland_input_host");
}
