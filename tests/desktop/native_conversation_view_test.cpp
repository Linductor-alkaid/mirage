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
    mirage::testing::XvfbDisplay display(mirage::testing::find_xvfb(), "1400x1000x24");
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
    core::TextPrimitive::setDefaultFontFiles(text_font(),
                                             "assets/Font Awesome 7 Free-Solid-900.otf");
    auto &page = state();
    page.runtime_notice = "合成测试会话 · 不连接模型";
    const auto session_id = page.chat.current().id;
    page.chat.apply_turn(
        session_id, "first", "ok", "帮我梳理这个项目的结构。",
        "## 实现结构\n\nMirage 提供 **原生界面**，Mira 负责 Agent 推理。\n\n- 窗口与状态栏分离\n- "
        "会话由服务管理\n\n可以先完善会话体验，再接入桌面工作流。",
        {}, {}, 1);
    page.chat.current().scroll_offset = 0;
    core::dsl::Runtime runtime;
    MIRAGE_CHECK(runtime.initialize(handle));
    eui::Ui *view = nullptr;
    int width = 1180, height = 800;
    auto compose = [&] {
        runtime.compose("test", static_cast<float>(width), static_cast<float>(height),
                        [&](auto &ui, const auto &screen) {
                            view = &ui;
                            compose_page(ui, screen);
                        });
    };
    auto frame = [&] {
        compose();
        runtime.update(handle, 1, 1, 1);
        compose();
        runtime.update(handle, 1, 1, 1);
        compose();
    };
    auto capture = [&](const std::string &name) {
        if (argc < 2)
            return;
        runtime.requestFullPaint();
        runtime.render(width, height, 1, palette(page.dark).background);
        glFinish();
        std::vector<unsigned char> pixels(static_cast<std::size_t>(width * height * 3));
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        glReadPixels(0, 0, width, height, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());
        std::filesystem::create_directories(argv[1]);
        std::ofstream output(std::filesystem::path(argv[1]) / (name + ".ppm"), std::ios::binary);
        output << "P6\n" << width << " " << height << "\n255\n";
        for (int row = height - 1; row >= 0; --row)
            output.write(reinterpret_cast<const char *>(pixels.data() + row * width * 3),
                         width * 3);
    };
    frame();
    const auto user = page.chat.current().messages.front().id;
    const auto assistant = page.chat.current().messages.back().id;
    auto element = [&](const std::string &key) {
        auto *found = view->find(key);
        MIRAGE_CHECK(found);
        return found;
    };
    const auto key = "message." + std::to_string(user);
    MIRAGE_CHECK(element("thread.title")->text == page.chat.current().title);
    capture("title-history");
    const auto history_count = page.chat.history_count();
    MIRAGE_CHECK(page.chat.new_draft());
    frame();
    MIRAGE_CHECK(element("thread.title")->text == "新对话" &&
                 page.chat.history_count() == history_count);
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
        std::vector<unsigned char> pixels(700 * 330 * 3);
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        glReadPixels(320, height - 550, 700, 330, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());
        return pixels;
    };
    const auto reading_pixels = visible_text_pixels();
    capture("stream-reading-light");
    auto *latest_hit = element("thread.latest.bg");
    MIRAGE_CHECK(latest_hit->focusable);
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
    capture("provider-empty-light");
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

    element("model.add.model.bg")->onClick();
    frame();
    page.new_model_id = "first-model";
    frame();
    element("model.add.confirm.bg")->onClick();
    frame();
    MIRAGE_CHECK(page.provider_models.size() == 1 && page.model.model_selector == "first-model");
    page.model_window = "128000";
    auto first_document = provider_document(true);
    MIRAGE_CHECK(first_document && first_document->models.size() == 1 &&
                 first_document->model->context_window_tokens == 128000);
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
    glfwPollEvents();
    window::close();
    glfwWaitEvents();
    MIRAGE_CHECK(glfwWindowShouldClose(window));
    window::shutdown();
    runtime.shutdown();
    backend.reset();
    core::window::destroyWindow(handle);
    glfwTerminate();
    return mirage::testing::finish("native_conversation_view_test");
}
