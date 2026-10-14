#include "../../apps/pet/bubble_view.hpp"
#include "../../apps/pet/frames.hpp"
#include "../support/ipc_io.hpp"
#include "../support/test.hpp"
#include "../support/xvfb_display.hpp"
#include <GLFW/glfw3.h>
#include <core/dsl_runtime.h>
#include <filesystem>
#include <png.h>
#include <vector>

namespace app {
void requestUpdate() {}
} // namespace app
int main(int argc, char **argv) {
    mirage::testing::TempDir private_state;
    for (const auto *variable : {"XDG_CONFIG_HOME", "XDG_STATE_HOME", "XDG_DATA_HOME"}) {
        const auto directory = private_state.root() / variable;
        std::filesystem::create_directories(directory);
        MIRAGE_CHECK(::setenv(variable, directory.c_str(), 1) == 0);
    }
    mirage::testing::XvfbDisplay display(mirage::testing::find_xvfb(), "1200x1000x24");
    ::setenv("DISPLAY", display.display_name().c_str(), 1);
    ::unsetenv("WAYLAND_DISPLAY");
    MIRAGE_CHECK(glfwInit());
    core::window::WindowCreateRequest request;
    request.width = 264;
    request.height = 322;
    request.title = "Pet bubble private fixture";
    auto handle = core::window::createWindow(request);
    MIRAGE_CHECK(handle);
    auto *window = static_cast<GLFWwindow *>(handle);
    glfwMakeContextCurrent(window);
    core::render::initializeRenderBackendLoader();
    auto backend = core::render::createRenderBackend(handle);
    MIRAGE_CHECK(backend && backend->initialize());
    core::render::ScopedRenderBackend backend_scope(*backend);
    core::TextPrimitive::setDefaultFontFiles("assets/NotoSansSC-Regular.otf", "");
    mirage::pet_ui::DecodedClip clip;
    std::string diagnostic;
    MIRAGE_CHECK(mirage::pet_ui::decode_clip(
        "assets/pet", *mirage::runtime::pet::clip_for_display("thinking"), clip, diagnostic));
    auto images = std::make_shared<eui::ImageStream>(1);
    eui::ImageFrame image;
    image.pixels = clip.rgba_frames[22];
    image.width = image.height = 512;
    image.stride = 2048;
    image.format = eui::ImagePixelFormat::RGBA8;
    image.sequence = 1;
    MIRAGE_CHECK(images->submit(std::move(image)));
    core::dsl::Runtime runtime;
    MIRAGE_CHECK(runtime.initialize(handle));
    using namespace mirage::pet_ui;
    using namespace std::chrono_literals;
    const auto now = BubblePresentation::Clock::now();
    for (const float scale : {1.0f, 2.0f}) {
        const int width = static_cast<int>(264 * scale), height = static_cast<int>(322 * scale);
        glfwSetWindowSize(window, width, height);
        glfwPollEvents();
        glfwSwapBuffers(window);
        for (bool dark : {false, true})
            for (const std::string kind : {"thinking", "dialog"}) {
                mirage::runtime::persistence::AppPreferences preferences;
                preferences.dark = dark;
                auto layout = mirage::platform::pet_window::Layout{static_cast<float>(width),
                                                                   static_cast<float>(height),
                                                                   12 * scale,
                                                                   70 * scale,
                                                                   240 * scale,
                                                                   false};
                auto bubble = std::optional{mirage::runtime::pet::Bubble{
                    kind, kind == "thinking" ? "model_preview" : "answer_summary", "request",
                    kind == "thinking" ? "正在检查当前配置，确认路径是否正确，再核对操作结果。"
                                       : "配置尚未验证，需要检查。",
                    1, false, false, 0}};
                BubblePresentation presentation;
                auto measure = [scale](const std::string &text) {
                    core::TextStyle style;
                    style.text = text;
                    style.fontSize = mirage::native_ui::ui_font_size(13) * scale;
                    return core::TextPrimitive::measureTextSize(style).x;
                };
                presentation.update("owner", "source", bubble, now, measure, scale);
                eui::Ui *view = nullptr;
                auto compose = [&] {
                    runtime.compose("pet-test", static_cast<float>(width),
                                    static_cast<float>(height), [&](auto &ui, const auto &) {
                                        view = &ui;
                                        ui.image("character")
                                            .position(layout.image_x, layout.image_y)
                                            .size(layout.image_side, layout.image_side)
                                            .stream(images)
                                            .contain()
                                            .hitTestMode(eui::dsl::HitTestMode::None)
                                            .build();
                                        compose_bubble_view(ui, presentation, layout, preferences,
                                                            now + 1s);
                                    });
                };
                compose();
                runtime.update(handle, 1, 1, 1);
                compose();
                runtime.update(handle, 1, 1, 1);
                compose();
                const auto *body = view->find("pet.bubble.body"),
                           *viewport = view->find("pet.bubble.viewport");
                MIRAGE_CHECK(body && viewport);
                MIRAGE_CHECK(body && std::abs(body->frame.width - 240 * scale) < .1f &&
                             std::abs(body->frame.height - 36 * scale) < .1f);
                MIRAGE_CHECK(viewport && std::abs(viewport->frame.width - 216 * scale) < .1f &&
                             std::abs(viewport->frame.height - 20 * scale) < .1f);
                MIRAGE_CHECK(body && body->hitTestMode == eui::dsl::HitTestMode::None);
                if (kind == "dialog") {
                    const auto *text = view->find("pet.bubble.line.static");
                    MIRAGE_CHECK(text && text->text == bubble->text);
                    MIRAGE_CHECK(text && measure(text->text) <= 216 * scale);
                    MIRAGE_CHECK(view->find("pet.bubble.tail") &&
                                 view->find("pet.bubble.tail")->frame.width > 0 &&
                                 view->find("pet.bubble.tail")->frame.height > 0);
                } else {
                    const auto *line = view->find("pet.bubble.line.0");
                    MIRAGE_CHECK(line && line->frame.x < viewport->frame.x);
                    MIRAGE_CHECK(view->find("pet.bubble.tail.small"));
                }
                if (argc > 1) {
                    runtime.requestFullPaint();
                    runtime.render(width, height, 1,
                                   dark ? core::Color{.08f, .08f, .08f, 1}
                                        : core::Color{.92f, .92f, .92f, 1});
                    glFinish();
                    std::vector<unsigned char> pixels(static_cast<std::size_t>(width * height * 3)),
                        flipped(pixels.size());
                    glPixelStorei(GL_PACK_ALIGNMENT, 1);
                    glReadPixels(0, 0, width, height, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());
                    for (int y = 0; y < height; ++y)
                        std::copy_n(pixels.data() + (height - y - 1) * width * 3, width * 3,
                                    flipped.data() + y * width * 3);
                    std::filesystem::create_directories(argv[1]);
                    png_image png{};
                    png.version = PNG_IMAGE_VERSION;
                    png.width = static_cast<png_uint_32>(width);
                    png.height = static_cast<png_uint_32>(height);
                    png.format = PNG_FORMAT_RGB;
                    const auto path = std::filesystem::path(argv[1]) /
                                      (kind + (dark ? "-dark-" : "-light-") +
                                       std::to_string(static_cast<int>(scale)) + ".png");
                    MIRAGE_CHECK(
                        png_image_write_to_file(&png, path.c_str(), 0, flipped.data(), 0, nullptr));
                }
                preferences.reduced_motion = true;
                compose();
                MIRAGE_CHECK(view->find("pet.bubble.line.static") &&
                             !view->find("pet.bubble.line.0"));
                if (kind == "thinking") {
                    const auto *line = view->find("pet.bubble.line.static");
                    MIRAGE_CHECK(line && measure(line->text) <= 216 * scale);
                }
                layout.bubble_below = true;
                layout.image_y = 12 * scale;
                compose();
                MIRAGE_CHECK(view->find("pet.bubble.body")->frame.y > layout.image_side);
                presentation.update("owner", "source", std::nullopt, now, measure, scale);
                compose();
                MIRAGE_CHECK(!view->find("pet.bubble.body"));
            }
    }
    images.reset();
    clip = {};
    runtime.shutdown();
    backend.reset();
    glfwDestroyWindow(window);
    glfwTerminate();
    return mirage::testing::finish("pet_bubble_view_test");
}
