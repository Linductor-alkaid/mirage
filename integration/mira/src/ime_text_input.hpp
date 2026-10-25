#pragma once
#include <functional>
#include <mirage/integration/desktop_tool_context.hpp>
#include <optional>
#include <string>
#include <vector>

namespace mirage::integration::detail {
struct ImeUnit {
    std::string text, pinyin;
};
using ImePlan = std::vector<ImeUnit>;
using ImePlanner =
    std::function<std::optional<ImePlan>(const std::string &, const mira::OperationContext &)>;
enum class ImeRoi { mode, candidates, target };
struct ImeIo {
    std::function<std::optional<ImageOcrOutcome>(ImeRoi)> read;
    std::function<bool(bool key, const std::string &)> send;
    std::function<bool()> stopped;
};
struct ImeResult {
    bool ok = false;
    std::string error, phase;
    std::size_t verified_bytes = 0;
    unsigned actions = 0, reads = 0, pages = 0;
};
/// Pure language-plan validation and finite deterministic ROI OCR control.
/// No model, screen, scheduler, platform types or input ownership live here.
bool valid_ime_plan(const std::string &, const ImePlan &);
ImeResult execute_ime_text(const ImePlan &, const ImeIo &);
} // namespace mirage::integration::detail
