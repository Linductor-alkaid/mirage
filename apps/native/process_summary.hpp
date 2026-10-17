#pragma once
#include <mirage/conversation.hpp>
#include <string>
#include <vector>

namespace mirage::native_ui {
inline std::vector<std::string> process_timing_lines(const conversation::Timings &timings) {
    if (timings.empty())
        return {};
    std::vector<std::string> lines;
    for (const auto &[label, phase] : {std::pair{"推理", timings.inference},
                                       {"取帧", timings.capture},
                                       {"OCR", timings.ocr},
                                       {"收尾重验", timings.settlement}}) {
        if (!phase.calls)
            lines.push_back(std::string(label) + " · 未计量");
        else
            lines.push_back(std::string(label) + " · " + std::to_string(phase.calls) + " 次 · " +
                            std::to_string(phase.duration_ms / 1000) + "." +
                            std::to_string((phase.duration_ms % 1000) / 100) + " 秒");
    }
    lines.emplace_back("收尾可包含取帧和 OCR，耗时不直接相加");
    return lines;
}
inline std::string thinking_detail(const conversation::Part &part) {
    if (part.text.empty() && (part.truncated || part.omitted_for_display_budget))
        return "思考内容因展示预算省略，不代表模型没有继续思考。";
    return part.text;
}
} // namespace mirage::native_ui
