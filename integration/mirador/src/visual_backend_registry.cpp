#include <mirage/integration/visual_backend_registry.hpp>

#include <mirador/backend_info.hpp>

#include "pixel_format_names.hpp"

#include <optional>
#include <utility>

namespace mirage::integration {
namespace {

/// The Mirage-side identity must describe the backend exactly: mirador feeds
/// `info()` into capability-result cache keys and format gating, so a
/// mismatch between the wiring declaration and the backend's own query would
/// make the registry lie about what is registered.
bool identity_matches_backend(const VisualBackendIdentity &identity,
                              const mirador::BackendInfo &info) {
    if (identity.name != info.name ||
        identity.implementation_version != info.implementation_version ||
        identity.model_id != info.model_id || identity.model_revision != info.model_revision) {
        return false;
    }
    if (identity.accepted_formats.size() != info.accepted_formats.size()) {
        return false;
    }
    for (std::size_t i = 0; i < identity.accepted_formats.size(); ++i) {
        // The accepted-format list is a preference order; it must be equal
        // element by element, not merely as a set.
        const std::optional<mirador::PixelFormat> mapped =
            detail::to_mirador_format(identity.accepted_formats[i]);
        if (!mapped || *mapped != info.accepted_formats[i]) {
            return false;
        }
    }
    return true;
}

} // namespace

VisualBackendRegistration VisualBackendRegistry::register_ocr(const VisualBackendIdentity &identity,
                                                              mirador::OcrBackend &backend) {
    if (!validate_backend_identity(identity)) {
        return {false, "backend identity fails the mirador capability contract"};
    }
    if (!identity_matches_backend(identity, backend.info())) {
        return {false, "backend identity does not match the backend's own info() query"};
    }
    return insert(identity.name, false, &backend);
}

VisualBackendRegistration
VisualBackendRegistry::register_detector(const VisualBackendIdentity &identity,
                                         mirador::DetectorBackend &backend) {
    if (!validate_backend_identity(identity)) {
        return {false, "backend identity fails the mirador capability contract"};
    }
    if (!identity_matches_backend(identity, backend.info())) {
        return {false, "backend identity does not match the backend's own info() query"};
    }
    return insert(identity.name, true, &backend);
}

mirador::OcrBackend *VisualBackendRegistry::find_ocr(std::string_view name) const {
    std::lock_guard lock(mutex_);
    for (const Entry &entry : entries_) {
        if (!entry.detector && entry.name == name) {
            return static_cast<mirador::OcrBackend *>(entry.backend);
        }
    }
    return nullptr;
}

mirador::DetectorBackend *VisualBackendRegistry::find_detector(std::string_view name) const {
    std::lock_guard lock(mutex_);
    for (const Entry &entry : entries_) {
        if (entry.detector && entry.name == name) {
            return static_cast<mirador::DetectorBackend *>(entry.backend);
        }
    }
    return nullptr;
}

std::vector<std::string> VisualBackendRegistry::names() const {
    std::lock_guard lock(mutex_);
    std::vector<std::string> result;
    result.reserve(entries_.size());
    for (const Entry &entry : entries_) {
        result.push_back(entry.name);
    }
    return result;
}

std::size_t VisualBackendRegistry::size() const {
    std::lock_guard lock(mutex_);
    return entries_.size();
}

VisualBackendRegistration VisualBackendRegistry::insert(std::string_view name, bool detector,
                                                        void *backend) {
    std::lock_guard lock(mutex_);
    if (entries_.size() >= kMaxBackends) {
        return {false, "registry is full"};
    }
    for (const Entry &entry : entries_) {
        if (entry.name == name) {
            return {false, "a backend is already registered under this name"};
        }
    }
    entries_.push_back({std::string(name), detector, backend});
    return {true, {}};
}

} // namespace mirage::integration
