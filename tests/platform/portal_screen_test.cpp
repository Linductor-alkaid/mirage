#include "../../integration/mira/src/desktop_image_supply.hpp"
#include "../../integration/mirador/src/ppocr_pipeline.hpp"
#include "../../platform/linux/src/portal_authorization.hpp"
#include "../../platform/linux/src/portal_screen_backend.hpp"
#include "../support/test.hpp"
#include <chrono>
#include <array>
#include <cstring>
#include <poll.h>
#ifdef MIRAGE_TEST_PIPEWIRE
#include "../../platform/linux/src/portal_pipewire_capture.hpp"
#endif
#include <kairo/executor.hpp>
#include <mirage/platform/linux/linux_desktop_environment.hpp>

int main(int argc, char **argv) {
    using namespace mirage;
    const std::string mode = argc > 1 ? argv[1] : "noscreencast";
#ifdef MIRAGE_TEST_PIPEWIRE
    if (mode == "pixels") {
        using namespace platform::linux_backend;
        const std::vector<std::uint8_t> input{10, 20, 30, 40, 50, 60, 70, 80, 99, 99,
                                              99, 99, 11, 21, 31, 41, 51, 61, 71, 81};
        for (auto format :
             {PackedPixels::Bgra, PackedPixels::Bgrx, PackedPixels::Rgba, PackedPixels::Rgbx}) {
            auto out = copy_portal_pixels(input, 2, 2, 12, format, {32}, {});
            MIRAGE_CHECK(out.ok && out.frame.pixels.size() == 16 && out.frame.stride == 8);
            const bool swap = format == PackedPixels::Rgba || format == PackedPixels::Rgbx;
            const bool opaque = format == PackedPixels::Bgrx || format == PackedPixels::Rgbx;
            MIRAGE_CHECK(out.frame.pixels[0] == (swap ? 30 : 10) &&
                         out.frame.pixels[8] == (swap ? 31 : 11));
            MIRAGE_CHECK(out.frame.pixels[3] == (opaque ? 255 : 40));
        }
        MIRAGE_CHECK(!copy_portal_pixels(input, 2, 2, -12, PackedPixels::Bgra, {32}, {}).ok);
        MIRAGE_CHECK(!copy_portal_pixels(input, 2, 2, 4, PackedPixels::Bgra, {32}, {}).ok);
        MIRAGE_CHECK(
            !copy_portal_pixels(std::span(input).first(19), 2, 2, 12, PackedPixels::Bgra, {32}, {})
                 .ok);
        MIRAGE_CHECK(copy_portal_pixels(input, 2, 2, 12, PackedPixels::Bgra, {15}, {}).error.code ==
                     "capture_too_large");
        MIRAGE_CHECK(
            !copy_portal_pixels(input, 16385, 1, 65540, PackedPixels::Bgra, {32u << 20}, {}).ok);
        desktop::CancelToken stop;
        stop.request_cancel();
        MIRAGE_CHECK(copy_portal_pixels(input, 2, 2, 12, PackedPixels::Bgra, {32}, stop).cancelled);
        // Wire-shaped metadata, deliberately unaligned/truncated cases included.
        std::array<std::uint32_t, 7> cursor{1, 0, 200, 120, 0, 0, 0};
        std::array<std::uint8_t, 29> cursor_bytes{};
        const auto metadata = [&] {
            std::memcpy(cursor_bytes.data() + 1, cursor.data(), sizeof(cursor));
            return std::span(cursor_bytes).subspan(1);
        };
        auto position = decode_portal_cursor(metadata(), 320, 200);
        MIRAGE_CHECK(position && position->x == 200 && position->y == 120);
        MIRAGE_CHECK(!decode_portal_cursor(metadata().first(20), 320, 200));
        cursor[0] = 0;
        MIRAGE_CHECK(!decode_portal_cursor(metadata(), 320, 200));
        cursor[0] = 1; cursor[2] = 320;
        MIRAGE_CHECK(!decode_portal_cursor(metadata(), 320, 200));
        cursor[2] = UINT32_MAX;
        MIRAGE_CHECK(!decode_portal_cursor(metadata(), 320, 200));
        return testing::finish("portal_stream_pixels");
    }
#endif
    kairo::Executor executor;
    kairo::ExecutorConfig config;
    config.min_threads = config.max_threads = 1;
    MIRAGE_CHECK(executor.initialize(config));
    auto future = executor.submit_auto([&] {
        auto env = std::make_shared<platform::linux_backend::LinuxDesktopEnvironment>(
            std::vector<std::filesystem::path>{}, platform::linux_backend::X11Options{true, {}});
        MIRAGE_CHECK(!env->window() && !env->input() && !env->clipboard());
        auto *screen = env->screen();
        MIRAGE_CHECK(screen && screen->desktop_observation_only());
        if (!screen)
            return;
        MIRAGE_CHECK(!screen->list_displays({}, {}).ok);
        MIRAGE_CHECK(!screen->capture_roi({0, 0, 2, 2}, {}, {}).ok);
        MIRAGE_CHECK(!screen->capture_window("unrelated-x-window", {}, {}).ok);
        if (mode == "cast_retry" || mode == "cast_pre_cancel_session") {
            auto session = screen->create_desktop_capture_session();
            MIRAGE_CHECK(session && !session->reusable());
            desktop::CancelToken stop;
            if (mode == "cast_pre_cancel_session")
                stop.request_cancel();
            auto first = session->capture({32u << 20}, stop);
            MIRAGE_CHECK(!first.ok &&
                         first.error.code ==
                             (mode == "cast_retry" ? "portal_remote_failed" : "cancelled"));
            auto retry = session->capture({32u << 20}, {});
            MIRAGE_CHECK(!retry.ok && retry.error.code == "portal_session_closed");
            return;
        }
        if (mode.starts_with("rd_persist_")) {
            std::string token = mode == "rd_persist_restore" ? "fixture-token-before" : "";
            bool fail_write = false;
            auto authorization = std::make_shared<platform::linux_backend::PortalAuthorization>(
                platform::linux_backend::PortalAuthorization::Store{
                    [&] {
                        return platform::CredentialResult{!token.empty(), token, {}, token.empty()};
                    },
                    [&](const std::string &value) {
                        if (fail_write)
                            return platform::CredentialResult{};
                        token = value;
                        return platform::CredentialResult{true, {}, {}};
                    }});
            platform::linux_backend::PortalScreenBackend backend(authorization);
            backend.configure_desktop_authorization(true);
            fail_write = mode == "rd_persist_store";
            const auto begin = std::chrono::steady_clock::now();
            desktop::CancelToken stop([&] {
                return mode == "rd_persist_cancel" &&
                       std::chrono::steady_clock::now() - begin > std::chrono::milliseconds(150);
            });
            if (mode == "rd_persist_pre_cancel")
                stop.request_cancel();
            const auto error = backend.manage_desktop_authorization(true, stop);
            const auto expected = mode == "rd_persist_missing"   ? "persistence_not_granted"
                                  : mode == "rd_persist_denied"  ? "portal_input_denied"
                                  : mode == "rd_persist_store"   ? "authorization_store_failed"
                                  : mode == "rd_persist_v1"      ? "persistence_unavailable"
                                  : mode == "rd_persist_timeout" ? "portal_timeout"
                                  : mode == "rd_persist_cancel" || mode == "rd_persist_pre_cancel"
                                      ? "cancelled"
                                      : "";
            MIRAGE_CHECK(error.code == expected);
            if (!error.code.empty()) {
                MIRAGE_CHECK(token.empty());
                return;
            }
            MIRAGE_CHECK(backend.desktop_authorization({}).remembered);
            if (mode == "rd_persist_readonly") {
                const auto first = token;
                auto readonly = backend.create_desktop_capture_session();
                MIRAGE_CHECK(!readonly->input_supported());
                MIRAGE_CHECK(readonly->capture({32u << 20}, {}).error.code ==
                             "portal_remote_failed");
                MIRAGE_CHECK(token != first && !token.empty());
                MIRAGE_CHECK(!readonly->input_supported());
            }
            const auto before = authorization->generation();
            MIRAGE_CHECK(backend.manage_desktop_authorization(false, {}).code.empty());
            MIRAGE_CHECK(token.empty() && !authorization->enabled() &&
                         authorization->generation() != before);
            return;
        }
        if (mode.starts_with("rd_")) {
            auto session = screen->create_desktop_interaction_session();
            MIRAGE_CHECK(session && !session->input_supported());
            const auto begin = std::chrono::steady_clock::now();
            desktop::CancelToken stop([&] {
                return mode == "rd_cancel" &&
                       std::chrono::steady_clock::now() - begin > std::chrono::milliseconds(150);
            });
            if (mode == "rd_pre_cancel")
                stop.request_cancel();
            auto out = session->capture({32u << 20}, stop);
            const std::string expected = mode == "rd_input_denied" ? "portal_input_denied"
                                         : mode == "rd_mapping" ? "portal_input_mapping_unavailable"
                                         : mode == "rd_remote"  ? "portal_remote_failed"
                                         : mode == "rd_user_cancel" ? "portal_user_cancelled"
                                         : mode == "rd_cancel" || mode == "rd_pre_cancel"
                                             ? "cancelled"
                                             : "portal_protocol_error";
            MIRAGE_CHECK(!out.ok && out.error.code == expected);
            MIRAGE_CHECK(!session->input_supported());
            MIRAGE_CHECK(!session->input({}, {}, {}).ok);
            MIRAGE_CHECK(session->capture({32u << 20}, {}).error.code == "portal_session_closed");
            return;
        }
        const auto start = std::chrono::steady_clock::now();
        desktop::CancelToken cancel([&] {
            return (mode == "cast_cancel" || mode == "cast_late") &&
                   std::chrono::steady_clock::now() - start >= std::chrono::milliseconds(150);
        });
        if (mode != "tool" && mode != "tool_denied" && mode != "--probe-host-tool" &&
            mode != "--probe-host-ocr") {
            if (mode == "budget") {
                // The only budget gate in front of any bus access is the
                // positive-pixel-budget check. Per-frame max_bytes enforcement
                // lives in the ScreenCast stream path (covered by "pixels");
                // an 8-byte budget must fail closed without a portal method.
                const auto zero = screen->capture_desktop({0u}, {});
                MIRAGE_CHECK(!zero.ok && zero.error.code == "invalid_argument");
                const auto tiny = screen->capture_desktop({8u}, {});
                MIRAGE_CHECK(!tiny.ok && tiny.error.code == "unsupported_capability");
                return;
            }
            const auto out = screen->capture_desktop({32u << 20}, cancel);
            if (mode == "noscreencast") {
                // DEC-015 fail-closed: the portal object exposes no ScreenCast,
                // so capture must fail closed with no second authorization.
                MIRAGE_CHECK(!out.ok && out.error.code == "unsupported_capability");
#ifdef MIRAGE_TEST_PIPEWIRE
                MIRAGE_CHECK(out.error.message.find("does not provide ScreenCast") !=
                             std::string::npos);
#else
                MIRAGE_CHECK(out.error.message.find("not compiled into this build") !=
                             std::string::npos);
#endif
                return;
            }
            if (mode == "absent") {
                // With PipeWire the private bus is alive but no portal service
                // owns the name; without it the capability is rejected before
                // any bus access.
#ifdef MIRAGE_TEST_PIPEWIRE
                MIRAGE_CHECK(!out.ok && out.error.code == "portal_unavailable");
#else
                MIRAGE_CHECK(!out.ok && out.error.code == "unsupported_capability");
#endif
                return;
            }
            MIRAGE_CHECK(!out.ok);
            if (mode == "--probe-host") {
                std::fprintf(stderr, "host Portal: ok=%d code=%s pixels=%dx%d\n", out.ok,
                             out.error.code.c_str(), out.frame.width, out.frame.height);
                MIRAGE_CHECK(out.ok);
                return;
            }
            const std::string expected =
                mode == "cast_cancel" || mode == "cast_late" ? "cancelled"
                : (mode == "cast_closed" || mode == "cast_closed_before_select")
                    ? "portal_session_closed"
                : mode == "cast_user_cancel" ? "portal_user_cancelled"
                : mode == "cast_failed"      ? "portal_failed"
                : mode == "cast_remote"      ? "portal_remote_failed"
                : mode == "cast_denied"      ? "portal_permission_denied"
                : mode == "cast_timeout"     ? "portal_timeout"
                                             : "portal_protocol_error";
            MIRAGE_CHECK(out.error.code == expected);
            return;
        }
        integration::DesktopToolContext tools;
        tools.environment = env;
        tools.actions = std::make_shared<desktop::ActionCoordinator>();
        tools.gate = [&](const auto &capability, const auto &scope, const auto &stop) {
            MIRAGE_CHECK(capability == "screen.capture" && scope == "desktop");
            return mode != "tool_denied" && !stop();
        };
        std::unique_ptr<integration::PpOcrPipeline> pipeline;
        if (mode == "--probe-host-ocr") {
            MIRAGE_CHECK(argc > 2);
            std::string error;
            pipeline = integration::PpOcrPipeline::create(executor, *screen, argv[2], error);
            MIRAGE_CHECK(pipeline && pipeline->running());
            tools.ocr_image = [&](const auto &image, const auto &ocr_cancel, const auto &deadline) {
                return pipeline->read_image_ocr(image, ocr_cancel, deadline);
            };
        }
        auto ctx = mira::make_control_context();
        ctx.task = mira::TaskId::generate();
        ctx.session = mira::SessionId::generate();
        auto source = std::make_shared<integration::detail::DesktopImageSource>();
        integration::detail::DesktopImageTurn turn(source, tools, ctx, {});
        mira::BuiltinToolRegistry registry;
        turn.register_tool(registry, {}, false);
        const auto specs = registry.exposed_tools();
        MIRAGE_CHECK(specs.size() == (mode == "--probe-host-ocr" ? 2 : 1));
        auto invoke = [&](const std::string &name, const char *arguments) {
            const auto spec = std::find_if(specs.begin(), specs.end(),
                                           [&](const auto &s) { return s.wire_name == name; });
            MIRAGE_CHECK(spec != specs.end());
            mira::ToolProposal proposal;
            proposal.operation_id = mira::OperationId::generate();
            proposal.tool_id = spec->tool_id;
            proposal.tool_version = spec->version;
            proposal.wire_name = spec->wire_name;
            proposal.provider_call_id.value = "portal";
            proposal.arguments = mira::parse_json(arguments).value();
            proposal.arguments_digest = mira::canonical_json_digest(proposal.arguments);
            return registry.execute(proposal, ctx);
        };
        auto execute = [&](const char *arguments) {
            return invoke("desktop.screen.capture", arguments);
        };
        auto result = execute(R"({"source":"desktop"})");
        if (mode == "tool_denied") {
            MIRAGE_CHECK(result && result.value().failed);
            return;
        }
        if (mode == "tool") {
            // The platform's fail-closed error must surface through the tool
            // result so the model can explain it; no image is supplied.
            MIRAGE_CHECK(result && result.value().failed);
            const auto &summary = result.value().safe_error_summary;
            MIRAGE_CHECK(summary.find("unsupported_capability") != std::string::npos);
#ifdef MIRAGE_TEST_PIPEWIRE
            MIRAGE_CHECK(summary.find("does not provide ScreenCast") != std::string::npos);
#else
            MIRAGE_CHECK(summary.find("not compiled into this build") != std::string::npos);
#endif
            return;
        }
        MIRAGE_CHECK(result && !result.value().failed);
        if (!result || result.value().failed)
            return;
        const auto &metadata = result.value().result;
        if (mode == "--probe-host-tool" || mode == "--probe-host-ocr")
            std::fprintf(
                stderr,
                "host Portal tool: image=%lldx%lld source=%lldx%lld observation_only=true\n",
                static_cast<long long>(metadata.find("width")->as_integer().value()),
                static_cast<long long>(metadata.find("height")->as_integer().value()),
                static_cast<long long>(metadata.find("source_width")->as_integer().value()),
                static_cast<long long>(metadata.find("source_height")->as_integer().value()));
        MIRAGE_CHECK(metadata.find("observation_only") &&
                     metadata.find("observation_only")->as_boolean() == true);
        MIRAGE_CHECK(!metadata.find("image_scope") && !metadata.find("origin_x") &&
                     !metadata.find("desktop_per_image_x_numerator"));
        MIRAGE_CHECK(metadata.find("received_at_ms") && !metadata.find("captured_at_ms"));
        mira::AgentLoopSpec loop;
        loop.task_id = ctx.task;
        loop.session_id = ctx.session;
        loop.task_epoch = ctx.task_epoch;
        auto supplied = turn.supply(loop);
        MIRAGE_CHECK(supplied && supplied.value().size() == 1);
        if (supplied && !supplied.value().empty()) {
            const auto *image =
                std::get_if<mira::ImagePart>(&supplied.value().front().content.back());
            MIRAGE_CHECK(image && image->source.media_type == "image/png");
            if (image)
                MIRAGE_CHECK(static_cast<bool>(source->fetch(image->source)));
        }
        if (mode == "--probe-host-tool" || mode == "--probe-host-ocr") {
            // Model/human latency followed by a mutation must permit a new
            // frame in the existing authorized session, with no second UI.
            (void)::poll(nullptr, 0, 6100);
            {
                auto lease = tools.actions->try_begin({});
                MIRAGE_CHECK(static_cast<bool>(lease));
            }
            auto refreshed = execute(R"({"source":"desktop"})");
            MIRAGE_CHECK(refreshed && !refreshed.value().failed);
            if (refreshed && !refreshed.value().failed) {
                MIRAGE_CHECK(*refreshed.value().result.find("artifact_id")->as_string() !=
                             *metadata.find("artifact_id")->as_string());
                std::fprintf(
                    stderr,
                    "host Portal: second fresh frame after 6.1s/action, same authorization\n");
            }
            if (mode == "--probe-host-ocr") {
                for (int iteration = 0; iteration < 3; ++iteration) {
                    auto read = invoke("desktop.vision.ocr", "{}");
                    if (read && read.value().failed)
                        std::fprintf(stderr, "host native OCR failure: %s\n",
                                     read.value().safe_error_summary.c_str());
                    MIRAGE_CHECK(read && !read.value().failed);
                    if (!read || read.value().failed)
                        break;
                    if (read && !read.value().failed) {
                        const auto &text = read.value().result;
                        MIRAGE_CHECK(text.find("coordinate_space") &&
                                     *text.find("coordinate_space")->as_string() == "image_pixels");
                        MIRAGE_CHECK(!text.find("display_id") && !text.find("visual_scope"));
                        MIRAGE_CHECK(!text.find("model_revision")->as_string()->empty());
                        MIRAGE_CHECK(text.find("backend_calls")->as_integer() > 0);
                        MIRAGE_CHECK(text.find("regions")->as_array()->size() <= 128);
                        auto supplied_ocr = turn.supply(loop);
                        MIRAGE_CHECK(supplied_ocr && supplied_ocr.value().size() == 1);
                        const auto *new_image = std::get_if<mira::ImagePart>(
                            &supplied_ocr.value().front().content.back());
                        MIRAGE_CHECK(new_image && new_image->source.id.to_string() ==
                                                      *text.find("image_artifact_id")->as_string());
                        std::fprintf(
                            stderr,
                            "host native OCR: %zu read-only regions, backend_calls=%lld; same "
                            "authorization, source image matched\n",
                            text.find("regions")->as_array()->size(),
                            static_cast<long long>(
                                text.find("backend_calls")->as_integer().value()));
                    }
                }
            }
            tools.actions->takeover();
            MIRAGE_CHECK(tools.actions->resume());
        }
        auto again = execute(R"({"source":"desktop"})");
        MIRAGE_CHECK(!again || again.value().failed);
        auto invalid = execute(R"({"source":"desktop","source_id":"eDP-1"})");
        MIRAGE_CHECK(invalid && invalid.value().failed);
    });
    future.get();
    executor.shutdown(true);
    return testing::finish("portal_screen_test");
}
