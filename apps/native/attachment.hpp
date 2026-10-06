#pragma once
#include <cstdint>
#include <optional>
#include <string>
namespace mirage::native_ui {
struct TextAttachment {
    std::uint64_t id = 0;
    std::string name;
    std::string text;
};
struct AttachmentResult {
    std::optional<TextAttachment> attachment;
    std::string error;
};
// Only user-selected regular UTF-8 files; the caller owns Executor scheduling.
AttachmentResult read_text_attachment(const std::string &path);
} // namespace mirage::native_ui
