#include "ui_scheme_handler.hpp"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <utility>

#include "include/cef_response.h"
#include "include/cef_stream.h"
#include "include/wrapper/cef_stream_resource_handler.h"

namespace mirage::desktop_shell {
namespace {

/// The scheme prefix every handled URL starts with ("mirage://app/").
constexpr const char *kUrlPrefix = "mirage://app/";

const char *index_content_type(const std::filesystem::path &path) {
    const std::string extension = path.extension().string();
    if (extension == ".html" || extension == ".htm") {
        return "text/html";
    }
    if (extension == ".js" || extension == ".mjs") {
        return "text/javascript";
    }
    if (extension == ".css") {
        return "text/css";
    }
    if (extension == ".json" || extension == ".map") {
        return "application/json";
    }
    if (extension == ".svg") {
        return "image/svg+xml";
    }
    if (extension == ".png") {
        return "image/png";
    }
    if (extension == ".webp") {
        return "image/webp";
    }
    if (extension == ".ico") {
        return "image/x-icon";
    }
    if (extension == ".woff") {
        return "font/woff";
    }
    if (extension == ".woff2") {
        return "font/woff2";
    }
    if (extension == ".ttf") {
        return "font/ttf";
    }
    if (extension == ".txt") {
        return "text/plain";
    }
    return "application/octet-stream";
}

/// Percent-decodes one path component; false on malformed escapes.
bool percent_decode(const std::string &input, std::string &out) {
    out.clear();
    out.reserve(input.size());
    for (std::size_t index = 0; index < input.size(); ++index) {
        if (input[index] != '%') {
            out.push_back(input[index]);
            continue;
        }
        if (index + 2 >= input.size()) {
            return false;
        }
        const auto hex = [](char digit) -> int {
            if (digit >= '0' && digit <= '9') {
                return digit - '0';
            }
            if (digit >= 'a' && digit <= 'f') {
                return digit - 'a' + 10;
            }
            if (digit >= 'A' && digit <= 'F') {
                return digit - 'A' + 10;
            }
            return -1;
        };
        const int high = hex(input[index + 1]);
        const int low = hex(input[index + 2]);
        if (high < 0 || low < 0) {
            return false;
        }
        out.push_back(static_cast<char>((high << 4) | low));
        index += 2;
    }
    return true;
}

/// Maps a URL path onto a file inside `root`; false when the path escapes
/// the root or contains traversal segments. The result is the UTF-8 path.
bool resolve_asset_path(const std::string &root, const std::string &url_path,
                        std::string &resolved) {
    std::string decoded;
    if (!percent_decode(url_path, decoded)) {
        return false;
    }
    std::filesystem::path relative;
    std::string segment;
    for (const char character : decoded) {
        if (character == '/' || character == '\\') {
            if (segment == "..") {
                return false; // traversal: fail closed, never normalize away
            }
            if (!segment.empty() && segment != ".") {
                relative /= segment;
            }
            segment.clear();
            continue;
        }
        segment.push_back(character);
    }
    if (segment == "..") {
        return false;
    }
    if (!segment.empty() && segment != ".") {
        relative /= segment;
    }
    if (relative.empty()) {
        relative = "index.html"; // the app root
    }
    const std::filesystem::path full = std::filesystem::path(root) / relative;
    std::error_code error;
    if (!std::filesystem::is_regular_file(full, error)) {
        return false;
    }
    resolved = full.string();
    return true;
}

} // namespace

CefRefPtr<CefResourceHandler> UiSchemeFactory::Create(CefRefPtr<CefBrowser> /*browser*/,
                                                      CefRefPtr<CefFrame> /*frame*/,
                                                      const CefString & /*scheme_name*/,
                                                      CefRefPtr<CefRequest> request) {
    std::string url = request->GetURL().ToString();
    const std::size_t prefix = url.find(kUrlPrefix);
    std::string path;
    if (prefix != std::string::npos) {
        path = url.substr(prefix + std::strlen(kUrlPrefix));
    }
    // The browser appends its own query/fragment (e.g. ?transport=desktop);
    // they are not part of the asset path.
    const std::size_t cut = path.find_first_of("?#");
    if (cut != std::string::npos) {
        path.resize(cut);
    }

    std::string file;
    if (!resolve_asset_path(root_, path, file)) {
        static char kNotFound[] = "not found";
        return new CefStreamResourceHandler(
            404, "Not Found", "text/plain", {},
            CefStreamReader::CreateForData(kNotFound, sizeof(kNotFound) - 1));
    }
    return new CefStreamResourceHandler(index_content_type(file),
                                        CefStreamReader::CreateForFile(file));
}

} // namespace mirage::desktop_shell
