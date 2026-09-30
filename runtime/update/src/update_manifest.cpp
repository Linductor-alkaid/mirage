#include <mirage/runtime/update/update_manifest.hpp>

#include <mira/json.hpp>

#include <algorithm>
#include <cctype>
#include <iomanip>
#include <sstream>

#include <openssl/sha.h>

namespace mirage::runtime::update {
namespace {

std::string to_hex(const unsigned char *data, std::size_t len) {
    std::ostringstream out;
    out << std::hex << std::setfill('0');
    for (std::size_t i = 0; i < len; ++i) {
        out << std::setw(2) << static_cast<int>(data[i]);
    }
    return out.str();
}

bool is_lower_hex_digest(const std::string &text) {
    if (text.size() != 64) {
        return false;
    }
    return std::all_of(text.begin(), text.end(), [](unsigned char c) {
        return std::isdigit(c) != 0 || (c >= 'a' && c <= 'f');
    });
}

} // namespace

std::string sha256_hex(const std::string &bytes) {
    unsigned char digest[SHA256_DIGEST_LENGTH];
    SHA256(reinterpret_cast<const unsigned char *>(bytes.data()), bytes.size(), digest);
    return to_hex(digest, sizeof(digest));
}

std::optional<UpdateManifest> decode_update_manifest(const std::string &json_text,
                                                     DecodeError &error) {
    const auto fail = [&error](std::string code, std::string message) {
        error.code = std::move(code);
        error.message = std::move(message);
        return std::nullopt;
    };

    if (json_text.size() > UpdateManifest::kMaxManifestBytes) {
        return fail("manifest_too_large", "manifest exceeds the byte budget");
    }

    const auto parsed = mira::parse_json(json_text);
    if (!parsed) {
        return fail("invalid_json", "manifest is not valid JSON: " + parsed.error().safe_message);
    }
    const auto &root = parsed.value();
    if (!root.as_object()) {
        return fail("not_an_object", "manifest is not a JSON object");
    }

    UpdateManifest manifest;

    const auto *schema = root.find("schema");
    if (schema == nullptr || !schema->is_string() ||
        *schema->as_string() != "mirage-update-manifest") {
        return fail("unknown_schema", "manifest schema is not 'mirage-update-manifest'");
    }
    manifest.schema = *schema->as_string();

    const auto *schema_version = root.find("schema_version");
    if (schema_version == nullptr || schema_version->kind() != mira::JsonValue::Kind::Integer ||
        schema_version->as_integer() != 1) {
        return fail("unsupported_schema_version", "manifest schema_version must be 1");
    }
    manifest.schema_version = *schema_version->as_integer();

    const auto *version = root.find("version");
    if (version == nullptr || !version->is_string() || version->as_string()->empty() ||
        version->as_string()->size() > UpdateManifest::kMaxVersionBytes) {
        return fail("bad_version", "manifest 'version' must be a non-empty string");
    }
    manifest.version = *version->as_string();

    const auto *timestamp = root.find("timestamp");
    if (timestamp == nullptr || !timestamp->is_string() || timestamp->as_string()->empty() ||
        timestamp->as_string()->size() > UpdateManifest::kMaxTimestampBytes) {
        return fail("bad_timestamp", "manifest 'timestamp' must be a non-empty string");
    }
    manifest.timestamp = *timestamp->as_string();

    const auto *files = root.find("files");
    if (files == nullptr || !files->as_array()) {
        return fail("bad_files", "manifest 'files' must be an array");
    }
    const auto &array = *files->as_array();
    if (array.empty() || array.size() > UpdateManifest::kMaxFiles) {
        return fail("bad_files", "manifest 'files' must hold 1..64 entries");
    }
    for (const auto &entry : array) {
        if (!entry.as_object()) {
            return fail("bad_files", "manifest file entry is not an object");
        }
        UpdateFile file;
        const auto *name = entry.find("name");
        const auto *size = entry.find("size");
        const auto *sha = entry.find("sha256");
        if (name == nullptr || !name->is_string() || name->as_string()->empty() ||
            name->as_string()->size() > UpdateManifest::kMaxNameBytes) {
            return fail("bad_files", "file entry 'name' must be a non-empty string");
        }
        if (size == nullptr || size->kind() != mira::JsonValue::Kind::Integer ||
            *size->as_integer() < 0) {
            return fail("bad_files", "file entry 'size' must be a non-negative integer");
        }
        if (sha == nullptr || !sha->is_string() || !is_lower_hex_digest(*sha->as_string())) {
            return fail("bad_files", "file entry 'sha256' must be lowercase 64-hex");
        }
        file.name = *name->as_string();
        file.size = static_cast<std::uint64_t>(*size->as_integer());
        file.sha256 = *sha->as_string();
        manifest.files.push_back(std::move(file));
    }

    // Duplicate names would make the apply order ambiguous — refused.
    for (std::size_t i = 0; i < manifest.files.size(); ++i) {
        for (std::size_t j = i + 1; j < manifest.files.size(); ++j) {
            if (manifest.files[i].name == manifest.files[j].name) {
                return fail("bad_files", "duplicate file name in manifest");
            }
        }
    }
    return manifest;
}

} // namespace mirage::runtime::update
