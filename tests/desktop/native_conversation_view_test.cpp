#include "../support/ipc_io.hpp"
#include "../support/test.hpp"
#include "../support/xvfb_display.hpp"
// Compile the product view into this isolated test translation unit, keeping
// private UI state out of the product's public API and production test hooks.
#include "../../apps/native/app.cpp"
#include <GLFW/glfw3.h>
#include <ft2build.h>
#include FT_FREETYPE_H
#include <core/dsl_runtime.h>
#include <fstream>
#include <iostream>

// The test drives explicit frames; it never starts the product event loop.
namespace app {
void requestUpdate() {}
} // namespace app

int main(int argc, char **argv) {
    // Keep direct screenshot invocations private too; CTest's fixture is not
    // involved when this binary is launched manually.
    mirage::testing::TempDir private_state;
    for (const auto *variable : {"XDG_CONFIG_HOME", "XDG_STATE_HOME", "XDG_DATA_HOME"}) {
        const auto directory = private_state.root() / variable;
        std::filesystem::create_directories(directory);
        MIRAGE_CHECK(::setenv(variable, directory.c_str(), 1) == 0);
    }
    mirage::testing::XvfbDisplay display(mirage::testing::find_xvfb(), "2600x1800x24");
    ::setenv("DISPLAY", display.display_name().c_str(), 1);
    ::unsetenv("WAYLAND_DISPLAY");
    MIRAGE_CHECK(glfwInit());
    core::window::WindowCreateRequest request;
    request.width = 1180;
    request.height = 800;
    request.title = "Mirage conversation test";
    auto handle = core::window::createWindow(request);
    MIRAGE_CHECK(handle);
    if (!handle)
        return mirage::testing::finish("native_conversation_view_test");
    auto *window = static_cast<GLFWwindow *>(handle);
    glfwMakeContextCurrent(window);
    core::render::initializeRenderBackendLoader();
    auto backend = core::render::createRenderBackend(handle);
    MIRAGE_CHECK(backend && backend->initialize());
    core::render::ScopedRenderBackend backend_scope(*backend);
    using namespace mirage::native_ui;
    window::initialize();
    // Reject an absent/corrupt shipped face rather than accepting EUI's fallback.
    FT_Library font_library = nullptr;
    FT_Face font_face = nullptr;
    MIRAGE_CHECK(FT_Init_FreeType(&font_library) == 0);
    MIRAGE_CHECK(FT_New_Face(font_library, text_font().c_str(), 0, &font_face) == 0);
    if (font_face) {
        MIRAGE_CHECK(std::string(font_face->family_name) == "Noto Sans SC");
        MIRAGE_CHECK(font_face->num_glyphs >= 29000);
        MIRAGE_CHECK(font_face->units_per_EM == 1000 && font_face->ascender == 1160 &&
                     font_face->descender == -288);
        for (const auto glyph : {0x4e2dUL, 0x6587UL, 0x7530UL, 0xff0cUL, 0x0041UL, 0x0030UL})
            MIRAGE_CHECK(FT_Get_Char_Index(font_face, glyph) != 0);
        FT_Done_Face(font_face);
    }
    if (font_library)
        FT_Done_FreeType(font_library);
    // Match the product's shipped icon face (dslAppConfig().fonts) so Phosphor
    // codepoints render exactly as they do in the application.
    core::TextPrimitive::setDefaultFontFiles(text_font(), "assets/Phosphor-Regular.ttf");
    // M6-42: metrics-only sizing must preserve the drawable's geometry at
    // physical font sizes, including wrapping and trailing empty paragraphs.
    {
        core::TextPrimitive atlas_owner;
        MIRAGE_CHECK(atlas_owner.initialize());
        PlainTextLayout measurements;
        for (const std::string sample :
             {"", "中文输入 English 123，标点。", "one\ntwo\n", "CRLF\r\n\r\n末行",
              "\tTab  spaces  ", "office ffi e\xcc\x81 中文"})
            for (const float scale : {1.0f, 1.25f, 2.0f})
                for (const float size : {12.0f, 14.0f, 24.0f, 32.0f})
                    for (const float width : {0.0f, 80.0f, 320.0f}) {
                        const float line = ui_input_line_height(size);
                        core::TextStyle style;
                        style.text = sample;
                        style.fontSize = ui_font_size(size) * scale;
                        style.maxWidth = width * scale;
                        style.wrap = width > 0;
                        style.lineHeight = line * scale;
                        core::TextPrimitive drawable;
                        drawable.setStyle(style);
                        const auto expected = drawable.measuredSize();
                        const auto actual = measurements.measure(sample, width, size, line, scale);
                        MIRAGE_CHECK(std::abs(actual.x * scale - expected.x) < 0.01f);
                        MIRAGE_CHECK(std::abs(actual.y * scale - expected.y) < 0.01f);
                        const auto cached = measurements.measure(sample, width, size, line, scale);
                        MIRAGE_CHECK(cached.x == actual.x && cached.y == actual.y);
                        for (const float limit : {24.0f, 60.0f, 144.0f})
                            MIRAGE_CHECK(std::abs(measurements.height_up_to(sample, width, size,
                                                                            line, scale, limit) -
                                                  std::min(limit, actual.y)) < 0.01f);
                    }
        std::string long_draft;
        for (int i = 0; i < 400; ++i)
            long_draft += "中文 English\n";
        for (const float scale : {1.0f, 1.25f, 2.0f}) {
            const auto full =
                measurements.measure(long_draft, 600, 14, ui_input_line_height(14), scale);
            MIRAGE_CHECK(measurements.height_up_to(long_draft, 600, 14, ui_input_line_height(14),
                                                   scale, 144) == std::min(144.0f, full.y));
        }
        // The oracle rasterizes many large fonts. Release its atlas before
        // the actual view so screenshot coverage starts with a fresh budget.
        atlas_owner.destroy();
    }
    auto &page = state();
    page.runtime_notice = "合成测试会话 · 不连接模型";
    const auto session_id = page.chat.current().id;
    page.chat.apply_turn(
        session_id, "first", "ok", "帮我梳理这个项目的结构。",
        "## 实现结构\n\nMirage 提供 **原生界面**，Mira 负责 Agent 推理。\n\n- 窗口与状态栏分离\n- "
        "会话由服务管理\n\n可以先完善会话体验，再接入桌面工作流。",
        {}, {}, 1);
    {
        auto sample = page.chat.current();
        const auto narrow_key = conversation_measure_key(sample, page, 799.75f, 500);
        const auto key = conversation_measure_key(sample, page, 800, 500);
        MIRAGE_CHECK(!key.empty() && key != narrow_key);
        sample.draft = "Typing does not change history height";
        sample.scroll_offset = 80;
        MIRAGE_CHECK(conversation_measure_key(sample, page, 800, 500) == key);
        sample.messages.back().text += "\n新段落";
        const auto edited = conversation_measure_key(sample, page, 800, 500);
        MIRAGE_CHECK(edited != key);
        sample.messages.back().status = "运行中";
        const auto pending = conversation_measure_key(sample, page, 800, 500);
        MIRAGE_CHECK(pending != edited);
        sample.messages.back().parts.push_back({});
        auto &part = sample.messages.back().parts.back();
        part.id = "tool";
        part.kind = "tool";
        part.output = "short";
        sample.messages.back().expanded_parts.push_back(part.id);
        const auto expanded = conversation_measure_key(sample, page, 800, 500);
        MIRAGE_CHECK(expanded != pending);
        part.output += "\nmore output";
        MIRAGE_CHECK(conversation_measure_key(sample, page, 800, 500) != expanded);
        const auto before_font = conversation_measure_key(sample, page, 800, 500);
        ++page.font_size_step;
        MIRAGE_CHECK(conversation_measure_key(sample, page, 800, 500) != before_font);
        --page.font_size_step;
        part.output.assign(64 * 1024, 'x');
        MIRAGE_CHECK(!conversation_measure_key(sample, page, 800, 500).empty());
        // The old 64 KiB key limit must not disable caching long histories.
        const auto long_key = conversation_measure_key(sample, page, 800, 500);
        MIRAGE_CHECK(conversation_measure_key(sample, page, 800, 500) == long_key);
        part.output.assign(9 * 1024 * 1024, 'x');
        MIRAGE_CHECK(conversation_measure_key(sample, page, 800, 500).empty());
    }
    {
        ConversationLayout cache;
        ConversationLayout::Context context{800, 500, 1, 1, 1, false};
        auto sample = page.chat.current();
        sample.messages.back().text.assign(80 * 1024, 'x');
        const auto key = cache.prepare(sample, context);
        int calculations = 0;
        const auto measure = [&](const LocalMessage &message, float width) {
            return cache.measure(message, width, [&] {
                ++calculations;
                MessageGeometry geometry;
                geometry.row_height = static_cast<float>(message.text.size()) / width;
                return geometry;
            });
        };
        for (const auto &message : sample.messages) {
            measure(message, 800);
            measure(message, 788);
        }
        const auto initial = calculations;
        MIRAGE_CHECK(initial == static_cast<int>(sample.messages.size()) * 2);
        MIRAGE_CHECK(cache.prepare(sample, context) == key);
        for (const auto &message : sample.messages) {
            measure(message, 800);
            measure(message, 788);
        }
        MIRAGE_CHECK(calculations == initial);
        // Only the edited message is measured again, including equal-length edits.
        sample.messages.back().text.back() = 'y';
        MIRAGE_CHECK(cache.prepare(sample, context) != key);
        for (const auto &message : sample.messages)
            measure(message, 788);
        MIRAGE_CHECK(calculations == initial + 1);
        int expected = calculations;
        const auto check_change = [&] {
            cache.prepare(sample, context);
            measure(sample.messages.back(), 788);
            MIRAGE_CHECK(calculations == ++expected);
        };
        sample.messages.back().error = "failure";
        check_change();
        sample.messages.back().status = "已停止或失败";
        check_change();
        sample.messages.back().timings.ocr = {1, 50};
        check_change();
        sample.messages.back().parts.push_back({});
        check_change();
        sample.messages.back().parts.back().output = "changed";
        check_change();
        sample.messages.back().expanded_parts.push_back("thinking");
        check_change();
        // Composer growth and theme changes invalidate the scroll extent key,
        // but preserve geometry. Width uses its own pair of cached measurements.
        const auto old_key = cache.prepare(sample, context);
        context.height -= 22;
        context.dark = true;
        MIRAGE_CHECK(cache.prepare(sample, context) != old_key);
        measure(sample.messages.back(), 788);
        MIRAGE_CHECK(calculations == expected);
        context.scale = 1.25f;
        check_change();
        ++context.font_step;
        check_change();
        ++context.code_step;
        check_change();
        const auto ordered = cache.prepare(sample, context);
        std::reverse(sample.messages.begin(), sample.messages.end());
        MIRAGE_CHECK(cache.prepare(sample, context) != ordered);
        ++sample.id;
        cache.prepare(sample, context);
        measure(sample.messages.front(), 788);
        MIRAGE_CHECK(calculations == ++expected);
    }
    page.chat.current().scroll_offset = 0;
    core::dsl::Runtime runtime;
    MIRAGE_CHECK(runtime.initialize(handle));
    eui::Ui *view = nullptr;
    int width = 1180, height = 800;
    float dpi_scale = 1;
    auto compose = [&] {
        runtime.compose("test", static_cast<float>(width), static_cast<float>(height),
                        [&](auto &ui, const auto &screen) {
                            view = &ui;
                            compose_page(ui, screen);
                        });
        // Validate rendered geometry across every product button and every state.
        // This catches regressions from new controls and dependency layout changes.
        auto audit = [&](auto &&self, const core::dsl::Element &node) -> void {
            if (node.id.ends_with(".text")) {
                const auto base = node.id.substr(0, node.id.size() - 5);
                const auto *background = view->find(base + ".bg");
                const auto *content = view->find(base + ".content");
                if (background && content) {
                    core::TextStyle measured;
                    measured.fontSize = node.fontSize;
                    measured.text = node.text;
                    MIRAGE_CHECK(core::TextPrimitive::measureTextSize(measured).x <=
                                 node.frame.width + 0.5f);
                    const auto *glyph = view->find(base + ".icon");
                    const float first_x =
                        glyph ? std::min(glyph->frame.x, node.frame.x) : node.frame.x;
                    const float left_inset = first_x - background->frame.x;
                    const float content_right = glyph
                                                    ? std::max(glyph->frame.x + glyph->frame.width,
                                                               node.frame.x + node.frame.width)
                                                    : node.frame.x + node.frame.width;
                    const float right_inset =
                        background->frame.x + background->frame.width - content_right;
                    MIRAGE_CHECK(std::abs(left_inset - control_text_inset) < 0.5f);
                    MIRAGE_CHECK(std::abs(right_inset - control_text_inset) < 0.5f);
                    if (glyph) {
                        const float gap = glyph->frame.x < node.frame.x
                                              ? node.frame.x - glyph->frame.x - glyph->frame.width
                                              : glyph->frame.x - node.frame.x - node.frame.width;
                        MIRAGE_CHECK(std::abs(gap - control_icon_gap) < 0.5f);
                    }
                }
            }
            if (node.id.ends_with(".textViewport")) {
                const auto base = node.id.substr(0, node.id.size() - 13);
                if (const auto *hit = view->find(base + ".hit")) {
                    MIRAGE_CHECK(std::abs(node.frame.x - hit->frame.x - control_text_inset) < 0.5f);
                    MIRAGE_CHECK(hit->frame.x + hit->frame.width - node.frame.x -
                                     node.frame.width >=
                                 control_text_inset - 0.5f);
                }
            }
            for (const auto &child : node.children)
                self(self, *child);
        };
        for (const auto &root : view->roots())
            audit(audit, *root);
    };
    auto frame = [&] {
        // Static layout assertions inspect settled endpoints; motion itself is
        // exercised with a deterministic clock below.
        page.sidebar_motion.initialized = false;
        page.theme_motion.initialized = false;
        int actual_width = 0, actual_height = 0;
        glfwGetWindowSize(window, &actual_width, &actual_height);
        const auto pixels_width = static_cast<int>(std::lround(width * dpi_scale));
        const auto pixels_height = static_cast<int>(std::lround(height * dpi_scale));
        if (actual_width != pixels_width || actual_height != pixels_height) {
            glfwSetWindowSize(window, pixels_width, pixels_height);
            glfwPollEvents();
            glfwSwapBuffers(window); // Realize resized GLX back buffers before rendering.
        }
        compose();
        runtime.update(handle, 1, dpi_scale, 1);
        compose();
        runtime.update(handle, 1, dpi_scale, 1);
        compose();
    };
    {
        MotionValue motion;
        const auto start = std::chrono::steady_clock::now();
        MIRAGE_CHECK(!motion.step(260.0f, start, 0.22f));
        MIRAGE_CHECK(motion.step(0.0f, start, 0.22f));
        MIRAGE_CHECK(motion.step(0.0f, start + std::chrono::milliseconds{110}, 0.22f));
        MIRAGE_CHECK(motion.current > 0.0f && motion.current < 260.0f);
        const float reversed_from = motion.current;
        MIRAGE_CHECK(motion.step(260.0f, start + std::chrono::milliseconds{110}, 0.22f));
        MIRAGE_CHECK(std::abs(motion.current - reversed_from) < 0.01f);
        MIRAGE_CHECK(!motion.step(260.0f, start + std::chrono::milliseconds{400}, 0.22f));
        MIRAGE_CHECK(motion.current == 260.0f);
        MIRAGE_CHECK(!motion.step(0.0f, start + std::chrono::milliseconds{410}, 0));
        MIRAGE_CHECK(motion.current == 0.0f);
    }
    auto capture = [&](const std::string &name) {
        if (argc < 2)
            return;
        runtime.requestFullPaint();
        const auto pixels_width = static_cast<int>(std::lround(width * dpi_scale));
        const auto pixels_height = static_cast<int>(std::lround(height * dpi_scale));
        runtime.render(pixels_width, pixels_height, dpi_scale, palette(page.dark).background);
        glFinish();
        std::vector<unsigned char> pixels(
            static_cast<std::size_t>(pixels_width * pixels_height * 3));
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        glReadPixels(0, 0, pixels_width, pixels_height, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());
        std::filesystem::create_directories(argv[1]);
        std::ofstream output(std::filesystem::path(argv[1]) / (name + ".ppm"), std::ios::binary);
        output << "P6\n" << pixels_width << " " << pixels_height << "\n255\n";
        for (int row = pixels_height - 1; row >= 0; --row)
            output.write(reinterpret_cast<const char *>(pixels.data() + row * pixels_width * 3),
                         pixels_width * 3);
    };
    frame();
    const auto user = page.chat.current().messages.front().id;
    const auto assistant = page.chat.current().messages.back().id;
    auto element = [&](const std::string &key) {
        auto *found = view->find(key);
        if (!found)
            std::fprintf(stderr, "element lookup failed: %s\n", key.c_str());
        MIRAGE_CHECK(found);
        return found;
    };
    {
        const auto estimate_saved_usage = page.chat.current().context_usage;
        const auto estimate_saved_popup = page.popup;
        const auto estimate_saved_width = width;
        const auto estimate_saved_height = height;
        const auto estimate_saved_dark = page.dark;
        for (const int estimate_viewport : {1180, 860}) {
            width = estimate_viewport;
            height = estimate_viewport == 1180 ? 800 : 620;
            glfwSetWindowSize(window, width, height);
            for (const bool estimate_dark : {false, true}) {
                page.dark = estimate_dark;
                page.popup = PageState::Popup::Context;
                page.chat.current().context_usage =
                    native_usage(ipc::ContextUsage{8192, 32768, "estimate-fixture", true});
                frame();
                MIRAGE_CHECK(element("composer.popup.context.percent")->text == "约 25%");
                MIRAGE_CHECK(element("composer.popup.context.tokens")->text ==
                             "约 8,192 / 32,768 Token");
                MIRAGE_CHECK(element("composer.popup.context.source")->text ==
                             "上次请求 · 本地估算");
                const auto estimate_bounds = element("composer.popup")->frame;
                MIRAGE_CHECK(estimate_bounds.x >= 0 && estimate_bounds.y >= 0 &&
                             estimate_bounds.x + estimate_bounds.width <= width &&
                             estimate_bounds.y + estimate_bounds.height <= height);
                capture("context-estimated-" + std::to_string(estimate_viewport) +
                        (estimate_dark ? "-dark" : "-light"));
                page.chat.current().context_usage->window_tokens = 0;
                frame();
                MIRAGE_CHECK(element("composer.popup.context.percent")->text == "未知");
                MIRAGE_CHECK(element("composer.popup.context.tokens")->text ==
                             "约 8,192 / 未知 Token");
                MIRAGE_CHECK(element("composer.popup.context.source")->text ==
                             "本地估算 · 窗口预算未知");
                page.chat.current().context_usage =
                    native_usage(ipc::ContextUsage{0, 32768, "provider-fixture", false});
                frame();
                MIRAGE_CHECK(element("composer.popup.context.percent")->text == "0%");
                MIRAGE_CHECK(element("composer.popup.context.source")->text ==
                             "上次请求 · 模型输入用量");
            }
        }
        page.chat.current().context_usage = estimate_saved_usage;
        page.popup = estimate_saved_popup;
        width = estimate_saved_width;
        height = estimate_saved_height;
        page.dark = estimate_saved_dark;
        glfwSetWindowSize(window, width, height);
        frame();
    }
    {
        std::vector<mirage::conversation::Part> parts{
            {"thought", "thinking", "complete", "先检查当前状态，再执行工具。"},
            {"call", "tool", "running", "", "wait", "{\"duration_ms\":1000}"}};
        page.chat.current().follow_output = false;
        page.chat.current().scroll_offset = 0;
        mirage::conversation::Timings timings;
        timings.inference = {2, 1234};
        timings.capture = {3, 200};
        timings.ocr = {1, 456};
        timings.settlement = {1, 90};
        page.chat.apply_process(session_id, "first", parts, 1, false, true, timings);
        MIRAGE_CHECK(page.chat.current().messages.back().timings == timings);
        const auto before_timing = conversation_measure_key(page.chat.current(), page, 800, 500);
        auto &timed_message = page.chat.current().messages.back();
        ++timed_message.timings.capture.calls;
        MIRAGE_CHECK(conversation_measure_key(page.chat.current(), page, 800, 500) !=
                     before_timing);
        --timed_message.timings.capture.calls;
        frame();
        const auto root = "message." + std::to_string(assistant) + ".part.";
        MIRAGE_CHECK(!view->find(root + "thought.detail.body"));
        MIRAGE_CHECK(!view->find(root + "call.detail.body"));
        MIRAGE_CHECK(element(root + "call.status")->text == "执行中");
        capture("process-collapsed-light");
        page.dark = true;
        width = 760;
        height = 620;
        frame();
        capture("process-timings-minimum-dark");
        page.dark = false;
        width = 1180;
        height = 800;
        frame();
        const auto before = element("message." + std::to_string(assistant) + ".select")->frame.y;
        auto bounds = element(root + "thought.toggle.bg")->frame;
        core::queuePointerButton(handle, bounds.x + 30, bounds.y + 14, core::PointerButton::Left,
                                 core::PointerAction::Press, {});
        frame();
        core::queuePointerButton(handle, bounds.x + 30, bounds.y + 14, core::PointerButton::Left,
                                 core::PointerAction::Release, {});
        frame();
        MIRAGE_CHECK(element(root + "thought.detail.body")->text == parts.front().text);
        MIRAGE_CHECK(element("message." + std::to_string(assistant) + ".select")->frame.y > before);
        element(root + "call.toggle.bg")->onClick();
        frame();
        MIRAGE_CHECK(element(root + "call.detail.body")->text.find("参数") != std::string::npos);
        element(root + "call.copy.bg")->onClick();
        const auto *copied = glfwGetClipboardString(window);
        MIRAGE_CHECK(copied && std::string(copied).find("参数") != std::string::npos);
        parts.back().status = "complete";
        parts.back().output = "{\"waited_ms\":1000}";
        page.chat.apply_process(session_id, "first", parts, 2);
        frame();
        MIRAGE_CHECK(element(root + "call.status")->text == "已完成");
        MIRAGE_CHECK(element(root + "call.detail.body")->text.find("waited_ms") !=
                     std::string::npos);
        capture("process-expanded-light");
        page.dark = true;
        frame();
        capture("process-expanded-dark");
        width = 860;
        height = 620;
        frame();
        capture("process-expanded-small-dark");
        MIRAGE_CHECK(element(root + "thought.toggle.bg")->frame.width > 200);
        parts.back().status = "failed";
        parts.back().output = "输入不合法";
        page.chat.apply_process(session_id, "first", parts, 3);
        frame();
        MIRAGE_CHECK(element(root + "call.status")->text == "失败");
        parts.back().status = "cancelled";
        page.chat.apply_process(session_id, "first", parts, 4);
        frame();
        MIRAGE_CHECK(element(root + "call.status")->text == "已取消");
        parts.front().text = std::string(4000, 'x');
        parts.front().truncated = true;
        page.chat.apply_process(session_id, "first", parts, 5);
        frame();
        MIRAGE_CHECK(element(root + "thought.detail")->frame.height <= 240);
        MIRAGE_CHECK(element(root + "thought.detail")->scrollMaxOffset > 40);
        element(root + "thought.detail")->onScrollOffsetChanged(40);
        frame();
        MIRAGE_CHECK(std::abs(element(root + "thought.detail")->scrollOffset - 40) < 1);
        parts.back().output += "新增结果";
        page.chat.apply_process(session_id, "first", parts, 6);
        frame();
        MIRAGE_CHECK(std::abs(element(root + "thought.detail")->scrollOffset - 40) < 1);
        parts.front().text = "检查新的桌面证据。";
        parts.front().truncated = false;
        parts.back().name = "desktop.input.key";
        parts.back().status = "complete";
        parts.back().output =
            R"({"window_id":"w1","action_id":"action-1","verified":true,"verification":{"status":"verified","verified":true,"window_id":"w1","evidence_id":"observation-1"}})";
        page.chat.apply_process(session_id, "first", parts, 7);
        page.dark = false;
        width = 1180;
        height = 800;
        frame();
        MIRAGE_CHECK(element(root + "call.status")->text == "已核验");
        capture("post-action-verified-light");
        page.dark = true;
        frame();
        capture("post-action-verified-dark");
        parts.back().output =
            R"({"verification":{"status":"unverified","verified":false,"window_id":"w1","evidence_id":""}})";
        page.chat.apply_process(session_id, "first", parts, 8);
        frame();
        MIRAGE_CHECK(element(root + "call.status")->text == "未验证");
        parts.back().status = "failed";
        parts.back().output =
            R"({"verification":{"status":"uncertain","verified":false,"window_id":"w1","evidence_id":"observation-1"}})";
        page.chat.apply_process(session_id, "first", parts, 9);
        frame();
        MIRAGE_CHECK(element(root + "call.status")->text == "不确定");
        page.chat.apply_process(session_id, "first", {}, 10);
        page.dark = false;
        frame();
    }
    {
        page.chat.apply_turn(session_id, "failed-answer", "pending", "公开失败测试", {}, {});
        page.chat.apply_preview(session_id, "failed-answer", "request", 1, "已经收到的正文", false);
        page.chat.apply_turn(session_id, "failed-answer", "failed", "公开失败测试", {},
                             "当前平台桌宠避让不可用");
        std::vector<mirage::conversation::Part> failed_parts;
        mirage::conversation::retain_terminal_reply(
            failed_parts, "**未执行操作**。当前桌面观察不可用，无法核对目标。");
        page.chat.apply_process(session_id, "failed-answer", failed_parts, 1, false);
        const auto row = "message." + std::to_string(page.chat.current().messages.back().id);
        page.dark = false;
        frame();
        MIRAGE_CHECK(element(row + ".markdown") && element(row + ".failure"));
        MIRAGE_CHECK(element(row + ".failure")->text.find("当前平台桌宠避让不可用") !=
                     std::string::npos);
        MIRAGE_CHECK(element(row + ".failure")->frame.y >=
                     element(row + ".markdown")->frame.y +
                         element(row + ".markdown")->frame.height);
        capture("failed-answer-retained-light");
        page.dark = true;
        width = 640;
        height = 560;
        frame();
        capture("failed-answer-retained-small-dark");
        page.chat.apply_turn(session_id, "failed-answer", "failed", "公开失败测试", {},
                             "更新后的失败原因，包含更长的诊断说明以验证高度缓存刷新");
        frame();
        MIRAGE_CHECK(element(row + ".failure")->text.find("更新后的失败原因") != std::string::npos);
        page.chat.reconcile_turns(session_id, {"first"}, 100);
        page.dark = false;
        width = 1180;
        height = 800;
        frame();
    }
    const auto key = "message." + std::to_string(user);
    const auto original_user_text = page.chat.current().messages.front().text;
    std::string long_cjk;
    for (int i = 0; i < 80; ++i)
        long_cjk += "中";
    const std::vector<std::string> bubble_cases = {"你会做什么",
                                                   "你会做什么？",
                                                   "你好 / Mirage 12345",
                                                   std::string(160, 'W'),
                                                   long_cjk,
                                                   "第一行\n第二行\n",
                                                   "emoji 👩‍💻 / 中文\n\n第三行"};
    for (const bool dark : {false, true})
        for (const int logical_width : {1180, 860})
            for (const float scale : {1.0f, 1.25f, 1.5f, 2.0f}) {
                page.dark = dark;
                width = logical_width;
                height = logical_width == 1180 ? 800 : 620;
                dpi_scale = scale;
                for (const auto &value : bubble_cases) {
                    page.chat.current().messages.front().text = value;
                    frame();
                    const auto *body = element(key + ".body");
                    const auto *bubble = element(key + ".bubble");
                    core::TextStyle emitted;
                    emitted.text = body->text;
                    emitted.fontFamily = body->fontFamily;
                    emitted.fontSize = body->fontSize * scale;
                    emitted.fontWeight = body->fontWeight;
                    emitted.maxWidth = body->maxWidth * scale;
                    emitted.lineHeight = body->lineHeight * scale;
                    emitted.wrap = body->wrap;
                    core::TextPrimitive actual;
                    actual.setStyle(emitted);
                    const auto size = actual.measuredSize();
                    MIRAGE_CHECK(std::abs(page.render_scale - scale) < 0.001f);
                    MIRAGE_CHECK(size.y <= body->frame.height * scale + 0.01f);
                    MIRAGE_CHECK(size.x <= body->frame.width * scale + 0.01f);
                    MIRAGE_CHECK(std::abs(body->frame.x - bubble->frame.x - 12) < 0.01f);
                    MIRAGE_CHECK(std::abs(body->frame.y - bubble->frame.y - 8) < 0.01f);
                    MIRAGE_CHECK(body->frame.x + body->frame.width <=
                                 bubble->frame.x + bubble->frame.width - 12 + 0.01f);
                    MIRAGE_CHECK(body->frame.y + size.y / scale <=
                                 bubble->frame.y + bubble->frame.height - 8 + 0.01f);
                    MIRAGE_CHECK(element(key + ".select")->frame.height == body->frame.height);
                    if (value == "你会做什么") {
                        MIRAGE_CHECK(std::abs(size.y / scale - 22) < 0.01f);
                        if (logical_width == 1180 && scale == 2)
                            capture(dark ? "bubble-short-dark-2x" : "bubble-short-light-2x");
                    }
                }
            }
    page.chat.current().messages.front().text = original_user_text;
    page.dark = false;
    width = 1180;
    height = 800;
    dpi_scale = 1;
    frame();
    MIRAGE_CHECK(element("thread.title")->text == page.chat.current().title);
    capture("title-history");
    const auto session_key = "session." + std::to_string(session_id);
    auto *delete_visibility = element(session_key + ".delete.visibility");
    MIRAGE_CHECK(delete_visibility && delete_visibility->opacity == 0);
    const auto title_bounds = element(session_key + ".title")->frame;
    core::queuePointerMotion(handle, title_bounds.x + 8, title_bounds.y + 10, {}, {});
    frame();
    MIRAGE_CHECK(element(session_key + ".delete.visibility")->opacity == 1);
    const auto delete_bounds = element(session_key + ".delete.bg")->frame;
    core::queuePointerMotion(handle, delete_bounds.x + 18, delete_bounds.y + 18, {}, {});
    frame();
    MIRAGE_CHECK(element(session_key + ".delete.visibility")->opacity == 1);
    core::queuePointerMotion(handle, 700, 450, {}, {});
    frame();
    MIRAGE_CHECK(element(session_key + ".delete.visibility")->opacity == 0);

    const auto previous_thread_cache = "thread." + std::to_string(session_id) + ".sentinel";
    view->state<int>(previous_thread_cache) = 42;
    const auto history_count = page.chat.history_count();
    MIRAGE_CHECK(page.chat.new_draft());
    frame();
    MIRAGE_CHECK(element("thread.title")->text == "新对话" &&
                 page.chat.history_count() == history_count);
    MIRAGE_CHECK(view->state<int>(previous_thread_cache) == 0);
    MIRAGE_CHECK(page.thread_cache_session == page.chat.current().id);
    capture("title-new-draft");
    const auto second_session = page.chat.current().id;
    page.chat.apply_turn(second_session, "second", "ok", "另一段合成会话", "合成回复", {}, {}, 1);
    frame();
    MIRAGE_CHECK(element("thread.title")->text == "另一段合成会话");
    MIRAGE_CHECK(page.chat.select_session(session_id));
    frame();
    MIRAGE_CHECK(element("thread.title")->text == page.chat.current().title);
    capture("title-switched-history");
    auto *copy = element(key + ".copy");
    MIRAGE_CHECK(copy && copy->opacity == 0);
    MIRAGE_CHECK(!view->find(key + ".quote"));
    capture("normal-light");
    auto *body = element(key + ".select");
    const auto bounds = body->frame;
    core::queuePointerMotion(handle, bounds.x + 8, bounds.y + 10, {}, {});
    frame();
    copy = element(key + ".copy");
    MIRAGE_CHECK(copy && copy->opacity == 1);
    capture("hover-actions");
    core::queuePointerMotion(handle, 700, 450, {}, {});
    frame();
    MIRAGE_CHECK(element(key + ".copy")->opacity == 0);
    core::queuePointerButton(handle, bounds.x + 16, bounds.y + 10, core::PointerButton::Left,
                             core::PointerAction::Press, {});
    frame();
    core::queuePointerMotion(handle, bounds.x + 112, bounds.y + 10, core::PointerButton::Left, {});
    frame();
    core::queuePointerButton(handle, bounds.x + 112, bounds.y + 10, core::PointerButton::Left,
                             core::PointerAction::Release, {});
    frame();
    MIRAGE_CHECK(page.selection.ready && !page.selection.excerpt.empty() &&
                 page.selection.excerpt != page.chat.current().messages.front().text);
    MIRAGE_CHECK(view->find("selection.quote"));
    const auto quote_bounds = element("selection.quote")->frame;
    MIRAGE_CHECK(quote_bounds.y >= bounds.y + bounds.height ||
                 quote_bounds.y + quote_bounds.height <= bounds.y);
    capture("selected-excerpt");
    const auto excerpt = page.selection.excerpt;
    element("selection.quote.bg")->onClick();
    frame();
    MIRAGE_CHECK(page.chat.current().references.size() == 1 &&
                 page.chat.current().references.front().text == excerpt);
    // Select from rendered Markdown, including a phrase crossing emphasis runs.
    auto *answer = element("message." + std::to_string(assistant) + ".select");
    const auto answer_bounds = answer->frame;
    core::queuePointerButton(handle, answer_bounds.x + 2, answer_bounds.y + 48,
                             core::PointerButton::Left, core::PointerAction::Press, {});
    frame();
    core::queuePointerMotion(handle, answer_bounds.x + 360, answer_bounds.y + 48,
                             core::PointerButton::Left, {});
    frame();
    core::queuePointerButton(handle, answer_bounds.x + 360, answer_bounds.y + 48,
                             core::PointerButton::Left, core::PointerAction::Release, {});
    frame();
    MIRAGE_CHECK(page.selection.ready && page.selection.excerpt.find("**") == std::string::npos &&
                 page.selection.excerpt.find("原生界面") != std::string::npos);
    capture("markdown-selection");
    element("background")->onClick();
    frame();
    MIRAGE_CHECK(!page.selection.ready && !view->find("selection.quote"));
    page.chat.set_draft("未发送草稿");
    element(key + ".edit.bg")->onClick();
    frame();
    MIRAGE_CHECK(view->find("composer.edit.cancel") && page.chat.current().edit_turn_id == "first");
    capture("editing-last-input");
    element("composer.edit.cancel.bg")->onClick();
    frame();
    MIRAGE_CHECK(page.chat.current().draft == "未发送草稿" &&
                 page.chat.current().messages.size() == 2);
    page.chat.current().references.clear();
    page.chat.set_draft("");
    core::queuePointerMotion(handle, 700, 450, {}, {});
    page.dark = true;
    frame();
    capture("normal-dark");
    width = 860;
    height = 620;
    glfwSetWindowSize(window, width, height);
    frame();
    capture("minimum-dark");
    page.dark = false;
    frame();
    capture("minimum-light");
    MIRAGE_CHECK(element("composer.panel")->frame.y >= 72);
    // Render a mixed-script stress sample through the actual product widgets.
    page.chat.current().messages.front().text =
        "字体检查：中文 ABC xyz 012345，。！？（括号） / Mirage v1.2";
    page.chat.current().messages.back().text = R"markdown(## 中文与 Latin 字体

天地人口田上下大小，中英文 ABC xyz 012345 应保持自然基线。

普通文字 **强调中文 Bold 123** 与标点：，。！？（括号）。

行内代码 `agent_loop()` 与 [链接文字 Link](https://example.com)。

- 列表 Chinese / English 2026
- 简繁字符：字体渲染 / 字體渲染

```cpp
const auto model = "Mirage";
```)markdown";
    page.chat.set_draft("输入框：中文 ABC xyz 012345，。！？ / Mira Agent");
    frame();
    auto *input = element("composer.input." + std::to_string(session_id));
    MIRAGE_CHECK(input->frame.height - 24 >= ui_input_line_height(14));
    capture("mixed-minimum-light");
    page.dark = true;
    frame();
    capture("mixed-minimum-dark");
    width = 1180;
    height = 800;
    glfwSetWindowSize(window, width, height);
    frame();
    capture("mixed-normal-dark");
    page.dark = false;
    frame();
    capture("mixed-normal-light");
    // Real preview snapshots change the displayed body; waiting has a single status line.
    page.chat.apply_turn(session_id, "stream", "pending", "给我一个流式回复", {}, {}, {}, 2);
    auto &live_answer = page.chat.current().messages.back();
    live_answer.started = std::chrono::steady_clock::now() - std::chrono::milliseconds{2300};
    const auto live_key = "message." + std::to_string(live_answer.id);
    frame();
    MIRAGE_CHECK(element(live_key + ".state")->text.starts_with("思考中 · "));
    MIRAGE_CHECK(view->find(live_key + ".copy") == nullptr);
    capture("waiting-light");
    page.dark = true;
    frame();
    capture("waiting-dark");
    page.chat.apply_preview(session_id, "stream", "req", 1, "## 流式回复\n\n这是实际收到的第一段。",
                            false);
    frame();
    MIRAGE_CHECK(element(live_key + ".state")->text.starts_with("正在回复 · "));
    MIRAGE_CHECK(view->find(live_key + ".markdown") != nullptr);
    capture("streaming-dark");
    page.dark = false;
    frame();
    capture("streaming-light");
    page.chat.apply_turn(session_id, "stream", "ok", "给我一个流式回复", "完整规范回复。", {}, {},
                         2);
    frame();
    MIRAGE_CHECK(view->find(live_key + ".duration") != nullptr);
    MIRAGE_CHECK(view->find(live_key + ".state") == nullptr);
    // Returning to the bottom after reading older content must resume stream following.
    page.chat.apply_turn(session_id, "long-stream", "pending", "长回复滚动验证", {}, {}, {}, 3);
    std::string long_preview;
    for (int line = 0; line < 40; ++line)
        long_preview += "流式内容第" + std::to_string(line) + "行。\n\n";
    page.chat.apply_preview(session_id, "long-stream", "long-request", 1, long_preview, false);
    frame();
    const auto thread_key = "thread." + std::to_string(session_id);
    element(thread_key)->onScrollOffsetChanged(40);
    frame();
    const float reading_offset = element(thread_key)->scrollOffset;
    MIRAGE_CHECK(reading_offset < element(thread_key)->scrollMaxOffset - 10);
    MIRAGE_CHECK(view->find("thread.latest") && !page.chat.current().follow_output);
    page.chat.apply_preview(session_id, "long-stream", "long-request", 2,
                            long_preview + "追加内容。\n\n", false);
    frame();
    MIRAGE_CHECK(std::abs(element(thread_key)->scrollOffset - reading_offset) < 1);
    element(thread_key)->onScrollOffsetChanged(element(thread_key)->scrollMaxOffset);
    frame();
    MIRAGE_CHECK(
        std::abs(element(thread_key)->scrollOffset - element(thread_key)->scrollMaxOffset) < 1);
    MIRAGE_CHECK(!view->find("thread.latest") && page.chat.current().follow_output);
    page.chat.apply_preview(session_id, "long-stream", "long-request", 3,
                            long_preview + "追加内容。\n\n新到达的末行。\n\n", false);
    frame();
    MIRAGE_CHECK(
        std::abs(element(thread_key)->scrollOffset - element(thread_key)->scrollMaxOffset) < 1);
    element(thread_key)->onScrollOffsetChanged(40);
    frame();
    page.chat.apply_turn(session_id, "long-stream", "ok", "长回复滚动验证", long_preview, {}, {},
                         3);
    frame();
    MIRAGE_CHECK(std::abs(element(thread_key)->scrollOffset - 40) < 1);
    auto visible_text_pixels = [&] {
        runtime.requestFullPaint();
        runtime.render(width, height, 1, palette(page.dark).background);
        glFinish();
        // Exclude the arrow and composer: observe movement of the reading
        // viewport, rather than only checking the requested DSL offset.
        const auto viewport_frame = element(thread_key)->frame;
        const int sample_x = static_cast<int>(viewport_frame.x) + 12;
        const int sample_y = static_cast<int>(viewport_frame.y) + 40;
        const int sample_width = std::min(700, static_cast<int>(viewport_frame.width) - 24);
        const int sample_height = std::min(330, static_cast<int>(viewport_frame.height) - 112);
        std::vector<unsigned char> pixels(
            static_cast<std::size_t>(sample_width * sample_height * 3));
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        glReadPixels(sample_x, height - sample_y - sample_height, sample_width, sample_height,
                     GL_RGB, GL_UNSIGNED_BYTE, pixels.data());
        return pixels;
    };
    const auto reading_pixels = visible_text_pixels();
    capture("stream-reading-light");
    auto *latest_hit = element("thread.latest.bg");
    MIRAGE_CHECK(latest_hit->focusable);
    const auto latest_bounds = latest_hit->frame;
    core::queuePointerMotion(handle, latest_bounds.x + 18, latest_bounds.y + 18, {}, {});
    core::queuePointerButton(handle, latest_bounds.x + 18, latest_bounds.y + 18,
                             core::PointerButton::Left, core::PointerAction::Press, {});
    frame();
    core::queuePointerButton(handle, latest_bounds.x + 18, latest_bounds.y + 18,
                             core::PointerButton::Left, core::PointerAction::Release, {});
    frame();
    MIRAGE_CHECK(page.chat.current().follow_output && !view->find("thread.latest"));
    MIRAGE_CHECK(
        std::abs(element(thread_key)->scrollOffset - element(thread_key)->scrollMaxOffset) < 1);
    MIRAGE_CHECK(visible_text_pixels() != reading_pixels);
    element(thread_key)->onScrollOffsetChanged(40);
    frame();
    latest_hit = element("thread.latest.bg");
    eui::KeyEvent latest_enter;
    latest_enter.key = eui::InputKey::Enter;
    latest_enter.action = eui::KeyAction::Press;
    MIRAGE_CHECK(latest_hit->onKeyEvent(latest_enter));
    frame();
    MIRAGE_CHECK(page.chat.current().follow_output && !view->find("thread.latest"));
    MIRAGE_CHECK(
        std::abs(element(thread_key)->scrollOffset - element(thread_key)->scrollMaxOffset) < 1);
    MIRAGE_CHECK(visible_text_pixels() != reading_pixels);
    capture("stream-following-light");
    // Real pointer dispatch must reach the overlay across supported viewports/themes.
    for (const int viewport : {1180, 860}) {
        width = viewport;
        height = viewport == 1180 ? 800 : 620;
        glfwSetWindowSize(window, width, height);
        for (const bool dark : {false, true}) {
            page.dark = dark;
            element(thread_key)->onScrollOffsetChanged(40);
            frame();
            MIRAGE_CHECK(!page.chat.current().follow_output && view->find("thread.latest"));
            const auto before_click = visible_text_pixels();
            const auto arrow_bounds = element("thread.latest.bg")->frame;
            core::queuePointerMotion(handle, arrow_bounds.x + 18, arrow_bounds.y + 18, {}, {});
            core::queuePointerButton(handle, arrow_bounds.x + 18, arrow_bounds.y + 18,
                                     core::PointerButton::Left, core::PointerAction::Press, {});
            frame();
            core::queuePointerButton(handle, arrow_bounds.x + 18, arrow_bounds.y + 18,
                                     core::PointerButton::Left, core::PointerAction::Release, {});
            frame();
            MIRAGE_CHECK(page.chat.current().follow_output && !view->find("thread.latest"));
            MIRAGE_CHECK(std::abs(element(thread_key)->scrollOffset -
                                  element(thread_key)->scrollMaxOffset) < 1);
            MIRAGE_CHECK(visible_text_pixels() != before_click);
        }
    }
    page.dark = false;
    width = 1180;
    height = 800;
    glfwSetWindowSize(window, width, height);
    frame();
    page.chat.current().messages.back().text = "滚动验证完成。";
    frame();
    // Compact model labels must remain readable and leave the adjacent actions clear.
    page.live_model.enabled = true;
    for (const auto &model : {std::string("GLM-4.6"),
                              std::string("synthetic/provider/very-long-model-name-20261005")}) {
        page.live_model.model_selector = model;
        const auto name = model == "GLM-4.6" ? "short" : "long";
        for (const int viewport : {1180, 860}) {
            width = viewport;
            height = viewport == 1180 ? 800 : 620;
            glfwSetWindowSize(window, width, height);
            for (const bool dark : {false, true}) {
                page.dark = dark;
                frame();
                const auto button = element("composer.model")->frame;
                MIRAGE_CHECK(button.x + button.width < element("composer.reasoning")->frame.x);
                capture(std::string("model-") + name + "-" + std::to_string(viewport) +
                        (dark ? "-dark" : "-light"));
            }
        }
    }
    element("composer.model.bg")->onClick();
    frame();
    MIRAGE_CHECK(page.popup == PageState::Popup::Model && view->find("composer.popup.settings"));
    page.popup = PageState::Popup::None;
    page.live_model.enabled = false;
    page.live_model.reasoning_options = {"",       "none", "adaptive", "minimal", "low",
                                         "medium", "high", "xhigh",    "max"};
    for (const auto &effort : page.live_model.reasoning_options) {
        page.chat.current().reasoning = effort;
        frame();
        const auto *label = element("composer.reasoning.text");
        MIRAGE_CHECK(label && !label->text.ends_with("…"));
        MIRAGE_CHECK(element("composer.model")->frame.x + element("composer.model")->frame.width +
                         control_icon_gap <=
                     element("composer.reasoning")->frame.x + 0.5f);
    }
    page.chat.current().reasoning.clear();
    // Density changes must preserve menu containment and mouse operation in both themes.
    for (const int viewport : {1180, 860}) {
        width = viewport;
        height = viewport == 1180 ? 800 : 620;
        glfwSetWindowSize(window, width, height);
        for (const bool dark : {false, true}) {
            page.dark = dark;
            page.live_model.supports_reasoning = true;
            page.live_model.dialect = "openai.chat_completions.v1";
            page.live_model.reasoning_options = {"", "none", "adaptive"};
            for (const auto popup :
                 {PageState::Popup::Actions, PageState::Popup::Mode, PageState::Popup::Reasoning}) {
                page.popup = popup;
                frame();
                const auto menu_bounds = element("composer.popup")->frame;
                MIRAGE_CHECK(menu_bounds.x >= 0 && menu_bounds.y >= 0 &&
                             menu_bounds.x + menu_bounds.width <= width &&
                             menu_bounds.y + menu_bounds.height <= height);
                if (popup == PageState::Popup::Reasoning) {
                    const auto last = element("composer.popup.effort.2")->frame;
                    MIRAGE_CHECK(last.y + last.height <= menu_bounds.y + menu_bounds.height);
                }
                capture("compact-menu-" + std::to_string(static_cast<int>(popup)) + "-" +
                        std::to_string(viewport) + (dark ? "-dark" : "-light"));
            }
            page.popup = PageState::Popup::None;
            page.settings = true;
            page.model_page = false;
            frame();
            const auto theme_bounds = element("settings.theme.dark.bg")->frame;
            const auto panel = element("settings.theme.panel")->frame;
            MIRAGE_CHECK(theme_bounds.x >= panel.x &&
                         theme_bounds.x + theme_bounds.width <= panel.x + panel.width &&
                         theme_bounds.y + theme_bounds.height <= panel.y + panel.height);
            core::queuePointerMotion(handle, theme_bounds.x + theme_bounds.width / 2,
                                     theme_bounds.y + theme_bounds.height / 2, {}, {});
            core::queuePointerButton(handle, theme_bounds.x + theme_bounds.width / 2,
                                     theme_bounds.y + theme_bounds.height / 2,
                                     core::PointerButton::Left, core::PointerAction::Press, {});
            frame();
            core::queuePointerButton(handle, theme_bounds.x + theme_bounds.width / 2,
                                     theme_bounds.y + theme_bounds.height / 2,
                                     core::PointerButton::Left, core::PointerAction::Release, {});
            frame();
            MIRAGE_CHECK(page.dark);
            page.dark = dark;
            frame();
            capture("compact-appearance-" + std::to_string(viewport) + (dark ? "-dark" : "-light"));
            page.settings = false;
        }
    }
    page.live_model.supports_reasoning = false;
    page.live_model.reasoning_options.clear();
    page.dark = false;
    width = 1180;
    height = 800;
    glfwSetWindowSize(window, width, height);
    page.chat.set_draft("");
    const auto saved_title = page.chat.current().title;
    const std::string long_title =
        "这是一段用于验证窗口标题省略显示的合成会话标题 / Mirage agent conversation 20261005 / "
        "超长标题省略测试超长标题省略测试超长标题省略测试超长标题省略测试";
    page.chat.current().title = long_title;
    width = 860;
    height = 620;
    glfwSetWindowSize(window, width, height);
    for (const bool sidebar : {true, false}) {
        page.sidebar = sidebar;
        for (const bool dark : {false, true}) {
            page.dark = dark;
            frame();
            const auto *title = element("thread.title");
            MIRAGE_CHECK(title->text.ends_with("…") && page.chat.current().title == long_title);
            MIRAGE_CHECK(title->frame.x + title->frame.width < element("window.minimize")->frame.x);
            capture(std::string("title-long-") + (sidebar ? "sidebar" : "collapsed") +
                    (dark ? "-dark" : "-light"));
        }
    }
    // Sidebar toggle redesign: a Phosphor sidebar-simple panel composite with an
    // overlay caret (U+E138 caret-left when expanded, U+E13A caret-right when
    // collapsed), mirroring ZCode's PanelLeftClose/PanelLeftOpen semantics.
    {
        const std::string panel_glyph = "\xee\xb0\xa4"; // U+EC24 sidebar-simple
        const std::string caret_left = "\xee\x84\xb8";  // U+E138
        const std::string caret_right = "\xee\x84\xba"; // U+E13A
        const float sidebar_extent =
            std::clamp(page.sidebar_width, 224.0f,
                       std::max(224.0f, std::min(400.0f, static_cast<float>(width) - 520.0f)));
        page.sidebar = true;
        page.dark = false;
        frame();
        MIRAGE_CHECK(!view->find("sidebar.toggle.collapsed"));
        MIRAGE_CHECK(element("sidebar.toggle.panel.icon")->text == panel_glyph);
        MIRAGE_CHECK(element("sidebar.toggle.caret")->text == caret_left);
        MIRAGE_CHECK(element("sidebar.toggle.panel")->frame.width == 36);
        const auto expanded = element("sidebar.toggle")->frame;
        MIRAGE_CHECK(expanded.width == 36 && expanded.height == 36);
        MIRAGE_CHECK(expanded.x + expanded.width <= sidebar_extent + 0.01f);
        MIRAGE_CHECK(std::abs(expanded.y - 12) < 0.01f);
        capture("sidebar-toggle-expanded");
        // Clicking the expanded toggle collapses the sidebar.
        element("sidebar.toggle.panel.bg")->onClick();
        frame();
        MIRAGE_CHECK(!page.sidebar && view->find("sidebar.toggle.collapsed") != nullptr);
        // Collapsed state: the composite moves to the window's top-left corner.
        MIRAGE_CHECK(!view->find("sidebar.toggle"));
        MIRAGE_CHECK(element("sidebar.toggle.collapsed.panel.icon")->text == panel_glyph);
        MIRAGE_CHECK(element("sidebar.toggle.collapsed.caret")->text == caret_right);
        const auto collapsed = element("sidebar.toggle.collapsed")->frame;
        MIRAGE_CHECK(collapsed.width == 36 && collapsed.height == 36);
        MIRAGE_CHECK(std::abs(collapsed.x - 16) < 0.01f && std::abs(collapsed.y - 12) < 0.01f);
        capture("sidebar-toggle-collapsed");
        // Ctrl+B toggles the sidebar like ZCode's workspace binding.
        eui::KeyEvent ctrl_b;
        ctrl_b.key = eui::InputKey::B;
        ctrl_b.action = eui::KeyAction::Press;
        ctrl_b.modifiers.control = true;
        app::dslAppConfig().keyEventHandler(ctrl_b);
        frame();
        MIRAGE_CHECK(page.sidebar && !view->find("sidebar.toggle.collapsed"));
        app::dslAppConfig().keyEventHandler(ctrl_b);
        frame();
        MIRAGE_CHECK(!page.sidebar && view->find("sidebar.toggle.collapsed") != nullptr);
        // Clicking the collapsed toggle expands the sidebar again.
        element("sidebar.toggle.collapsed.panel.bg")->onClick();
        frame();
        MIRAGE_CHECK(page.sidebar && !view->find("sidebar.toggle.collapsed") &&
                     view->find("sidebar.toggle.caret"));
        // Restore the collapsed state the surrounding sections expect.
        page.sidebar = false;
        frame();
        MIRAGE_CHECK(view->find("sidebar.toggle.collapsed.caret"));
    }
    page.settings = true;
    frame();
    MIRAGE_CHECK(element("thread.title")->text == "设置");
    MIRAGE_CHECK(element("thread.title")->frame.x + element("thread.title")->frame.width <
                 element("settings.back.collapsed")->frame.x);
    capture("title-settings-collapsed");
    page.settings = false;
    page.sidebar = true;
    page.dark = false;
    page.chat.current().title = saved_title;
    width = 1180;
    height = 800;
    glfwSetWindowSize(window, width, height);
    // A scrolled content transform must not shift selection into an earlier line.
    auto &long_user = page.chat.current().messages.front();
    long_user.text.clear();
    for (int line = 0; line < 32; ++line)
        long_user.text += "第" + std::to_string(line) + "行选择验证abc\n";
    page.chat.current().scroll_offset = 0;
    page.chat.current().follow_output = false;
    frame();
    const auto long_bounds = element(key + ".select")->frame;
    const float scroll_y = 120;
    core::queuePointerMotion(handle, long_bounds.x + 1, scroll_y, {}, {});
    core::queueScrollInput(handle, 0, -5);
    frame();
    // Finish framework scroll inertia before starting a new selection gesture.
    for (int settle = 0; settle < 80; ++settle)
        frame();
    MIRAGE_CHECK(page.chat.current().scroll_offset > 0);
    const auto selected_line =
        static_cast<int>((scroll_y + page.chat.current().scroll_offset - long_bounds.y) / 22);
    const auto expected_prefix = "第" + std::to_string(selected_line) + "行";
    core::queuePointerButton(handle, long_bounds.x + 1, scroll_y, core::PointerButton::Left,
                             core::PointerAction::Press, {});
    frame();
    core::queuePointerMotion(handle, long_bounds.x + 100, scroll_y, core::PointerButton::Left, {});
    frame();
    core::queuePointerButton(handle, long_bounds.x + 100, scroll_y, core::PointerButton::Left,
                             core::PointerAction::Release, {});
    frame();
    if (!page.selection.excerpt.starts_with(expected_prefix))
        std::cerr << "Scrolled synthetic selection: " << page.selection.excerpt
                  << "; offset=" << page.chat.current().scroll_offset
                  << "; frame.y=" << long_bounds.y << "; origin.y=" << page.selection.origin_y
                  << "; ready=" << page.selection.ready << '\n';
    MIRAGE_CHECK(page.selection.ready && page.selection.excerpt.starts_with(expected_prefix));
    // M6-21: empty service drafts never masquerade as saved models. Exercise
    // the same rendered controls and ACK projection used by the product.
    page.settings = page.model_page = true;
    page.model_dirty = false;
    persistence::LocalSettings empty_catalog;
    empty_catalog.models_present = true;
    // BUG-20261006-003: initial connection failure must not block local previews.
    page.model_loaded = false;
    frame();
    // DEC-047: preset selection moved behind the 添加服务 dropdown, whose button
    // is gated while disconnected. The offline preview itself survives at the
    // state layer through select_preset — the exact handler each menu row
    // invokes — so the BUG-20261006-003 projection stays exercisable.
    MIRAGE_CHECK(element("model.add.provider.bg")->disabled);
    select_preset(0);
    MIRAGE_CHECK(page.preview_preset == 0 && !page.model_dirty &&
                 page.model.provider_id == "preset:openai");
    frame();
    MIRAGE_CHECK(element("model.save.bg")->disabled && element("model.add.model.bg")->disabled);
    eui::TextInputEvent preview_key;
    preview_key.pasteText = "fixture-preview-key";
    element("model.key.hit")->onTextInput(preview_key);
    MIRAGE_CHECK(page.api_key == "fixture-preview-key" && page.model_dirty && !page.model_loaded);
    discard_model_edits();
    MIRAGE_CHECK(page.api_key.empty() && !page.model_dirty && !page.model_loaded);
    // Dropdown rows are not wired into keyboard_activation yet, so the second
    // preview drives select_preset directly instead of a row key event.
    select_preset(2);
    MIRAGE_CHECK(page.preview_preset == 2 && !page.model_dirty && page.models.empty());
    frame();
    element("model.key.hit")->onTextInput(preview_key);
    accept_model_configuration(empty_catalog, "model", "");
    MIRAGE_CHECK(page.model_loaded && !page.preview_preset && page.model_dirty &&
                 page.model.provider_id == "preset:minimax" &&
                 page.api_key == "fixture-preview-key");
    discard_model_edits();
    page.model_loaded = false;
    select_preset(2);
    frame();
    element("model.key.hit")->onTextInput(preview_key);
    auto saved_preview = page.model;
    saved_preview.display_name = "saved-preview-model";
    saved_preview.endpoint_origin = "https://example.com";
    saved_preview.api_prefix = "/custom";
    saved_preview.model_selector = "saved-preview";
    saved_preview.credential_ref = "fixture-ref";
    persistence::LocalSettings saved_preview_catalog;
    saved_preview_catalog.model = saved_preview;
    saved_preview_catalog.models_present = true;
    saved_preview_catalog.models = {saved_preview};
    accept_model_configuration(saved_preview_catalog, "model", "");
    MIRAGE_CHECK(!page.preview_preset && page.model_dirty &&
                 page.model_base_url == "https://example.com/custom" &&
                 page.model.model_selector == "saved-preview" &&
                 page.model.credential_ref == "fixture-ref" && page.models.size() == 1 &&
                 page.api_key == "fixture-preview-key");
    frame();
    page.saving_model = true;
    element("model.key.hit")->onTextInput(preview_key);
    MIRAGE_CHECK(page.api_key == "fixture-preview-key");
    page.saving_model = false;
    page.model_dirty = false;
    accept_model_configuration(empty_catalog, "model", "");
    frame();
    MIRAGE_CHECK(page.model_loaded && page.models.empty() && !page.live_model.enabled);
    MIRAGE_CHECK(element("model.provider.0.label")->text == "未命名服务");
    MIRAGE_CHECK(element("model.models.empty.label")->text == "暂无模型，请添加模型");
    const auto brand_bounds = element("brand.mira")->frame;
    const auto nav_bounds = element("settings.nav.1.icon")->frame;
    MIRAGE_CHECK(std::abs(brand_bounds.x + brand_bounds.width / 2 - nav_bounds.x -
                          nav_bounds.width / 2) < 1);
    MIRAGE_CHECK(element("settings.nav.1.label")->frame.x == element("brand")->frame.x);
    MIRAGE_CHECK(element("model.name")->frame.x > element("model.provider.logo")->frame.x + 20);
    capture("provider-empty-light");
    // DEC-047: presets live behind the 添加服务 dropdown. Nothing is saved yet,
    // so every preset is offered (the height budget clips the last row) plus a
    // manual configuration entry anchored under the button.
    element("model.add.provider.bg")->onClick();
    frame();
    MIRAGE_CHECK(page.add_provider_menu && !page.provider_actions && !page.model_format_open);
    const auto add_button = element("model.add.provider")->frame;
    const auto add_menu = element("model.add.menu")->frame;
    const auto form_bounds = element("model.form")->frame;
    // Height budgets every registered preset even though the row guard clips
    // the last one at this viewport.
    MIRAGE_CHECK(add_menu.width == 256 &&
                 add_menu.height == 4 + provider_presets.size() * 36 + 1 + 36 + 4);
    MIRAGE_CHECK(std::abs(add_menu.y - (add_button.y + add_button.height + 8)) < 0.5f);
    MIRAGE_CHECK(std::abs(add_menu.x + add_menu.width - (form_bounds.x + form_bounds.width)) <
                 0.5f);
    MIRAGE_CHECK(add_menu.x >= 0 && add_menu.y >= 0 && add_menu.x + add_menu.width <= width &&
                 add_menu.y + add_menu.height <= height);
    MIRAGE_CHECK(!view->find("model.add.menu.preset.10.bg")); // clipped by the height budget
    MIRAGE_CHECK(view->find("model.add.menu.preset.0.bg") &&
                 view->find("model.add.menu.preset.2.bg"));
    for (std::size_t i = 0; i < 10; ++i)
        MIRAGE_CHECK(element("model.add.menu.preset." + std::to_string(i) + ".label")->text ==
                     fitted_title(std::string(provider_presets[i].name), 200, 14));
    MIRAGE_CHECK(element("model.add.menu.manual.label")->text == "手动配置");
    capture("provider-add-menu-light");
    // 手动配置 falls through to a blank unnamed draft instead of copying a preset.
    element("model.add.menu.manual.bg")->onClick();
    frame();
    MIRAGE_CHECK(!page.add_provider_menu && !page.model.provider_id.starts_with("preset:") &&
                 page.model_dirty && page.editing_provider_name && page.models.empty());
    discard_model_edits();
    frame();
    MIRAGE_CHECK(!page.model_dirty && page.models.empty());
    element("model.add.provider.bg")->onClick();
    frame();
    MIRAGE_CHECK(page.add_provider_menu);
    // MiniMax: complete connection/model, key is the only missing value.
    element("model.add.menu.preset.2.bg")->onClick();
    frame();
    MIRAGE_CHECK(!page.add_provider_menu);
    MIRAGE_CHECK(page.models.empty() && !page.live_model.enabled && page.model_dirty);
    MIRAGE_CHECK(page.model.provider_name == "MiniMax" &&
                 page.model.model_selector == "MiniMax-M3");
    MIRAGE_CHECK(page.model_base_url == "https://api.minimaxi.com/anthropic/v1");
    MIRAGE_CHECK(page.model.dialect == "anthropic.messages.v1" && page.provider_models.size() == 1);
    MIRAGE_CHECK(page.api_key.empty() && page.model.credential_ref.empty() &&
                 page.model.credential_env.empty());
    auto preset_document = provider_document(true);
    MIRAGE_CHECK(preset_document && preset_document->model->enabled &&
                 preset_document->models[0].provider_id == "preset:minimax");
    capture("provider-preset-minimax-light");
    // Drive the actual masked input handler on a clean, acknowledged service.
    // Multiple text events can arrive before the next compose (fast typing).
    accept_model_configuration(*preset_document, "save", "");
    frame();
    MIRAGE_CHECK(element("model.save.bg")->disabled && !page.model_dirty);
    auto input_key = [&](const std::string &text, bool paste = false) {
        eui::TextInputEvent event;
        if (paste)
            event.pasteText = text;
        else
            event.text = text;
        element("model.key.hit")->onTextInput(event);
    };
    eui::TextInputEvent combined;
    combined.text = "s";
    combined.pasteText = "k-test";
    element("model.key.hit")->onTextInput(combined);
    MIRAGE_CHECK(page.api_key == "sk-test" && page.model_dirty);
    frame();
    MIRAGE_CHECK(!element("model.save.bg")->disabled);
    accept_model_configuration(*preset_document, "save", "");
    frame();
    // A saved preset leaves the dropdown; the shared scrim closes the menu and
    // only that menu.
    element("model.add.provider.bg")->onClick();
    frame();
    MIRAGE_CHECK(page.add_provider_menu && !view->find("model.add.menu.preset.2.bg") &&
                 view->find("model.add.menu.preset.0.bg"));
    element("model.menu.dismiss")->onClick();
    MIRAGE_CHECK(!page.add_provider_menu && !page.provider_actions && !page.model_format_open);
    frame();
    for (const auto *part : {"s", "k", "-", "test"})
        input_key(part);
    MIRAGE_CHECK(page.api_key == "sk-test" && page.model_dirty);
    frame();
    MIRAGE_CHECK(!element("model.save.bg")->disabled);
    auto &key_edit = view->state<components::input_detail::InputModel::InputState>("model.key");
    MIRAGE_CHECK(key_edit.text == "*******" && key_edit.undoStack.empty());
    key_edit.selectionStart = 0;
    key_edit.selectionEnd = key_edit.cursor = static_cast<int>(key_edit.text.size());
    input_key("replacement-key", true);
    MIRAGE_CHECK(page.api_key == "replacement-key");
    frame();
    MIRAGE_CHECK(!element("model.save.bg")->disabled);
    // A refresh reply cannot erase a key draft; saving still waits for ACK.
    accept_model_configuration(*preset_document, "model", "");
    MIRAGE_CHECK(page.api_key == "replacement-key" && page.model_dirty);
    accept_model_configuration(empty_catalog, "discard", "");
    select_preset(2);
    frame();
    select_preset(0); // Unsaved key/service edits cannot silently disappear.
    MIRAGE_CHECK(page.model.provider_id == "preset:minimax");
    page.model_dirty = false;
    accept_model_configuration(empty_catalog, "discard", "");
    frame();
    page.model.provider_name = "本地测试服务";
    set_base_url("https://api.example.test/v1");
    page.model_dirty = true;
    auto draft_document = provider_document(false);
    MIRAGE_CHECK(draft_document && draft_document->models.size() == 1 &&
                 !draft_document->model->enabled && draft_document->model->model_selector.empty());
    MIRAGE_CHECK(page.models.empty());
    frame();
    MIRAGE_CHECK(element("model.provider.0.label")->text == "未命名服务");
    // Rejected/disconnected submission must leave the editable name and catalog intact.
    apply_model(false);
    MIRAGE_CHECK(!page.saving_model && page.model_dirty && page.models.empty() &&
                 page.model.provider_name == "本地测试服务");
    accept_model_configuration(*draft_document, "save", "");
    frame();
    MIRAGE_CHECK(!page.model_dirty && page.models.size() == 1);
    MIRAGE_CHECK(element("model.provider.0.label")->text == "本地测试服务");
    element("model.provider.actions.bg")->onClick();
    frame();
    eui::KeyEvent activate;
    activate.action = eui::KeyAction::Press;
    activate.key = eui::InputKey::Enter;
    MIRAGE_CHECK(element("model.provider.rename.bg")->onKeyEvent(activate));
    frame();
    MIRAGE_CHECK(page.editing_provider_name && !page.provider_actions);
    element("model.protocol.control.bg")->onClick();
    frame();
    element("model.protocol.1.bg")->onClick();
    frame();
    MIRAGE_CHECK(page.model.dialect == "openai.responses.v1" && !page.model_format_open);
    element("model.protocol.control.bg")->onClick();
    frame();
    element("model.protocol.0.bg")->onClick();
    frame();
    MIRAGE_CHECK(page.model.dialect == "openai.chat-completions.v1");
    element("model.protocol.control.bg")->onClick();
    frame();
    element("model.protocol.2.bg")->onClick();
    frame();
    MIRAGE_CHECK(page.model.dialect == "anthropic.messages.v1" && !page.model.supports_reasoning);
    element("model.protocol.control.bg")->onClick();
    frame();
    element("model.protocol.0.bg")->onClick();
    frame();

    element("model.add.model.bg")->onClick();
    frame();
    page.new_model_id = "first-model";
    frame();
    element("model.add.confirm.bg")->onClick();
    frame();
    MIRAGE_CHECK(page.provider_models.size() == 1 && page.model.model_selector == "first-model");
    MIRAGE_CHECK(view->find("model.fetch.models.bg") != nullptr);
    element("model.reasoning.supported.hit")->onClick();
    frame();
    MIRAGE_CHECK(page.model.supports_reasoning);
    element("model.images.supported.hit")->onClick();
    MIRAGE_CHECK(page.model.supports_images && page.model_dirty);
    page.model_window = "128000";
    auto first_document = provider_document(true);
    MIRAGE_CHECK(first_document && first_document->models.size() == 1 &&
                 first_document->model->context_window_tokens == 128000);
    MIRAGE_CHECK(first_document->model->supports_images &&
                 first_document->models.front().supports_images);
    accept_model_configuration(*first_document, "save", "");
    page.model_window = "64000";
    page.model.supports_reasoning = true;
    element("model.add.model.bg")->onClick();
    page.new_model_id = "second-model";
    frame();
    element("model.add.confirm.bg")->onClick();
    frame();
    auto second_document = provider_document(true);
    MIRAGE_CHECK(second_document && second_document->models.size() == 2 &&
                 second_document->models[0].context_window_tokens == 64000 &&
                 second_document->models[0].supports_reasoning);
    accept_model_configuration(*second_document, "save", "");
    frame();
    MIRAGE_CHECK(!view->find("model.provider.1.label"));
    MIRAGE_CHECK(element("model.entry.1.label")->text == "second-model");
    {
        // A catalog revision can equal a session ID; failures cannot settle that session.
        auto &session = page.chat.current();
        const bool submitting = session.submitting, deleting = session.deleting;
        session.submitting = session.deleting = true;
        page.fetched_revision = page.model_catalog_revision = session.id;
        page.fetching_models = true;
        RuntimeMessage catalog;
        catalog.tag = "catalog";
        catalog.local_id = session.id;
        catalog.response.error = {"unavailable", "catalog failure"};
        accept_catalog_response(catalog);
        MIRAGE_CHECK(!page.fetching_models && page.model_notice == "catalog failure");
        MIRAGE_CHECK(session.submitting && session.deleting);
        session.submitting = submitting;
        session.deleting = deleting;
        ++page.model_catalog_revision;
        page.fetched_revision = page.model_catalog_revision;
        page.fetching_models = true;
        accept_catalog_response(catalog); // old failure after the editor changed
        MIRAGE_CHECK(page.fetching_models);
        catalog.local_id = page.fetched_revision;
        catalog.response.ok = true;
        catalog.response.payload = ipc::ModelList{{"downloaded-model", "other-model"}};
        accept_catalog_response(catalog);
        MIRAGE_CHECK(!page.fetching_models && page.fetched_model_ids.size() == 2);
        MIRAGE_CHECK(page.provider_models.size() == 2 && !page.model_dirty);
        frame();
        MIRAGE_CHECK(element("model.fetched.list")->frame.height <= 160);
        element("model.fetched.0.bg")->onClick();
        MIRAGE_CHECK(page.provider_models.size() == 3 && page.model_dirty);
        MIRAGE_CHECK(page.live_model.model_selector == "second-model");
        accept_model_configuration(*second_document, "discard", "");
        frame();
    }
    capture("provider-models-light");
    for (const bool dark : {false, true}) {
        page.dark = dark;
        width = 860;
        height = 620;
        glfwSetWindowSize(window, width, height);
        frame();
        MIRAGE_CHECK(element("model.providers")->frame.width > 0);
        MIRAGE_CHECK(element("model.form")->frame.width > 260);
        MIRAGE_CHECK(element("model.save.bg")->frame.x >= element("model.form")->frame.x);
        capture(dark ? "provider-minimum-dark" : "provider-minimum-light");
    }
    width = 1180;
    height = 800;
    page.dark = true;
    glfwSetWindowSize(window, width, height);
    frame();
    capture("provider-models-dark");
    element("model.entry.1.delete.bg")->onClick();
    frame();
    MIRAGE_CHECK(page.provider_models.size() == 1 && page.model_dirty);
    MIRAGE_CHECK(page.live_model.model_selector == "second-model");
    auto deleted_model_document = provider_document(true);
    MIRAGE_CHECK(deleted_model_document && deleted_model_document->models.size() == 1);
    accept_model_configuration(*deleted_model_document, "save", "");
    MIRAGE_CHECK(page.live_model.model_selector == "first-model");
    page.model.provider_name = "重命名服务";
    page.model_dirty = true;
    auto renamed_document = provider_document(true);
    MIRAGE_CHECK(renamed_document->models[0].display_name ==
                 first_document->models[0].display_name);
    MIRAGE_CHECK(renamed_document &&
                 renamed_document->model->provider_id == first_document->model->provider_id);
    accept_model_configuration(*renamed_document, "save", "");
    frame();
    MIRAGE_CHECK(element("model.provider.0.label")->text == "重命名服务");
    page.model_window = "wrong";
    MIRAGE_CHECK(!provider_document(true));
    page.model_dirty = true;
    accept_model_configuration(*renamed_document, "model", "");
    MIRAGE_CHECK(page.model_window == "wrong");
    accept_model_configuration(*renamed_document, "discard", "");
    MIRAGE_CHECK(page.model_window == "64000" && !page.model_dirty);
    // M6-23 / DEC-044: source-level state regressions complement XTest of the
    // actual Release process. No test hooks, network, or production keyring.
    auto service_a = *renamed_document;
    auto service_b = service_a.models.front();
    service_b.provider_id = "synthetic-b";
    service_b.provider_name = "Sandbox B";
    service_b.display_name = "synthetic-model-b";
    service_b.model_selector = "fixture-b";
    service_b.enabled = false;
    service_b.context_window_tokens = 65536;
    service_a.models.push_back(service_b);
    accept_model_configuration(service_a, "save", "");
    page.model_notice = "已保存，配置已生效";
    load_provider(service_b);
    MIRAGE_CHECK(page.model_notice.empty());
    page.model.provider_name = "changed B";
    page.model_dirty = true;
    discard_model_edits();
    MIRAGE_CHECK(page.model.provider_id == "synthetic-b" &&
                 page.model.provider_name == "Sandbox B");
    MIRAGE_CHECK(page.live_model.provider_id != "synthetic-b" && !page.model_dirty);
    page.adding_model = true;
    page.new_model_id = "  fixture-b2  ";
    add_editor_model();
    MIRAGE_CHECK(page.provider_models.size() == 2 && !page.model.enabled && !page.adding_model);
    MIRAGE_CHECK(page.provider_models.back().model_selector == "fixture-b2");
    page.model_window = "96000";
    MIRAGE_CHECK(remember_model_edits());
    select_editor_model(0);
    page.model_window = "32000";
    page.model.supports_reasoning = true;
    const auto deleted_id = page.provider_models.back().display_name;
    delete_editor_model(deleted_id);
    delete_editor_model(deleted_id); // An event from the old frame is idempotent.
    MIRAGE_CHECK(page.provider_models.size() == 1 && page.provider_model_index == 0);
    MIRAGE_CHECK(page.model_window == "32000" && page.model.display_name == service_b.display_name);
    auto preserved = provider_document(false);
    MIRAGE_CHECK(preserved && preserved->models.back().context_window_tokens == 32000 &&
                 preserved->models.back().supports_reasoning && !preserved->models.back().enabled);
    page.adding_model = true;
    page.new_model_id = "  fixture-b  ";
    add_editor_model();
    MIRAGE_CHECK(page.adding_model && page.provider_models.size() == 1 &&
                 page.model_notice == "该模型已存在。");
    MIRAGE_CHECK(!provider_document(false)); // Pending IDs are never silently omitted.
    cancel_model_add();
    MIRAGE_CHECK(page.model_notice.empty());
    page.adding_model = true;
    page.new_model_id = "  ";
    add_editor_model();
    MIRAGE_CHECK(page.adding_model && page.provider_models.size() == 1);
    cancel_model_add();
    page.model.provider_name = " \t ";
    MIRAGE_CHECK(!provider_document(false));
    page.model.provider_name = "  Sandbox B  ";
    MIRAGE_CHECK(provider_document(false) && page.model.provider_name == "Sandbox B");
    page.model_window = "999";
    delete_editor_model(service_b.display_name);
    MIRAGE_CHECK(page.provider_models.size() == 1 && page.model_window == "999");
    auto newer_catalog = service_a;
    newer_catalog.models.front().provider_name = "ACK update";
    accept_model_configuration(newer_catalog, "model", "");
    MIRAGE_CHECK(page.models.front().provider_name == "ACK update" && page.model_window == "999");
    discard_model_edits();
    MIRAGE_CHECK(page.model.provider_id == "synthetic-b" && page.model_window == "65536");
    page.model_window = "72000";
    page.model_dirty = true;
    navigate_model(
        {PageState::ModelDestination::Provider, service_a.models.front().provider_id, 0});
    MIRAGE_CHECK(page.model_dialog == PageState::ModelDialog::Discard &&
                 page.model.provider_id == "synthetic-b");
    frame();
    capture("model-dirty-navigation-light");
    eui::TextInputEvent blocked_input;
    blocked_input.pasteText = "111111";
    element("model.window.hit")->onTextInput(blocked_input);
    MIRAGE_CHECK(page.model_window == "72000");
    element("model.dialog.cancel.bg")->onClick();
    MIRAGE_CHECK(page.model_dirty && page.model_window == "72000");
    leave_settings(false);
    MIRAGE_CHECK(page.model_dialog == PageState::ModelDialog::Discard && page.model_page);
    frame();
    element("model.dialog.confirm.bg")->onClick();
    MIRAGE_CHECK(!page.model_page && !page.model_dirty && page.model_window == "65536");
    page.model_page = true;
    page.adding_model = true;
    page.new_model_id = "pending";
    leave_settings(true);
    MIRAGE_CHECK(page.model_dialog == PageState::ModelDialog::Discard && page.settings);
    frame();
    element("model.dialog.confirm.bg")->onClick();
    MIRAGE_CHECK(!page.settings && !page.adding_model && !page.model_dirty);
    page.settings = page.model_page = true;
    load_provider(service_b);
    delete_provider();
    MIRAGE_CHECK(page.model_dialog == PageState::ModelDialog::Delete && page.models.size() == 2);
    frame();
    capture("model-delete-confirm-light");
    element("model.dialog.cancel.bg")->onClick();
    MIRAGE_CHECK(!page.saving_model && page.models.size() == 2);
    delete_provider();
    frame();
    element("model.dialog.confirm.bg")->onClick(); // Disconnected: preserve catalog/editor.
    MIRAGE_CHECK(!page.saving_model && page.models.size() == 2 &&
                 page.model.provider_id == "synthetic-b");
    frame();
    auto repeat_delete = element("model.entry.0.delete.bg")->onKeyEvent;
    eui::KeyEvent repeat;
    repeat.key = eui::InputKey::Enter;
    repeat.action = eui::KeyAction::Repeat;
    MIRAGE_CHECK(repeat_delete(repeat) && page.provider_models.size() == 1);
    page.saving_model = true;
    page.model_window = "65536";
    delete_editor_model(service_b.display_name);
    navigate_model({PageState::ModelDestination::New, "", 0});
    MIRAGE_CHECK(page.provider_models.size() == 1 &&
                 page.model_dialog == PageState::ModelDialog::None);
    page.saving_model = false;
    page.model_loaded = false;
    frame();
    MIRAGE_CHECK(element("model.add.model.bg")->disabled &&
                 element("model.provider.enabled.hit")->disabled);
    element("model.key.hit")->onTextInput(blocked_input);
    MIRAGE_CHECK(page.api_key == "111111" && page.model_dirty);
    const auto gap_provider = page.model.provider_id;
    page.model_dirty = true;
    select_preset(0);
    MIRAGE_CHECK(!page.preview_preset && page.model.provider_id == gap_provider &&
                 page.model_dirty && page.model_dialog == PageState::ModelDialog::Discard);
    frame();
    element("model.dialog.cancel.bg")->onClick();
    page.model_dirty = false;
    page.api_key.clear();
    page.saving_model = true;
    select_preset(0);
    MIRAGE_CHECK(!page.preview_preset && page.model.provider_id == gap_provider);
    page.saving_model = false;
    page.model_dialog = PageState::ModelDialog::Discard;
    select_preset(0);
    MIRAGE_CHECK(!page.preview_preset && page.model.provider_id == gap_provider);
    page.model_dialog = PageState::ModelDialog::None;
    page.model_loaded = true;
    frame();
    input_key("  fixture-key \t", true);
    MIRAGE_CHECK(page.api_key == "fixture-key" && page.model_dirty);
    input_key("invalid key", true);
    MIRAGE_CHECK(page.api_key == "fixture-key" &&
                 page.model_notice.find("API Key") != std::string::npos);
    discard_model_edits();
    // An initially unnamed service keeps its key while receiving other providers.
    page.model_loaded = false;
    load_provider(unnamed_provider());
    page.models.clear();
    page.model_dirty = false;
    frame();
    input_key("fixture-unnamed", true);
    const auto unnamed_id = page.model.provider_id;
    accept_model_configuration(service_a, "model", "");
    MIRAGE_CHECK(page.model.provider_id == unnamed_id && page.api_key == "fixture-unnamed" &&
                 page.model_dirty && page.models.size() == service_a.models.size());
    discard_model_edits();
    frame();
    element("model.protocol.control.bg")->onClick();
    frame();
    MIRAGE_CHECK(view->find("model.menu.dismiss"));
    element("model.menu.dismiss")->onClick();
    MIRAGE_CHECK(!page.model_format_open && !page.provider_actions);
    // Every preset is a new draft with its own connection and no copied key.
    for (std::size_t preset = 0; preset < provider_presets.size(); ++preset) {
        discard_model_edits();
        select_preset(preset);
        MIRAGE_CHECK(page.model.provider_id ==
                     "preset:" + std::string(provider_presets[preset].id));
        MIRAGE_CHECK(page.api_key.empty() && page.model.credential_ref.empty());
    }
    discard_model_edits();
    load_provider(service_b);
    // Capacity includes models belonging to other services.
    while (page.models.size() < 12) {
        auto extra = service_b;
        extra.provider_id = "extra-" + std::to_string(page.models.size());
        extra.display_name = extra.provider_id;
        page.models.push_back(extra);
    }
    page.adding_model = true;
    page.new_model_id = "over-budget";
    add_editor_model();
    MIRAGE_CHECK(page.provider_models.size() == 1 && page.adding_model);
    cancel_model_add();
    discard_model_edits();
    frame();
    MIRAGE_CHECK(element("model.add.model.bg")->disabled);
    delete_editor_model(service_b.display_name);
    MIRAGE_CHECK(page.provider_models.empty() && !page.model.enabled && page.model_window.empty());
    frame();
    MIRAGE_CHECK(element("model.provider.enabled.hit")->disabled);
    accept_model_configuration(service_a, "save", "");
    load_provider(service_b);
    for (const bool dark : {false, true}) {
        width = 860;
        height = 620;
        page.dark = dark;
        page.model_window = "72000";
        page.model_dirty = true;
        leave_settings(true);
        frame();
        MIRAGE_CHECK(element("model.dialog.panel")->frame.x >= 0 &&
                     element("model.dialog.confirm.bg")->frame.y + 40 <= height);
        capture(dark ? "model-recovery-minimum-dark" : "model-recovery-minimum-light");
        element("model.dialog.cancel.bg")->onClick();
        discard_model_edits();
    }
    page.model_dialog = PageState::ModelDialog::None;
    page.confirm_exit = true;
    page.exit_epoch = 9;
    page.active_work = 256;
    for (const bool dark : {false, true}) {
        width = 860;
        height = 620;
        page.dark = dark;
        frame();
        MIRAGE_CHECK(element("product.exit.panel")->frame.x >= 0);
        MIRAGE_CHECK(element("product.exit.confirm.bg")->frame.y + 36 <= height);
        MIRAGE_CHECK(element("product.exit.cancel.bg") && element("product.exit.title"));
        MIRAGE_CHECK(element("product.exit.body")->text.find("256") != std::string::npos);
        capture(dark ? "exit-confirm-minimum-dark" : "exit-confirm-minimum-light");
        const auto count = page.chat.current().messages.size();
        eui::KeyEvent blocked_new;
        blocked_new.key = eui::InputKey::N;
        blocked_new.action = eui::KeyAction::Press;
        blocked_new.modifiers.control = true;
        const auto exit_dialog_session = page.chat.current().id;
        app::dslAppConfig().keyEventHandler(blocked_new);
        MIRAGE_CHECK(page.chat.current().id == exit_dialog_session);
        MIRAGE_CHECK(page.chat.current().messages.size() == count && page.confirm_exit);
    }
    page.confirm_exit = false;
    // DEC-079: inspect actual sidebar row geometry, with background updates.
    page.settings = false;
    page.sidebar = true;
    page.selection.clear();
    page.selection_cache_ids.clear();
    page.hovered_message = page.hovered_session = 0;
    for (const auto &item : page.chat.sessions())
        page.chat.clear_session(item.id);
    page.chat.new_draft();
    const auto activity_first = page.chat.current().id;
    page.chat.apply_turn(activity_first, "a", "ok", "较早会话", "回复", {}, {}, 1, {}, 100);
    page.chat.create_session();
    const auto activity_second = page.chat.current().id;
    page.chat.apply_turn(activity_second, "b", "ok", "较新会话", "回复", {}, {}, 1, {}, 200);
    page.chat.select_session(activity_first);
    page.chat.set_draft("保持当前草稿");
    const auto first_key = "session." + std::to_string(activity_first) + ".title";
    const auto second_key = "session." + std::to_string(activity_second) + ".title";
    for (const bool dark : {false, true}) {
        width = dark ? 860 : 1180;
        height = dark ? 620 : 800;
        page.dark = dark;
        frame();
        MIRAGE_CHECK(element(second_key)->frame.y < element(first_key)->frame.y);
        page.chat.apply_turn(activity_first, "a", "ok", "较早会话", dark ? "再次新回复" : "新回复",
                             {}, {}, 1);
        frame();
        MIRAGE_CHECK(element(first_key)->frame.y < element(second_key)->frame.y);
        MIRAGE_CHECK(page.chat.current().id == activity_first &&
                     page.chat.current().draft == "保持当前草稿");
        capture(dark ? "session-activity-minimum-dark" : "session-activity-light");
        page.chat.apply_turn(activity_second, "b", "ok", "较新会话", dark ? "再次更新" : "新回复",
                             {}, {}, 1);
    }
    // M6-38 clarification: the conversation selector contains only three modes.
    // Its native pointer path and isolated preference save preserve the draft.
    MIRAGE_CHECK(!page.runtime);
    page.settings = page.model_page = page.desktop_page = false;
    page.about = page.confirm_clear = page.confirm_delete = false;
    page.popup = PageState::Popup::None;
    page.chat.current().access = "default";
    const auto permission_draft = page.chat.current().draft;
    page.desktop_authorization_pending = false;
    page.desktop_authorization_loaded = true;
    page.desktop_authorization = {true, false, false, "ask", ""};
    const auto initial_revision = page.desktop_authorization_revision;
    for (const int viewport : {1180, 860}) {
        width = viewport;
        height = viewport == 1180 ? 800 : 620;
        page.dark = viewport == 860;
        frame();
        element("composer.mode.bg")->onClick();
        frame();
        MIRAGE_CHECK(page.popup == PageState::Popup::Mode &&
                     page.desktop_authorization_revision == initial_revision);
        const auto mode_bounds = element("composer.popup")->frame;
        MIRAGE_CHECK(mode_bounds.height == 120 && mode_bounds.width == 280);
        const auto mode_trigger = element("composer.mode.bg")->frame;
        MIRAGE_CHECK(mode_bounds.x == mode_trigger.x &&
                     mode_bounds.y + mode_bounds.height == mode_trigger.y - 4);
        for (const auto *id : {"conversation", "readonly", "default"}) {
            const auto *button = element(std::string("composer.popup.") + id + ".bg");
            MIRAGE_CHECK(button && !button->disabled && button->frame.y >= mode_bounds.y &&
                         button->frame.y + button->frame.height <=
                             mode_bounds.y + mode_bounds.height);
        }
        MIRAGE_CHECK(!view->find("composer.popup.authorization") &&
                     !view->find("composer.popup.authorization.heading") &&
                     !view->find("composer.popup.authorization.authorize.bg") &&
                     !view->find("composer.popup.access.help"));
        capture(viewport == 1180 ? "composer-three-modes-light"
                                 : "composer-three-modes-small-dark");
        page.popup = PageState::Popup::None;
    }
    for (const auto *access : {"read_only", "chat_only"}) {
        page.popup = PageState::Popup::Mode;
        frame();
        const auto *id = std::string(access) == "read_only" ? "readonly" : "conversation";
        const auto hit = element(std::string("composer.popup.") + id + ".bg")->frame;
        const auto px = hit.x + hit.width / 2;
        const auto py = hit.y + hit.height / 2;
        core::queuePointerMotion(handle, px, py, {}, {});
        core::queuePointerButton(handle, px, py, core::PointerButton::Left,
                                 core::PointerAction::Press, {});
        frame();
        core::queuePointerButton(handle, px, py, core::PointerButton::Left,
                                 core::PointerAction::Release, {});
        frame();
        MIRAGE_CHECK(page.chat.current().access == access && page.popup == PageState::Popup::None &&
                     page.desktop_authorization_revision == initial_revision &&
                     page.chat.current().draft == permission_draft);
        MIRAGE_CHECK(load_preferences().default_access == access);
    }
    page.popup = PageState::Popup::Mode;
    page.desktop_authorization_pending = true;
    frame();
    for (const auto *id : {"conversation", "readonly", "default"})
        MIRAGE_CHECK(element(std::string("composer.popup.") + id + ".bg")->disabled);
    select_desktop_mode("default");
    MIRAGE_CHECK(page.chat.current().access == "chat_only");
    page.desktop_authorization_pending = false;
    page.chat.current().running = true;
    frame();
    select_desktop_mode("default");
    MIRAGE_CHECK(page.chat.current().access == "chat_only" &&
                 element("composer.popup.default.bg")->disabled);
    page.chat.current().running = false;
    frame();
    element("composer.popup.default.bg")->onClick();
    frame();
    MIRAGE_CHECK(page.chat.current().access == "default" && page.popup == PageState::Popup::None &&
                 page.desktop_authorization_revision == initial_revision + 1 &&
                 !page.desktop_authorization_notice.empty());
    MIRAGE_CHECK(element("composer.notice")->text ==
                     fitted_title(page.desktop_authorization_notice,
                                  element("composer.notice")->frame.width, 12) &&
                 page.chat.current().draft == permission_draft);
    // M6-40: every visual element carries its own registered icon. The expected
    // strings below are the UTF-8 encodings of the Phosphor codepoints kept in
    // apps/native/icons.hpp; rendering already matches the product font setup.
    {
        page.dark = false;
        width = 1180;
        height = 800;
        // Reasoning depth rows: one distinct glyph per thinking option.
        page.live_model.reasoning_options = {"",       "none", "adaptive", "minimal", "low",
                                             "medium", "high", "xhigh",    "max"};
        page.popup = PageState::Popup::Reasoning;
        frame();
        const std::vector<std::string> effort_glyphs = {
            "\xee\x9a\xa2", "\xee\x8f\x9e", "\xee\x9d\x8e", "\xee\x8b\x9c", "\xee\x96\x8e",
            "\xee\x99\x92", "\xee\x9a\xa4", "\xee\x8b\x9e", "\xee\x91\xb2"};
        for (std::size_t i = 0; i < effort_glyphs.size(); ++i) {
            MIRAGE_CHECK(element("composer.popup.effort." + std::to_string(i) + ".icon")->text ==
                         effort_glyphs[i]);
            for (std::size_t j = 0; j < i; ++j)
                MIRAGE_CHECK(effort_glyphs[i] != effort_glyphs[j]);
        }
        page.popup = PageState::Popup::None;
        // Access mode rows: 对话 / 视觉 / 完全访问.
        page.popup = PageState::Popup::Mode;
        frame();
        MIRAGE_CHECK(element("composer.popup.conversation.icon")->text == "\xee\x85\xba");
        MIRAGE_CHECK(element("composer.popup.readonly.icon")->text == "\xee\x88\xa0");
        MIRAGE_CHECK(element("composer.popup.default.icon")->text == "\xee\x90\x92");
        page.popup = PageState::Popup::None;
        frame();
        // Session rows distinguish idle (chat-circle) from running (circle-notch).
        const auto live_session = "session." + std::to_string(page.chat.current().id);
        MIRAGE_CHECK(element(live_session + ".icon")->text == "\xee\x85\xa8");
        page.chat.current().running = true;
        frame();
        MIRAGE_CHECK(element(live_session + ".icon")->text == "\xee\xad\x84");
        page.chat.current().running = false;
        frame();
        // Reference popup: the attachment type glyph follows the file extension.
        page.chat.current().attachments = {{1, "notes.txt", "纯文本附件"},
                                           {2, "shot.png", "图片描述"}};
        page.chat.current().references = {{user, "你", "被引用的选段", 7}};
        page.popup = PageState::Popup::References;
        frame();
        MIRAGE_CHECK(element("attachment.1.type")->text == "\xee\x88\xba"); // file-text
        MIRAGE_CHECK(element("attachment.2.type")->text == "\xee\x8b\x8a"); // image
        MIRAGE_CHECK(element("attachment.1.name") != nullptr &&
                     view->find("attachment.1.preview") != nullptr);
        MIRAGE_CHECK(view->find("attachment.1.remove") != nullptr &&
                     view->find("attachment.2.remove") != nullptr);
        MIRAGE_CHECK(element("reference.preview.7.type")->text == "\xee\x99\xa0"); // quotes
        page.popup = PageState::Popup::None;
        page.chat.current().attachments.clear();
        page.chat.current().references.clear();
        frame();
        // Process part rows carry a kind glyph and indent the toggle past it.
        {
            std::vector<mirage::conversation::Part> icon_parts{
                {"think", "thinking", "complete", "先思考再执行。"},
                {"hold", "tool", "complete", "", "wait", "{\"duration_ms\":5}"},
                {"press", "tool", "complete", "", "desktop.input.key", "{}"},
                {"seek", "tool", "complete", "", "browser.search", "{}"}};
            page.chat.apply_process(page.chat.current().id, "a", icon_parts, 1);
            frame();
            const auto parts_message =
                "message." + std::to_string(page.chat.current().messages.back().id);
            MIRAGE_CHECK(element(parts_message + ".part.think.kind")->text == "\xee\x9d\x8e");
            MIRAGE_CHECK(element(parts_message + ".part.hold.kind")->text == "\xee\x8a\xb2");
            MIRAGE_CHECK(element(parts_message + ".part.press.kind")->text == "\xee\x9f\x88");
            MIRAGE_CHECK(element(parts_message + ".part.seek.kind")->text == "\xee\x97\x94");
            const auto *row = element(parts_message);
            MIRAGE_CHECK(std::abs(element(parts_message + ".part.think.kind")->frame.x -
                                  row->frame.x - 4) < 0.5f);
            MIRAGE_CHECK(std::abs(element(parts_message + ".part.think.toggle")->frame.x -
                                  row->frame.x - 20) < 0.5f);
            page.chat.apply_process(page.chat.current().id, "a", {}, 2);
            frame();
        }
        // Dialog titles and the exit confirmation carry their own glyphs.
        page.confirm_delete = true;
        frame();
        MIRAGE_CHECK(element("dialog.title.icon")->text == "\xee\x92\xa6");
        page.confirm_delete = false;
        page.confirm_clear = true;
        frame();
        MIRAGE_CHECK(element("dialog.title.icon")->text == "\xee\xb1\x94");
        page.confirm_clear = false;
        page.about = true;
        frame();
        MIRAGE_CHECK(element("dialog.title.icon")->text == "\xee\x8b\x8e");
        page.about = false;
        page.confirm_exit = true;
        frame();
        MIRAGE_CHECK(element("product.exit.title.icon")->text == "\xee\x93\xa0");
        page.confirm_exit = false;
        frame();
        // Desktop tools page: every action button keeps its guiding glyph.
        page.settings = true;
        page.desktop_page = true;
        frame();
        MIRAGE_CHECK(element("settings.nav.2.icon")->text == "\xee\x8c\xae"); // monitor
        MIRAGE_CHECK(element("desktop.resources.choose.icon")->text == "\xee\x89\x96");
        MIRAGE_CHECK(element("desktop.resources.save.icon")->text == "\xee\x89\x88");
        MIRAGE_CHECK(element("desktop.resources.refresh.icon")->text == "\xee\x82\x94");
        MIRAGE_CHECK(element("desktop.permissions.authorize.icon")->text == "\xee\x90\x8c");
        MIRAGE_CHECK(element("desktop.permissions.forget.icon")->text == "\xee\x90\x90");
        MIRAGE_CHECK(element("desktop.permissions.refresh.icon")->text == "\xee\x82\x94");
        page.desktop_page = false;
        // Model page: fetch/save/discard glyphs and the widened 96 px action row.
        page.model_page = true;
        frame();
        MIRAGE_CHECK(element("model.fetch.models.icon")->text == "\xee\x88\x8c");
        MIRAGE_CHECK(element("model.discard.icon")->text == "\xee\x93\xb6");
        MIRAGE_CHECK(element("model.save.icon")->text == "\xee\x89\x88");
        MIRAGE_CHECK(element("model.discard")->frame.width == 96);
        MIRAGE_CHECK(element("model.save")->frame.width == 96);
        MIRAGE_CHECK(std::abs(element("model.save")->frame.x + element("model.save")->frame.width -
                              element("model.form")->frame.x - element("model.form")->frame.width) <
                     0.5f);
        page.model_dialog = PageState::ModelDialog::Delete;
        frame();
        MIRAGE_CHECK(element("model.dialog.title.icon")->text == "\xee\x92\xa6");
        page.model_dialog = PageState::ModelDialog::Discard;
        frame();
        MIRAGE_CHECK(element("model.dialog.title.icon")->text == "\xee\x80\xb8");
        page.model_dialog = PageState::ModelDialog::None;
        page.model_page = false;
        // Appearance page: the font steppers carry text-aa and code glyphs.
        frame();
        MIRAGE_CHECK(element("settings.font.step0.icon")->text == "\xee\x9b\xae");
        MIRAGE_CHECK(element("settings.codefont.step0.icon")->text == "\xee\x86\xbc");
        page.settings = false;
        frame();
    }
    // M6-47: retain exact row geometry while keeping distant history out of
    // the drawable/control tree. A scroll callback must reveal the next rows.
    {
        page.settings = page.about = false;
        page.confirm_exit = false;
        page.popup = PageState::Popup::None;
        page.selection.clear();
        page.chat.create_session();
        const auto history_id = page.chat.current().id;
        std::string historical_reply = "## 历史回复\n\n";
        while (historical_reply.size() < 2048)
            historical_reply += "正文保持原始高度与滚动位置。\n\n";
        for (int i = 0; i < 40; ++i)
            page.chat.apply_turn(history_id, "virtual-" + std::to_string(i), "ok", "历史输入",
                                 i == 39 ? "最新短回复" : historical_reply, {}, {}, i + 1);
        width = 1180;
        height = 800;
        dpi_scale = 1;
        frame();
        const auto thread = "thread." + std::to_string(history_id);
        const auto first = "message." + std::to_string(page.chat.current().messages.front().id);
        const auto last = "message." + std::to_string(page.chat.current().messages.back().id);
        MIRAGE_CHECK(view->find(first) && !view->find(first + ".body"));
        MIRAGE_CHECK(view->find(last + ".markdown"));
        const auto extent = element(thread)->scrollMaxOffset;
        MIRAGE_CHECK(extent > 3000);
        element(thread)->onScrollOffsetChanged(0);
        frame();
        MIRAGE_CHECK(view->find(first + ".body") && !view->find(last + ".markdown"));
        MIRAGE_CHECK(std::abs(element(thread)->scrollMaxOffset - extent) < .5f);
        element(thread)->onScrollOffsetChanged(extent / 2);
        frame();
        MIRAGE_CHECK(!view->find(first + ".body") && !view->find(last + ".markdown"));
        MIRAGE_CHECK(std::abs(element(thread)->scrollOffset - extent / 2) < .5f);
        element(thread)->onScrollOffsetChanged(extent);
        frame();
        MIRAGE_CHECK(view->find(last + ".markdown") && page.chat.current().follow_output);
    }
    // M6-50: selecting a scrolled Markdown slice must use the full source
    // geometry, including lines omitted from the normal control tree.
    {
        page.selection.clear();
        page.chat.create_session();
        const auto sid = page.chat.current().id;
        std::string source;
        for (int line = 0; line < 120; ++line)
            source += "paragraph" + std::to_string(line) + " **重点** English。\n\n";
        page.chat.apply_turn(sid, "viewport-selection", "ok", "长正文", source, {}, {}, 1);
        frame();
        const auto thread = "thread." + std::to_string(sid);
        const auto message = "message." + std::to_string(page.chat.current().messages.back().id);
        const auto extent = element(thread)->scrollMaxOffset;
        element(thread)->onScrollOffsetChanged(extent / 2);
        frame();
        const auto *md = element(message + ".markdown");
        MIRAGE_CHECK(md && extent > 2000);
        std::vector<const core::dsl::Element *> leaves;
        auto walk = [&](auto &&self, const auto &node) -> void {
            if (node.kind == core::dsl::ElementKind::Text && node.text.starts_with("paragraph"))
                leaves.push_back(&node);
            for (const auto &child : node.children)
                self(self, *child);
        };
        walk(walk, *md);
        MIRAGE_CHECK(!leaves.empty() && leaves.size() < 40);
        const auto offset = element(thread)->scrollOffset;
        const auto visible = std::find_if(leaves.begin(), leaves.end(), [&](const auto *leaf) {
            return leaf->frame.y - offset > 180 && leaf->frame.y - offset < 350;
        });
        MIRAGE_CHECK(visible != leaves.end());
        if (visible != leaves.end()) {
            const auto *leaf = *visible;
            const auto prefix = leaf->text;
            MIRAGE_CHECK(prefix != "paragraph0");
            const auto x = leaf->frame.x + 1;
            const auto y = leaf->frame.y - offset + 10;
            core::queuePointerButton(handle, x, y, core::PointerButton::Left,
                                     core::PointerAction::Press, {});
            frame();
            core::queuePointerMotion(handle, x + 250, y, core::PointerButton::Left, {});
            frame();
            core::queuePointerButton(handle, x + 250, y, core::PointerButton::Left,
                                     core::PointerAction::Release, {});
            frame();
            MIRAGE_CHECK(page.selection.ready && page.selection.excerpt.starts_with(prefix) &&
                         page.selection.excerpt.find("**") == std::string::npos);
            capture("viewport-markdown-selection");
        }
    }
    width = 1180;
    height = 800;
    glfwPollEvents();
    window::close();
    const auto close_wait = std::chrono::steady_clock::now();
    glfwWaitEventsTimeout(1.0);
    // A missing idle wakeup is an assertion failure, not an unbounded test hang.
    MIRAGE_CHECK(std::chrono::steady_clock::now() - close_wait < std::chrono::milliseconds{900});
    MIRAGE_CHECK(glfwWindowShouldClose(window));
    window::shutdown();
    runtime.shutdown();
    backend.reset();
    core::window::destroyWindow(handle);
    glfwTerminate();
    return mirage::testing::finish("native_conversation_view_test");
}
