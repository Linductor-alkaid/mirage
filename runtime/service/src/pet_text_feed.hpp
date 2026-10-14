#pragma once
#include <atomic>
#include <kairo/comm.hpp>
#include <mirage/runtime/pet/bubble.hpp>

namespace mirage::runtime::detail {
// One pair of bounded latest full snapshots per active dialog turn. Transport
// callbacks only validate/publish; Runtime's existing serial projection consumes.
struct PetTextFeed {
    kairo::comm::LatestMailbox<pet::Preview> thinking{"pet-thinking-preview"};
    kairo::comm::LatestMailbox<pet::Preview> answer{"pet-answer-preview"};
    std::atomic_uint64_t sequence{0};
    void publish(bool body, const std::string &request, const std::string &text, bool truncated) {
        if (request.empty() || request.size() > 128 || text.size() > 16384)
            return;
        pet::Preview value{request, text, sequence.fetch_add(1) + 1, truncated};
        (body ? answer : thinking).publish(value);
    }
};
} // namespace mirage::runtime::detail
