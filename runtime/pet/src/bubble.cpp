#include <algorithm>
#include <array>
#include <limits>
#include <mirage/runtime/pet/bubble.hpp>
#include <mirage/runtime/pet/status.hpp>
#include <vector>

namespace mirage::runtime::pet {
namespace {
std::size_t codepoint(std::string_view text, std::size_t pos) {
    const auto head = static_cast<unsigned char>(text[pos]);
    std::size_t length = head < 0x80                    ? 1
                         : head >= 0xc2 && head <= 0xdf ? 2
                         : head >= 0xe0 && head <= 0xef ? 3
                         : head >= 0xf0 && head <= 0xf4 ? 4
                                                        : 0;
    if (!length || length > text.size() - pos)
        return 0;
    for (std::size_t i = 1; i < length; ++i)
        if ((static_cast<unsigned char>(text[pos + i]) & 0xc0U) != 0x80U)
            return 0;
    if (length >= 3) {
        const auto next = static_cast<unsigned char>(text[pos + 1]);
        if ((head == 0xe0 && next < 0xa0) || (head == 0xed && next >= 0xa0) ||
            (head == 0xf0 && next < 0x90) || (head == 0xf4 && next >= 0x90))
            return 0;
    }
    return length;
}
std::size_t characters(std::string_view value) {
    std::size_t count = 0;
    for (std::size_t pos = 0; pos < value.size(); ++count) {
        const auto length = codepoint(value, pos);
        if (!length)
            return 100000;
        pos += length;
    }
    return count;
}
std::string trim(std::string_view text) {
    while (!text.empty() && (text.front() == ' ' || text.front() == '#' || text.front() == '*' ||
                             (text.front() == '-' && text.size() > 1 && text[1] == ' ')))
        text.remove_prefix(1);
    while (!text.empty() && (text.back() == ' ' || text.back() == '*' || text.back() == '#'))
        text.remove_suffix(1);
    return std::string(text);
}
bool has(std::string_view value, std::initializer_list<std::string_view> words) {
    return std::any_of(words.begin(), words.end(),
                       [value](auto word) { return value.find(word) != std::string_view::npos; });
}
} // namespace
std::string bubble_line(std::string_view text, std::size_t bound) {
    std::string result;
    result.reserve(std::min(text.size(), bound));
    bool space = false;
    for (std::size_t pos = 0; pos < text.size();) {
        const auto length = codepoint(text, pos);
        if (!length)
            return {};
        const auto part = text.substr(pos, length);
        pos += length;
        const auto head = static_cast<unsigned char>(part.front());
        if (head <= 0x20 || head == 0x7f || part == "　") {
            space = !result.empty();
            continue;
        }
        if (space) {
            result += ' ';
            space = false;
        }
        result.append(part);
        if (result.size() > bound) {
            std::size_t begin = result.size() - bound;
            while (begin < result.size() &&
                   (static_cast<unsigned char>(result[begin]) & 0xc0U) == 0x80U)
                ++begin;
            result.erase(0, begin);
        }
    }
    return result;
}
bool valid_bubble(const Bubble &b) {
    return (b.kind == "thinking" || b.kind == "dialog") &&
           (b.content_origin == "status_hint" || b.content_origin == "model_preview" ||
            b.content_origin == "answer_summary") &&
           b.request_id.size() <= 128 && b.request_id.find('\0') == std::string::npos &&
           !b.text.empty() && b.text.size() <= max_bubble_text_bytes &&
           bubble_line(b.text) == b.text &&
           b.sequence <= static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()) &&
           b.remaining_ms <= 5000 && (!b.final || b.kind == "dialog") &&
           (!b.final || b.remaining_ms > 0) &&
           (b.kind != "thinking" || b.content_origin != "answer_summary");
}
std::string answer_summary(std::string_view text, bool complete) {
    // The original qualifiers remain in the selected sentence. If a long
    // sentence cannot fit, fall back rather than inventing a shorter claim.
    if (text.size() > 16384) {
        text = text.substr(0, 16384);
        complete = false; // a budget cut is never a completed trailing phrase
        while (!text.empty() && (static_cast<unsigned char>(text.back()) & 0xc0U) == 0x80U)
            text.remove_suffix(1);
        if (!text.empty() && static_cast<unsigned char>(text.back()) >= 0xc0U)
            text.remove_suffix(1);
    }
    std::string candidate, best;
    int best_score = -1;
    bool code = false;
    bool line_start = true, skip_context = false, skip_body = false;
    std::size_t skip_heading = 0;
    auto qualified = [](std::string_view value) {
        std::string lower{value};
        for (auto &c : lower)
            if (c >= 'A' && c <= 'Z')
                c = static_cast<char>(c + ('a' - 'A'));
        for (auto courtesy : {"no problem", "no worries"})
            if (lower.starts_with(courtesy))
                lower.erase(0, std::string_view{courtesy}.size());
        return has(lower, {"不",   "未",    "没",      "无法",   "需要",   "尝试",    "可能",
                           "尚",   "待",    "计划",    "将",     "cannot", "not ",    "no ",
                           "need", "may ",  "never",   "unable", "might",  "pending", "unverified",
                           "n't",  "will ", "plan to", "try "});
    };
    auto pick = [&](const std::string &value, bool require_qualification) {
        if (value.empty() || characters(value) > 24 || (require_qualification && !qualified(value)))
            return false;
        float units = 0;
        for (std::size_t pos = 0; pos < value.size();) {
            const auto length = codepoint(value, pos);
            if (!length)
                return false;
            units += length == 1 ? 0.6f : 1.0f;
            pos += length;
        }
        if (units > 16)
            return false;
        std::string lower{value};
        for (auto &c : lower)
            if (c >= 'A' && c <= 'Z')
                c = static_cast<char>(c + ('a' - 'A'));
        const bool core =
            has(lower, {"修复",      "错误",  "原因",     "确认",    "完成",   "通过",
                        "找到",      "发现",  "配置",     "保存",    "创建",   "更新",
                        "检查",      "失败",  "done",     "fixed",   "failed", "found",
                        "saved",     "need",  "verified", "check",   "create", "updated",
                        "completed", "error", "config",   "blocked", "passed"});
        const bool numeric = std::any_of(value.begin(), value.end(),
                                         [](unsigned char c) { return c >= '0' && c <= '9'; }) &&
                             std::all_of(value.begin(), value.end(), [](unsigned char c) {
                                 return (c >= '0' && c <= '9') || c == '.' || c == '+' || c == '-';
                             });
        const bool scalar =
            complete && (numeric || value == "OK" || value == "ok" || value == "是" ||
                         value == "否" || value == "完成" || value == "通过" || value == "失败");
        if (characters(value) < 3 && value != "你好" && !scalar)
            return false;
        // Core facts and qualifiers share priority: a later complete fact
        // can replace an unsure preamble, and a later limitation cannot be
        // hidden behind an earlier success claim.
        int score = (core || qualified(value)) ? 2 : 0;
        if (score >= best_score) {
            best = value;
            best_score = score;
        }
        return true;
    };
    auto consider = [&] {
        auto value = trim(candidate);
        candidate.clear();
        std::string lower{value};
        for (auto &c : lower)
            if (c >= 'A' && c <= 'Z')
                c = static_cast<char>(c + ('a' - 'A'));
        if (has(lower, {"```", "http://", "https://", "如果", "假设", "例如", "比如", "if ",
                        "suppose ", "for example"}) ||
            value.ends_with(":") || value.ends_with("："))
            return;
        for (const auto prefix : {"好的，", "好的。", "当然，", "首先，", "总之，"})
            if (value.starts_with(prefix))
                value.erase(0, std::string_view(prefix).size());
        // Small explicit wording reductions keep modality/negation. Unsupported
        // long statements are handled by whole-clause selection, not truncation.
        for (const auto &[from, to] :
             std::initializer_list<std::pair<std::string_view, std::string_view>>{
                 {"还无法确定是哪项配置导致失败", "原因未明"},
                 {"需要补充日志后继续排查", "需要补充日志"},
                 {"找到了配置中的路径错误", "发现路径错误"},
                 {"接下来会修改配置并重新检查", "将修改配置并复查"},
                 {"已修复配置", "配置已修复"},
                 {"验证已经通过", "验证通过"},
                 {"当前操作需要你确认权限", "需要确认权限"},
                 {"确认后我才能继续", "确认后才能继续"}}) {
            const auto where = value.find(from);
            if (where != std::string::npos)
                value.replace(where, from.size(), to);
        }
        if (pick(value, false))
            return;
        const bool require_qualification = qualified(value);
        std::size_t begin = 0;
        while (begin < value.size()) {
            const auto chinese = value.find("，", begin);
            const auto english = value.find(",", begin);
            const auto comma = std::min(chinese, english);
            const auto end = comma == std::string::npos ? value.size() : comma;
            pick(trim(std::string_view(value).substr(begin, end - begin)), require_qualification);
            if (comma == std::string::npos)
                break;
            begin = comma + (comma == chinese ? 3 : 1);
        }
    };
    for (std::size_t pos = 0; pos < text.size();) {
        if (line_start && !code) {
            const auto end = text.find('\n', pos);
            auto line =
                text.substr(pos, end == std::string_view::npos ? text.size() - pos : end - pos);
            while (!line.empty() &&
                   (line.front() == ' ' || line.front() == '\t' || line.front() == '\r'))
                line.remove_prefix(1);
            while (!line.empty() &&
                   (line.back() == ' ' || line.back() == '\t' || line.back() == '\r'))
                line.remove_suffix(1);
            std::size_t heading = 0;
            while (heading < line.size() && line[heading] == '#')
                ++heading;
            if (heading && (heading == line.size() || line[heading] != ' '))
                heading = 0;
            std::string lower{trim(line)};
            for (auto &c : lower)
                if (c >= 'A' && c <= 'Z')
                    c = static_cast<char>(c + ('a' - 'A'));
            if (skip_context && heading && (!skip_heading || heading <= skip_heading)) {
                skip_context = skip_body = false;
                skip_heading = 0;
            }
            const bool example_header = (heading || line.ends_with(":") || line.ends_with("：")) &&
                                        has(lower, {"示例", "例子", "例如", "比如", "引用", "原文",
                                                    "example", "sample", "e.g."});
            const bool condition = line.starts_with("如果") || line.starts_with("假设") ||
                                   line.starts_with("假如") || line.starts_with("假定") ||
                                   line.starts_with("只要") || line.starts_with("一旦") ||
                                   (line.starts_with("若") && !line.starts_with("若干")) ||
                                   lower.starts_with("if ") || lower.starts_with("suppose ") ||
                                   lower.starts_with("assuming ") || lower.starts_with("once ") ||
                                   lower.starts_with("when ") || lower.starts_with("provided ");
            const bool quoted =
                line.starts_with(">") || line.starts_with("“") || line.starts_with("\"");
            if (example_header || condition || quoted) {
                skip_context = true;
                skip_body = skip_body || !example_header;
                if (!skip_heading)
                    skip_heading = heading; // preserve the outer example section
            }
            if (line.empty() && skip_context && skip_body && !skip_heading) {
                skip_context = skip_body = false;
            }
            if (heading || skip_context || line.empty()) {
                if (skip_context && !example_header && !line.empty())
                    skip_body = true;
                candidate.clear(); // a title/quote/example is never an agent outcome
                pos = end == std::string_view::npos ? text.size() : end + 1;
                continue;
            }
            line_start = false;
        }
        if (text.substr(pos, 3) == "```") {
            consider();
            code = !code;
            pos += 3;
            continue;
        }
        const auto length = codepoint(text, pos);
        if (!length)
            return {};
        const auto part = text.substr(pos, length);
        pos += length;
        if (part == "\n")
            line_start = true;
        if (code)
            continue;
        if (part == "\n" || part == "。" || part == "！" || part == "？" || part == ";" ||
            part == "；" ||
            ((part == "." || part == "!" || part == "?") &&
             (pos == text.size() || text[pos] == ' ' || text[pos] == '\n'))) {
            consider();
            continue;
        }
        candidate.append(part);
    }
    if (complete)
        consider();
    return bubble_line(best, 256);
}
std::optional<Bubble> BubbleProjection::update(const Status &status, bool model_active,
                                               std::string_view final_reply,
                                               const Preview *thinking, const Preview *answer,
                                               Clock::time_point now) {
    if (source_ != status.owner_epoch + ":" + status.source_id) {
        *this = BubbleProjection{};
        source_ = status.owner_epoch + ":" + status.source_id;
    }
    const bool allowed =
        status.host_state == "running" && status.source_kind == "turn" &&
        (status.display_state == "thinking" || status.display_state == "task_success");
    const auto newest = std::max(thinking ? thinking->sequence : 0, answer ? answer->sequence : 0);
    if (!allowed || (!model_active && status.display_state != "task_success")) {
        consumed_ = std::max(consumed_, newest);
        has_answer_ = false;
        request_.clear();
        pending_answer_.clear();
        bubble_ = {};
        return std::nullopt;
    }
    if (status.display_state == "task_success") {
        if (!settled_) {
            settled_ = true;
            settled_at_ = now;
            if (answer && !answer->request_id.empty())
                request_ = answer->request_id;
            else if (thinking && !thinking->request_id.empty())
                request_ = thinking->request_id;
            auto text = answer_summary(final_reply);
            bubble_ = {"dialog",  text.empty() ? "status_hint" : "answer_summary",
                       request_,  text.empty() ? "回复已结束。" : std::move(text),
                       consumed_, false,
                       true,      5000};
        }
        const auto elapsed =
            std::chrono::duration_cast<std::chrono::milliseconds>(now - settled_at_).count();
        if (elapsed >= 5000)
            return std::nullopt;
        bubble_.remaining_ms =
            5000 - static_cast<std::uint64_t>(std::max<std::int64_t>(0, elapsed));
        return bubble_;
    }
    if (settled_)
        return std::nullopt; // late stream cannot revive a settled turn
    std::array<std::pair<const Preview *, bool>, 2> updates{{{thinking, false}, {answer, true}}};
    std::sort(updates.begin(), updates.end(), [](const auto &a, const auto &b) {
        return (a.first ? a.first->sequence : 0) < (b.first ? b.first->sequence : 0);
    });
    for (const auto &[value, body] : updates) {
        if (!value || value->sequence <= consumed_ || value->request_id.empty() ||
            value->request_id.size() > 128 || value->text.size() > 16384)
            continue;
        consumed_ = value->sequence;
        if (request_ != value->request_id || value->text.empty()) {
            request_ = value->request_id;
            pending_answer_.clear();
            has_answer_ = false;
            summary_at_ = {};
            bubble_ = {};
        }
        if (body && !value->text.empty()) {
            has_answer_ = true;
            pending_answer_ = value->text;
        } else if (!body && !has_answer_) {
            auto line = bubble_line(value->text);
            bubble_ = {"thinking", line.empty() ? "status_hint" : "model_preview",
                       request_,   line.empty() ? "正在思考…" : std::move(line),
                       consumed_,  value->truncated || value->text.size() > max_bubble_text_bytes,
                       false,      0};
        }
    }
    if (has_answer_) {
        if (bubble_.kind != "dialog")
            bubble_ = {"dialog", "status_hint", request_, "正在回复…", consumed_, false, false, 0};
        if (summary_at_ == Clock::time_point{} ||
            now - summary_at_ >= std::chrono::milliseconds{500}) {
            auto text = answer_summary(pending_answer_, false);
            if (!text.empty())
                bubble_ = {
                    "dialog", "answer_summary", request_, std::move(text), consumed_, false, false,
                    0};
            summary_at_ = now;
        }
    } else if (bubble_.kind.empty()) {
        bubble_ = {"thinking", "status_hint", request_, "正在思考…", consumed_, false, false, 0};
    }
    return bubble_;
}
} // namespace mirage::runtime::pet
