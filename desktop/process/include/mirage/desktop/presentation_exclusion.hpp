#pragma once
#include <memory>
#include <mirage/desktop/cancellation.hpp>
#include <mirage/desktop/provider_error.hpp>

namespace mirage::desktop {
// DEC-103: product-owned surface, never a model-selected window. A lease is
// used by one finite tool operation. Different leases may overlap. release()
// is idempotent, ignores cancellation for cleanup, and reports restore failure.
class PresentationLease {
  public:
    virtual ~PresentationLease() = default;
    // DEC-106: false is an explicit capability fallback, not a hidden surface.
    virtual bool excluded() const { return true; }
    virtual ProviderError verify(const CancelToken &) = 0;
    virtual ProviderError release() = 0;
};
struct PresentationExclusion {
    std::unique_ptr<PresentationLease> lease;
    ProviderError error;
};
class PresentationExcluder {
  public:
    virtual ~PresentationExcluder() = default;
    virtual PresentationExclusion acquire(const CancelToken &) = 0;
};
} // namespace mirage::desktop
