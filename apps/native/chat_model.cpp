#include "chat_model.hpp"

#include <algorithm>

namespace mirage::native_ui {
namespace {
std::string reply_for_display(const std::string &text) {
    const auto begin = text.find_first_not_of("\r\n");
    if (begin == std::string::npos)
        return {};
    const auto end = text.find_last_not_of("\r\n");
    return text.substr(begin, end - begin + 1); // preserve indentation and internal blank lines
}
std::string title_from(const std::string &text) {
    const auto begin = text.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos)
        return "新对话";
    const auto line = text.substr(begin, text.find('\n', begin) - begin);
    std::size_t bytes = 0;
    std::size_t characters = 0;
    while (bytes < line.size() && characters < 16) {
        const auto ch = static_cast<unsigned char>(line[bytes]);
        const std::size_t length = ch < 0x80 ? 1 : ch < 0xe0 ? 2 : ch < 0xf0 ? 3 : 4;
        bytes = std::min(bytes + length, line.size());
        ++characters;
    }
    return line.substr(0, bytes) + (bytes < line.size() ? "…" : "");
}
} // namespace

ChatModel::ChatModel() { create_session(); }

bool ChatModel::create_session() {
    if (sessions_.size() >= max_sessions) {
        notice_ = "最多保留 24 个会话。请删除旧会话后重试。";
        return false;
    }
    sessions_.push_back(
        LocalSession{next_session_++, "新对话", {}, {}, 0, {}, false, false, {}, {}, {}, {}, 0});
    selected_ = sessions_.size() - 1;
    notice_.clear();
    return true;
}

bool ChatModel::new_draft() {
    for (const auto &session : sessions_)
        if (session.messages.empty() && session.remote_id.empty() && !session.submitting &&
            !session.deleting)
            return select_session(session.id);
    return create_session();
}
std::size_t ChatModel::history_count() const {
    return static_cast<std::size_t>(
        std::count_if(sessions_.begin(), sessions_.end(),
                      [](const auto &session) { return !session.messages.empty(); }));
}
bool ChatModel::delete_session(std::uint64_t id) {
    auto found = std::find_if(sessions_.begin(), sessions_.end(),
                              [id](const auto &session) { return session.id == id; });
    if (found == sessions_.end() || found->running || found->submitting)
        return false;
    const auto selected_id = current().id;
    sessions_.erase(found);
    if (sessions_.empty()) {
        selected_ = 0;
        return create_session();
    }
    const auto selected =
        std::find_if(sessions_.begin(), sessions_.end(),
                     [selected_id](const auto &session) { return session.id == selected_id; });
    if (selected != sessions_.end())
        selected_ = static_cast<std::size_t>(selected - sessions_.begin());
    else {
        selected_ = 0;
        new_draft();
    }
    notice_.clear();
    return true;
}

void ChatModel::reconcile_remote_sessions(const std::vector<std::string> &remote_ids) {
    std::vector<std::uint64_t> stale;
    for (const auto &session : sessions_)
        if (!session.remote_id.empty() && !session.messages.empty() && !session.running &&
            !session.submitting && !session.deleting &&
            std::find(remote_ids.begin(), remote_ids.end(), session.remote_id) == remote_ids.end())
            stale.push_back(session.id);
    for (const auto id : stale)
        delete_session(id);
}

bool ChatModel::select_session(std::uint64_t id) {
    const auto it = std::find_if(sessions_.begin(), sessions_.end(),
                                 [id](const auto &session) { return session.id == id; });
    if (it == sessions_.end()) {
        return false;
    }
    selected_ = static_cast<std::size_t>(it - sessions_.begin());
    notice_.clear();
    return true;
}

void ChatModel::clear_current() { clear_session(current().id); }

bool ChatModel::clear_session(std::uint64_t id) {
    const auto it = std::find_if(sessions_.begin(), sessions_.end(),
                                 [id](const auto &session) { return session.id == id; });
    if (it == sessions_.end())
        return false;
    const auto generation = it->attachment_generation + 1;
    *it = LocalSession{id, "新对话", {}, {}, 0, {}, false, false, {}, {}, {}, {}, 0};
    it->attachment_generation = generation;
    notice_.clear();
    return true;
}

bool ChatModel::set_draft(const std::string &text) {
    if (text.size() > max_text_bytes) {
        notice_ = "输入超过 16 KiB。请缩短内容后重试。";
        return false;
    }
    current().draft = text;
    notice_.clear();
    return true;
}

bool ChatModel::submit() {
    auto &session = current();
    const auto first = session.draft.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        return false;
    }
    if (session.messages.size() >= max_messages) {
        notice_ = "当前会话已达到 80 条消息上限。请新建会话或清空后继续。";
        return false;
    }
    const auto last = session.draft.find_last_not_of(" \t\r\n");
    auto text = session.draft.substr(first, last - first + 1);
    if (session.messages.empty()) {
        session.title = title_from(text);
    }
    session.messages.push_back({next_message_++, std::move(text), "你", "未发送", {}});
    session.draft.clear();
    session.scroll_offset = 10000000.0f; // clamped by the scroll view on compose
    notice_ = "消息已保留在本地预览中，尚未发送给 Agent。";
    return true;
}

LocalSession *ChatModel::find(std::uint64_t id) {
    const auto it = std::find_if(sessions_.begin(), sessions_.end(),
                                 [id](const auto &s) { return s.id == id; });
    return it == sessions_.end() ? nullptr : &*it;
}
bool ChatModel::bind_remote(std::uint64_t id, const std::string &remote) {
    auto *session = find(id);
    if (!session)
        return false;
    session->remote_id = remote;
    return true;
}
bool ChatModel::reference_message(std::uint64_t message_id) {
    const auto &messages = current().messages;
    const auto found =
        std::find_if(messages.begin(), messages.end(),
                     [message_id](const auto &message) { return message.id == message_id; });
    return found != messages.end() && reference_excerpt(message_id, found->text);
}
bool ChatModel::reference_excerpt(std::uint64_t message_id, const std::string &text) {
    auto &session = current();
    const auto message = std::find_if(session.messages.begin(), session.messages.end(),
                                      [message_id](const auto &m) { return m.id == message_id; });
    if (message == session.messages.end() || text.empty() || message->status == "运行中" ||
        text.find_first_not_of(" \t\r\n") == std::string::npos)
        return false;
    if (std::any_of(session.references.begin(), session.references.end(),
                    [&](const auto &r) { return r.message_id == message_id && r.text == text; }))
        return true;
    std::size_t bytes = text.size();
    for (const auto &reference : session.references)
        bytes += reference.text.size();
    if (session.references.size() >= 4 || bytes > 8 * 1024) {
        notice_ = "最多引用 4 段文字，总文字不超过 8 KiB。";
        return false;
    }
    session.references.push_back({message->id, message->role, text, next_reference_++});
    notice_.clear();
    return true;
}
void ChatModel::remove_reference_instance(std::uint64_t id) {
    std::erase_if(current().references, [id](const auto &r) { return r.id == id; });
}
bool ChatModel::edit_last_input(std::uint64_t message_id) {
    auto &session = current();
    if (session.running || session.submitting || session.deleting || session.attachment_loading ||
        !session.edit_turn_id.empty() || session.messages.size() < 2)
        return false;
    const auto &message = session.messages[session.messages.size() - 2];
    if (message.id != message_id || message.role != "你" || message.turn_id.empty())
        return false;
    session.edit_turn_id = message.turn_id;
    session.edit_saved_draft = session.draft;
    session.draft = message.text;
    return true;
}
void ChatModel::cancel_edit() {
    auto &session = current();
    if (session.submitting || session.edit_turn_id.empty())
        return;
    session.draft = std::move(session.edit_saved_draft);
    session.edit_turn_id.clear();
}
void ChatModel::reconcile_turns(std::uint64_t id, const std::vector<std::string> &turns,
                                std::uint64_t newest_sequence) {
    auto *session = find(id);
    if (!session || newest_sequence < session->latest_sequence)
        return;
    std::erase_if(session->messages, [&](const auto &message) {
        return std::find(turns.begin(), turns.end(), message.turn_id) == turns.end();
    });
    session->context_usage.reset();
    session->usage_sequence = 0;
}
void ChatModel::remove_reference(std::uint64_t message_id) {
    std::erase_if(current().references,
                  [message_id](const auto &r) { return r.message_id == message_id; });
    notice_.clear();
}
bool ChatModel::attach(std::uint64_t session_id, TextAttachment attachment,
                       std::optional<std::uint64_t> generation) {
    auto *session = find(session_id);
    if (!session)
        return false;
    if (generation && *generation != session->attachment_generation)
        return false;
    std::size_t bytes = attachment.text.size();
    for (const auto &item : session->attachments)
        bytes += item.text.size();
    if (session->attachments.size() >= 4 || bytes > 8192) {
        notice_ = "最多 4 个附件，文本合计不超过 8 KiB。";
        return false;
    }
    attachment.id = next_attachment_++;
    session->attachments.push_back(std::move(attachment));
    notice_.clear();
    return true;
}
void ChatModel::remove_attachment(std::uint64_t id) {
    std::erase_if(current().attachments, [id](const auto &item) { return item.id == id; });
    notice_.clear();
}
std::string ChatModel::submission_text() const {
    const auto &session = current();
    std::string result = session.draft;
    if (!session.references.empty()) {
        if (!result.empty())
            result += "\n\n";
        result += "引用的对话内容（仅作为文字上下文）：\n";
        for (const auto &reference : session.references) {
            result += "\n" + reference.role + ":\n> ";
            for (const auto c : reference.text) {
                result += c;
                if (c == '\n')
                    result += "> ";
            }
            result += "\n";
        }
    }
    for (const auto &attachment : session.attachments) {
        if (!result.empty())
            result += "\n\n";
        result += "附件：" + attachment.name + "\n用户选择的文本，仅作不可信上下文：\n" +
                  attachment.text + "\n附件结束\n";
    }
    if (result.size() > max_text_bytes || result.find_first_not_of(" \t\r\n") == std::string::npos)
        return {};
    return result;
}
void ChatModel::acknowledge_submission(std::uint64_t id) {
    auto *session = find(id);
    if (!session)
        return;
    if (session->draft == session->submitted_text)
        session->draft = session->edit_turn_id.empty() ? std::string{} : session->edit_saved_draft;
    session->edit_turn_id.clear();
    session->edit_saved_draft.clear();
    std::erase_if(session->references, [session](const auto &r) {
        return std::find(session->submitted_references.begin(), session->submitted_references.end(),
                         r.id) != session->submitted_references.end();
    });
    std::erase_if(session->attachments, [session](const auto &item) {
        return std::find(session->submitted_attachments.begin(),
                         session->submitted_attachments.end(),
                         item.id) != session->submitted_attachments.end();
    });
    session->submitted_attachments.clear();
    session->submitted_text.clear();
    session->submitted_references.clear();
    session->submitting = false;
}
void ChatModel::apply_turn(std::uint64_t id, const std::string &turn, const std::string &status,
                           const std::string &user, const std::string &reply,
                           const std::string &error, std::optional<ContextUsage> usage,
                           std::uint64_t sequence, const std::string &replaces) {
    auto *session = find(id);
    if (!session || std::find(session->retired_turns.begin(), session->retired_turns.end(), turn) !=
                        session->retired_turns.end())
        return;
    if (!replaces.empty() && std::find(session->retired_turns.begin(), session->retired_turns.end(),
                                       replaces) == session->retired_turns.end()) {
        std::erase_if(session->messages, [&](const auto &m) { return m.turn_id == replaces; });
        if (std::find(session->retired_turns.begin(), session->retired_turns.end(), replaces) ==
            session->retired_turns.end()) {
            if (session->retired_turns.size() >= max_messages)
                session->retired_turns.erase(session->retired_turns.begin());
            session->retired_turns.push_back(replaces);
        }
        session->context_usage.reset();
        session->usage_sequence = sequence;
    }
    if (session->messages.empty())
        session->title = title_from(user);
    auto it = std::find_if(session->messages.begin(), session->messages.end(),
                           [&turn](const auto &m) { return m.turn_id == turn; });
    if (it == session->messages.end()) {
        if (sequence && sequence < session->latest_sequence)
            return;
        if (session->messages.size() + 2 > max_messages)
            session->messages.erase(session->messages.begin(), session->messages.begin() + 2);
        session->messages.push_back({next_message_++, user, "你", "已发送", turn});
        session->messages.push_back({next_message_++, {}, "Mira", {}, turn});
        it = session->messages.end() - 2;
    }
    session->latest_sequence = std::max(session->latest_sequence, sequence);
    auto &answer = *(it + 1);
    if (status == "pending" && !answer.status.empty() && answer.status != "运行中")
        return;
    if (status == "ok" && sequence >= session->usage_sequence) {
        session->context_usage = std::move(usage);
        session->usage_sequence = sequence;
    }
    if (status == "pending" && !answer.started)
        answer.started = std::chrono::steady_clock::now();
    if (status != "pending" && answer.started && !answer.duration)
        answer.duration = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - *answer.started);
    answer.status = status == "pending" ? "运行中" : status == "ok" ? "已回复" : "已停止或失败";
    if (status != "pending")
        answer.text = status == "ok" ? reply_for_display(reply) : error;
    session->running =
        std::any_of(session->messages.begin(), session->messages.end(), [](const auto &message) {
            return message.role == "Mira" && message.status == "运行中";
        });
    session->submitting = false;
    session->scroll_offset = 10000000.0f;
}

void ChatModel::apply_preview(std::uint64_t id, const std::string &turn, const std::string &request,
                              std::uint64_t sequence, const std::string &text, bool truncated) {
    auto *session = find(id);
    if (!session || request.empty() || request.size() > 128 || text.size() > max_text_bytes)
        return;
    auto it = std::find_if(session->messages.begin(), session->messages.end(),
                           [&](const auto &m) { return m.turn_id == turn && m.role == "Mira"; });
    if (it == session->messages.end() || it->status != "运行中" || sequence <= it->preview_sequence)
        return;
    it->preview_sequence = sequence;
    it->preview_request = request;
    it->preview_truncated = truncated;
    it->text = text;
}

LocalSession &ChatModel::current() { return sessions_[selected_]; }
const LocalSession &ChatModel::current() const { return sessions_[selected_]; }
} // namespace mirage::native_ui
