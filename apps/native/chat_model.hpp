#pragma once

#include "attachment.hpp"
#include "context_usage.hpp"
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace mirage::native_ui {

struct LocalMessage {
    std::uint64_t id;
    std::string text;
    std::string role = "你";
    std::string status = "未发送";
    std::string turn_id;
};

struct ContextReference {
    std::uint64_t message_id;
    std::string role;
    std::string text;
    std::uint64_t id;
};

struct LocalSession {
    std::uint64_t id;
    std::string title = "新对话";
    std::string draft;
    std::vector<LocalMessage> messages;
    float scroll_offset = 0;
    std::string remote_id;
    bool running = false;
    bool submitting = false;
    std::string submitted_text;
    std::vector<ContextReference> references;
    std::vector<std::uint64_t> submitted_references;
    std::optional<ContextUsage> context_usage;
    std::uint64_t usage_sequence = 0;
    std::vector<TextAttachment> attachments = {};
    std::vector<std::uint64_t> submitted_attachments = {};
    std::string access = "default";
    std::string reasoning = "";
    bool attachment_loading = false;
    std::uint64_t attachment_generation = 0;
    std::string pending_prompt = {};
    std::string pending_access = {};
    std::string pending_reasoning = {};
    bool deleting = false;
};

// UI-thread-only, bounded preview state. IDs are never reused, and switching
// sessions preserves drafts. This is not a parallel Runtime task state model.
class ChatModel {
  public:
    static constexpr std::size_t max_sessions = 24;
    static constexpr std::size_t max_messages = 80;
    static constexpr std::size_t max_text_bytes = 16 * 1024;

    ChatModel();
    bool create_session();
    bool new_draft();
    bool delete_session(std::uint64_t id);
    void reconcile_remote_sessions(const std::vector<std::string> &remote_ids);
    std::size_t history_count() const;
    bool select_session(std::uint64_t id);
    void clear_current();
    bool clear_session(std::uint64_t id);
    bool set_draft(const std::string &text);
    bool submit();
    bool bind_remote(std::uint64_t id, const std::string &remote);
    bool reference_message(std::uint64_t message_id);
    void remove_reference(std::uint64_t message_id);
    bool attach(std::uint64_t session_id, TextAttachment attachment,
                std::optional<std::uint64_t> generation = {});
    void remove_attachment(std::uint64_t id);
    std::string submission_text() const;
    void acknowledge_submission(std::uint64_t id);
    LocalSession *find(std::uint64_t id);
    void apply_turn(std::uint64_t id, const std::string &turn, const std::string &status,
                    const std::string &user, const std::string &reply, const std::string &error,
                    std::optional<ContextUsage> usage = {}, std::uint64_t sequence = 0);
    LocalSession &current();
    const LocalSession &current() const;
    const std::vector<LocalSession> &sessions() const { return sessions_; }
    const std::string &notice() const { return notice_; }

  private:
    std::vector<LocalSession> sessions_;
    std::size_t selected_ = 0;
    std::uint64_t next_session_ = 1;
    std::uint64_t next_message_ = 1;
    std::uint64_t next_reference_ = 1;
    std::uint64_t next_attachment_ = 1;
    std::string notice_;
};

} // namespace mirage::native_ui
