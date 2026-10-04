#include "chat_model.hpp"
#include "markdown_adapter.hpp"
#include "runtime_bridge.hpp"
#include "window_controls.hpp"
#include <mirage/runtime/persistence/settings.hpp>

#include <algorithm>
#include <charconv>
#include <components/markdown.h>
#include <eui_neo.h>
#include <filesystem>
#include <functional>
#include <string>

namespace mirage::native_ui {
namespace {
eui::Color color(unsigned int rgb) {
    return {static_cast<float>((rgb >> 16) & 255) / 255.0f,
            static_cast<float>((rgb >> 8) & 255) / 255.0f, static_cast<float>(rgb & 255) / 255.0f,
            1};
}
struct Palette {
    eui::Color background, sidebar, surface, hover, selected, text, muted, border, action, inverse;
};
Palette palette(bool dark) {
    if (dark)
        return {color(0x161616), color(0x1d1d1d), color(0x222222), color(0x2c2c2c),
                color(0x333333), color(0xeeeeee), color(0xa1a1a1), color(0x353535),
                color(0xeeeeee), color(0x161616)};
    return {color(0xf8f8f8), color(0xf0f0f0), color(0xffffff), color(0xe8e8e8), color(0xe2e2e2),
            color(0x202020), color(0x6d6d6d), color(0xdfdfdf), color(0x222222), color(0xffffff)};
}
struct PageState {
    ChatModel chat;
    bool dark = false;
    bool sidebar = true;
    bool settings = false;
    bool model_page = false;
    bool agent_mode = true;
    bool saving_model = false;
    bool model_loaded = false;
    bool model_dirty = false;
    std::unique_ptr<RuntimeBridge> runtime;
    mirage::runtime::persistence::ModelSettings model;
    mirage::runtime::persistence::ModelSettings live_model;
    std::string runtime_notice = "正在连接 Runtime Service…";
    std::string model_notice;
    std::string model_window;
    float model_scroll = 0;
    float sidebar_width = 260;
    float sidebar_drag_width = 260;
    float sidebar_drag_scale = 1;
    bool sidebar_hover = false;
    bool sidebar_dragging = false;
    bool about = false;
    bool confirm_clear = false;
    std::uint64_t clear_target = 0;
    float session_scroll = 0;
    enum class Popup { None, Actions, Mode, Model, Context, References };
    Popup popup = Popup::None;
    bool composer_focused = false;
    bool composer_composing = false;
};
// UI state stays on the main thread; RuntimeBridge owns all IPC work.
PageState &state() {
    static PageState value;
    return value;
}
namespace ipc = mirage::runtime::ipc;
namespace persistence = mirage::runtime::persistence;
std::optional<ContextUsage> native_usage(const std::optional<ipc::ContextUsage> &usage);
bool call_runtime(ipc::Request request, const std::string &tag, std::uint64_t id = 0) {
    auto &s = state();
    if (s.runtime && s.runtime->call(std::move(request), tag, id))
        return true;
    s.runtime_notice = "服务未连接或请求已达上限，请重新连接后重试。";
    return false;
}
void history(std::uint64_t id) {
    const auto *session = state().chat.find(id);
    if (session && !session->remote_id.empty())
        call_runtime(ipc::ChatHistoryRequest{session->remote_id, 40}, "history", id);
}
void new_session() {
    auto &s = state();
    if (s.chat.create_session())
        call_runtime(ipc::OpenSessionRequest{}, "open", s.chat.current().id);
}
void submit_turn() {
    auto &s = state();
    auto &session = s.chat.current();
    if (session.remote_id.empty() || session.running || session.submitting)
        return;
    const auto prompt = s.chat.submission_text();
    if (prompt.empty()) {
        s.runtime_notice = "请输入消息，并将输入和引用合计缩短到 16 KiB 以内。";
        return;
    }
    if (call_runtime(ipc::SessionChatRequest{session.remote_id, prompt, s.agent_mode}, "send",
                     session.id)) {
        session.submitted_text = session.draft;
        session.submitting = true;
        s.runtime_notice = "正在提交…";
        session.submitted_references.clear();
        for (const auto &reference : session.references)
            session.submitted_references.push_back(reference.id);
        s.popup = PageState::Popup::None;
    }
}
void start_runtime() {
    auto &s = state();
    if (s.runtime)
        s.runtime->shutdown();
    s.runtime = std::make_unique<RuntimeBridge>([] { app::requestUpdate(); });
    s.runtime_notice = "正在连接 Runtime Service…";
}
std::string display_error(const std::string &error) {
    const std::string credential = "credential environment variable is not set: ";
    if (error.starts_with(credential))
        return "服务未设置凭据变量 " + error.substr(credential.size()) +
               "。设置该变量后重启 Runtime Service。";
    if (error == "dialog turn was cancelled")
        return "当前任务已停止。";
    if (error == "another model turn is active" ||
        error == "stop active turns before changing model")
        return "有模型任务正在运行，请先停止后重试。";
    if (error == "endpoint must be an HTTP(S) origin; put its path in API prefix")
        return "服务地址仅填写 http(s)://主机；路径请填在 API 路径中。";
    return error;
}
void drain_runtime() {
    auto &s = state();
    if (!s.runtime)
        return;
    RuntimeMessage message;
    for (int i = 0; i < 128 && s.runtime->receive(message); ++i) {
        if (message.kind == RuntimeMessage::Kind::Connected) {
            s.runtime_notice = "已连接 Runtime Service";
            call_runtime(ipc::SubscribeEventsRequest{}, "subscribe");

        } else if (message.kind == RuntimeMessage::Kind::Lost) {
            s.runtime_notice = "服务连接已断开，请在设置 → 模型中重新连接。";
            s.saving_model = false;
            for (const auto &item : s.chat.sessions()) {
                auto *session = s.chat.find(item.id);
                session->submitting = false;
            }
        } else if (message.kind == RuntimeMessage::Kind::Response) {
            auto *session = s.chat.find(message.local_id);
            if (!message.response.ok) {
                s.runtime_notice = display_error(message.response.error.message);
                if (session)
                    session->submitting = false;
                if (message.tag == "save") {
                    s.saving_model = false;
                    s.model_notice = display_error(message.response.error.message);
                }
                continue;
            }
            if (message.tag == "subscribe") {
                call_runtime(ipc::ListSessionsRequest{}, "sessions");
                call_runtime(ipc::GetModelRequest{}, "model");
            }
            const auto &payload = message.response.payload;
            if (const auto *config = std::get_if<ipc::ModelConfiguration>(&payload)) {
                const auto decoded = persistence::decode_settings(config->settings_json);
                if (decoded.ok && decoded.settings.model) {
                    s.live_model = *decoded.settings.model;
                    if (!s.model_dirty || message.tag == "save") {
                        s.model = *decoded.settings.model;
                        s.model_window = s.model.context_window_tokens
                                             ? std::to_string(s.model.context_window_tokens)
                                             : "";
                    }
                    s.model_loaded = true;
                    if (message.tag == "save") {
                        s.saving_model = false;
                        s.model_dirty = false;
                        s.model_notice = "已保存并应用到 Runtime Service";
                    }
                }
            } else if (const auto *opened = std::get_if<ipc::SessionOpened>(&payload)) {
                s.chat.bind_remote(message.local_id, opened->session_id);
                history(message.local_id);
            } else if (const auto *sessions = std::get_if<ipc::SessionList>(&payload)) {
                const auto selected = s.chat.current().id;
                for (const auto &remote : sessions->sessions) {
                    if (remote.state == "closed" || remote.state == "failed")
                        continue;
                    const auto existing = std::find_if(
                        s.chat.sessions().begin(), s.chat.sessions().end(),
                        [&remote](const auto &entry) { return entry.remote_id == remote.id; });
                    if (existing != s.chat.sessions().end())
                        continue;
                    if (s.chat.current().remote_id.empty())
                        s.chat.bind_remote(s.chat.current().id, remote.id);
                    else if (s.chat.create_session())
                        s.chat.bind_remote(s.chat.current().id, remote.id);
                }
                s.chat.select_session(selected);
                history(selected);
            } else if (const auto *snapshot = std::get_if<ipc::DialogHistory>(&payload)) {
                if (session && session->remote_id == snapshot->session_id) {
                    for (const auto &turn : snapshot->turns)
                        s.chat.apply_turn(session->id, turn.turn_id, turn.status, turn.user_text,
                                          turn.reply_text, display_error(turn.error),
                                          native_usage(turn.context_usage), turn.sequence);
                }
            } else if (std::holds_alternative<ipc::DialogTurnAccepted>(payload) &&
                       message.tag == "send") {
                if (session) {
                    s.chat.acknowledge_submission(session->id);
                }
                s.runtime_notice = "请求已接纳，等待 Mira 回复。";
                history(message.local_id);
            }
        } else if (message.event) {
            if (const auto *turn =
                    std::get_if<ipc::ChatTurnUpdatedEvent>(&message.event->payload)) {
                for (const auto &session : s.chat.sessions())
                    if (session.remote_id == turn->session_id) {
                        s.chat.apply_turn(session.id, turn->turn_id, turn->status, turn->user_text,
                                          turn->reply_text, display_error(turn->error),
                                          native_usage(turn->context_usage), turn->sequence);
                        s.runtime_notice = turn->status == "pending" ? "Mira 正在运行…"
                                           : turn->status == "ok"    ? "Mira 已回复"
                                                                     : display_error(turn->error);
                        break;
                    }
            } else if (std::holds_alternative<ipc::EventsOverflowEvent>(message.event->payload)) {
                history(s.chat.current().id);
            }
        }
    }
    if (s.runtime->take_gap()) {
        s.saving_model = false;
        s.model_loaded = false;
        call_runtime(ipc::GetModelRequest{}, "model");
        s.runtime_notice = "事件发生缺口，正在重新读取服务状态。";
        call_runtime(ipc::ListSessionsRequest{}, "sessions");
        history(s.chat.current().id);
    }
}
std::string text_font() {
    const char *candidates[] = {
#ifdef _WIN32
        "C:/Windows/Fonts/msyh.ttc",
        "C:/Windows/Fonts/simhei.ttf",
#endif
        "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc",
        "/usr/share/fonts/truetype/droid/DroidSansFallbackFull.ttf",
        "/usr/share/fonts/truetype/wqy/wqy-microhei.ttc",
    };
    for (const auto *path : candidates) {
        std::error_code error;
        if (std::filesystem::is_regular_file(path, error))
            return path;
    }
    return "assets/JingNanJunJunTi-JinNanJunJunTi-Bold-2.ttf";
}
void text(eui::Ui &ui, const std::string &id, const std::string &value, float x, float y,
          float width, float height, float size, eui::Color ink, int weight = 400) {
    ui.text(id)
        .position(x, y)
        .size(width, height)
        .text(value)
        .fontSize(size)
        .lineHeight(size * 1.5f)
        .verticalAlign(eui::VerticalAlign::Center)
        .fontWeight(weight)
        .color(ink)
        .hitTestMode(eui::dsl::HitTestMode::None)
        .build();
}
// Fit the display title to its actual font metrics without changing the session title.
std::string fitted_title(const std::string &value, float width, float size) {
    core::TextStyle style;
    style.fontSize = size;
    style.text = value;
    if (core::TextPrimitive::measureTextSize(style).x <= width)
        return value;
    std::string prefix = value;
    while (!prefix.empty()) {
        std::size_t last = prefix.size() - 1;
        while (last > 0 && (static_cast<unsigned char>(prefix[last]) & 0xc0) == 0x80)
            --last;
        prefix.resize(last);
        style.text = prefix + "…";
        if (core::TextPrimitive::measureTextSize(style).x <= width)
            return style.text;
    }
    return "…";
}
void icon(eui::Ui &ui, const std::string &id, unsigned int value, float x, float y, float size,
          float row_height, eui::Color ink) {
    ui.text(id)
        .position(x, y)
        .size(24, row_height)
        .icon(value)
        .fontSize(size)
        .lineHeight(size)
        .horizontalAlign(eui::HorizontalAlign::Center)
        .verticalAlign(eui::VerticalAlign::Center)
        .color(ink)
        .hitTestMode(eui::dsl::HitTestMode::None)
        .build();
}
components::ButtonStyle button_style(const Palette &p, bool filled = false) {
    components::ButtonStyle result;
    result.normal = filled ? p.action : eui::Color{0, 0, 0, 0};
    result.hover = filled ? p.muted : p.hover;
    result.pressed = p.selected;
    result.text = filled ? p.inverse : p.text;
    result.icon = result.text;
    result.border = {};
    result.shadow = {};
    result.radius = 7;
    result.pressScale = 1;
    return result;
}
void icon_button(eui::Ui &ui, const std::string &id, unsigned int glyph, float x, float y,
                 const Palette &p, std::function<void()> action, bool filled = false,
                 bool disabled = false) {
    components::button(ui, id)
        .position(x, y)
        .size(36, 36)
        .text("")
        .icon(glyph)
        .iconSize(17)
        .style(button_style(p, filled))
        .disabled(disabled)
        .onClick(std::move(action))
        .build();
}
void modal(eui::Ui &ui, const eui::Screen &screen, const Palette &p) {
    auto &s = state();
    if (!s.about && !s.confirm_clear)
        return;
    const float width = 460, x = (screen.width - width) / 2, y = (screen.height - 252) / 2;
    ui.stack("dialog")
        .size(screen.width, screen.height)
        .zIndex(30)
        .content([&] {
            ui.rect("dialog.scrim")
                .size(screen.width, screen.height)
                .color({0, 0, 0, 0.25f})
                .onClick([] { state().about = state().confirm_clear = false; })
                .build();
            ui.rect("dialog.panel")
                .position(x, y)
                .size(width, 252)
                .color(p.surface)
                .radius(12)
                .border(1, p.border)
                .onClick([] {})
                .build();
            const bool clearing = s.confirm_clear;
            text(ui, "dialog.title", clearing ? "清空当前对话？" : "Mirage · Mira", x + 24, y + 22,
                 width - 48, 36, 21, p.text, 600);
            ui.text("dialog.body")
                .position(x + 24, y + 67)
                .size(width - 48, 85)
                .text(clearing
                          ? "当前会话的草稿和未发送消息将被清空。其他会话会保留。"
                          : "会话通过 Runtime Service 接入 Mira 模型与任务控制。当前提供通用对话 "
                            "harness，RPA workflow 尚未接入。")
                .fontSize(17)
                .lineHeight(28)
                .wrap()
                .color(p.muted)
                .build();
            if (clearing) {
                components::button(ui, "dialog.cancel")
                    .position(x + width - 188, y + 192)
                    .size(76, 38)
                    .text("取消")
                    .fontSize(15)
                    .style(button_style(p))
                    .onClick([] { state().confirm_clear = false; })
                    .build();
            }
            components::button(ui, "dialog.confirm")
                .position(x + width - 100, y + 192)
                .size(76, 38)
                .text(clearing ? "清空" : "知道了")
                .fontSize(15)
                .style(button_style(p, true))
                .onClick([] {
                    auto &value = state();
                    if (value.confirm_clear)
                        value.chat.clear_session(value.clear_target);
                    value.about = value.confirm_clear = false;
                })
                .build();
        })
        .build();
}
void appearance_page(eui::Ui &ui, float x, float width, const Palette &p) {
    text(ui, "settings.title", "外观", x, 104, width, 48, 30, p.text, 600);
    text(ui, "settings.description", "调整 Mirage 的显示方式。", x, 160, width, 32, 17, p.muted);
    const float row_y = 232;
    const bool narrow = width < 600;
    ui.rect("settings.theme.panel")
        .position(x, row_y)
        .size(width, narrow ? 126.0f : 100.0f)
        .color(p.surface)
        .radius(12)
        .border(1, p.border)
        .build();
    text(ui, "settings.theme.label", "主题", x + 24, row_y + 16, width - 48, 30, 17, p.text, 500);
    // Narrow windows stack the control below its label instead of compressing the choices.
    const float control_x = narrow ? x + 24 : x + width - 264;
    const float control_y = narrow ? row_y + 62 : row_y + 30;
    if (!narrow) {
        text(ui, "settings.theme.description", "选择浅色或深色界面", x + 24, row_y + 50,
             width - 312, 26, 14, p.muted);
    }
    for (int i = 0; i < 2; ++i) {
        const bool dark = i == 1;
        const bool selected = state().dark == dark;
        auto style = button_style(p);
        style.normal = selected ? p.selected : p.surface;
        style.border = {1, p.border};
        components::button(ui, dark ? "settings.theme.dark" : "settings.theme.light")
            .position(control_x + static_cast<float>(i) * 124, control_y)
            .size(116, 40)
            .text(dark ? "深色" : "浅色")
            .icon(dark ? 0xf186 : 0xf185)
            .iconSize(16)
            .fontSize(16)
            .style(style)
            .onClick([dark] { state().dark = dark; })
            .build();
    }
    text(ui, "settings.theme.notice", "即时应用 · 本次运行内保留外观偏好", x,
         row_y + (narrow ? 136 : 120), width, 30, 14, p.muted);
}
std::optional<ContextUsage> native_usage(const std::optional<ipc::ContextUsage> &usage) {
    if (!usage)
        return {};
    return ContextUsage{usage->input_tokens, usage->window_tokens, usage->model};
}
void model_settings_page(eui::Ui &ui, const eui::Screen &screen, float x, float width,
                         const Palette &p, const components::theme::ThemeColorTokens &tokens) {
    auto &s = state();
    text(ui, "model.title", "模型", x, 104, width, 48, 30, p.text, 600);
    text(ui, "model.description", "配置 Mira 使用的模型服务。", x, 160, width, 32, 17, p.muted);
    components::InputStyle input_style(tokens);
    input_style.background = input_style.focused = p.surface;
    input_style.text = p.text;
    input_style.placeholder = p.muted;
    input_style.cursor = p.text;
    input_style.border = p.border;
    input_style.focusBorder = p.action;
    input_style.shadow = {};
    input_style.radius = 7;
    components::scrollView(ui, "model.form")
        .position(x, 214)
        .size(width, screen.height - 342)
        .theme(tokens)
        .gap(0)
        .offset(s.model_scroll)
        .scrollbarWidth(4)
        .scrollbarGap(12)
        .onChange([](float value) { state().model_scroll = value; })
        .content([&](eui::Ui &list, float w, float) {
            auto field = [&](const char *id, const char *label, const std::string &value,
                             const char *placeholder,
                             std::function<void(const std::string &)> change) {
                list.column(std::string(id) + ".row")
                    .width(w)
                    .height(92)
                    .gap(8)
                    .content([&] {
                        list.text(std::string(id) + ".label")
                            .size(w, 28)
                            .text(label)
                            .fontSize(16)
                            .verticalAlign(eui::VerticalAlign::Center)
                            .color(p.text)
                            .build();
                        components::input(list, id)
                            .size(w, 44)
                            .value(value)
                            .placeholder(placeholder)
                            .fontSize(16)
                            .inset(12)
                            .style(input_style)
                            .onChange([change](const std::string &next) {
                                if (!state().settings || !state().model_page || state().about ||
                                    state().saving_model || next.size() > 2048)
                                    return;
                                change(next);
                                state().model_dirty = true;
                                state().model_notice.clear();
                            })
                            .build();
                        if (auto *hit = list.find(std::string(id) + ".hit")) {
                            auto key = hit->onKeyEvent;
                            auto input = hit->onTextInput;
                            hit->onKeyEvent = [key](const eui::KeyEvent &event) {
                                if (event.key == eui::InputKey::Escape ||
                                    (event.modifiers.control && event.key == eui::InputKey::Comma))
                                    return false;
                                if (!state().settings || !state().model_page || state().about ||
                                    state().saving_model)
                                    return true;
                                return key ? key(event) : false;
                            };
                            hit->onTextInput = [input](const eui::TextInputEvent &event) {
                                if (state().settings && state().model_page && !state().about &&
                                    !state().saving_model && input)
                                    input(event);
                            };
                        }
                    })
                    .build();
            };
            field("model.endpoint", "服务地址", s.model.endpoint_origin, "https://api.example.com",
                  [](const auto &v) { state().model.endpoint_origin = v; });
            field("model.prefix", "API 路径", s.model.api_prefix, "/v1",
                  [](const auto &v) { state().model.api_prefix = v; });
            field("model.selector", "模型名称", s.model.model_selector, "供应商提供的模型 ID",
                  [](const auto &v) { state().model.model_selector = v; });
            field("model.credential", "凭据环境变量", s.model.credential_env,
                  "例如 MIRAGE_API_KEY（填写变量名）",
                  [](const auto &v) { state().model.credential_env = v; });
            field("model.window", "上下文窗口预算（Token）", s.model_window,
                  "留空表示未知；填写供应商支持的窗口大小",
                  [](const auto &v) { state().model_window = v; });
            list.column("model.protocol.row")
                .width(w)
                .height(94)
                .gap(8)
                .content([&] {
                    list.text("model.protocol.label")
                        .size(w, 28)
                        .text("协议")
                        .fontSize(16)
                        .color(p.text)
                        .build();
                    list.row("model.protocol.choices")
                        .width(w)
                        .height(44)
                        .gap(8)
                        .content([&] {
                            for (int i = 0; i < 2; ++i) {
                                const std::string dialect =
                                    i == 0 ? "openai.responses.v1" : "openai.chat-completions.v1";
                                auto style = button_style(p);
                                style.border = {1, p.border};
                                style.normal =
                                    (s.model.dialect.empty() ? i == 0 : s.model.dialect == dialect)
                                        ? p.selected
                                        : p.surface;
                                components::button(list, "model.protocol." + std::to_string(i))
                                    .size(i == 0 ? 128 : 172, 40)
                                    .text(i == 0 ? "Responses" : "Chat Completions")
                                    .fontSize(15)
                                    .style(style)
                                    .disabled(s.saving_model)
                                    .onClick([dialect] {
                                        state().model.dialect = dialect;
                                        state().model_dirty = true;
                                    })
                                    .build();
                            }
                        })
                        .build();
                })
                .build();
            list.text("model.secret.help")
                .width(w)
                .height(60)
                .text("API Key 从 Runtime Service 的环境读取，不写入配置文件。")
                .fontSize(14)
                .lineHeight(24)
                .wrap()
                .color(p.muted)
                .build();
        })
        .build();
    const float y = screen.height - 110;
    const std::string status = s.model_notice.empty() ? s.runtime_notice : s.model_notice;
    text(ui, "model.status", fitted_title(status, width, 14), x, y, width, 28, 14, p.muted);
    components::button(ui, "model.reconnect")
        .position(x, y + 38)
        .size(104, 40)
        .text("重新连接")
        .fontSize(15)
        .style(button_style(p))
        .onClick(start_runtime)
        .build();
    components::button(ui, "model.disable")
        .position(x + width - 220, y + 38)
        .size(100, 40)
        .text("停用模型")
        .fontSize(15)
        .style(button_style(p))
        .disabled(!s.model_loaded || s.saving_model)
        .onClick([] {
            auto &v = state();
            std::uint64_t window = 0;
            if (!v.model_window.empty()) {
                const auto parsed = std::from_chars(
                    v.model_window.data(), v.model_window.data() + v.model_window.size(), window);
                if (parsed.ec != std::errc{} ||
                    parsed.ptr != v.model_window.data() + v.model_window.size() ||
                    (window != 0 && (window < 2048 || window > 2000000))) {
                    v.model_notice = "窗口预算请输入 2048–2000000 的整数，或留空。";
                    return;
                }
            }
            v.model.context_window_tokens = window;
            persistence::LocalSettings settings;
            settings.model = v.model;
            settings.model->enabled = false;
            v.saving_model =
                call_runtime(ipc::SetModelRequest{persistence::encode_settings(settings)}, "save");
        })
        .build();
    components::button(ui, "model.save")
        .position(x + width - 112, y + 38)
        .size(112, 40)
        .text(s.saving_model ? "应用中…" : "保存并应用")
        .fontSize(15)
        .style(button_style(p, true))
        .disabled(!s.model_loaded || s.saving_model || s.model.endpoint_origin.empty() ||
                  s.model.model_selector.empty())
        .onClick([] {
            auto &v = state();
            std::uint64_t window = 0;
            if (!v.model_window.empty()) {
                const auto parsed = std::from_chars(
                    v.model_window.data(), v.model_window.data() + v.model_window.size(), window);
                if (parsed.ec != std::errc{} ||
                    parsed.ptr != v.model_window.data() + v.model_window.size() ||
                    (window != 0 && (window < 2048 || window > 2000000))) {
                    v.model_notice = "窗口预算请输入 2048–2000000 的整数，或留空。";
                    return;
                }
            }
            v.model.context_window_tokens = window;
            persistence::LocalSettings settings;
            settings.model = v.model;
            settings.model->enabled = true;
            settings.model->display_name = v.model.model_selector;
            if (settings.model->dialect.empty())
                settings.model->dialect = "openai.responses.v1";
            v.saving_model =
                call_runtime(ipc::SetModelRequest{persistence::encode_settings(settings)}, "save");
        })
        .build();
}
float text_height(const std::string &value, float width, float size = 16, float line = 26) {
    core::TextStyle style;
    style.text = value;
    style.fontSize = size;
    style.maxWidth = width;
    style.wrap = true;
    style.lineHeight = line;
    return std::max(line, core::TextPrimitive::measureTextSize(style).y);
}
void conversation_page(eui::Ui &ui, const eui::Screen &screen, float sidebar, const Palette &p,
                       const components::theme::ThemeColorTokens &tokens) {
    auto &s = state();
    auto &session = s.chat.current();
    const bool empty = session.messages.empty();
    const float main_width = screen.width - sidebar;
    const float column = empty ? std::min(672.0f, main_width - 48)
                               : std::min(896.0f, main_width - (main_width >= 864 ? 96 : 32));
    const float x = sidebar + (main_width - column) / 2;
    const float input_height =
        std::clamp(text_height(session.draft, column - 40, 16, 24) + 16, 48.0f, 168.0f);
    const float refs_height = session.references.empty() ? 0.0f : 36.0f;
    const float composer_height = input_height + refs_height + 56;
    const float wanted_y =
        empty ? std::max(188.0f, screen.height * .29f + 106) : screen.height - composer_height - 28;
    const float y = std::min(wanted_y, screen.height - composer_height - 28);
    const float toolbar_y = y + composer_height - 44;
    const std::string input_id = "composer.input." + std::to_string(session.id);
    const auto session_id = session.id;
    if (empty) {
        const float greeting_y = std::max(80.0f, y - 104);
        text(ui, "welcome.title", "今天，我们从哪里开始？", x, greeting_y, column, 54, 30, p.text,
             500);
        if (auto *title = ui.find("welcome.title"))
            title->horizontalAlign = eui::HorizontalAlign::Center;
        text(ui, "welcome.description", "和 Mira 一起，把想法变成下一步。", x, greeting_y + 54,
             column, 28, 14, p.muted);
        if (auto *description = ui.find("welcome.description"))
            description->horizontalAlign = eui::HorizontalAlign::Center;
    } else {
        components::MarkdownStyle markdown(tokens);
        markdown.text = markdown.heading = markdown.codeText = p.text;
        markdown.muted = p.muted;
        markdown.accent = s.dark ? color(0x80beff) : color(0x1a70b8);
        markdown.codeBackground = s.dark ? color(0x222222) : color(0xeeeeee);
        markdown.quoteBackground = s.dark ? color(0x202020) : color(0xf0f0f0);
        markdown.divider = p.border;
        markdown.bodySize = 16;
        markdown.bodyLineHeight = 26;
        markdown.h1Size = 22;
        markdown.h2Size = 20;
        markdown.h3Size = 18;
        markdown.codeSize = 14;
        markdown.blockGap = 12;
        markdown.radius = 8;
        components::scrollView(ui, "thread." + std::to_string(session_id))
            .position(x, 72)
            .size(column, std::max(48.0f, y - 88))
            .theme(tokens)
            .gap(24)
            .offset(session.scroll_offset)
            .scrollbarWidth(4)
            .scrollbarGap(8)
            .onChange([session_id](float offset) {
                auto &chat = state().chat;
                if (chat.current().id == session_id)
                    chat.current().scroll_offset = offset;
            })
            .content([&](eui::Ui &list, float width, float) {
                for (const auto &message : session.messages) {
                    const std::string key = "message." + std::to_string(message.id);
                    const bool user = message.role == "你";
                    const bool pending = message.status == "运行中";
                    const bool failed = message.status == "已停止或失败";
                    float bubble_width = width;
                    if (user) {
                        core::TextStyle measured;
                        measured.text = message.text;
                        measured.fontSize = 16;
                        bubble_width = std::min(
                            std::min(576.0f, width),
                            std::max(80.0f, core::TextPrimitive::measureTextSize(measured).x + 32));
                    }
                    const float body_width = user ? bubble_width - 32 : width - 24;
                    const float body_height =
                        user || pending || failed
                            ? text_height(message.text, body_width)
                            : std::max(26.0f, components::MarkdownBuilder::estimateHeight(
                                                  message.text, body_width, markdown));
                    const float body_y = user ? 12.0f : pending || failed ? 28.0f : 0.0f;
                    const float row_height =
                        body_height + body_y + (user ? 12 : 0) + (pending ? 0 : 36);
                    list.stack(key)
                        .size(width, row_height)
                        .content([&] {
                            const float left = user ? width - bubble_width : 0;
                            if (user)
                                list.rect(key + ".bubble")
                                    .position(left, 0)
                                    .size(bubble_width, body_height + 24)
                                    .color(s.dark ? color(0x222222) : color(0xf0f0f0))
                                    .radius(12)
                                    .border(1, p.border)
                                    .build();
                            if (pending || failed) {
                                icon(list, key + ".state.icon", pending ? 0xf141 : 0xf06a, 0, 0, 13,
                                     24, p.muted);
                                text(list, key + ".state",
                                     pending ? "Mira 正在处理" : "任务已停止或失败", 28, 0,
                                     width - 28, 24, 14, p.muted);
                            }
                            if (user || pending || failed)
                                list.text(key + ".body")
                                    .position(left + (user ? 16 : 0), body_y)
                                    .size(body_width, body_height)
                                    .text(message.text)
                                    .fontSize(16)
                                    .lineHeight(26)
                                    .wrap()
                                    .color(p.text)
                                    .hitTestMode(eui::dsl::HitTestMode::None)
                                    .build();
                            else {
                                components::markdown(list, key + ".markdown")
                                    .position(0, 0)
                                    .width(body_width)
                                    .height(body_height)
                                    .markdown(message.text)
                                    .style(markdown)
                                    .build();
                                if (auto *view = list.find(key + ".markdown"))
                                    compact_markdown_cjk(*view, message.text);
                            }
                            if (!pending) {
                                const float action_y = row_height - 32;
                                const float action_x = user ? width - 68 : 0;
                                const std::string copy = message.text;
                                const auto id = message.id;
                                auto small = button_style(p);
                                small.radius = 6;
                                components::button(list, key + ".copy")
                                    .position(action_x, action_y)
                                    .size(30, 28)
                                    .text("")
                                    .icon(0xf0c5)
                                    .iconSize(13)
                                    .style(small)
                                    .onClick([copy] {
                                        window::copy_text(copy);
                                        state().runtime_notice = "已复制消息";
                                    })
                                    .build();
                                components::button(list, key + ".quote")
                                    .position(action_x + 34, action_y)
                                    .size(30, 28)
                                    .text("")
                                    .icon(0xf10d)
                                    .iconSize(13)
                                    .style(small)
                                    .onClick([id] {
                                        if (!state().chat.reference_message(id))
                                            state().runtime_notice = state().chat.notice();
                                    })
                                    .build();
                            }
                        })
                        .build();
                }
            })
            .build();
    }
    const auto input_fill = s.dark ? color(0x2b2b2b) : p.surface;
    ui.rect("composer.panel")
        .position(x, y)
        .size(column, composer_height)
        .color(input_fill)
        .radius(16)
        .border(1, s.composer_focused ? p.muted : p.border)
        .build();
    if (!session.references.empty()) {
        auto pill = button_style(p);
        pill.normal = p.hover;
        pill.radius = 14;
        components::button(ui, "context.references")
            .position(x + 12, y + 8)
            .size(176, 28)
            .text("引用 " + std::to_string(session.references.size()) + " 条消息")
            .fontSize(12)
            .icon(0xf10d)
            .iconSize(11)
            .style(pill)
            .onClick([] { state().popup = PageState::Popup::References; })
            .build();
    }
    components::InputStyle input_style(tokens);
    input_style.background = input_style.focused = input_fill;
    input_style.border = input_style.focusBorder = {0, 0, 0, 0};
    input_style.text = p.text;
    input_style.placeholder = p.muted;
    input_style.cursor = p.text;
    input_style.shadow = {};
    input_style.radius = 4;
    components::input(ui, input_id)
        .position(x + 4, y + 4 + refs_height)
        .size(column - 8, input_height)
        .value(session.draft)
        .placeholder("向 Mira 提问，或描述一个任务…")
        .multiline()
        .scrollbar()
        .fontSize(16)
        .inset(12)
        .style(input_style)
        .onFocus([](bool focused) {
            state().composer_focused = focused;
            if (!focused)
                state().composer_composing = false;
        })
        .onChange([](const auto &value) { state().chat.set_draft(value); })
        .build();
    if (auto *hit = ui.find(input_id + ".hit")) {
        hit->border.width = 0;
        auto original = hit->onKeyEvent;
        hit->onKeyEvent = [original](const eui::KeyEvent &event) {
            if (event.key == eui::InputKey::Escape ||
                (event.modifiers.control && event.key == eui::InputKey::Comma))
                return false;
            if (state().settings || state().about || state().confirm_clear ||
                state().popup != PageState::Popup::None)
                return true;
            if (event.key == eui::InputKey::Enter && event.isDown() && !event.modifiers.shift &&
                !state().composer_composing) {
                submit_turn();
                return true;
            }
            return original ? original(event) : false;
        };
        auto original_text = hit->onTextInput;
        hit->onTextInput = [original_text](const eui::TextInputEvent &event) {
            if (state().settings || state().about || state().confirm_clear ||
                state().popup != PageState::Popup::None)
                return;
            if (event.compositionChanged || event.composing)
                state().composer_composing = event.composing;
            if (original_text)
                original_text(event);
        };
    }
    auto toggle = [](PageState::Popup popup) {
        auto &value = state();
        value.popup = value.popup == popup ? PageState::Popup::None : popup;
    };
    icon_button(ui, "composer.add", 0xf067, x + 8, toolbar_y, p,
                [toggle] { toggle(PageState::Popup::Actions); });
    auto toolbar = button_style(p);
    toolbar.radius = 6;
    components::button(ui, "composer.mode")
        .position(x + 48, toolbar_y)
        .size(84, 32)
        .text(s.agent_mode ? "Agent" : "对话")
        .icon(0xf078)
        .iconSize(9)
        .fontSize(13)
        .style(toolbar)
        .onClick([toggle] { toggle(PageState::Popup::Mode); })
        .build();
    const float model_width = std::max(72.0f, column - 244);
    components::button(ui, "composer.model")
        .position(x + 136, toolbar_y)
        .size(model_width, 32)
        .text(s.live_model.enabled ? fitted_title(s.live_model.model_selector, model_width - 24, 13)
                                   : "选择模型")
        .icon(0xf078)
        .iconSize(9)
        .fontSize(13)
        .style(toolbar)
        .onClick([toggle] { toggle(PageState::Popup::Model); })
        .build();
    const auto usage_ratio = context_ratio(session.context_usage);
    components::button(ui, "composer.context")
        .position(x + column - 88, toolbar_y)
        .size(36, 32)
        .text("")
        .style(toolbar)
        .onClick([toggle] { toggle(PageState::Popup::Context); })
        .build();
    ui.svg("composer.context.ring")
        .source(context_ring_svg(usage_ratio, s.dark))
        .position(x + column - 80, toolbar_y + 6)
        .size(20, 20)
        .build();
    icon_button(
        ui, "composer.send", session.running ? 0xf04d : 0xf062, x + column - 44, toolbar_y, p,
        [] {
            auto &value = state();
            auto &active = value.chat.current();
            if (active.running)
                call_runtime(ipc::CancelChatRequest{active.remote_id}, "cancel", active.id);
            else
                submit_turn();
        },
        true,
        session.submitting || !s.runtime || !s.runtime->connected() || session.remote_id.empty() ||
            (!session.running && s.chat.submission_text().empty()));
    const auto notice = !s.chat.notice().empty() ? s.chat.notice() : s.runtime_notice;
    text(ui, "composer.notice", fitted_title(notice, column - 168, 12), x, y + composer_height + 4,
         column - 168, 24, 12, p.muted);
    text(ui, "composer.shortcut", "Enter 发送 · Shift+Enter 换行", x + column - 168,
         y + composer_height + 4, 168, 24, 11, p.muted);
    if (s.popup != PageState::Popup::None) {
        const auto popup = s.popup;
        const float popup_width = std::min(320.0f, column);
        const float popup_height = popup == PageState::Popup::References
                                       ? std::min(352.0f, screen.height - 160)
                                   : popup == PageState::Popup::Context ? 224
                                   : popup == PageState::Popup::Model   ? 136
                                                                        : 116;
        const float popup_x = popup == PageState::Popup::Context ? x + column - popup_width : x + 8;
        const float popup_y = std::max(68.0f, y - popup_height - 8);
        ui.rect("composer.popup.dismiss")
            .size(screen.width, screen.height)
            .color({0, 0, 0, 0})
            .zIndex(20)
            .onClick([] { state().popup = PageState::Popup::None; })
            .build();
        ui.stack("composer.popup")
            .position(popup_x, popup_y)
            .size(popup_width, popup_height)
            .zIndex(21)
            .content([&] {
                ui.rect("composer.popup.panel")
                    .size(popup_width, popup_height)
                    .color(s.dark ? color(0x2b2b2b) : p.surface)
                    .radius(12)
                    .border(1, p.border)
                    .onClick([] {})
                    .build();
                auto row = [&](const std::string &id, const std::string &label, unsigned int glyph,
                               float top, std::function<void()> action, bool disabled = false) {
                    components::button(ui, "composer.popup." + id)
                        .position(8, top)
                        .size(popup_width - 16, 40)
                        .text(label)
                        .icon(glyph)
                        .iconSize(14)
                        .fontSize(14)
                        .style(button_style(p))
                        .disabled(disabled)
                        .onClick(std::move(action))
                        .build();
                };
                if (popup == PageState::Popup::Actions) {
                    const auto answer = std::find_if(
                        session.messages.rbegin(), session.messages.rend(),
                        [](const auto &message) {
                            return message.role == "Mira" && message.status == "已回复";
                        });
                    const auto id = answer == session.messages.rend() ? 0 : answer->id;
                    row(
                        "quote", "引用上一条回复", 0xf10d, 12,
                        [id] {
                            state().chat.reference_message(id);
                            state().popup = PageState::Popup::None;
                        },
                        id == 0);
                    row("new", "新建会话", 0xf067, 60, [] {
                        new_session();
                        state().popup = PageState::Popup::None;
                    });
                } else if (popup == PageState::Popup::Mode) {
                    row(
                        "agent", s.agent_mode ? "Agent  ·  已选择" : "Agent", 0xf544, 12,
                        [] {
                            state().agent_mode = true;
                            state().popup = PageState::Popup::None;
                        },
                        session.running || session.submitting);
                    row(
                        "chat", s.agent_mode ? "对话" : "对话  ·  已选择", 0xf075, 60,
                        [] {
                            state().agent_mode = false;
                            state().popup = PageState::Popup::None;
                        },
                        session.running || session.submitting);
                } else if (popup == PageState::Popup::Model) {
                    text(ui, "composer.popup.model.label", "当前模型", 16, 12, popup_width - 32, 24,
                         12, p.muted);
                    text(ui, "composer.popup.model.current",
                         fitted_title(s.live_model.enabled ? s.live_model.model_selector
                                                           : "尚未配置",
                                      popup_width - 32, 15),
                         16, 40, popup_width - 32, 28, 15, p.text, 500);
                    row("settings", "模型设置", 0xf013, 84, [] {
                        state().settings = state().model_page = true;
                        state().popup = PageState::Popup::None;
                    });
                } else if (popup == PageState::Popup::References) {
                    text(ui, "composer.popup.references.title", "引用的消息", 16, 12,
                         popup_width - 32, 28, 16, p.text, 500);
                    components::scrollView(ui, "composer.popup.references.list")
                        .position(12, 48)
                        .size(popup_width - 24, popup_height - 60)
                        .theme(tokens)
                        .gap(16)
                        .scrollbarWidth(4)
                        .scrollbarGap(6)
                        .content([&](eui::Ui &list, float width, float) {
                            for (const auto &reference : session.references) {
                                const auto key =
                                    "reference.preview." + std::to_string(reference.id);
                                const auto source_id = reference.message_id;
                                const float height = text_height(reference.text, width - 8, 14, 22);
                                list.stack(key)
                                    .size(width, height + 38)
                                    .content([&] {
                                        text(list, key + ".source",
                                             reference.role + " · 消息 #" +
                                                 std::to_string(source_id),
                                             4, 0, width - 40, 26, 12, p.muted);
                                        components::button(list, key + ".remove")
                                            .position(width - 32, 0)
                                            .size(28, 26)
                                            .text("")
                                            .icon(0xf2ed)
                                            .iconSize(12)
                                            .style(button_style(p))
                                            .onClick([source_id] {
                                                state().chat.remove_reference(source_id);
                                                if (state().chat.current().references.empty())
                                                    state().popup = PageState::Popup::None;
                                            })
                                            .build();
                                        list.text(key + ".text")
                                            .position(4, 32)
                                            .size(width - 8, height)
                                            .text(reference.text)
                                            .fontSize(14)
                                            .lineHeight(22)
                                            .color(p.text)
                                            .wrap()
                                            .hitTestMode(eui::dsl::HitTestMode::None)
                                            .build();
                                    })
                                    .build();
                            }
                        })
                        .build();
                } else {
                    text(ui, "composer.popup.context.title", "上下文", 16, 12, popup_width - 32, 28,
                         16, p.text, 500);
                    text(ui, "composer.popup.context.percent",
                         usage_ratio ? context_percent(*usage_ratio) : "未知", popup_width - 96, 12,
                         80, 28, 16, p.text, 500);
                    const auto &usage = session.context_usage;
                    const auto summary =
                        usage ? token_number(usage->input_tokens) + " / " +
                                    (usage->window_tokens ? token_number(usage->window_tokens)
                                                          : "未知") +
                                    " Token"
                              : "Token 用量尚不可用";
                    text(ui, "composer.popup.context.tokens", summary, 16, 48, popup_width - 32, 28,
                         14, p.text);
                    ui.rect("composer.popup.context.track")
                        .position(16, 88)
                        .size(popup_width - 32, 6)
                        .radius(3)
                        .color(p.border)
                        .build();
                    if (usage_ratio && *usage_ratio > 0)
                        ui.rect("composer.popup.context.fill")
                            .position(16, 88)
                            .size((popup_width - 32) *
                                      static_cast<float>(std::clamp(*usage_ratio, 0.0, 1.0)),
                                  6)
                            .radius(3)
                            .color(p.muted)
                            .build();
                    const auto note = usage_ratio ? "上次请求 · 模型输入用量"
                                      : usage     ? "请在模型设置中填写窗口预算"
                                                  : "等待模型返回 Token 用量";
                    text(ui, "composer.popup.context.source", note, 16, 106, popup_width - 32, 24,
                         13, p.muted);
                    text(ui, "composer.popup.context.model",
                         fitted_title(usage ? usage->model : s.live_model.model_selector,
                                      popup_width - 32, 13),
                         16, 134, popup_width - 32, 24, 13, p.muted);
                    ui.rect("composer.popup.context.line")
                        .position(16, 170)
                        .size(popup_width - 32, 1)
                        .color(p.border)
                        .build();
                    text(ui, "composer.popup.context.refs",
                         "本次引用 " + std::to_string(session.references.size()) + " 条消息", 16,
                         182, popup_width - 32, 24, 13, p.muted);
                }
            })
            .build();
    }
}

void resize_edges(eui::Ui &ui, const eui::Screen &screen) {
    if (window::maximized())
        return;
    struct Grip {
        const char *id;
        float x, y, w, h;
        int edges;
    };
    constexpr float edge = 5, corner = 14;
    const float w = screen.width, h = screen.height;
    const Grip grips[] = {
        {"left", 0, corner, edge, h - 2 * corner, window::left},
        {"right", w - edge, corner, edge, h - 2 * corner, window::right},
        {"top", corner, 0, w - 2 * corner, edge, window::top},
        {"bottom", corner, h - edge, w - 2 * corner, edge, window::bottom},
        {"tl", 0, 0, corner, corner, window::top | window::left},
        {"tr", w - corner, 0, corner, corner, window::top | window::right},
        {"bl", 0, h - corner, corner, corner, window::bottom | window::left},
        {"br", w - corner, h - corner, corner, corner, window::bottom | window::right},
    };
    for (const auto &grip : grips) {
        const auto edges = grip.edges;
        ui.rect(std::string("resize.") + grip.id)
            .position(grip.x, grip.y)
            .size(grip.w, grip.h)
            .color({0, 0, 0, 0})
            .zIndex(10)
            .onPress([edges](const eui::PointerEvent &, const eui::Rect &) {
                window::begin_resize(edges);
            })
            .onDrag([](const auto &) { window::resize(); })
            .build();
    }
}
} // namespace

void compose_page(eui::Ui &ui, const eui::Screen &screen) {
    drain_runtime();
    auto &s = state();
    const auto p = palette(s.dark);
    const auto tokens = s.dark ? components::theme::dark() : components::theme::light();
    const float sidebar_limit = std::max(224.0f, std::min(400.0f, screen.width - 520));
    const float sidebar = s.sidebar ? std::clamp(s.sidebar_width, 224.0f, sidebar_limit) : 0.0f;
    const float main_width = screen.width - sidebar;
    const float column_width = std::min(800.0f, main_width - 64);
    const float column_x = sidebar + (main_width - column_width) / 2;
    ui.stack("root")
        .size(screen.width, screen.height)
        .content([&] {
            ui.rect("background").size(screen.width, screen.height).color(p.background).build();
            if (s.sidebar) {
                ui.rect("sidebar.background").size(sidebar, screen.height).color(p.sidebar).build();
                ui.rect("sidebar.border")
                    .position(sidebar - 1, 0)
                    .size(1, screen.height)
                    .color(p.border)
                    .build();
                // EUI-20261004-003: metadata-free, pixel-identical UI copy.
                ui.image("brand.mira")
                    .position(24, 12)
                    .size(36, 36)
                    .source("assets/mira-ui.png")
                    .contain()
                    .hitTestMode(eui::dsl::HitTestMode::None)
                    .build();
                text(ui, "brand", "Mirage", 72, 12, sidebar - 140, 36, 22, p.text, 600);
                icon_button(ui, "sidebar.hide", 0xf0db, sidebar - 52, 12, p,
                            [] { state().sidebar = false; });
                if (s.settings) {
                    components::button(ui, "settings.back")
                        .position(16, 76)
                        .size(sidebar - 32, 44)
                        .text("返回对话")
                        .icon(0xf060)
                        .fontSize(16)
                        .iconSize(16)
                        .style(button_style(p))
                        .onClick([] { state().settings = false; })
                        .build();
                    for (int i = 0; i < 2; ++i) {
                        const bool model = i == 1;
                        const float y = 152 + static_cast<float>(i) * 56;
                        const auto key = "settings.nav." + std::to_string(i);
                        ui.rect(key)
                            .position(12, y)
                            .size(sidebar - 24, 48)
                            .radius(7)
                            .states(s.model_page == model ? p.selected : eui::Color{0, 0, 0, 0},
                                    p.hover, p.selected)
                            .focusable()
                            .cursor(eui::CursorShape::Hand)
                            .onClick([model] { state().model_page = model; })
                            .build();
                        icon(ui, key + ".icon", model ? 0xf544 : 0xf53f, 24, y, 17, 48, p.text);
                        text(ui, key + ".label", model ? "模型" : "外观", 64, y, sidebar - 88, 48,
                             16, p.text, 500);
                    }
                } else {
                    components::button(ui, "session.new")
                        .position(16, 76)
                        .size(sidebar - 32, 44)
                        .text("新建对话")
                        .icon(0xf067)
                        .fontSize(16)
                        .iconSize(16)
                        .style(button_style(p))
                        .onClick([] { new_session(); })
                        .build();
                    ui.rect("sidebar.divider")
                        .position(16, 140)
                        .size(sidebar - 32, 1)
                        .color(p.border)
                        .build();
                    text(ui, "session.label", "会话", 24, 156, 180, 28, 14, p.muted);
                    components::scrollView(ui, "session.list")
                        .position(12, 196)
                        .size(sidebar - 24, screen.height - 296)
                        .gap(4)
                        .offset(s.session_scroll)
                        .theme(tokens)
                        .scrollbarWidth(3)
                        .scrollbarGap(2)
                        .onChange([](float offset) { state().session_scroll = offset; })
                        .content([&](eui::Ui &list, float width, float) {
                            for (const auto &session : s.chat.sessions()) {
                                const auto id = session.id;
                                const bool selected = id == s.chat.current().id;
                                const std::string key = "session." + std::to_string(id);
                                list.stack(key)
                                    .size(width, 48)
                                    .content([&] {
                                        list.rect(key + ".hit")
                                            .size(width, 48)
                                            .radius(7)
                                            .states(selected ? p.selected : eui::Color{0, 0, 0, 0},
                                                    p.hover, p.selected)
                                            .cursor(eui::CursorShape::Hand)
                                            .focusable()
                                            .onClick([id] {
                                                state().chat.select_session(id);
                                                history(id);
                                            })
                                            .build();
                                        icon(list, key + ".icon", 0xf075, 12, 0, 16, 48, p.muted);
                                        text(list, key + ".title",
                                             fitted_title(session.title, width - 56, 16), 44, 0,
                                             width - 56, 48, 16, p.text);
                                    })
                                    .build();
                            }
                        })
                        .build();
                }
                ui.rect("sidebar.footer.line")
                    .position(16, screen.height - 86)
                    .size(sidebar - 32, 1)
                    .color(p.border)
                    .build();
                icon_button(ui, "about", 0xf05a, 16, screen.height - 62, p,
                            [] { state().about = true; });
                text(ui, "preview.label", "Mira", 60, screen.height - 62, sidebar - 112, 36, 14,
                     p.muted);
                icon_button(ui, "settings.open", 0xf013, sidebar - 52, screen.height - 62, p,
                            [] { state().settings = true; });
                ui.rect("sidebar.resize")
                    .position(sidebar - 4, 60)
                    .size(8, screen.height - 60)
                    .states({0, 0, 0, 0}, p.border, p.muted)
                    .zIndex(12)
                    .focusable()
                    .onHover([](bool hovered) {
                        state().sidebar_hover = hovered;
                        if (!hovered && !state().sidebar_dragging)
                            window::sidebar_resize_cursor(false);
                    })
                    .onPress([sidebar](const auto &, const eui::Rect &bounds) {
                        auto &value = state();
                        value.sidebar_drag_width = sidebar;
                        value.sidebar_drag_scale = std::max(1.0f, bounds.width / 8);
                        value.sidebar_dragging = true;
                    })
                    .onDrag([sidebar_limit](const auto &event) {
                        auto &value = state();
                        value.sidebar_width =
                            std::clamp(value.sidebar_drag_width + static_cast<float>(event.totalX) /
                                                                      value.sidebar_drag_scale,
                                       224.0f, sidebar_limit);
                    })
                    .onRelease([](const auto &, const auto &) {
                        state().sidebar_dragging = false;
                        if (!state().sidebar_hover)
                            window::sidebar_resize_cursor(false);
                    })
                    .onKeyEvent([sidebar, sidebar_limit](const eui::KeyEvent &event) {
                        if (!event.isDown() ||
                            (event.key != eui::InputKey::Left && event.key != eui::InputKey::Right))
                            return false;
                        state().sidebar_width =
                            std::clamp(sidebar + (event.key == eui::InputKey::Left ? -8 : 8),
                                       224.0f, sidebar_limit);
                        return true;
                    })
                    .cursor(eui::CursorShape::Arrow)
                    .build();
            }
            ui.rect("title.drag")
                .position(sidebar, 0)
                .size(main_width - 136, 60)
                .color({0, 0, 0, 0})
                .onPress([](const eui::PointerEvent &, const eui::Rect &) { window::begin_move(); })
                .onDrag([](const auto &) { window::move(); })
                .build();
            if (!s.sidebar) {
                icon_button(ui, "sidebar.show", 0xf0db, 16, 12, p, [] { state().sidebar = true; });
                icon_button(ui, "settings.collapsed", 0xf013, 60, 12, p,
                            [] { state().settings = true; });
            }
            if (s.settings)
                text(ui, "thread.title", fitted_title("设置", main_width - 300, 16),
                     sidebar + (s.sidebar ? 28 : 116), 0, main_width - 300, 60, 16, p.text, 500);
            icon_button(ui, "window.minimize", 0xf068, screen.width - 130, 12, p, window::minimize);
            icon_button(ui, "window.maximize", window::maximized() ? 0xf2d2 : 0xf2d0,
                        screen.width - 88, 12, p, window::toggle_maximize);
            icon_button(ui, "window.close", 0xf00d, screen.width - 46, 12, p, window::close);
            if (s.settings)
                ui.rect("header.line")
                    .position(sidebar, 60)
                    .size(main_width, 1)
                    .color(p.border)
                    .build();
            if (s.settings) {
                if (!s.sidebar) {
                    components::button(ui, "settings.back.collapsed")
                        .position(screen.width - 270, 12)
                        .size(124, 36)
                        .text("返回对话")
                        .icon(0xf060)
                        .fontSize(14)
                        .style(button_style(p))
                        .onClick([] { state().settings = false; })
                        .build();
                }
                if (s.model_page)
                    model_settings_page(ui, screen, column_x, column_width, p, tokens);
                else
                    appearance_page(ui, column_x, column_width, p);
            } else {
                conversation_page(ui, screen, sidebar, p, tokens);
            }
            resize_edges(ui, screen);
            modal(ui, screen, p);
        })
        .build();
    if (!s.sidebar || s.about || s.confirm_clear)
        s.sidebar_hover = s.sidebar_dragging = false;
    if (!window::primary_pointer_down())
        s.sidebar_dragging = false;
    window::sidebar_resize_cursor(s.sidebar && (s.sidebar_hover || s.sidebar_dragging));
}
} // namespace mirage::native_ui

namespace app {
const DslAppConfig &dslAppConfig() {
    static const DslAppConfig config =
        DslAppConfig{}
            .title("Mirage · Agent")
            .appId("org.mirage.native")
            .pageId("mirage-native")
            .windowSize(1180, 800)
            .minWindowSize(860, 620)
            .decorated(false)
            .resizable(true)
            .tray(false)
            .iconPath("assets/mira.png")
            .trayIcon("assets/mira.png")
            .showDebugStatsInTitle(false)
            .showDebugOverlay(false)
            .clearColor({0.973f, 0.973f, 0.973f, 1})
            .fonts(mirage::native_ui::text_font(), "assets/Font Awesome 7 Free-Solid-900.otf")
            .onStart([] {
                mirage::native_ui::window::initialize();
                mirage::native_ui::start_runtime();
            })
            .onShutdown([] {
                if (mirage::native_ui::state().runtime)
                    mirage::native_ui::state().runtime->shutdown();
                mirage::native_ui::window::shutdown();
            })
            .onKeyEvent([](const eui::KeyEvent &event) {
                if (event.action == eui::KeyAction::Press && event.modifiers.control &&
                    event.key == eui::InputKey::Comma) {
                    auto &s = mirage::native_ui::state();
                    if (!s.about && !s.confirm_clear) {
                        s.settings = true;
                        s.popup = mirage::native_ui::PageState::Popup::None;
                    }
                    app::requestUpdate();
                }
                if (event.action == eui::KeyAction::Press && event.modifiers.control &&
                    event.key == eui::InputKey::N) {
                    if (mirage::native_ui::state().settings || mirage::native_ui::state().about ||
                        mirage::native_ui::state().confirm_clear)
                        return;
                    mirage::native_ui::new_session();
                    app::requestUpdate();
                }
                if (event.action == eui::KeyAction::Press && event.key == eui::InputKey::Escape) {
                    auto &s = mirage::native_ui::state();
                    if (s.about || s.confirm_clear)
                        s.about = s.confirm_clear = false;
                    else if (s.popup != mirage::native_ui::PageState::Popup::None)
                        s.popup = mirage::native_ui::PageState::Popup::None;
                    else
                        s.settings = false;
                    app::requestUpdate();
                }
            });
    return config;
}
void compose(eui::Ui &ui, const eui::Screen &screen) {
    mirage::native_ui::compose_page(ui, screen);
}
} // namespace app
