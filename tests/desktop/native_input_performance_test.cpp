#include "../../apps/native/app.cpp"
#include "../support/ipc_io.hpp"
#include "../support/test.hpp"
#include "../support/xvfb_display.hpp"
#include <GLFW/glfw3.h>
#include <core/dsl_runtime.h>
#include <iostream>

namespace app {
void requestUpdate() {}
} // namespace app

namespace {
using Clock = std::chrono::steady_clock;
double milliseconds(Clock::time_point begin) {
    return std::chrono::duration<double, std::milli>(Clock::now() - begin).count();
}
void report(const char *phase, std::size_t bytes, int history, std::vector<double> values) {
    std::sort(values.begin(), values.end());
    std::cout << "{\"phase\":\"" << phase << "\",\"bytes\":" << bytes
              << ",\"history_turns\":" << history << ",\"samples\":" << values.size()
              << ",\"p50_ms\":" << values[values.size() / 2]
              << ",\"p95_ms\":" << values[values.size() * 95 / 100]
              << ",\"max_ms\":" << values.back() << "}\n";
}
} // namespace

int main(int argc, char **argv) {
    const bool unbroken = argc > 1 && std::string_view(argv[1]) == "--unbroken";
    mirage::testing::TempDir private_state;
    for (const auto *variable : {"XDG_CONFIG_HOME", "XDG_STATE_HOME", "XDG_DATA_HOME"}) {
        const auto directory = private_state.root() / variable;
        std::filesystem::create_directories(directory);
        MIRAGE_CHECK(::setenv(variable, directory.c_str(), 1) == 0);
    }
    mirage::testing::XvfbDisplay display(mirage::testing::find_xvfb(), "1180x800x24");
    ::setenv("DISPLAY", display.display_name().c_str(), 1);
    ::unsetenv("WAYLAND_DISPLAY");
    MIRAGE_CHECK(glfwInit());
    core::window::WindowCreateRequest request;
    request.width = 1180;
    request.height = 800;
    request.title = "Mirage isolated input benchmark";
    auto handle = core::window::createWindow(request);
    MIRAGE_CHECK(handle);
    if (!handle)
        return mirage::testing::finish("native_input_performance_test");
    auto *window = static_cast<GLFWwindow *>(handle);
    glfwMakeContextCurrent(window);
    core::render::initializeRenderBackendLoader();
    auto backend = core::render::createRenderBackend(handle);
    MIRAGE_CHECK(backend && backend->initialize());
    core::render::ScopedRenderBackend backend_scope(*backend);
    using namespace mirage::native_ui;
    core::TextPrimitive::setDefaultFontFiles(text_font(), "assets/Phosphor-Regular.ttf");
    auto &page = state();
    page.reduced_motion = true;
    page.runtime_notice = "隔离输入性能测试";
    core::dsl::Runtime runtime;
    MIRAGE_CHECK(runtime.initialize(handle));
    eui::Ui *view = nullptr;
    auto compose = [&] {
        runtime.compose("input-performance", 1180, 800, [&](auto &ui, const auto &screen) {
            view = &ui;
            compose_page(ui, screen);
        });
    };
    // Keep every document below the product's 16 KiB draft limit, including edits.
    for (const int history : {0, 40}) {
        if (history) {
            const auto id = page.chat.current().id;
            for (int i = 0; i < history; ++i)
                page.chat.apply_turn(
                    id, "turn-" + std::to_string(i), "ok", "测试输入",
                    "## 回复\n\n中文与 English 混排的性能测试。\n\n- 列表\n- **内容**", {}, {},
                    static_cast<std::uint64_t>(i + 1));
        }
        for (const std::size_t bytes : {64, 4096, 16000}) {
            std::string document;
            while (document.size() + 40 < bytes)
                document +=
                    unbroken ? "输入测试 English 123，继续。 " : "输入测试 English 123，继续。\n";
            page.chat.set_draft(document);
            compose();
            const auto input_id = "composer.input." + std::to_string(page.chat.current().id);
            const auto bounds = view->find(input_id + ".hit")->frame;
            core::queuePointerButton(handle, bounds.x + 20, bounds.y + 20,
                                     core::PointerButton::Left, core::PointerAction::Press, {});
            runtime.update(handle, 0, 1, 1);
            core::queuePointerButton(handle, bounds.x + 20, bounds.y + 20,
                                     core::PointerButton::Left, core::PointerAction::Release, {});
            runtime.update(handle, 0, 1, 1);
            auto &input = view->state<components::input_detail::InputModel::InputState>(input_id);
            input.cursor = static_cast<int>(input.text.size());
            input.selectionStart = input.selectionEnd = input.cursor;
            compose();
            runtime.update(handle, 0, 1, 1);
            runtime.render(1180, 800, 1, palette(page.dark).background);
            glFinish();
            std::vector<double> edits, compositions, updates, renders, totals;
            for (int i = 0; i < 65; ++i) {
                const auto total_start = Clock::now();
                const auto edit_start = Clock::now();
                auto callback = view->find(input_id + ".hit")->onTextInput;
                eui::TextInputEvent event;
                event.text = "字";
                callback(event);
                const auto edit = milliseconds(edit_start);
                const auto compose_start = Clock::now();
                compose();
                const auto composition = milliseconds(compose_start);
                const auto update_start = Clock::now();
                runtime.update(handle, 0, 1, 1);
                const auto update = milliseconds(update_start);
                const auto render_start = Clock::now();
                runtime.render(1180, 800, 1, palette(page.dark).background);
                glFinish();
                const auto render = milliseconds(render_start);
                const auto total = milliseconds(total_start);
                if (i >= 5) {
                    edits.push_back(edit);
                    compositions.push_back(composition);
                    updates.push_back(update);
                    renders.push_back(render);
                    totals.push_back(total);
                }
            }
            std::string expected = document;
            for (int i = 0; i < 65; ++i)
                expected += "字";
            MIRAGE_CHECK(page.chat.current().draft == expected);
            report("edit", bytes, history, edits);
            report("compose", bytes, history, compositions);
            report("update", bytes, history, updates);
            report("render_finish", bytes, history, renders);
            report("total", bytes, history, totals);
        }
    }
    runtime.shutdown();
    backend.reset();
    core::window::destroyWindow(handle);
    glfwTerminate();
    return mirage::testing::finish("native_input_performance_test");
}
