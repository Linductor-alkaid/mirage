#include "../support/test.hpp"
#include "chat_model.hpp"
#include "conversation_preview.hpp"
#include "secret_edit.hpp"
#include "text_selection.hpp"
#include <chrono>
#include <filesystem>
#include <fstream>
#ifndef _WIN32
#include <sys/stat.h>
#include <unistd.h>
#endif

int main() {
    using mirage::native_ui::ChatModel;
    using mirage::native_ui::conversation_preview;
    MIRAGE_CHECK(conversation_preview("短消息") == "短消息");
    MIRAGE_CHECK(conversation_preview("第一行\n第二行") == "第一行…");
    MIRAGE_CHECK(conversation_preview("第一行\r\n第二行") == "第一行…");
    MIRAGE_CHECK(conversation_preview("a\tb") == "a b");
    MIRAGE_CHECK(conversation_preview(std::string(16384, 'x')) == std::string(256, 'x') + "…");
    std::string long_cjk;
    for (int i = 0; i < 100; ++i)
        long_cjk += "字";
    MIRAGE_CHECK(conversation_preview(long_cjk) == long_cjk.substr(0, 255) + "…");
    using mirage::native_ui::edit_secret;
    MIRAGE_CHECK(edit_secret("abc", "**X*", 2, 2, 2, 3) == "abXc");
    MIRAGE_CHECK(edit_secret("abc", "**", 2, 2, 2, 1) == "ac");
    MIRAGE_CHECK(edit_secret("abc", "**", 1, 1, 1, 1) == "ac");
    MIRAGE_CHECK(edit_secret("abcd", "*XY*", 3, 1, 3, 3) == "aXYd");
    MIRAGE_CHECK(edit_secret("ab", "***", 1, 1, 1, 2) == "a*b");
    MIRAGE_CHECK(!edit_secret("ab", "a b", 1, 1, 1, 2));
    MIRAGE_CHECK(!edit_secret("ab", std::string(2049, '*'), 1, 1, 1, 2));
    using namespace mirage::native_ui;
    SelectionText selected;
    selected.append({"红发", 0, 0, 24, {0, 3, 6}, {0, 16, 32}});
    selected.append({"蓝眼", 32, 0, 24, {0, 3, 6}, {0, 16, 32}});
    selected.append({"下一行", 0, 24, 24, {0, 3, 6, 9}, {0, 16, 32, 48}});
    MIRAGE_CHECK(selected.text == "红发蓝眼\n下一行");
    MIRAGE_CHECK(selected.hit(15, 10) == 3 && selected.hit(49, 10) == 9);
    MIRAGE_CHECK(selected.hit(31, 30) == 19);
    TextSelection selection;
    selection.anchor = 9;
    selection.caret = 3;
    selection.finish(selected);
    MIRAGE_CHECK(selection.ready && selection.excerpt == "发蓝");
    selection.anchor = 6;
    selection.caret = 6;
    selection.finish(selected);
    MIRAGE_CHECK(!selection.ready);
    ChatModel streaming;
    const auto stream_session = streaming.current().id;
    streaming.apply_turn(stream_session, "live", "pending", "问题", {}, {});
    MIRAGE_CHECK(streaming.current().messages.back().text.empty() &&
                 streaming.current().messages.back().started);
    streaming.apply_preview(stream_session, "unknown", "request", 1, "不应出现", false);
    streaming.apply_preview(stream_session, "live", "request", 1, "第一段", false);
    MIRAGE_CHECK(streaming.current().messages.back().text == "第一段");
    streaming.current().scroll_offset = 11;
    streaming.apply_preview(stream_session, "live", "request", 2, "第一段第二段", false);
    MIRAGE_CHECK(streaming.current().scroll_offset == 11);
    streaming.apply_turn(stream_session, "live", "pending", "问题", {}, {});
    MIRAGE_CHECK(streaming.current().messages.back().text == "第一段第二段");
    streaming.apply_preview(stream_session, "live", "request", 1, "乱序", false);
    MIRAGE_CHECK(streaming.current().messages.back().text == "第一段第二段");
    streaming.apply_preview(stream_session, "live", "retry", 3, "", false);
    MIRAGE_CHECK(streaming.current().messages.back().text.empty());
    streaming.apply_preview(stream_session, "live", "retry", 4, std::string(16385, 'x'), false);
    MIRAGE_CHECK(streaming.current().messages.back().preview_sequence == 3);
    streaming.apply_turn(stream_session, "live", "ok", "问题", "规范结果", {});
    MIRAGE_CHECK(streaming.current().messages.back().duration);
    streaming.apply_preview(stream_session, "live", "retry", 5, "迟到", false);
    MIRAGE_CHECK(streaming.current().messages.back().text == "规范结果");
    streaming.apply_turn(stream_session, "cancel", "pending", "取消", {}, {});
    streaming.apply_preview(stream_session, "cancel", "c", 1, "部分结果", true);
    streaming.apply_turn(stream_session, "cancel", "failed", "取消", {}, "已取消");
    streaming.apply_preview(stream_session, "cancel", "c", 2, "迟到", false);
    MIRAGE_CHECK(streaming.current().messages.back().text == "已取消" &&
                 !streaming.current().running);
    ChatModel revised;
    const auto revision_session = revised.current().id;
    revised.apply_turn(revision_session, "base", "ok", "更早输入", "更早回答", {}, {}, 1);
    revised.apply_turn(revision_session, "old", "ok", "最后输入", "最后回答", {},
                       ContextUsage{300, 1000, "model"}, 2);
    revised.set_draft("原来草稿");
    const auto last_input = revised.current().messages[2].id;
    MIRAGE_CHECK(!revised.edit_last_input(revised.current().messages[0].id));
    MIRAGE_CHECK(revised.edit_last_input(last_input));
    MIRAGE_CHECK(revised.current().draft == "最后输入" && revised.current().messages.size() == 4);
    revised.set_draft("修改输入");
    revised.cancel_edit();
    MIRAGE_CHECK(revised.current().draft == "原来草稿" &&
                 revised.current().messages[3].text == "最后回答");
    MIRAGE_CHECK(revised.reference_excerpt(last_input, "最后"));
    MIRAGE_CHECK(revised.reference_excerpt(last_input, "输入"));
    MIRAGE_CHECK(revised.current().references.size() == 2);
    const auto excerpt_id = revised.current().references.front().id;
    revised.remove_reference_instance(excerpt_id);
    MIRAGE_CHECK(revised.current().references.size() == 1 &&
                 revised.current().references.front().text == "输入");
    MIRAGE_CHECK(revised.edit_last_input(last_input));
    revised.set_draft("修改输入");
    revised.current().submitted_text = "修改输入";
    revised.apply_turn(revision_session, "new", "pending", "修改输入", {}, {}, {}, 3, "old");
    MIRAGE_CHECK(revised.current().messages.size() == 4 && !revised.current().context_usage);
    revised.acknowledge_submission(revision_session);
    MIRAGE_CHECK(revised.current().draft == "原来草稿" && revised.current().edit_turn_id.empty());
    revised.apply_turn(revision_session, "new", "ok", "修改输入", "修改回答", {},
                       ContextUsage{120, 1000, "model"}, 3);
    revised.apply_turn(revision_session, "old", "ok", "最后输入", "迟到回答", {}, {}, 2);
    MIRAGE_CHECK(revised.current().messages.size() == 4 &&
                 revised.current().messages.back().text == "修改回答");
    revised.apply_turn(revision_session, "new", "pending", "修改输入", {}, {}, {}, 0, "old");
    MIRAGE_CHECK(revised.current().context_usage->input_tokens == 120);
    revised.reconcile_turns(revision_session, {"old"}, 2);
    MIRAGE_CHECK(revised.current().messages.size() == 4);
    revised.reconcile_turns(revision_session, {"new"}, 3);
    MIRAGE_CHECK(revised.current().messages.size() == 2);
    ChatModel drafts;
    const auto draft_id = drafts.current().id;
    MIRAGE_CHECK(drafts.history_count() == 0);
    drafts.set_draft("保留未发送的草稿");
    for (int i = 0; i < 100; ++i)
        MIRAGE_CHECK(drafts.new_draft());
    MIRAGE_CHECK(drafts.sessions().size() == 1 && drafts.current().id == draft_id);
    MIRAGE_CHECK(drafts.current().draft == "保留未发送的草稿" && drafts.history_count() == 0);
    drafts.apply_turn(draft_id, "accepted", "pending", "首发消息", {}, {});
    MIRAGE_CHECK(drafts.history_count() == 1 && !drafts.delete_session(draft_id));
    MIRAGE_CHECK(drafts.new_draft() && drafts.current().id != draft_id);
    const auto next_draft = drafts.current().id;
    drafts.apply_turn(draft_id, "accepted", "ok", "首发消息", "回答", {});
    MIRAGE_CHECK(drafts.delete_session(draft_id) && drafts.current().id == next_draft);
    drafts.apply_turn(draft_id, "late", "ok", "迟到", "不得复活", {});
    MIRAGE_CHECK(drafts.history_count() == 0 && drafts.sessions().size() == 1);
    MIRAGE_CHECK(drafts.delete_session(next_draft) && drafts.current().id > next_draft);
    MIRAGE_CHECK(!drafts.delete_session(next_draft));
    ChatModel reconciled;
    const auto kept_draft = reconciled.current().id;
    MIRAGE_CHECK(reconciled.set_draft("保留草稿"));
    MIRAGE_CHECK(reconciled.create_session());
    const auto stale = reconciled.current().id;
    MIRAGE_CHECK(reconciled.bind_remote(stale, "removed-server-id"));
    reconciled.apply_turn(stale, "settled", "ok", "旧历史", "回复", {});
    MIRAGE_CHECK(reconciled.create_session());
    const auto active = reconciled.current().id;
    MIRAGE_CHECK(reconciled.bind_remote(active, "active-server-id"));
    reconciled.apply_turn(active, "pending", "pending", "活动历史", {}, {});
    MIRAGE_CHECK(reconciled.select_session(kept_draft));
    reconciled.reconcile_remote_sessions({});
    MIRAGE_CHECK(!reconciled.find(stale));
    MIRAGE_CHECK(reconciled.find(active) && reconciled.current().id == kept_draft &&
                 reconciled.current().draft == "保留草稿");
    reconciled.apply_turn(active, "pending", "ok", "活动历史", "结束", {});
    MIRAGE_CHECK(reconciled.select_session(active));
    reconciled.reconcile_remote_sessions({"active-server-id"});
    MIRAGE_CHECK(reconciled.find(active));
    reconciled.reconcile_remote_sessions({});
    MIRAGE_CHECK(!reconciled.find(active) && reconciled.current().id == kept_draft);
    ChatModel model;
    const auto first = model.current().id;
    MIRAGE_CHECK(!model.submit());
    MIRAGE_CHECK(model.set_draft(" \n\t"));
    MIRAGE_CHECK(!model.submit());
    MIRAGE_CHECK(model.set_draft("会话一的草稿"));
    MIRAGE_CHECK(model.create_session());
    const auto second = model.current().id;
    MIRAGE_CHECK(first != second);
    MIRAGE_CHECK(model.current().draft.empty());
    MIRAGE_CHECK(model.set_draft("第二个会话"));
    MIRAGE_CHECK(model.select_session(first));
    MIRAGE_CHECK(model.current().draft == "会话一的草稿");
    MIRAGE_CHECK(!model.select_session(99999));
    MIRAGE_CHECK(model.current().id == first);
    MIRAGE_CHECK(model.submit());
    const auto message = model.current().messages.front().id;
    MIRAGE_CHECK(model.current().draft.empty());
    MIRAGE_CHECK(!model.set_draft(std::string(ChatModel::max_text_bytes + 1, 'x')));
    MIRAGE_CHECK(model.current().draft.empty());
    MIRAGE_CHECK(model.set_draft(std::string(ChatModel::max_text_bytes, 'x')));
    MIRAGE_CHECK(model.submit());
    for (std::size_t i = 2; i < ChatModel::max_messages; ++i) {
        model.set_draft("消息");
        MIRAGE_CHECK(model.submit());
    }
    model.set_draft("保留拒绝的草稿");
    MIRAGE_CHECK(!model.submit());
    MIRAGE_CHECK(model.current().messages.size() == ChatModel::max_messages);
    MIRAGE_CHECK(model.current().draft == "保留拒绝的草稿");
    model.clear_current();
    MIRAGE_CHECK(model.current().id == first);
    MIRAGE_CHECK(model.current().messages.empty());
    model.set_draft("新的消息");
    MIRAGE_CHECK(model.submit());
    MIRAGE_CHECK(model.current().messages.front().id > message);
    MIRAGE_CHECK(model.select_session(second));
    MIRAGE_CHECK(model.current().draft == "第二个会话");
    MIRAGE_CHECK(model.clear_session(first));
    MIRAGE_CHECK(model.current().id == second);
    MIRAGE_CHECK(model.current().draft == "第二个会话");
    MIRAGE_CHECK(!model.clear_session(99999));
    for (std::size_t i = 2; i < ChatModel::max_sessions; ++i)
        MIRAGE_CHECK(model.create_session());
    MIRAGE_CHECK(!model.create_session());
    MIRAGE_CHECK(model.sessions().size() == ChatModel::max_sessions);
    ChatModel unicode;
    unicode.set_draft("一二三四五六七八九十一二三四五六七八九十");
    unicode.submit();
    MIRAGE_CHECK(unicode.current().title == "一二三四五六七八九十一二三四五六…");
    ChatModel live;
    const auto live_id = live.current().id;
    MIRAGE_CHECK(live.bind_remote(live_id, "remote"));
    live.set_draft("编辑中的草稿");
    live.apply_turn(live_id, "turn-1", "pending", "问题", {}, {});
    MIRAGE_CHECK(live.current().running && live.current().messages.size() == 2);
    live.apply_turn(live_id, "turn-1", "ok", "问题", "\n\n回答\r\n", {});
    live.apply_turn(live_id, "turn-1", "pending", "问题", {}, {});
    MIRAGE_CHECK(!live.current().running && live.current().messages.back().text == "回答");
    live.apply_turn(live_id, "turn-2", "pending", "新问题", {}, {});
    live.apply_turn(live_id, "turn-1", "ok", "问题", "回答", {});
    MIRAGE_CHECK(live.current().running && live.current().draft == "编辑中的草稿");
    live.apply_turn(live_id, "turn-2", "failed", "新问题", {}, "已取消");
    MIRAGE_CHECK(!live.current().running && live.current().messages.back().text == "已取消");
    live.apply_turn(live_id, "format", "ok", "格式", "\n  缩进\n\n第二段\n", {});
    MIRAGE_CHECK(live.current().messages.back().text == "  缩进\n\n第二段");
    for (int i = 3; i <= 50; ++i)
        live.apply_turn(live_id, "turn-" + std::to_string(i), "ok", "问题", "回答", {});
    MIRAGE_CHECK(live.current().messages.size() == ChatModel::max_messages);
    ChatModel references;
    const auto reference_session = references.current().id;
    references.apply_turn(reference_session, "one", "ok", "问题", "**回答**\n第二行", {});
    const auto answer_id = references.current().messages.back().id;
    MIRAGE_CHECK(references.reference_message(answer_id));
    MIRAGE_CHECK(references.reference_message(answer_id) &&
                 references.current().references.size() == 1);
    references.set_draft("继续分析");
    MIRAGE_CHECK(references.submission_text().find("> **回答**\n> 第二行") != std::string::npos);
    references.current().submitted_text = references.current().draft;
    references.current().submitted_references = {references.current().references.front().id};
    references.set_draft("发送后写的新草稿");
    references.apply_turn(reference_session, "two", "ok", "另一问题", "另一回答", {});
    const auto next_answer = references.current().messages.back().id;
    MIRAGE_CHECK(references.reference_message(next_answer));
    references.acknowledge_submission(reference_session);
    MIRAGE_CHECK(references.current().draft == "发送后写的新草稿");
    MIRAGE_CHECK(references.current().references.size() == 1 &&
                 references.current().references.front().message_id == next_answer);
    MIRAGE_CHECK(references.create_session() && references.current().references.empty());
    MIRAGE_CHECK(references.select_session(reference_session) &&
                 references.current().references.size() == 1);
    references.remove_reference(next_answer);
    MIRAGE_CHECK(references.current().references.empty());
    MIRAGE_CHECK(references.reference_message(answer_id));
    const auto old_reference = references.current().references.front().id;
    references.current().submitted_references = {old_reference};
    references.remove_reference(answer_id);
    MIRAGE_CHECK(references.reference_message(answer_id));
    MIRAGE_CHECK(references.current().references.front().id > old_reference);
    references.acknowledge_submission(reference_session);
    MIRAGE_CHECK(references.current().references.size() == 1);
    references.remove_reference(answer_id);
    MIRAGE_CHECK(!references.reference_message(9999));
    references.apply_turn(reference_session, "large", "ok", "大问题", std::string(8193, 'x'), {});
    MIRAGE_CHECK(!references.reference_message(references.current().messages.back().id));
    MIRAGE_CHECK(references.reference_message(answer_id));
    references.set_draft(std::string(ChatModel::max_text_bytes, 'x'));
    MIRAGE_CHECK(references.submission_text().empty());
    MIRAGE_CHECK(references.current().draft.size() == ChatModel::max_text_bytes &&
                 references.current().references.size() == 1);
    references.clear_current();
    MIRAGE_CHECK(references.current().references.empty());
    for (int i = 0; i < 5; ++i) {
        references.apply_turn(reference_session, "capacity-" + std::to_string(i), "ok", "问题",
                              "回答", {});
        MIRAGE_CHECK(references.reference_message(references.current().messages.back().id) ==
                     (i < 4));
    }
    MIRAGE_CHECK(references.current().references.size() == 4);
    using mirage::native_ui::context_percent;
    using mirage::native_ui::context_ratio;
    using mirage::native_ui::ContextUsage;
    ChatModel usage;
    const auto usage_session = usage.current().id;
    MIRAGE_CHECK(!context_ratio(usage.current().context_usage));
    usage.apply_turn(usage_session, "u1", "ok", "question", "reply", {},
                     ContextUsage{8192, 32768, "model"}, 2);
    MIRAGE_CHECK(context_ratio(usage.current().context_usage) == .25);
    MIRAGE_CHECK(context_percent(*context_ratio(usage.current().context_usage)) == "25%");
    usage.set_draft("new draft");
    MIRAGE_CHECK(context_ratio(usage.current().context_usage) == .25);
    usage.apply_turn(usage_session, "old", "ok", "older", "reply", {},
                     ContextUsage{4000, 8000, "old-model"}, 1);
    MIRAGE_CHECK(context_ratio(usage.current().context_usage) == .25);
    usage.apply_turn(usage_session, "u2", "pending", "new", {}, {}, {}, 3);
    MIRAGE_CHECK(context_ratio(usage.current().context_usage) == .25);
    usage.apply_turn(usage_session, "u2", "failed", "new", {}, "cancelled", {}, 3);
    MIRAGE_CHECK(context_ratio(usage.current().context_usage) == .25);
    usage.apply_turn(usage_session, "u3", "ok", "new", "reply", {}, ContextUsage{10, 0, "model"},
                     4);
    MIRAGE_CHECK(!context_ratio(usage.current().context_usage));
    usage.apply_turn(usage_session, "u4", "ok", "new", "reply", {}, {}, 5);
    MIRAGE_CHECK(!usage.current().context_usage);
    MIRAGE_CHECK(context_ratio(ContextUsage{0, 32768, "model"}) == 0);
    MIRAGE_CHECK(context_ratio(ContextUsage{40000, 32768, "model"}) > 1);
    MIRAGE_CHECK(mirage::native_ui::token_number(128000) == "128,000");
    usage.create_session();
    MIRAGE_CHECK(!usage.current().context_usage);
    usage.select_session(usage_session);
    usage.clear_current();
    MIRAGE_CHECK(!usage.current().context_usage && usage.current().usage_sequence == 0);
    ChatModel attachments;
    const auto attachment_session = attachments.current().id;
    attachments.apply_turn(attachment_session, "leading", "ok", "\n\n  用户任务\n正文", "回复", {});
    MIRAGE_CHECK(attachments.current().title == "用户任务");
    attachments.clear_current();
    MIRAGE_CHECK(attachments.attach(attachment_session, {0, "notes.txt", "内容"}));
    const auto attachment_only = attachments.submission_text();
    attachments.apply_turn(attachment_session, "attachment-only", "ok", attachment_only, "回复",
                           {});
    MIRAGE_CHECK(attachments.current().title == "附件：notes.txt");
    attachments.clear_current();
    MIRAGE_CHECK(attachments.attach(attachment_session, {0, "notes.txt", "内容"}));
    MIRAGE_CHECK(attachments.set_draft("我的附件任务"));
    const auto with_task = attachments.submission_text();
    attachments.apply_turn(attachment_session, "attachment-task", "ok", with_task, "回复", {});
    MIRAGE_CHECK(attachments.current().title == "我的附件任务");
    attachments.clear_current();
    MIRAGE_CHECK(attachments.attach(attachment_session, {0, "note.txt", "附件测试"}));
    const auto attachment_id = attachments.current().attachments.front().id;
    MIRAGE_CHECK(attachments.submission_text().find("附件测试") != std::string::npos);
    attachments.current().submitted_attachments = {attachment_id};
    MIRAGE_CHECK(attachments.attach(attachment_session, {0, "later.md", "后来的附件"}));
    attachments.acknowledge_submission(attachment_session);
    MIRAGE_CHECK(attachments.current().attachments.size() == 1);
    MIRAGE_CHECK(
        !attachments.attach(attachment_session, {0, "oversize.txt", std::string(8193, 'a')}));
    const auto generation = attachments.current().attachment_generation;
    attachments.clear_current();
    MIRAGE_CHECK(!attachments.attach(attachment_session, {0, "late.txt", "迟到"}, generation));
    MIRAGE_CHECK(attachments.current().attachments.empty());
    for (int i = 0; i < 4; ++i)
        MIRAGE_CHECK(attachments.attach(attachment_session, {0, "a.txt", "a"}));
    MIRAGE_CHECK(!attachments.attach(attachment_session, {0, "fifth.txt", "a"}));
    attachments.set_draft(std::string(ChatModel::max_text_bytes, 'x'));
    MIRAGE_CHECK(attachments.submission_text().empty());
    const auto directory =
        std::filesystem::temp_directory_path() /
        ("mirage-attachment-" +
         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(directory);
    const auto path = (directory / "note.txt").string();
    {
        std::ofstream file(path);
        file << "中文 UTF-8";
    }
    MIRAGE_CHECK(mirage::native_ui::read_text_attachment(path).attachment.has_value());
    {
        std::ofstream file(path, std::ios::binary);
        file.write("a\0b", 3);
    }
    MIRAGE_CHECK(!mirage::native_ui::read_text_attachment(path).error.empty());
    {
        std::ofstream file(path, std::ios::binary);
        file << "\xc0\x80";
    }
    MIRAGE_CHECK(!mirage::native_ui::read_text_attachment(path).attachment);
    {
        std::ofstream file(path);
        file << std::string(8193, 'x');
    }
    MIRAGE_CHECK(!mirage::native_ui::read_text_attachment(path).attachment);
    MIRAGE_CHECK(!mirage::native_ui::read_text_attachment(directory.string()).attachment);
#ifndef _WIN32
    const auto fifo = (directory / "fifo").string();
    MIRAGE_CHECK(::mkfifo(fifo.c_str(), 0600) == 0);
    MIRAGE_CHECK(!mirage::native_ui::read_text_attachment(fifo).attachment);
    std::filesystem::create_symlink(path, directory / "link");
    MIRAGE_CHECK(
        !mirage::native_ui::read_text_attachment((directory / "link").string()).attachment);
#endif
    std::filesystem::remove_all(directory);
    return mirage::testing::finish("native_chat_model_test");
}
