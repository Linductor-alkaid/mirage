#pragma once

#include <chrono>
#include <future>
#include <iostream>
#include <kairo/executor.hpp>
#include <memory>
#include <mirage/desktop/frontend_process.hpp>
#include <stdexcept>
#include <string>

namespace mirage::runtime::detail {
// Serial-context control; only stop() leaves that context. While its retained
// future is live, nobody else accesses the platform process or OS handle.
class PetProcess final {
  public:
    using Clock = std::chrono::steady_clock;
    PetProcess(kairo::Executor &executor, std::shared_ptr<desktop::FrontendProcess> process)
        : executor_(executor), process_(std::move(process)) {}

    bool start(const std::string &endpoint, std::string &diagnostic,
               Clock::time_point now = Clock::now()) {
        poll(now);
        if (closing_ || stop_.valid()) {
            diagnostic = "pet is stopping";
            return false;
        }
        if (pid_ && (state_ == "starting" || state_ == "running"))
            return true;
        if (surface_ownership_.use_count() > 1) {
            diagnostic = "pet is yielding to a desktop operation; retry after it settles";
            return false;
        }
        if (pid_) {
            diagnostic = "pet still owns a live process; stop it before restarting";
            return false;
        }
        try {
            if (!process_->open(endpoint, diagnostic)) {
                fail(diagnostic.empty() ? "pet launch refused" : diagnostic);
                diagnostic = diagnostic_;
                return false;
            }
            pid_ = process_->pid();
            if (!pid_) {
                fail("pet exited during launch");
                diagnostic = diagnostic_;
                return false;
            }
        } catch (const std::exception &error) {
            fail(error.what());
            diagnostic = diagnostic_;
            return false; // explicit operation failure; never takes down the Agent
        }
        state_ = "starting";
        diagnostic_.clear();
        started_ = now;
        return true;
    }
    bool ready(std::int64_t pid, std::string &diagnostic) {
        poll();
        if (closing_ || !pid || pid != pid_ || (state_ != "starting" && state_ != "running")) {
            diagnostic = "pet readiness does not match the owned starting process";
            return false;
        }
        state_ = "running";
        return true;
    }
    desktop::PresentationExclusion exclude_presentation(const desktop::CancelToken &cancel) {
        if (cancel.cancelled())
            return {{}, {"cancelled", "pet exclusion cancelled"}};
        if (closing_ || stop_.valid())
            return {{}, {"pet_exclusion_unavailable", "pet owner is stopping"}};
        poll();
        desktop::PresentationExclusion exclusion;
        if (pid_) {
            if (state_ != "running")
                return {{},
                        {"pet_exclusion_unavailable", "pet is not ready for surface exclusion"}};
            exclusion = process_->exclude_presentation(cancel);
            if (!exclusion.error.code.empty())
                return exclusion;
            if (!exclusion.lease)
                return {{}, {"pet_exclusion_failed", "owned pet returned no exclusion lease"}};
        }
        // Even absent pets need an ownership lease: a concurrent Start must
        // not introduce a new visible generation during this tool operation.
        exclusion.lease =
            std::make_unique<SurfaceLease>(std::move(exclusion.lease), surface_ownership_);
        return exclusion;
    }
    bool stop(std::string &diagnostic) {
        poll();
        if (closing_) {
            diagnostic = "pet owner is closing";
            return false;
        }
        if (stop_.valid())
            return true; // repeated Stop is idempotent; no second task/handle
        if (!pid_) {
            state_ = "stopped";
            diagnostic_.clear();
            return true;
        }
        return admit_stop(false, diagnostic);
    }
    void poll(Clock::time_point now = Clock::now()) {
        if (stop_.valid()) {
            if (stop_.wait_for(std::chrono::milliseconds{0}) != std::future_status::ready)
                return;
            try {
                stop_.get();
                state_ = bootstrap_failed_ ? "failed" : "stopped";
            } catch (const std::exception &error) {
                fail(error.what());
            }
            bootstrap_failed_ = false;
        }
        const auto previous = pid_;
        pid_ = process_->pid();
        if (!pid_ && previous && (state_ == "starting" || state_ == "running"))
            fail("pet process exited unexpectedly; start it again to recover");
        if (pid_ && state_ == "starting" && now - started_ >= std::chrono::seconds{10}) {
            diagnostic_ = "pet did not report readiness within 10 seconds";
            std::string diagnostic;
            (void)admit_stop(true, diagnostic);
        }
    }
    const std::string &state() const { return state_; }
    const std::string &diagnostic() const { return diagnostic_; }
    std::int64_t pid() const { return pid_; } // cached while stop task owns the process

    // Fence on serial after producers stop, then join on the external owner.
    void begin_shutdown() { closing_ = true; }
    bool finish_shutdown(std::string &diagnostic) {
        bool clean = true;
        if (stop_.valid()) {
            try {
                stop_.get();
            } catch (const std::exception &error) {
                diagnostic = error.what();
                clean = false;
            }
        }
        std::string final_diagnostic;
        if (!process_->stop(final_diagnostic)) {
            diagnostic = final_diagnostic;
            clean = false;
        }
        return clean;
    }

  private:
    struct SurfaceOwnership {};
    class SurfaceLease final : public desktop::PresentationLease {
      public:
        SurfaceLease(std::unique_ptr<desktop::PresentationLease> lease,
                     std::shared_ptr<const SurfaceOwnership> ownership)
            : lease_(std::move(lease)), ownership_(std::move(ownership)) {}
        ~SurfaceLease() override {
            const auto error = release();
            if (!error.code.empty())
                std::cerr << "mira_pet: " << error.code << ": " << error.message << '\n';
        }
        bool excluded() const override { return lease_ ? lease_->excluded() : true; }
        desktop::ProviderError verify(const desktop::CancelToken &cancel) override {
            if (!ownership_)
                return {"pet_exclusion_failed", "pet exclusion was released"};
            if (cancel.cancelled())
                return {"cancelled", "pet exclusion cancelled"};
            return lease_ ? lease_->verify(cancel) : desktop::ProviderError{};
        }
        desktop::ProviderError release() override {
            if (!ownership_)
                return {};
            desktop::ProviderError result;
            try {
                if (lease_)
                    result = lease_->release();
            } catch (...) {
                result = {"pet_restore_failed", "pet exclusion cleanup threw"};
            }
            lease_.reset();
            ownership_.reset();
            return result;
        }

      private:
        std::unique_ptr<desktop::PresentationLease> lease_;
        std::shared_ptr<const SurfaceOwnership> ownership_;
    };
    // Serial admits new leases and Start; returned tokens only release their
    // own references on tool workers. This is resource ownership, no queue.
    std::shared_ptr<const SurfaceOwnership> surface_ownership_ =
        std::make_shared<SurfaceOwnership>();
    bool admit_stop(bool bootstrap_failed, std::string &diagnostic) {
        try {
            stop_ = executor_.submit_auto([process = process_] {
                std::string detail;
                if (!process->stop(detail))
                    throw std::runtime_error(detail.empty() ? "pet stop failed" : detail);
            });
            bootstrap_failed_ = bootstrap_failed;
            state_ = "stopping";
            if (!bootstrap_failed)
                diagnostic_.clear();
            // An immediately rejected admission must be consumed now.
            if (stop_.wait_for(std::chrono::milliseconds{0}) == std::future_status::ready)
                poll();
            if (state_ == "failed") {
                diagnostic = diagnostic_;
                return false;
            }
            return true;
        } catch (const std::exception &error) {
            fail(error.what());
            diagnostic = diagnostic_;
            return false;
        }
    }
    void fail(std::string diagnostic) {
        state_ = "failed";
        diagnostic_ = diagnostic.substr(0, 512);
    }
    kairo::Executor &executor_;
    std::shared_ptr<desktop::FrontendProcess> process_;
    std::future<void> stop_;
    std::int64_t pid_ = 0;
    Clock::time_point started_{};
    std::string state_ = "stopped", diagnostic_;
    bool closing_ = false, bootstrap_failed_ = false;
};
} // namespace mirage::runtime::detail
