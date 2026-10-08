#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace mirage::conversation {
// DEC-049: display projection only; never replayed as instructions/tool authority.
inline constexpr std::size_t max_parts = 96;
inline constexpr std::size_t max_part_text = 16 * 1024;
inline constexpr std::size_t max_content_bytes = 64 * 1024;
struct Part {
    std::string id;
    std::string kind;                // thinking / text / tool
    std::string status = "complete"; // pending / running / complete / failed / cancelled
    std::string text = {};
    std::string name = {};
    std::string input = {};
    std::string output = {};
    bool truncated = false;
    friend bool operator==(const Part &, const Part &) = default;
};
bool valid_parts(const std::vector<Part> &parts);
// JSON is hidden behind product types; both IPC and persistence use this codec.
std::string encode_parts(const std::vector<Part> &parts);
bool decode_parts(std::string_view json, std::vector<Part> &parts, std::string &error);
// Bounded UTF-8 prefix; true when projection was shortened.
bool truncate_text(std::string &text, std::size_t limit);
} // namespace mirage::conversation
