#include "attachment.hpp"
#include <array>
#include <filesystem>
#include <fstream>
#ifndef _WIN32
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif
namespace mirage::native_ui {
namespace {
bool utf8_text(const std::string &text) {
    for (std::size_t i = 0; i < text.size();) {
        const auto first = static_cast<unsigned char>(text[i++]);
        if (first < 0x80) {
            if (first < 32 && first != '\n' && first != '\r' && first != '\t')
                return false;
            continue;
        }
        unsigned int value = first & (first < 0xe0 ? 31U : first < 0xf0 ? 15U : 7U);
        const unsigned int length = first >= 0xc2 && first < 0xe0    ? 2
                                    : first < 0xf0 && first >= 0xe0  ? 3
                                    : first <= 0xf4 && first >= 0xf0 ? 4
                                                                     : 0;
        if (!length || i + length - 1 > text.size())
            return false;
        for (unsigned int j = 1; j < length; ++j) {
            const auto next = static_cast<unsigned char>(text[i++]);
            if ((next & 0xc0) != 0x80)
                return false;
            value = (value << 6) | (next & 63);
        }
        if ((length == 2 && value < 128) || (length == 3 && value < 2048) ||
            (length == 4 && value < 65536) || (value >= 0xd800 && value <= 0xdfff) ||
            value > 0x10ffff)
            return false;
    }
    return true;
}
} // namespace
AttachmentResult read_text_attachment(const std::string &path) {
    AttachmentResult result;
    if (path.empty() || path.size() > 4096) {
        result.error = "文件路径无效。";
        return result;
    }
    std::array<char, 8193> bytes{};
    std::size_t count = 0;
#ifndef _WIN32
    const int fd = ::open(path.c_str(), O_RDONLY | O_NONBLOCK | O_NOFOLLOW | O_CLOEXEC);
    if (fd < 0) {
        result.error = "无法读取文件，请选择可访问的普通文本文件。";
        return result;
    }
    struct stat info {};
    if (::fstat(fd, &info) != 0 || !S_ISREG(info.st_mode) || info.st_size > 8192) {
        ::close(fd);
        result.error = "仅支持不超过 8 KiB 的普通文本文件。";
        return result;
    }
    while (count < bytes.size()) {
        const auto n = ::read(fd, bytes.data() + count, bytes.size() - count);
        if (n == 0)
            break;
        if (n < 0) {
            ::close(fd);
            result.error = "读取失败，请重新选择文件。";
            return result;
        }
        count += static_cast<std::size_t>(n);
    }
    ::close(fd);
#else
    std::error_code error;
    const auto info = std::filesystem::symlink_status(
        std::filesystem::path(std::u8string(path.begin(), path.end())), error);
    if (error || !std::filesystem::is_regular_file(info)) {
        result.error = "请选择普通文本文件。";
        return result;
    }
    std::ifstream stream(std::filesystem::path(std::u8string(path.begin(), path.end())),
                         std::ios::binary);
    stream.read(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    count = static_cast<std::size_t>(stream.gcount());
    if (stream.bad()) {
        result.error = "读取失败，请重新选择文件。";
        return result;
    }
#endif
    std::string text(bytes.data(), count);
    if (count > 8192 || !utf8_text(text)) {
        result.error = "附件需为 UTF-8 文本，且不超过 8 KiB。";
        return result;
    }
    if (text.starts_with("\xef\xbb\xbf"))
        text.erase(0, 3);
    if (text.empty()) {
        result.error = "文件为空，请选择有内容的文本文件。";
        return result;
    }
    const auto name =
        std::filesystem::path(std::u8string(path.begin(), path.end())).filename().u8string();
    result.attachment = TextAttachment{0, std::string(name.begin(), name.end()), std::move(text)};
    return result;
}
} // namespace mirage::native_ui
