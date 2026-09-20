#pragma once

#include <cstddef>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

#include <mirador/detector_backend.hpp>
#include <mirador/ocr_backend.hpp>

#include "mirador_service.hpp"

namespace mirage::integration {

/// Outcome of one registration attempt. Failures are explicit and carry the
/// reason; nothing is silently overwritten or truncated (RULE-07).
struct VisualBackendRegistration {
    bool ok = false;
    std::string error; ///< meaningful only when ok is false
};

/// Bounded, name-addressed home for the visual backends a process wires in
/// (plan item `M3-02`): "SPI 注册 + VisualBackendIdentity 校验接线". A backend
/// is admitted only when
///
/// 1. its Mirage-side `VisualBackendIdentity` passes
///    `validate_backend_identity` (the mirador capability contract), and
/// 2. the identity is consistent with the backend's own `info()` query —
///    name, implementation version, model id, model revision and the
///    accepted-format preference order must match exactly, because the
///    identity drives cache keys and format gating (RULE-07). A backend that
///    misreports itself cannot be used: registration fails closed, mirroring
///    the mirador "capability/identity query failed" semantics.
///
/// Registrations are capacity-bounded; a full registry rejects further
/// entries instead of evicting. Thread-safe: registrations and lookups may
/// come from different wiring contexts. The registry stores non-owning
/// backend pointers — callers keep the backend objects alive.
class VisualBackendRegistry {
  public:
    /// Upper bound of registered backends per kind (RULE-07).
    static constexpr std::size_t kMaxBackends = 8;

    /// Validates and registers one OCR backend under `identity.name`.
    [[nodiscard]] VisualBackendRegistration register_ocr(const VisualBackendIdentity &identity,
                                                         mirador::OcrBackend &backend);
    /// Validates and registers one detector backend under `identity.name`.
    [[nodiscard]] VisualBackendRegistration register_detector(const VisualBackendIdentity &identity,
                                                              mirador::DetectorBackend &backend);

    /// Registered OCR backend by name, or nullptr.
    [[nodiscard]] mirador::OcrBackend *find_ocr(std::string_view name) const;
    /// Registered detector backend by name, or nullptr.
    [[nodiscard]] mirador::DetectorBackend *find_detector(std::string_view name) const;

    /// Registered names, in registration order (diagnostics).
    [[nodiscard]] std::vector<std::string> names() const;
    [[nodiscard]] std::size_t size() const;

  private:
    [[nodiscard]] VisualBackendRegistration insert(std::string_view name, bool detector,
                                                   void *backend);

    mutable std::mutex mutex_;
    struct Entry {
        std::string name;
        bool detector = false;
        void *backend = nullptr;
    };
    std::vector<Entry> entries_;
};

} // namespace mirage::integration
