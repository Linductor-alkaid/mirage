#include "chat_model.hpp"
#include "conversation_preview.hpp"
#include "markdown_adapter.hpp"
#include "runtime_bridge.hpp"
#include "secret_input.hpp"
#include "selection_adapter.hpp"
#include "typography.hpp"
#include "window_controls.hpp"
#include <mirage/runtime/persistence/settings.hpp>

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <components/markdown.h>
#include <cstdlib>
#include <eui/platform.h>
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
    TextSelection selection;
    std::uint64_t hovered_message = 0;
    std::vector<std::uint64_t> selection_cache_ids;
    bool dark = false;
    bool sidebar = true;
    bool settings = false;
    bool model_page = false;
    bool model_chooser = false;
    bool agent_mode = true;
    std::vector<mirage::runtime::persistence::ModelSettings> models;
    std::size_t model_index = 0;
    std::string api_key;
    bool show_api_key = false;
    bool remove_api_key = false;
    std::uint64_t history_epoch = 0;
    bool confirm_delete = false;
    std::uint64_t delete_target = 0;
    bool saving_model = false;
    bool model_loaded = false;
    bool model_dirty = false;
    std::unique_ptr<RuntimeBridge> runtime;
    mirage::runtime::persistence::ModelSettings model;
    mirage::runtime::persistence::ModelSettings live_model;
    std::string runtime_endpoint;
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
    enum class Popup { None, Actions, Mode, Model, Context, References, Reasoning };
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
    if (!s.about && !s.confirm_clear && !s.confirm_delete)
        s.chat.new_draft();
}
bool send_prepared(LocalSession &session) {
    return call_runtime(ipc::SessionChatRequest{session.remote_id, session.pending_prompt,
                                                state().agent_mode, session.pending_access,
                                                session.pending_reasoning, session.edit_turn_id},
                        "send", session.id);
}
void submit_turn() {
    auto &s = state();
    auto &session = s.chat.current();
    if (session.running || session.submitting || session.deleting || session.attachment_loading ||
        s.saving_model || !s.runtime || !s.runtime->connected())
        return;
    const auto prompt = s.chat.submission_text();
    if (prompt.empty()) {
        s.runtime_notice = "请输入消息，并将输入和引用合计缩短到 16 KiB 以内。";
        return;
    }
    session.pending_prompt = prompt;
    session.pending_access = session.access;
    session.pending_reasoning = s.live_model.supports_reasoning ? session.reasoning : "";
    session.submitted_text = session.draft;
    session.submitted_attachments.clear();
    for (const auto &attachment : session.attachments)
        session.submitted_attachments.push_back(attachment.id);
    session.submitted_references.clear();
    for (const auto &reference : session.references)
        session.submitted_references.push_back(reference.id);
    session.submitting = session.remote_id.empty()
                             ? call_runtime(ipc::OpenSessionRequest{}, "open_send", session.id)
                             : send_prepared(session);
    if (session.submitting) {
        s.runtime_notice = "正在提交…";
        s.popup = PageState::Popup::None;
    }
}
void delete_history() {
    auto &s = state();
    auto *target = s.chat.find(s.delete_target);
    if (!target || target->running || target->submitting || target->deleting)
        return;
    if (target->remote_id.empty())
        s.chat.delete_session(target->id);
    else
        target->deleting =
            call_runtime(ipc::DeleteSessionRequest{target->remote_id}, "delete", target->id);
    s.confirm_delete = false;
}
void start_runtime() {
    auto &s = state();
    if (const auto *endpoint = std::getenv("MIRAGE_NATIVE_SOCKET"))
        s.runtime_endpoint = endpoint;
    if (s.runtime)
        s.runtime->shutdown();
    s.runtime = std::make_unique<RuntimeBridge>([] { app::requestUpdate(); }, s.runtime_endpoint);
    s.runtime_notice = "正在连接 Runtime Service…";
}
std::string display_error(const std::string &error) {
    const std::string credential = "credential environment variable is not set: ";
    if (error.starts_with(credential))
        return "服务未设置凭据变量 " + error.substr(credential.size()) +
               "。设置该变量后重启 Runtime Service。";
    if (error == "only the latest settled turn can be replaced")
        return "最后一轮已发生变化，请取消编辑并重新选择最后一条输入。";
    if (error == "conversation replacement could not be persisted")
        return "无法保存修改，原对话已保留。请检查存储空间后重试。";
    if (error == "model task admission failed")
        return "服务繁忙，消息未提交。请稍后重试。";
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
        if (message.kind == RuntimeMessage::Kind::Attachment) {
            if (auto *target = s.chat.find(message.local_id)) {
                if (target->attachment_generation != message.attachment_generation)
                    continue;
                target->attachment_loading = false;
                if (message.attachment.attachment)
                    s.chat.attach(target->id, std::move(*message.attachment.attachment),
                                  message.attachment_generation);
                else
                    s.runtime_notice = message.attachment.error;
            }
        } else if (message.kind == RuntimeMessage::Kind::Connected) {
            s.runtime_notice = "已连接 Runtime Service";
            call_runtime(ipc::SubscribeEventsRequest{true}, "subscribe");

        } else if (message.kind == RuntimeMessage::Kind::Lost) {
            s.runtime_notice = "服务连接已断开，请在设置 → 模型中重新连接。";
            s.saving_model = false;
            for (const auto &item : s.chat.sessions()) {
                auto *session = s.chat.find(item.id);
                session->submitting = false;
                session->deleting = false;
                session->attachment_loading = false;
            }
        } else if (message.kind == RuntimeMessage::Kind::Response) {
            auto *session = s.chat.find(message.local_id);
            if (!message.response.ok) {
                s.runtime_notice = display_error(message.response.error.message);
                if (session) {
                    session->submitting = false;
                    session->deleting = false;
                }
                if (message.tag == "save" || message.tag == "discard") {
                    s.saving_model = false;
                    s.model_notice = display_error(message.response.error.message);
                }
                continue;
            }
            if (message.tag == "subscribe") {
                call_runtime(ipc::GetModelRequest{}, "model");
            }
            const auto &payload = message.response.payload;
            if (const auto *config = std::get_if<ipc::ModelConfiguration>(&payload)) {
                const auto decoded = persistence::decode_settings(config->settings_json);
                if (decoded.ok && decoded.settings.model) {
                    s.live_model = *decoded.settings.model;
                    if (!s.model_dirty || message.tag == "save" || message.tag == "discard") {
                        s.models = decoded.settings.models;
                        if (s.models.empty()) {
                            auto initial = *decoded.settings.model;
                            if (initial.display_name.empty())
                                initial.display_name = "当前服务";
                            s.models.push_back(initial);
                        }
                        s.model_index = 0;
                        for (std::size_t profile_index = 0; profile_index < s.models.size();
                             ++profile_index)
                            if (s.models[profile_index].display_name ==
                                decoded.settings.model->display_name)
                                s.model_index = profile_index;
                        s.model = *decoded.settings.model;
                        s.model_window = s.model.context_window_tokens
                                             ? std::to_string(s.model.context_window_tokens)
                                             : "";
                    }
                    s.model_loaded = true;
                    if (message.tag == "model")
                        call_runtime(ipc::ListSessionsRequest{}, "sessions");
                    if (message.tag == "save" || message.tag == "discard") {
                        s.saving_model = false;
                        s.model_dirty = false;
                        s.api_key.clear();
                        s.show_api_key = false;
                        s.remove_api_key = false;
                        s.model_notice =
                            message.tag == "discard"
                                ? "已放弃未保存的修改"
                                : (config->warning.empty() ? "已保存并应用到 Runtime Service"
                                                           : config->warning);
                    }
                }
            } else if (const auto *opened = std::get_if<ipc::SessionOpened>(&payload)) {
                if (session && message.tag == "open_send") {
                    s.chat.bind_remote(session->id, opened->session_id);
                    if (!send_prepared(*session))
                        session->submitting = false;
                }
            } else if (std::holds_alternative<ipc::SessionDeleted>(payload) &&
                       message.tag == "delete") {
                if (session)
                    s.chat.delete_session(session->id);
                ++s.history_epoch;
                call_runtime(ipc::ListSessionsRequest{}, "sessions");
                s.runtime_notice = "对话已删除。";
            } else if (const auto *sessions = std::get_if<ipc::SessionList>(&payload)) {
                ++s.history_epoch;
                std::vector<std::string> remote_ids;
                for (const auto &remote : sessions->sessions)
                    if (remote.state != "closed")
                        remote_ids.push_back(remote.id);
                s.chat.reconcile_remote_sessions(remote_ids);
                for (const auto &remote : sessions->sessions)
                    if (remote.state != "closed")
                        call_runtime(ipc::ChatHistoryRequest{remote.id, 40},
                                     "discover." + std::to_string(s.history_epoch) + "." +
                                         remote.id);
            } else if (const auto *snapshot = std::get_if<ipc::DialogHistory>(&payload)) {
                if (message.tag.starts_with("discover.")) {
                    const auto dot = message.tag.find('.', 9);
                    const auto epoch = message.tag.substr(9, dot - 9);
                    if (epoch != std::to_string(s.history_epoch))
                        continue;
                    auto known = std::find_if(
                        s.chat.sessions().begin(), s.chat.sessions().end(),
                        [&](const auto &item) { return item.remote_id == snapshot->session_id; });
                    if (snapshot->turns.empty()) {
                        if (known != s.chat.sessions().end() && !known->messages.empty() &&
                            !known->running && !known->submitting && !known->deleting)
                            s.chat.delete_session(known->id);
                        continue;
                    }
                    if (known == s.chat.sessions().end()) {
                        const auto selected = s.chat.current().id;
                        if (!s.chat.create_session())
                            continue;
                        const auto id = s.chat.current().id;
                        s.chat.bind_remote(id, snapshot->session_id);
                        session = s.chat.find(id);
                        s.chat.select_session(selected);
                    } else
                        session = s.chat.find(known->id);
                }
                if (session && !session->deleting && session->remote_id == snapshot->session_id) {
                    std::vector<std::string> ids;
                    std::uint64_t newest = 0;
                    for (const auto &turn : snapshot->turns) {
                        ids.push_back(turn.turn_id);
                        newest = std::max(newest, turn.sequence);
                    }
                    s.chat.reconcile_turns(session->id, ids, newest);
                    for (const auto &turn : snapshot->turns)
                        s.chat.apply_turn(session->id, turn.turn_id, turn.status, turn.user_text,
                                          turn.reply_text, display_error(turn.error),
                                          native_usage(turn.context_usage), turn.sequence);
                }
            } else if (std::holds_alternative<ipc::DialogTurnAccepted>(payload) &&
                       message.tag == "send") {
                if (session) {
                    const auto *accepted = std::get_if<ipc::DialogTurnAccepted>(&payload);
                    s.chat.apply_turn(session->id, accepted->turn_id, "pending",
                                      session->pending_prompt, {}, {}, {}, 0,
                                      accepted->replaces_turn_id);
                    s.chat.acknowledge_submission(session->id);
                }
                s.runtime_notice = "请求已接纳，等待 Mira 回复。";
                history(message.local_id);
            }
        } else if (message.kind == RuntimeMessage::Kind::Diagnostic) {
            s.runtime_notice = message.tag;
        } else if (message.event) {
            if (const auto *turn =
                    std::get_if<ipc::ChatTurnUpdatedEvent>(&message.event->payload)) {
                for (const auto &session : s.chat.sessions())
                    if (session.remote_id == turn->session_id) {
                        s.chat.apply_turn(session.id, turn->turn_id, turn->status, turn->user_text,
                                          turn->reply_text, display_error(turn->error),
                                          native_usage(turn->context_usage), turn->sequence,
                                          turn->replaces_turn_id);
                        s.runtime_notice = turn->status == "pending" ? "Mira 正在运行…"
                                           : turn->status == "ok"    ? "Mira 已回复"
                                                                     : display_error(turn->error);
                        break;
                    }
            } else if (const auto *preview =
                           std::get_if<ipc::ChatPreviewEvent>(&message.event->payload)) {
                for (const auto &session : s.chat.sessions())
                    if (session.remote_id == preview->session_id) {
                        s.chat.apply_preview(session.id, preview->turn_id, preview->request_id,
                                             preview->sequence, preview->text, preview->truncated);
                        break;
                    }
            } else if (std::holds_alternative<ipc::EventsOverflowEvent>(message.event->payload)) {
                history(s.chat.current().id);
            }
        }
    }
    if (s.runtime->take_gap()) {
        for (const auto &entry : s.chat.sessions())
            s.chat.find(entry.id)->attachment_loading = false;
        s.saving_model = false;
        s.model_loaded = false;
        call_runtime(ipc::GetModelRequest{}, "model");
        s.runtime_notice = "事件发生缺口，正在重新读取服务状态。";
        call_runtime(ipc::ListSessionsRequest{}, "sessions");
        history(s.chat.current().id);
    }
}
std::string text_font() {
    // DEC-040: shipped full Simplified Chinese face, independent of host fonts.
    return "assets/NotoSansSC-Regular.otf";
}
void text(eui::Ui &ui, const std::string &id, const std::string &value, float x, float y,
          float width, float height, float size, eui::Color ink, int weight = 400) {
    ui.text(id)
        .position(x, y)
        .size(width, height)
        .text(value)
        .fontSize(ui_font_size(size))
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
    style.fontSize = ui_font_size(size);
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
    auto style = button_style(p, filled);
    if (disabled) {
        style.normal = filled ? p.hover : eui::Color{0, 0, 0, 0};
        style.text = style.icon = p.muted;
    }
    components::button(ui, id)
        .position(x, y)
        .size(36, 36)
        .text("")
        .icon(glyph)
        .iconSize(17)
        .style(style)
        .disabled(disabled)
        .onClick(std::move(action))
        .build();
}
void modal(eui::Ui &ui, const eui::Screen &screen, const Palette &p) {
    auto &s = state();
    if (!s.about && !s.confirm_clear && !s.confirm_delete)
        return;
    const float height = s.confirm_delete ? 316 : 252;
    const float width = 460, x = (screen.width - width) / 2, y = (screen.height - height) / 2;
    ui.stack("dialog")
        .size(screen.width, screen.height)
        .zIndex(30)
        .content([&] {
            ui.rect("dialog.scrim")
                .size(screen.width, screen.height)
                .color({0, 0, 0, 0.25f})
                .onClick(
                    [] { state().about = state().confirm_clear = state().confirm_delete = false; })
                .build();
            ui.rect("dialog.panel")
                .position(x, y)
                .size(width, height)
                .color(p.surface)
                .radius(12)
                .border(1, p.border)
                .onClick([] {})
                .build();
            const bool deleting = s.confirm_delete;
            const bool clearing = s.confirm_clear;
            text(ui, "dialog.title",
                 deleting   ? "删除这段对话？"
                 : clearing ? "清空当前对话？"
                            : "Mirage · Mira",
                 x + 24, y + 22, width - 48, 36, 21, p.text, 600);
            if (deleting) {
                const auto *target = s.chat.find(s.delete_target);
                const auto title = target && !target->messages.empty()
                                       ? conversation_preview(target->messages.front().text)
                                       : "对话已不可用";
                const auto preview = target && !target->messages.empty()
                                         ? target->messages.back().role + "：" +
                                               conversation_preview(target->messages.back().text)
                                         : "";
                text(ui, "dialog.delete.target", fitted_title("对话：" + title, width - 48, 14),
                     x + 24, y + 67, width - 48, 26, 14, p.text);
                text(ui, "dialog.delete.preview",
                     fitted_title("最近消息 · " + preview, width - 48, 14), x + 24, y + 94,
                     width - 48, 26, 14, p.muted);
            }
            ui.text("dialog.body")
                .position(x + 24, y + (deleting ? 131 : 67))
                .size(width - 48, 85)
                .text(deleting ? "这段对话及其上下文将被删除，无法恢复。其他对话会保留。"
                      : clearing
                          ? "当前会话的草稿和未发送消息将被清空。其他会话会保留。"
                          : "会话通过 Runtime Service 接入 Mira 模型与任务控制。当前提供通用对话 "
                            "harness，RPA workflow 尚未接入。")
                .fontSize(ui_font_size(17))
                .lineHeight(28)
                .wrap()
                .color(p.muted)
                .build();
            if (clearing || deleting) {
                components::button(ui, "dialog.cancel")
                    .position(x + width - 188, y + height - 60)
                    .size(76, 38)
                    .text("取消")
                    .fontSize(ui_font_size(15))
                    .style(button_style(p))
                    .onClick([] { state().confirm_clear = state().confirm_delete = false; })
                    .build();
            }
            components::button(ui, "dialog.confirm")
                .position(x + width - 100, y + height - 60)
                .size(76, 38)
                .text(deleting   ? "删除"
                      : clearing ? "清空"
                                 : "知道了")
                .fontSize(ui_font_size(15))
                .style(button_style(p, true))
                .onClick([] {
                    auto &value = state();
                    if (value.confirm_delete)
                        delete_history();
                    else if (value.confirm_clear)
                        value.chat.clear_session(value.clear_target);
                    value.about = value.confirm_clear = value.confirm_delete = false;
                })
                .build();
        })
        .build();
}
void appearance_page(eui::Ui &ui, float x, float width, const Palette &p) {
    text(ui, "settings.title", "外观", x, 104, width, 48, 24, p.text, 600);
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
            .fontSize(ui_font_size(16))
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
void apply_model(bool enabled) {
    auto &v = state();
    if (v.about || v.confirm_clear || v.confirm_delete || v.saving_model)
        return;
    std::uint64_t window = 0;
    if (!v.model_window.empty()) {
        const auto parsed = std::from_chars(v.model_window.data(),
                                            v.model_window.data() + v.model_window.size(), window);
        if (parsed.ec != std::errc{} ||
            parsed.ptr != v.model_window.data() + v.model_window.size() ||
            (window && (window < 2048 || window > 2000000))) {
            v.model_notice = "窗口预算请输入 2048–2000000 的整数，或留空。";
            return;
        }
    }
    v.model.context_window_tokens = window;
    v.model.enabled = enabled;
    if (v.model.display_name.empty())
        v.model.display_name = v.model.model_selector;
    if (v.model.dialect.empty())
        v.model.dialect = "openai.chat-completions.v1";
    if (v.model_index < v.models.size())
        v.models[v.model_index] = v.model;
    persistence::LocalSettings document;
    document.model = v.model;
    document.models = v.models;
    v.saving_model = call_runtime(
        ipc::SetModelRequest{persistence::encode_settings(document),
                             v.remove_api_key    ? std::optional<std::string>{""}
                             : v.api_key.empty() ? std::optional<std::string>{}
                                                 : std::optional<std::string>{v.api_key}},
        "save");
}
void choose_model(std::size_t index) {
    auto &v = state();
    if (index >= v.models.size() || v.saving_model)
        return;
    persistence::LocalSettings document;
    document.model = v.models[index];
    document.model->enabled = true;
    document.models = v.models;
    v.saving_model =
        call_runtime(ipc::SetModelRequest{persistence::encode_settings(document)}, "save");
    v.popup = PageState::Popup::None;
}
void add_attachment() {
    auto &v = state();
    auto &session = v.chat.current();
    if (session.attachment_loading || session.attachments.size() >= 4 || !v.runtime)
        return;
    const auto result = eui::platform::openFileDialog(
        {"添加文本附件",
         {".txt", ".md", ".json", ".csv", ".cpp", ".hpp", ".py", ".log"},
         "",
         "UTF-8 文本"});
    if (result.status == eui::platform::FileDialogStatus::Failed) {
        v.runtime_notice = "无法打开文件选择器，请安装 zenity 或 kdialog 后重试。";
        return;
    }
    if (result.selected() && !result.paths.empty()) {
        session.attachment_loading = v.runtime->load_attachment(result.paths.front(), session.id,
                                                                session.attachment_generation);
        if (!session.attachment_loading)
            v.runtime_notice = "附件任务已达上限，请稍后重试。";
    }
    v.popup = PageState::Popup::None;
}
void model_settings_page(eui::Ui &ui, const eui::Screen &screen, float x, float width,
                         const Palette &p, const components::theme::ThemeColorTokens &tokens) {
    auto &s = state();
    text(ui, "model.title", "模型服务", x, 90, width, 48, 24, p.text, 600);
    text(ui, "model.description", "连接服务商，管理 Agent 使用的模型。", x, 140, width - 136, 32,
         15, p.muted);
    components::button(ui, "model.add.provider")
        .position(x + width - 124, 138)
        .size(124, 36)
        .text("添加服务商")
        .icon(0xf067)
        .fontSize(ui_font_size(14))
        .style(button_style(p))
        .disabled(s.saving_model || s.models.size() >= 12 || s.model_dirty)
        .onClick([] {
            auto &v = state();
            persistence::ModelSettings profile;
            if (v.about || v.confirm_clear || v.confirm_delete)
                return;
            v.model_chooser = false;
            profile.display_name = "自定义服务 " + std::to_string(v.models.size() + 1);
            profile.endpoint_origin = "https://api.openai.com";
            profile.api_prefix = "/v1";
            profile.dialect = "openai.chat-completions.v1";
            v.models.push_back(profile);
            v.model_index = v.models.size() - 1;
            v.model = profile;
            v.api_key.clear();
            v.show_api_key = false;
            v.remove_api_key = false;
            v.model_window.clear();
            v.model_scroll = 0;
            v.model_dirty = true;
        })
        .build();
    const bool compact = width < 700;
    const float nav_width = compact ? 0 : 176;
    const float chooser_x = x + 16;
    const float chooser_width = width - 32;
    const float panel_y = 194, panel_height = screen.height - 334;
    ui.rect("model.panel")
        .position(x, panel_y)
        .size(width, panel_height)
        .radius(12)
        .border(1, p.border)
        .color(p.surface)
        .build();
    if (!compact) {
        ui.rect("model.panel.divider")
            .position(x + nav_width, panel_y)
            .size(1, panel_height)
            .color(p.border)
            .build();
        components::scrollView(ui, "model.providers")
            .position(x + 8, panel_y + 12)
            .size(nav_width - 16, panel_height - 24)
            .theme(tokens)
            .gap(6)
            .content([&](eui::Ui &list, float w, float) {
                for (std::size_t i = 0; i < s.models.size(); ++i) {
                    auto style = button_style(p);
                    style.normal = i == s.model_index ? p.selected : p.surface;
                    components::button(list, "model.provider." + std::to_string(i))
                        .size(w, 44)
                        .text(nav_width < 100 ? ""
                                              : fitted_title(s.models[i].display_name, w - 40, 14))
                        .icon(0xf1b2)
                        .iconSize(16)
                        .fontSize(ui_font_size(14))
                        .style(style)
                        .disabled(s.saving_model || s.model_dirty)
                        .onClick([i] {
                            auto &v = state();
                            if (v.about || v.confirm_clear || v.confirm_delete)
                                return;
                            v.model_index = i;
                            v.model = v.models[i];
                            v.api_key.clear();
                            v.show_api_key = false;
                            v.remove_api_key = false;
                            v.model_window = v.model.context_window_tokens
                                                 ? std::to_string(v.model.context_window_tokens)
                                                 : "";
                            v.model_scroll = 0;
                        })
                        .build();
                }
            })
            .build();
    }
    x += nav_width + 24;
    width -= nav_width + 48;
    if (compact) {
        components::button(ui, "model.provider.selector")
            .position(x, panel_y + 12)
            .size(width, 40)
            .text(fitted_title(s.model.display_name.empty() ? "当前服务" : s.model.display_name,
                               width - 48, 18))
            .icon(0xf078)
            .iconSize(14)
            .fontSize(ui_font_size(18))
            .style(button_style(p))
            .disabled(s.saving_model || s.model_dirty)
            .onClick([] {
                auto &v = state();
                if (!v.about && !v.confirm_clear && !v.confirm_delete)
                    v.model_chooser = !v.model_chooser;
            })
            .build();
    } else {
        text(ui, "model.provider.title",
             fitted_title(s.model.display_name.empty() ? "当前服务" : s.model.display_name, width,
                          20),
             x, panel_y + 16, width, 32, 20, p.text, 600);
    }
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
        .position(x, 250)
        .size(width, screen.height - 402)
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
                            .fontSize(ui_font_size(16))
                            .verticalAlign(eui::VerticalAlign::Center)
                            .color(p.text)
                            .build();
                        components::input(list, id)
                            .size(w, 44)
                            .value(value)
                            .placeholder(placeholder)
                            .fontSize(ui_font_size(16))
                            .inset(12)
                            .style(input_style)
                            .onChange([change](const std::string &next) {
                                if (!state().settings || !state().model_page || state().about ||
                                    state().confirm_clear || state().confirm_delete ||
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
                                    state().confirm_clear || state().confirm_delete ||
                                    state().saving_model)
                                    return true;
                                return key ? key(event) : false;
                            };
                            hit->onTextInput = [input](const eui::TextInputEvent &event) {
                                if (state().settings && state().model_page && !state().about &&
                                    !state().confirm_clear && !state().confirm_delete &&
                                    !state().saving_model && input)
                                    input(event);
                            };
                        }
                    })
                    .build();
            };
            field("model.name", "服务商名称", s.model.display_name, "例如 SiliconFlow",
                  [](const auto &v) { state().model.display_name = v; });
            field("model.endpoint", "服务地址", s.model.endpoint_origin, "https://api.example.com",
                  [](const auto &v) { state().model.endpoint_origin = v; });
            field("model.prefix", "API 路径", s.model.api_prefix, "/v1",
                  [](const auto &v) { state().model.api_prefix = v; });
            field("model.selector", "模型名称", s.model.model_selector, "供应商提供的模型 ID",
                  [](const auto &v) { state().model.model_selector = v; });
            list.column("model.key.row")
                .width(w)
                .height(92)
                .gap(8)
                .content([&] {
                    list.stack("model.key.label.row")
                        .size(w, 28)
                        .content([&] {
                            text(list, "model.key.label", "API Key", 0, 0, w - 70, 28, 16, p.text);
                            if (s.model.api_key_configured || s.remove_api_key)
                                components::button(list, "model.key.remove")
                                    .position(w - 64, 0)
                                    .size(64, 28)
                                    .text(s.remove_api_key ? "已移除" : "移除")
                                    .fontSize(ui_font_size(13))
                                    .style(button_style(p))
                                    .disabled(s.saving_model)
                                    .onClick([] {
                                        auto &v = state();
                                        if (v.about || v.confirm_clear || v.confirm_delete)
                                            return;
                                        v.api_key.clear();
                                        v.remove_api_key = true;
                                        v.model_dirty = true;
                                    })
                                    .build();
                        })
                        .build();
                    list.stack("model.key.control")
                        .size(w, 44)
                        .content([&] {
                            secret_input(list, "model.key", w - 44, s.api_key, s.show_api_key,
                                         s.remove_api_key ? "保存后移除 API Key"
                                         : s.model.api_key_configured ? "已配置；填写新 Key 可替换"
                                                                      : "输入 API Key",
                                         input_style, [](const auto &key) {
                                             auto &v = state();
                                             if (v.about || v.confirm_clear || v.confirm_delete ||
                                                 v.saving_model)
                                                 return;
                                             v.api_key = key;
                                             v.remove_api_key = false;
                                             v.model_dirty = true;
                                             v.model_notice.clear();
                                         });
                            if (auto *hit = list.find("model.key.hit")) {
                                auto key = hit->onKeyEvent;
                                auto input = hit->onTextInput;
                                hit->onKeyEvent = [key](const eui::KeyEvent &event) {
                                    if (event.key == eui::InputKey::Escape ||
                                        (event.modifiers.control &&
                                         event.key == eui::InputKey::Comma))
                                        return false;
                                    const auto &v = state();
                                    if (!v.settings || !v.model_page || v.about ||
                                        v.confirm_clear || v.confirm_delete || v.saving_model)
                                        return true;
                                    return key ? key(event) : false;
                                };
                                hit->onTextInput = [input](const eui::TextInputEvent &event) {
                                    const auto &v = state();
                                    if (v.settings && v.model_page && !v.about &&
                                        !v.confirm_clear && !v.confirm_delete && !v.saving_model &&
                                        input)
                                        input(event);
                                };
                            }
                            icon_button(
                                list, "model.key.visible", s.show_api_key ? 0xf070 : 0xf06e, w - 40,
                                4, p,
                                [] {
                                    auto &v = state();
                                    if (!v.about && !v.confirm_clear && !v.confirm_delete)
                                        v.show_api_key = !v.show_api_key;
                                },
                                false, s.saving_model);
                        })
                        .build();
                })
                .build();
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
                        .fontSize(ui_font_size(16))
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
                                    .fontSize(ui_font_size(15))
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
            components::button(list, "model.reasoning.support")
                .size(w, 40)
                .text(s.model.supports_reasoning ? "思考深度 · 已启用 reasoning_effort"
                                                 : "启用思考深度（reasoning_effort）")
                .icon(s.model.supports_reasoning ? 0xf14a : 0xf0c8)
                .iconSize(15)
                .fontSize(ui_font_size(14))
                .style(button_style(p))
                .disabled(s.saving_model)
                .onClick([] {
                    auto &v = state();
                    if (v.about || v.confirm_clear || v.confirm_delete)
                        return;
                    v.model.supports_reasoning = !v.model.supports_reasoning;
                    v.model_dirty = true;
                })
                .build();
            list.text("model.reasoning.help")
                .width(w)
                .height(64)
                .text("仅在该模型支持 reasoning_effort 时启用。供应商拒绝参数会显示错误。")
                .fontSize(ui_font_size(13))
                .lineHeight(22)
                .wrap()
                .color(p.muted)
                .build();
            list.text("model.secret.help")
                .width(w)
                .height(60)
                .text("API Key 保存在系统钥匙环中。留空保留现有 Key，点击移除后保存可清除。")
                .fontSize(ui_font_size(14))
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
        .size(width < 480 ? 36 : 104, 40)
        .text(width < 480 ? "" : "重新连接")
        .icon(width < 480 ? 0xf021 : 0)
        .iconSize(16)
        .fontSize(ui_font_size(15))
        .style(button_style(p))
        .onClick(start_runtime)
        .build();
    components::button(ui, "model.discard")
        .position(x + width - 308, y + 38)
        .size(80, 40)
        .text("取消修改")
        .fontSize(ui_font_size(14))
        .style(button_style(p))
        .disabled(!s.model_dirty || s.saving_model)
        .onClick([] {
            auto &v = state();
            if (v.about || v.confirm_clear || v.confirm_delete)
                return;
            v.model_dirty = false;
            v.saving_model = call_runtime(ipc::GetModelRequest{}, "discard");
        })
        .build();
    components::button(ui, "model.disable")
        .position(x + width - 220, y + 38)
        .size(100, 40)
        .text("停用模型")
        .fontSize(ui_font_size(15))
        .style(button_style(p))
        .disabled(!s.model_loaded || s.saving_model)
        .onClick([] { apply_model(false); })
        .build();
    components::button(ui, "model.save")
        .position(x + width - 112, y + 38)
        .size(112, 40)
        .text(s.saving_model ? "应用中…" : "保存并应用")
        .fontSize(ui_font_size(15))
        .style(button_style(p, true))
        .disabled(!s.model_loaded || s.saving_model || s.model.endpoint_origin.empty() ||
                  s.model.model_selector.empty())
        .onClick([] { apply_model(true); })
        .build();
    if (compact && s.model_chooser) {
        const float chooser_height = std::min(std::max(88.0f, screen.height - 390),
                                              16.0f + 50.0f * static_cast<float>(s.models.size()));
        ui.rect("model.chooser.dismiss")
            .size(screen.width, screen.height)
            .color({0, 0, 0, 0})
            .zIndex(20)
            .onClick([] { state().model_chooser = false; })
            .build();
        ui.stack("model.chooser")
            .position(chooser_x, panel_y + 58)
            .size(chooser_width, chooser_height)
            .zIndex(21)
            .content([&] {
                ui.rect("model.chooser.panel")
                    .size(chooser_width, chooser_height)
                    .color(p.surface)
                    .radius(10)
                    .border(1, p.border)
                    .onClick([] {})
                    .build();
                components::scrollView(ui, "model.chooser.list")
                    .position(8, 8)
                    .size(chooser_width - 16, chooser_height - 16)
                    .theme(tokens)
                    .gap(6)
                    .content([&](eui::Ui &list, float w, float) {
                        for (std::size_t i = 0; i < s.models.size(); ++i) {
                            components::button(list, "model.chooser.option." + std::to_string(i))
                                .size(w, 44)
                                .text(fitted_title(s.models[i].display_name, w - 44, 14))
                                .icon(i == s.model_index ? 0xf00c : 0xf1b2)
                                .iconSize(16)
                                .fontSize(ui_font_size(14))
                                .style(button_style(p))
                                .disabled(s.saving_model || s.model_dirty)
                                .onClick([i] {
                                    auto &v = state();
                                    if (v.about || v.confirm_clear || v.confirm_delete)
                                        return;
                                    v.model_index = i;
                                    v.model = v.models[i];
                                    v.api_key.clear();
                                    v.show_api_key = false;
                                    v.remove_api_key = false;
                                    v.model_window =
                                        v.model.context_window_tokens
                                            ? std::to_string(v.model.context_window_tokens)
                                            : "";
                                    v.model_scroll = 0;
                                    v.model_chooser = false;
                                })
                                .build();
                        }
                    })
                    .build();
            })
            .build();
    }
}
float text_height(const std::string &value, float width, float size = 16, float line = 26) {
    core::TextStyle style;
    style.text = value;
    style.fontSize = ui_font_size(size);
    style.maxWidth = width;
    style.wrap = true;
    style.lineHeight = line;
    return std::max(line, core::TextPrimitive::measureTextSize(style).y);
}
void conversation_page(eui::Ui &ui, const eui::Screen &screen, float sidebar, const Palette &p,
                       const components::theme::ThemeColorTokens &tokens) {
    auto &s = state();
    auto &session = s.chat.current();
    for (const auto cached : s.selection_cache_ids)
        if (std::none_of(session.messages.begin(), session.messages.end(),
                         [cached](const auto &message) { return message.id == cached; }))
            ui.releaseStateScope("message." + std::to_string(cached) + ".text.geometry");
    s.selection_cache_ids.clear();
    for (const auto &message : session.messages)
        s.selection_cache_ids.push_back(message.id);
    const bool empty = session.messages.empty();
    if (s.selection.session != session.id || s.settings || s.about || s.confirm_clear ||
        s.confirm_delete || s.popup != PageState::Popup::None ||
        std::none_of(session.messages.begin(), session.messages.end(),
                     [&](const auto &message) { return message.id == s.selection.message; }))
        s.selection.clear();
    const float main_width = screen.width - sidebar;
    const float column = empty ? std::min(672.0f, main_width - 48)
                               : std::min(800.0f, main_width - (main_width >= 864 ? 96 : 48));
    const float x = sidebar + (main_width - column) / 2;
    const float input_height = std::clamp(
        text_height(session.draft, column - 40, 14, ui_input_line_height(14)) + 24, 50.0f, 168.0f);
    const float refs_height =
        session.references.empty() && session.attachments.empty() ? 0.0f : 36.0f;
    const float edit_height = session.edit_turn_id.empty() ? 0 : 32;
    const float composer_height = input_height + refs_height + edit_height + 56;
    const float wanted_y =
        empty ? std::max(188.0f, screen.height * .29f + 106) : screen.height - composer_height - 28;
    const float y = std::min(wanted_y, screen.height - composer_height - 28);
    const float toolbar_y = y + composer_height - 44;
    const std::string input_id = "composer.input." + std::to_string(session.id);
    const auto session_id = session.id;
    if (empty) {
        const float greeting_y = std::max(80.0f, y - 104);
        text(ui, "welcome.title", "今天，我们从哪里开始？", x, greeting_y, column, 54, 26, p.text,
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
        markdown.bodySize = ui_font_size(14);
        markdown.bodyLineHeight = 22;
        markdown.h1Size = ui_font_size(18);
        markdown.h2Size = ui_font_size(16);
        markdown.h3Size = ui_font_size(15);
        markdown.codeSize = 13;
        markdown.blockGap = 8;
        markdown.radius = 8;
        components::scrollView(ui, "thread." + std::to_string(session_id))
            .position(x, 72)
            .size(column, std::max(48.0f, y - 88))
            .theme(tokens)
            .gap(14)
            .offset(session.follow_output ? 10000000.0f : session.scroll_offset)
            .scrollbarWidth(4)
            .scrollbarGap(8)
            .onChange([session_id](float offset) {
                auto &chat = state().chat;
                if (chat.current().id == session_id) {
                    chat.current().scroll_offset = offset;
                    const bool follow = offset >= chat.current().scroll_extent - 1;
                    if (follow != chat.current().follow_output)
                        app::requestUpdate();
                    chat.current().follow_output = follow;
                    state().selection.clear();
                }
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
                        measured.fontSize = ui_font_size(14);
                        bubble_width = std::min(
                            std::min(576.0f, width),
                            std::max(80.0f, core::TextPrimitive::measureTextSize(measured).x + 24));
                    }
                    const float body_width = user ? bubble_width - 24 : width - 16;
                    const float body_height =
                        pending && message.text.empty() ? 0
                        : user || failed
                            ? text_height(message.text, body_width, 14, 22)
                            : std::max(22.0f, components::MarkdownBuilder::estimateHeight(
                                                  message.text, body_width, markdown));
                    const float elapsed =
                        message.started
                            ? static_cast<float>(
                                  (message.duration.value_or(
                                       std::chrono::duration_cast<std::chrono::milliseconds>(
                                           std::chrono::steady_clock::now() - *message.started)))
                                      .count()) /
                                  1000.0f
                            : 0;
                    const auto tenths =
                        static_cast<unsigned long long>(std::max(0.0f, elapsed) * 10);
                    const std::string elapsed_label =
                        std::to_string(tenths / 10) + "." + std::to_string(tenths % 10) + " 秒";
                    const float body_y = user ? 8.0f : pending || failed ? 28.0f : 0.0f;
                    const float row_height =
                        body_height + body_y + (user ? 8 : 0) + (pending ? 4 : 28);
                    list.stack(key)
                        .size(width, row_height)
                        .content([&] {
                            const float left = user ? width - bubble_width : 0;
                            if (user)
                                list.rect(key + ".bubble")
                                    .position(left, 0)
                                    .size(bubble_width, body_height + 16)
                                    .color(s.dark ? color(0x222222) : color(0xf0f0f0))
                                    .radius(10)
                                    .build();
                            if (pending || failed) {
                                if (pending) {
                                    // One restrained travelling pulse; no simulated text reveal.
                                    for (int dot = 0; dot < 3; ++dot) {
                                        const float phase =
                                            elapsed * 5 - static_cast<float>(dot) * 1.6f;
                                        list.rect(key + ".activity." + std::to_string(dot))
                                            .position(static_cast<float>(dot) * 6, 10)
                                            .size(3, 3)
                                            .radius(1.5f)
                                            .color(p.muted)
                                            .opacity(0.3f + 0.7f * (0.5f + 0.5f * std::sin(phase)))
                                            .hitTestMode(eui::dsl::HitTestMode::None)
                                            .build();
                                    }
                                } else
                                    icon(list, key + ".state.icon", 0xf06a, 0, 0, 13, 24, p.muted);
                                text(list, key + ".state",
                                     pending
                                         ? (message.text.empty() ? "思考中 · " : "正在回复 · ") +
                                               elapsed_label
                                         : "任务已停止或失败",
                                     28, 0, width - 28, 24, 12, p.muted);
                            }
                            if (user || failed)
                                list.text(key + ".body")
                                    .position(left + (user ? 12 : 0), body_y)
                                    .size(body_width, body_height)
                                    .text(message.text)
                                    .fontSize(ui_font_size(14))
                                    .lineHeight(22)
                                    .wrap()
                                    .color(p.text)
                                    .hitTestMode(eui::dsl::HitTestMode::None)
                                    .build();
                            else {
                                components::markdown(list, key + ".markdown")
                                    .position(0, body_y)
                                    .width(body_width)
                                    .height(body_height)
                                    .markdown(message.text)
                                    .style(markdown)
                                    .build();
                                if (auto *view = list.find(key + ".markdown"))
                                    compact_markdown_cjk(*view, message.text);
                            }
                            if (!pending) {
                                const auto id = message.id;
                                auto &cache = list.state<SelectionCache>(key + ".text.geometry");
                                if (!cache.rendered)
                                    cache.rendered = std::make_shared<SelectionText>();
                                const auto rendered = cache.rendered;
                                // Layout frames are unscrolled; the press bounds include the
                                // rendered scroll transform. The overlay covers only the body.
                                list.rect(key + ".select")
                                    .position(left + (user ? 12 : 0), body_y)
                                    .size(body_width, body_height)
                                    .color({0, 0, 0, 0})
                                    .onHover([id](bool entered) {
                                        auto &page = state();
                                        if (entered)
                                            page.hovered_message = id;
                                        else if (page.hovered_message == id)
                                            page.hovered_message = 0;
                                    })
                                    .onPress([rendered, id, session_id, width = body_width, &list,
                                              key, plain = user || failed](const auto &event,
                                                                           const auto &bounds) {
                                        *rendered = {};
                                        if (const auto *body =
                                                list.find(key + (plain ? ".body" : ".markdown")))
                                            collect_selection_text(*body, *rendered);
                                        auto &selection = state().selection;
                                        selection.clear();
                                        selection.session = session_id;
                                        selection.message = id;
                                        selection.scale = bounds.width / width;
                                        if (const auto *target = list.find(key + ".select")) {
                                            selection.origin_x =
                                                bounds.x - target->frame.x * selection.scale;
                                            selection.origin_y =
                                                bounds.y - target->frame.y * selection.scale;
                                        }
                                        if (const auto *row = list.find(key)) {
                                            selection.row_x = row->frame.x;
                                            selection.row_y = row->frame.y;
                                        }
                                        selection.anchor = selection.caret = rendered->hit(
                                            (event.x - selection.origin_x) / selection.scale,
                                            (event.y - selection.origin_y) / selection.scale);
                                        selection.dragging = true;
                                    })
                                    .onDrag([rendered, id](const auto &event) {
                                        auto &selection = state().selection;
                                        if (selection.dragging && selection.message == id)
                                            selection.caret = rendered->hit(
                                                (event.x - selection.origin_x) / selection.scale,
                                                (event.y - selection.origin_y) / selection.scale);
                                    })
                                    .onRelease([rendered, id](const auto &event, const auto &) {
                                        auto &selection = state().selection;
                                        if (selection.message != id)
                                            return;
                                        if (event.action == eui::PointerAction::Cancel) {
                                            selection.clear();
                                            return;
                                        }
                                        selection.caret = rendered->hit(
                                            (event.x - selection.origin_x) / selection.scale,
                                            (event.y - selection.origin_y) / selection.scale);
                                        selection.finish(*rendered);
                                        selection.popup_x = event.x / selection.scale;
                                        selection.popup_y = event.y / selection.scale;
                                    })
                                    .onKeyEvent([](const auto &event) {
                                        auto &selection = state().selection;
                                        if (!event.isDown())
                                            return false;
                                        if (event.key == eui::InputKey::Escape) {
                                            selection.clear();
                                            return true;
                                        }
                                        if (event.modifiers.control &&
                                            event.key == eui::InputKey::C &&
                                            !selection.excerpt.empty()) {
                                            window::copy_text(selection.excerpt);
                                            return true;
                                        }
                                        return false;
                                    })
                                    .build();
                                const auto &selection = s.selection;
                                if (selection.message == id && selection.session == session_id) {
                                    const auto begin = std::min(selection.anchor, selection.caret);
                                    const auto end = std::max(selection.anchor, selection.caret);
                                    for (std::size_t i = 0; i < rendered->lines.size(); ++i) {
                                        const auto &line = rendered->lines[i];
                                        if (line.start >= end ||
                                            line.start + line.text.size() <= begin)
                                            continue;
                                        float start_x = 0, end_x = 0;
                                        for (std::size_t j = 0;
                                             j < std::min(line.bytes.size(), line.carets.size());
                                             ++j) {
                                            const auto offset =
                                                line.start +
                                                static_cast<std::size_t>(line.bytes[j]);
                                            if (offset <= begin)
                                                start_x = line.carets[j];
                                            if (offset <= end)
                                                end_x = line.carets[j];
                                        }
                                        list.rect(key + ".selection." + std::to_string(i))
                                            .position(line.x - selection.row_x + start_x,
                                                      line.y - selection.row_y)
                                            .size(std::max(0.0f, end_x - start_x), line.height)
                                            .color(s.dark ? eui::Color{.5f, .7f, 1, .25f}
                                                          : eui::Color{.1f, .4f, .8f, .18f})
                                            .hitTestMode(eui::dsl::HitTestMode::None)
                                            .build();
                                    }
                                }
                                const float action_y = row_height - 26;
                                if (!user && message.duration)
                                    text(list, key + ".duration", "用时 " + elapsed_label, 38,
                                         action_y, width - 38, 26, 11, p.muted);
                                const bool can_edit =
                                    user && session.messages.size() >= 2 &&
                                    &message == &session.messages[session.messages.size() - 2];
                                const float action_x = user ? width - (can_edit ? 60 : 28) : 0;
                                auto small = button_style(p);
                                small.radius = 5;
                                small.icon = p.muted;
                                const bool visible = s.hovered_message == id;
                                auto wire_action = [&](const std::string &action) {
                                    if (auto *button = list.find(action)) {
                                        button->opacity =
                                            visible || list.isFocused(action + ".bg") ? 1 : 0;
                                    }
                                    if (auto *button = list.find(action + ".bg")) {
                                        button->focusable = true;
                                        button->onHoverChanged = [id](bool entered) {
                                            auto &page = state();
                                            if (entered)
                                                page.hovered_message = id;
                                            else if (page.hovered_message == id)
                                                page.hovered_message = 0;
                                        };
                                        if (list.isFocused(action + ".bg"))
                                            button->border = {1, p.muted};
                                    }
                                };
                                list.rect(key + ".actions.hit")
                                    .position(action_x, action_y)
                                    .size(can_edit ? 60 : 28, 26)
                                    .color({0, 0, 0, 0})
                                    .onHover([id](bool entered) {
                                        auto &page = state();
                                        if (entered)
                                            page.hovered_message = id;
                                        else if (page.hovered_message == id)
                                            page.hovered_message = 0;
                                    })
                                    .build();
                                components::button(list, key + ".copy")
                                    .position(action_x, action_y)
                                    .size(28, 26)
                                    .text("")
                                    .icon(0xf0c5)
                                    .iconSize(12)
                                    .style(small)
                                    .onClick([copy = message.text] {
                                        window::copy_text(copy);
                                        state().runtime_notice = "已复制消息";
                                    })
                                    .build();
                                wire_action(key + ".copy");
                                if (can_edit) {
                                    components::button(list, key + ".edit")
                                        .position(action_x + 32, action_y)
                                        .size(28, 26)
                                        .text("")
                                        .icon(0xf303)
                                        .iconSize(12)
                                        .style(small)
                                        .disabled(session.running || session.submitting ||
                                                  !session.edit_turn_id.empty())
                                        .onClick([id] {
                                            if (state().chat.edit_last_input(id)) {
                                                state().selection.clear();
                                                state().runtime_notice =
                                                    "编辑后重发将替换最后一轮对话";
                                            }
                                        })
                                        .build();
                                    wire_action(key + ".edit");
                                }
                            }
                        })
                        .build();
                }
            })
            .build();
        if (const auto *thread = ui.find("thread." + std::to_string(session_id)))
            session.scroll_extent = thread->scrollMaxOffset;
        if (!session.follow_output && session.scroll_extent > 1) {
            auto latest = button_style(p);
            latest.normal = p.surface;
            latest.border = {1, p.border};
            auto follow = [] {
                state().chat.current().follow_output = true;
                state().chat.current().scroll_offset = 10000000.0f;
                app::requestUpdate();
            };
            components::button(ui, "thread.latest")
                .position(x + column / 2 - 18, y - 58)
                .size(36, 36)
                .text("")
                .icon(0xf063)
                .iconSize(14)
                .style(latest)
                .onClick(follow)
                .build();
            if (auto *hit = ui.find("thread.latest.bg")) {
                hit->focusable = true;
                hit->onKeyEvent = [follow](const eui::KeyEvent &event) {
                    if (!event.isDown() ||
                        (event.key != eui::InputKey::Enter && event.key != eui::InputKey::Space))
                        return false;
                    follow();
                    return true;
                };
            }
        }
    }
    const auto input_fill = s.dark ? color(0x2b2b2b) : p.surface;
    ui.rect("composer.panel")
        .position(x, y)
        .size(column, composer_height)
        .color(input_fill)
        .radius(16)
        .border(1, s.composer_focused ? p.muted : p.border)
        .build();
    if (!session.references.empty() || !session.attachments.empty()) {
        auto pill = button_style(p);
        pill.normal = p.hover;
        pill.radius = 14;
        components::button(ui, "context.references")
            .position(x + 12, y + 8)
            .size(176, 28)
            .text("附件 " + std::to_string(session.attachments.size()) + " · 引用 " +
                  std::to_string(session.references.size()))
            .fontSize(ui_font_size(12))
            .icon(0xf10d)
            .iconSize(11)
            .style(pill)
            .onClick([] { state().popup = PageState::Popup::References; })
            .build();
    }
    if (edit_height) {
        text(ui, "composer.edit.label", "编辑最后一条输入 · 重发后替换上一轮", x + 16,
             y + refs_height + 6, column - 88, 24, 12, p.muted);
        components::button(ui, "composer.edit.cancel")
            .position(x + column - 68, y + refs_height + 4)
            .size(56, 26)
            .text("取消")
            .fontSize(ui_font_size(12))
            .style(button_style(p))
            .disabled(session.submitting || session.running)
            .onClick([] { state().chat.cancel_edit(); })
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
        .position(x + 4, y + 4 + refs_height + edit_height)
        .size(column - 8, input_height)
        .value(session.draft)
        .placeholder("向 Mira 提问，或描述一个任务…")
        .multiline()
        .scrollbar()
        .fontSize(ui_font_size(14))
        .inset(12)
        .style(input_style)
        .onFocus([](bool focused) {
            state().composer_focused = focused;
            if (focused)
                state().selection.clear();
            else
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
                state().confirm_delete || state().popup != PageState::Popup::None)
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
                state().confirm_delete || state().popup != PageState::Popup::None)
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
        .size(110, 32)
        .text(session.access == "read_only" ? "只读模式" : "默认权限")
        .icon(0xf078)
        .iconSize(9)
        .fontSize(ui_font_size(13))
        .style(toolbar)
        .onClick([toggle] { toggle(PageState::Popup::Mode); })
        .build();
    const std::string model_label = s.live_model.enabled ? s.live_model.model_selector : "选择模型";
    core::TextStyle model_text;
    model_text.fontSize = ui_font_size(13);
    model_text.text = model_label;
    // Match ButtonBuilder's icon, gap and horizontal insets when fitting the label.
    const components::theme::ThemeMetricTokens button_metrics;
    const float model_insets = 9 * 1.15f + std::max(button_metrics.spacing.small, 32 * 0.12f) +
                               button_metrics.spacing.section * 2;
    const float model_width =
        std::clamp(core::TextPrimitive::measureTextSize(model_text).x + model_insets, 96.0f,
                   std::min(180.0f, std::max(96.0f, column - 342)));
    const float model_x = column - 140 - model_width;
    const float context_x = model_x - 40;
    const auto usage_ratio = context_ratio(session.context_usage);
    components::button(ui, "composer.context")
        .position(x + context_x, toolbar_y)
        .size(36, 32)
        .text("")
        .style(toolbar)
        .onClick([toggle] { toggle(PageState::Popup::Context); })
        .build();
    ui.svg("composer.context.ring")
        .source(context_ring_svg(usage_ratio, s.dark))
        .position(x + context_x + 8, toolbar_y + 6)
        .size(20, 20)
        .build();
    components::button(ui, "composer.model")
        .position(x + model_x, toolbar_y)
        .size(model_width, 32)
        .text(fitted_title(model_label, model_width - model_insets, 13))
        .icon(0xf078)
        .iconSize(9)
        .fontSize(ui_font_size(13))
        .style(toolbar)
        .disabled(s.saving_model)
        .onClick([toggle] { toggle(PageState::Popup::Model); })
        .build();
    const std::string effort = !s.live_model.supports_reasoning || session.reasoning.empty()
                                   ? "默认"
                               : session.reasoning == "minimal" ? "最少"
                               : session.reasoning == "low"     ? "低"
                               : session.reasoning == "medium"  ? "中"
                                                                : "高";
    components::button(ui, "composer.reasoning")
        .position(x + column - 136, toolbar_y)
        .size(88, 32)
        .text(effort + "思考")
        .icon(0xf078)
        .iconSize(9)
        .fontSize(ui_font_size(13))
        .style(toolbar)
        .onClick([toggle] { toggle(PageState::Popup::Reasoning); })
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
        session.submitting || session.attachment_loading || s.saving_model || !s.runtime ||
            !s.runtime->connected() || session.deleting ||
            (!session.running && s.chat.submission_text().empty()));
    const auto notice = !s.chat.notice().empty() ? s.chat.notice() : s.runtime_notice;
    text(ui, "composer.notice", fitted_title(notice, column - 168, 12), x, y + composer_height + 4,
         column - 168, 24, 12, p.muted);
    text(ui, "composer.shortcut", "Enter 发送 · Shift+Enter 换行", x + column - 168,
         y + composer_height + 4, 168, 24, 11, p.muted);
    if (s.popup != PageState::Popup::None) {
        const auto popup = s.popup;
        const float popup_width = std::min(320.0f, column);
        const float popup_height =
            popup == PageState::Popup::References ? std::min(352.0f, screen.height - 160)
            : popup == PageState::Popup::Context  ? 224
            : popup == PageState::Popup::Model
                ? std::min(352.0f, 68.0f + 44.0f * static_cast<float>(s.models.size()))
            : popup == PageState::Popup::Reasoning ? (s.live_model.supports_reasoning ? 244 : 148)
                                                   : 164;
        const float anchor = popup == PageState::Popup::Context     ? context_x
                             : popup == PageState::Popup::Model     ? model_x
                             : popup == PageState::Popup::Reasoning ? column - 136
                             : popup == PageState::Popup::Mode      ? 48
                                                                    : 8;
        const float popup_x = x + std::clamp(anchor, 8.0f, column - popup_width - 8);
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
                        .fontSize(ui_font_size(14))
                        .style(button_style(p))
                        .disabled(disabled)
                        .onClick(std::move(action))
                        .build();
                };
                if (popup == PageState::Popup::Actions) {
                    row("attach", session.attachment_loading ? "正在读取附件…" : "添加文本附件",
                        0xf0c6, 12, add_attachment,
                        session.attachment_loading || session.attachments.size() >= 4);
                    row(
                        "attachments", "查看附件与引用", 0xf15c, 60,
                        [] { state().popup = PageState::Popup::References; },
                        session.attachments.empty() && session.references.empty());
                    text(ui, "composer.popup.attach.help", "UTF-8 文本 · 最多 4 个 · 合计 8 KiB",
                         16, 112, popup_width - 32, 28, 12, p.muted);
                } else if (popup == PageState::Popup::Mode) {
                    row("readonly",
                        session.access == "read_only" ? "只读模式 · 已选择" : "只读模式", 0xf06e,
                        12, [] {
                            state().chat.current().access = "read_only";
                            state().popup = PageState::Popup::None;
                        });
                    row("default", session.access == "default" ? "默认权限 · 已选择" : "默认权限",
                        0xf3ed, 60, [] {
                            state().chat.current().access = "default";
                            state().popup = PageState::Popup::None;
                        });
                    text(ui, "composer.popup.access.help", "只读不调用工具；默认仅开放等待工具。",
                         16, 112, popup_width - 32, 36, 12, p.muted);
                } else if (popup == PageState::Popup::Reasoning) {
                    if (!s.live_model.supports_reasoning) {
                        text(ui, "composer.popup.reasoning.help", "此模型尚未启用思考深度。", 16,
                             16, popup_width - 32, 32, 15, p.text);
                        text(ui, "composer.popup.reasoning.note",
                             "确认模型支持后，在模型设置中启用。", 16, 50, popup_width - 32, 32, 13,
                             p.muted);
                        row("reasoning.settings", "模型设置", 0xf013, 96, [] {
                            state().settings = state().model_page = true;
                            state().popup = PageState::Popup::None;
                        });
                    } else {
                        const std::array<std::string, 5> values{"", "minimal", "low", "medium",
                                                                "high"};
                        const std::array<std::string, 5> labels{"默认（不传参数）", "最少", "低",
                                                                "中", "高"};
                        for (std::size_t i = 0; i < values.size(); ++i)
                            row("effort." + std::to_string(i),
                                labels[i] + (session.reasoning == values[i] ? " · 已选择" : ""),
                                0xf5dc, 12 + static_cast<float>(i) * 44, [value = values[i]] {
                                    state().chat.current().reasoning = value;
                                    state().popup = PageState::Popup::None;
                                });
                    }
                } else if (popup == PageState::Popup::Model) {
                    components::scrollView(ui, "composer.popup.models")
                        .position(8, 8)
                        .size(popup_width - 16, popup_height - 60)
                        .theme(tokens)
                        .gap(4)
                        .content([&](eui::Ui &list, float w, float) {
                            for (std::size_t i = 0; i < s.models.size(); ++i) {
                                const auto &model = s.models[i];
                                components::button(list,
                                                   "composer.model.option." + std::to_string(i))
                                    .size(w, 40)
                                    .text(fitted_title(model.display_name +
                                                           (model.model_selector.empty()
                                                                ? ""
                                                                : " · " + model.model_selector),
                                                       w - 40, 14))
                                    .icon(model.display_name == s.live_model.display_name ? 0xf00c
                                                                                          : 0xf1b2)
                                    .iconSize(14)
                                    .fontSize(ui_font_size(14))
                                    .style(button_style(p))
                                    .disabled(session.running || session.submitting ||
                                              s.saving_model || model.model_selector.empty())
                                    .onClick([i] { choose_model(i); })
                                    .build();
                            }
                        })
                        .build();
                    row("settings", "管理模型", 0xf013, popup_height - 48, [] {
                        state().settings = state().model_page = true;
                        state().popup = PageState::Popup::None;
                    });
                } else if (popup == PageState::Popup::References) {
                    text(ui, "composer.popup.references.title", "附件与引用", 16, 12,
                         popup_width - 32, 28, 16, p.text, 500);
                    components::scrollView(ui, "composer.popup.references.list")
                        .position(12, 48)
                        .size(popup_width - 24, popup_height - 60)
                        .theme(tokens)
                        .gap(16)
                        .scrollbarWidth(4)
                        .scrollbarGap(6)
                        .content([&](eui::Ui &list, float width, float) {
                            for (const auto &attachment : session.attachments) {
                                list.column("attachment." + std::to_string(attachment.id))
                                    .width(width)
                                    .height(40 + text_height(attachment.text, width, 13, 22))
                                    .gap(4)
                                    .content([&] {
                                        components::button(list, "attachment.remove." +
                                                                     std::to_string(attachment.id))
                                            .size(width, 32)
                                            .text(fitted_title(
                                                attachment.name + " · " +
                                                    std::to_string(attachment.text.size()) + " B",
                                                width - 38, 14))
                                            .icon(0xf00d)
                                            .fontSize(ui_font_size(14))
                                            .style(button_style(p))
                                            .onClick([id = attachment.id] {
                                                state().chat.remove_attachment(id);
                                            })
                                            .build();
                                        list.text("attachment.preview." +
                                                  std::to_string(attachment.id))
                                            .size(width,
                                                  text_height(attachment.text, width, 13, 22))
                                            .text(attachment.text)
                                            .fontSize(ui_font_size(13))
                                            .lineHeight(22)
                                            .wrap()
                                            .color(p.muted)
                                            .build();
                                    })
                                    .build();
                            }
                            for (const auto &reference : session.references) {
                                const auto key =
                                    "reference.preview." + std::to_string(reference.id);
                                const auto source_id = reference.message_id;
                                const auto reference_id = reference.id;
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
                                            .onClick([reference_id] {
                                                state().chat.remove_reference_instance(
                                                    reference_id);
                                                if (state().chat.current().references.empty())
                                                    state().popup = PageState::Popup::None;
                                            })
                                            .build();
                                        list.text(key + ".text")
                                            .position(4, 32)
                                            .size(width - 8, height)
                                            .text(reference.text)
                                            .fontSize(ui_font_size(14))
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
                         "本次引用 " + std::to_string(session.references.size()) + " 段文字", 16,
                         182, popup_width - 32, 24, 13, p.muted);
                }
            })
            .build();
    }
    if (s.selection.ready && s.selection.session == session_id) {
        const float sx = std::clamp(s.selection.popup_x - 50, sidebar + 12, screen.width - 116);
        const float desired_top = s.selection.popup_y - 40;
        const float sy =
            std::clamp(desired_top < 72 ? s.selection.popup_y + 18 : desired_top, 72.0f, y - 36);
        ui.stack("selection.popup")
            .position(sx, sy)
            .size(100, 32)
            .zIndex(15)
            .content([&] {
                ui.rect("selection.panel")
                    .size(100, 32)
                    .color(p.surface)
                    .radius(7)
                    .border(1, p.border)
                    .build();
                components::button(ui, "selection.quote")
                    .size(100, 32)
                    .text("引用选段")
                    .icon(0xf10d)
                    .iconSize(12)
                    .fontSize(ui_font_size(13))
                    .style(button_style(p))
                    .onClick([] {
                        auto &page = state();
                        if (page.selection.session == page.chat.current().id &&
                            page.chat.reference_excerpt(page.selection.message,
                                                        page.selection.excerpt)) {
                            page.runtime_notice = "已添加选中文字";
                            page.selection.clear();
                        } else
                            page.runtime_notice = page.chat.notice();
                    })
                    .build();
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
    if (s.runtime)
        s.runtime->set_activity(
            std::any_of(s.chat.sessions().begin(), s.chat.sessions().end(),
                        [](const auto &session) { return session.running || session.submitting; }));
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
            ui.rect("background")
                .size(screen.width, screen.height)
                .color(p.background)
                .onClick([] { state().selection.clear(); })
                .build();
            if (s.sidebar) {
                ui.rect("sidebar.background").size(sidebar, screen.height).color(p.sidebar).build();
                ui.rect("sidebar.border")
                    .position(sidebar - 1, 0)
                    .size(1, screen.height)
                    .color(p.border)
                    .build();
                // EUI-20261004-003: metadata-free, pixel-identical UI copy.
                ui.image("brand.mira")
                    .position(24, 16)
                    .size(28, 28)
                    .source("assets/mira-ui.png")
                    .contain()
                    .hitTestMode(eui::dsl::HitTestMode::None)
                    .build();
                text(ui, "brand", "Mirage", 64, 12, sidebar - 132, 36, 18, p.text, 600);
                icon_button(ui, "sidebar.hide", 0xf0db, sidebar - 52, 12, p,
                            [] { state().sidebar = false; });
                if (s.settings) {
                    components::button(ui, "settings.back")
                        .position(16, 76)
                        .size(sidebar - 32, 36)
                        .text("返回对话")
                        .icon(0xf060)
                        .fontSize(ui_font_size(14))
                        .iconSize(16)
                        .style(button_style(p))
                        .onClick([] {
                            state().settings = false;
                            state().model_chooser = false;
                        })
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
                            .onClick([model] {
                                state().model_page = model;
                                state().model_chooser = false;
                            })
                            .build();
                        icon(ui, key + ".icon", model ? 0xf544 : 0xf53f, 24, y, 17, 48, p.text);
                        text(ui, key + ".label", model ? "模型" : "外观", 64, y, sidebar - 88, 48,
                             16, p.text, 500);
                    }
                } else {
                    components::button(ui, "session.new")
                        .position(16, 76)
                        .size(sidebar - 32, 36)
                        .text("新建对话")
                        .icon(0xf067)
                        .fontSize(ui_font_size(14))
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
                            if (s.chat.history_count() == 0) {
                                text(list, "session.empty", "暂无历史对话", 12, 8, width - 24, 28,
                                     14, p.muted);
                                text(list, "session.empty.help", "发送消息后将显示在这里", 12, 38,
                                     width - 24, 28, 13, p.muted);
                            }
                            for (const auto &session : s.chat.sessions()) {
                                if (session.messages.empty())
                                    continue;
                                const auto id = session.id;
                                const bool selected = id == s.chat.current().id;
                                const std::string key = "session." + std::to_string(id);
                                list.stack(key)
                                    .size(width, 40)
                                    .content([&] {
                                        list.rect(key + ".hit")
                                            .size(width, 40)
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
                                        icon(list, key + ".icon", 0xf075, 12, 0, 14, 40, p.muted);
                                        text(list, key + ".title",
                                             fitted_title(session.title, width - 100, 14), 44, 0,
                                             width - 100, 40, 14, p.text);
                                        icon_button(
                                            list, key + ".delete", 0xf2ed, width - 40, 2, p,
                                            [id] {
                                                auto &v = state();
                                                if (v.about || v.confirm_clear || v.confirm_delete)
                                                    return;
                                                v.delete_target = id;
                                                v.confirm_delete = true;
                                            },
                                            false,
                                            session.running || session.submitting ||
                                                session.deleting);
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
            const float title_x = sidebar + (s.sidebar ? 28 : 116);
            const float title_right = screen.width - (s.settings && !s.sidebar ? 286 : 146);
            const float title_width = std::max(0.0f, title_right - title_x);
            const auto &session_title = s.chat.current().title;
            const std::string title = s.settings              ? "设置"
                                      : session_title.empty() ? "新对话"
                                                              : session_title;
            text(ui, "thread.title", fitted_title(title, title_width, 14), title_x, 0, title_width,
                 60, 14, p.text, 500);
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
                        .fontSize(ui_font_size(14))
                        .style(button_style(p))
                        .onClick([] {
                            state().settings = false;
                            state().model_chooser = false;
                        })
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
    if (!s.sidebar || s.about || s.confirm_clear || s.confirm_delete)
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
                    if (!s.about && !s.confirm_clear && !s.confirm_delete) {
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
                    if (s.selection.ready || s.selection.dragging)
                        s.selection.clear();
                    else if (s.about || s.confirm_clear || s.confirm_delete)
                        s.about = s.confirm_clear = s.confirm_delete = false;
                    else if (s.popup != mirage::native_ui::PageState::Popup::None)
                        s.popup = mirage::native_ui::PageState::Popup::None;
                    else if (s.model_chooser)
                        s.model_chooser = false;
                    else if (!s.chat.current().edit_turn_id.empty() &&
                             !s.chat.current().submitting && !s.chat.current().running)
                        s.chat.cancel_edit();
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
