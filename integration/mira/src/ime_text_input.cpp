#include "ime_text_input.hpp"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <mirador/text_normalize.hpp>
#include <mirage/desktop/input_provider.hpp>
#include <set>
#include <sstream>

namespace mirage::integration::detail {
namespace {
std::string trim(std::string s) {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back())))
        s.pop_back();
    std::size_t first = 0;
    while (first < s.size() && std::isspace(static_cast<unsigned char>(s[first])))
        ++first;
    return s.substr(first);
}
bool ascii(const std::string &s) {
    return !s.empty() &&
           std::all_of(s.begin(), s.end(), [](unsigned char c) { return c >= 32 && c < 127; });
}
struct Candidate {
    char number;
    std::string text;
    double confidence;
};
bool is_label(const std::string &s, std::size_t i) {
    if (s[i] < '1' || s[i] > '9' ||
        (i && (std::isdigit(static_cast<unsigned char>(s[i - 1])) || s[i - 1] == ':')))
        return false;
    if (i + 1 == s.size())
        return true;
    const auto next = static_cast<unsigned char>(s[i + 1]);
    return next >= 0x80 || next == '.' || next == ')' || std::isspace(next);
}
std::vector<Candidate> candidates(const std::string &s, const std::vector<double> &scores = {}) {
    std::vector<Candidate> out;
    std::size_t i = 0;
    while (i < s.size()) {
        if (!is_label(s, i)) {
            ++i;
            continue;
        }
        const auto label = i;
        const char key = s[i++];
        while (i < s.size() && (s[i] == ' ' || s[i] == '.' || s[i] == ':' || s[i] == ')'))
            ++i;
        for (const auto *mark : {"、", "．", "："})
            if (s.compare(i, std::char_traits<char>::length(mark), mark) == 0)
                i += std::char_traits<char>::length(mark);
        const auto begin = i;
        while (i < s.size() && !is_label(s, i))
            ++i;
        auto value = trim(s.substr(begin, i - begin));
        // OCR can include visible page controls after the final candidate.
        for (const auto *suffix : {"下一页", "上一页", "<", ">", "←", "→"}) {
            const auto pos = value.find(suffix);
            if (pos != std::string::npos)
                value = trim(value.substr(0, pos));
        }
        if (!value.empty()) {
            double confidence = 1;
            if (scores.size() == s.size())
                confidence = *std::min_element(scores.begin() + label, scores.begin() + i);
            out.push_back({key, std::move(value), confidence});
        }
    }
    return out;
}
} // namespace
bool valid_ime_plan(const std::string &text, const ImePlan &plan) {
    if (text.empty() || text.size() > 256 || !desktop::is_valid_utf8(text) || plan.empty() ||
        plan.size() > 32)
        return false;
    std::string assembled;
    std::size_t count = 0;
    for (const auto &unit : plan) {
        if (unit.pinyin.empty()) {
            if (!ascii(unit.text))
                return false;
            count += unit.text.size();
        } else {
            // A short basic Han phrase. Other scripts/punctuation must
            // gain a separately validated transport before being advertised.
            if (unit.text.empty() || unit.text.size() > 24 || unit.text.size() % 3 ||
                !desktop::is_valid_utf8(unit.text))
                return false;
            for (std::size_t i = 0; i < unit.text.size(); i += 3) {
                const auto *p = reinterpret_cast<const unsigned char *>(unit.text.data() + i);
                const unsigned cp = ((p[0] & 15u) << 12) | ((p[1] & 63u) << 6) | (p[2] & 63u);
                if (cp < 0x3400 || cp > 0x9fff)
                    return false;
            }
            if (unit.pinyin.empty() || unit.pinyin.size() > 64 ||
                !std::all_of(unit.pinyin.begin(), unit.pinyin.end(),
                             [](char c) { return c >= 'a' && c <= 'z'; }))
                return false;
            count += unit.text.size() / 3;
        }
        assembled += unit.text;
    }
    return count <= 32 && assembled == text;
}
ImeResult execute_ime_text(const ImePlan &plan, const ImeIo &io) {
    ImeResult result;
    const auto fail = [&](const char *code) {
        if (result.error.empty())
            result.error = code;
        return result;
    };
    std::vector<double> candidate_scores;
    const auto read = [&](ImeRoi roi) -> std::optional<std::string> {
        if (io.stopped())
            return {};
        if (result.reads >= 256) {
            result.error = "roi_budget_exhausted";
            return {};
        }
        ++result.reads;
        auto out = io.read(roi);
        if (!out || !out->ok || out->cancelled || out->regions.size() > 128 ||
            out->total_region_count > out->regions.size() ||
            (out->diagnostics && (out->diagnostics->partial || out->diagnostics->det_limit_hit)))
            return {};
        // A final-value proof cannot concatenate separate boxes: their gap
        // could contain a space or line break absent from the requested text.
        // The initial contract accepts one complete OCR line only.
        if (roi == ImeRoi::target && out->regions.size() != 1)
            return {};
        // Reuse Mirador's public line merger for geometry/order. Marker
        // indices retain each original region's confidence: merging an entire
        // candidate row at minimum confidence would let an unrelated weak
        // candidate suppress a clearly recognized target/number.
        std::vector<mirador::TextRegion> markers;
        for (std::size_t i = 0; i < out->regions.size(); ++i) {
            const auto &r = out->regions[i];
            markers.push_back({{float(r.x), float(r.y), float(r.width), float(r.height)},
                               std::to_string(i),
                               float(r.confidence),
                               {}});
        }
        mirador::LineMergeParams params;
        params.max_center_offset_ratio = 0.65;
        params.max_gap_ratio = 3;
        params.max_lines = 128;
        params.text_separator = " ";
        const auto lines = mirador::merge_text_lines(markers, params);
        if (!lines)
            return {};
        std::string text;
        if (roi == ImeRoi::candidates)
            candidate_scores.clear();
        for (const auto &line : lines.value()) {
            std::istringstream indices(line.utf8_text);
            std::vector<std::size_t> members;
            std::size_t index;
            while (indices >> index) {
                if (index >= out->regions.size())
                    return {};
                members.push_back(index);
            }
            // Keep original IME label/glyph positions inside the merged line;
            // an intermediate union box can overlap a later numeric label.
            std::stable_sort(members.begin(), members.end(),
                             [&](auto a, auto b) { return out->regions[a].x < out->regions[b].x; });
            for (const auto member : members) {
                const auto &region = out->regions[member];
                // Page arrows and decorations are not candidate evidence. Do not
                // let their OCR confidence suppress otherwise legible labels.
                if (roi == ImeRoi::candidates &&
                    std::none_of(region.text.begin(), region.text.end(),
                                 [](unsigned char c) { return std::isalnum(c) || c >= 0x80; }))
                    continue;
                if (!std::isfinite(region.confidence) || region.confidence < 0 ||
                    (roi != ImeRoi::candidates && region.confidence < 0.85) ||
                    region.confidence > 1 || region.text.size() > 1024 ||
                    !desktop::is_valid_utf8(region.text) || text.size() + region.text.size() > 4096)
                    return {};
                if (roi == ImeRoi::candidates && !text.empty()) {
                    text += ' ';
                    candidate_scores.push_back(1);
                }
                text += region.text;
                if (roi == ImeRoi::candidates)
                    candidate_scores.insert(candidate_scores.end(), region.text.size(),
                                            region.confidence);
            }
        }
        return roi == ImeRoi::mode ? trim(std::move(text)) : text;
    };
    const auto send = [&](bool key, const std::string &text) {
        if (io.stopped())
            return false;
        if (result.actions >= 256) {
            result.error = "action_budget_exhausted";
            return false;
        }
        ++result.actions;
        return io.send(key, text) && !io.stopped();
    };
    // UI painting is asynchronous. Only repeat observations (at most three
    // fresh frames), never composition, selection or mode-switch actions.
    const auto reread = [&](ImeRoi roi, const auto &accepted) -> std::optional<std::string> {
        for (unsigned attempt = 0; attempt < 3 && !io.stopped(); ++attempt) {
            auto current = read(roi);
            if (current && accepted(*current))
                return current;
        }
        return {};
    };
    const auto parse_mode = [](const std::string &s) {
        return s == "中" || s == "中文" ? 1 : s == "英" || s == "英文" ? 0 : -1;
    };
    const auto mode = [&](bool chinese) {
        auto current = reread(ImeRoi::mode, [&](const auto &s) { return parse_mode(s) >= 0; });
        if (!current)
            return false;
        const int value = parse_mode(*current);
        if (value == int(chinese))
            return true;
        if (!send(true, "shift"))
            return false;
        return bool(
            reread(ImeRoi::mode, [&](const auto &s) { return parse_mode(s) == int(chinese); }));
    };
    result.phase = "preflight";
    auto pending = read(ImeRoi::candidates);
    if (!pending)
        return fail("candidate_ocr_failed");
    if (!candidates(*pending).empty())
        return fail("preedit_unresolved");
    if (!reread(ImeRoi::mode, [&](const auto &s) { return parse_mode(s) >= 0; }))
        return fail("ime_mode_unconfirmed");
    // Every send is a complete action; never hold keys across observations.
    result.phase = "replace";
    if (!send(true, "ctrl+a") || !send(true, "backspace"))
        return fail("input_failed");
    std::string prefix;
    for (const auto &unit : plan) {
        result.phase = "mode";
        if (!mode(!unit.pinyin.empty()))
            return fail("ime_mode_unconfirmed");
        result.phase = "compose";
        if (!send(false, unit.pinyin.empty() ? unit.text : unit.pinyin))
            return fail("input_failed");
        if (!unit.pinyin.empty()) {
            result.phase = "candidate";
            std::set<std::string> seen;
            bool selected = false;
            for (unsigned page = 0; page < 8; ++page) {
                std::string previous, stable;
                std::vector<Candidate> entries;
                // Input release does not imply that the IME has painted the
                // last syllable. Require the same numbered page in two fresh
                // OCR frames before selection/paging. Ignore preedit/caret
                // decorations when comparing pages, never the candidate text.
                for (unsigned attempt = 0; attempt < 3 && !io.stopped(); ++attempt) {
                    const auto text = read(ImeRoi::candidates);
                    if (!text)
                        continue;
                    auto current = candidates(*text, candidate_scores);
                    if (current.empty() || current.size() > 9)
                        continue;
                    std::string signature;
                    for (const auto &entry : current)
                        signature += entry.number + std::to_string(entry.text.size()) + ":" +
                                     entry.text + ";";
                    if (signature == previous && !seen.contains(signature)) {
                        stable = std::move(signature);
                        entries = std::move(current);
                        break;
                    }
                    previous = std::move(signature);
                }
                if (stable.empty())
                    return fail(seen.empty() ? "candidate_labels_unreadable"
                                             : "candidate_page_repeated");
                seen.insert(std::move(stable));
                std::string key;
                std::set<char> numbers;
                for (const auto &[number, value, confidence] : entries) {
                    if (!numbers.insert(number).second)
                        return fail("candidate_labels_ambiguous");
                    if (value == unit.text) {
                        if (confidence < 0.85)
                            return fail("candidate_target_low_confidence");
                        if (!key.empty())
                            return fail("candidate_ambiguous");
                        key.assign(1, number);
                    }
                }
                if (!key.empty()) {
                    if (!send(true, key))
                        return fail("input_failed");
                    selected = true;
                    break;
                }
                if (page == 7)
                    break;
                if (!send(true, "="))
                    return fail("input_failed");
                ++result.pages;
            }
            if (!selected)
                return fail("candidate_not_found");
            pending =
                reread(ImeRoi::candidates, [&](const auto &s) { return candidates(s).empty(); });
            if (!pending)
                return fail("candidate_not_closed");
        }
        prefix += unit.text;
        result.phase = "verify";
        const auto actual = reread(ImeRoi::target, [&](const auto &s) { return s == prefix; });
        if (!actual)
            return fail("target_text_unconfirmed");
        result.verified_bytes = prefix.size();
    }
    result.ok = true;
    result.phase = "complete";
    return result;
}
} // namespace mirage::integration::detail
