#include "../support/test.hpp"

#include <functional>
#include <mirage/desktop/accessibility_provider.hpp>
#include <stdexcept>

namespace d = mirage::desktop;
namespace {
class Reader final : public d::AccessibilityProvider {
  public:
    bool supported = true, throw_capability = false;
    int reads = 0;
    std::string value = "deepseek";
    std::function<void(d::TextValueReadOutcome &, const d::CancelToken &)> alter;
    bool text_value_read_supported() const override {
        if (throw_capability)
            throw std::runtime_error("private backend failure");
        return supported;
    }
    d::TextValueReadOutcome read_text_value(const d::ElementTarget &target,
                                            const d::TextValueReadLimits &limits,
                                            const d::CancelToken &cancel) override {
        ++reads;
        MIRAGE_CHECK(limits.max_bytes <= 4096 && limits.timeout.count() <= 5000);
        d::TextValueReadOutcome result;
        result.read_started = std::chrono::steady_clock::now();
        result.ok = result.complete = result.committed = true;
        result.reference = target.reference.id;
        result.snapshot_scope = target.snapshot_scope;
        result.source = d::TextValueSource::accessibility;
        result.utf8_text = value;
        result.read_finished = std::chrono::steady_clock::now();
        if (alter)
            alter(result, cancel);
        return result;
    }
    d::SnapshotOutcome semantic_snapshot(const std::string &, const d::SemanticSnapshotLimits &,
                                         const d::CancelToken &) override {
        return {};
    }
    d::ElementActionOutcome activate_element(const d::ElementTarget &,
                                             const d::CancelToken &) override {
        throw std::runtime_error("verification must not activate");
    }
    d::ElementActionOutcome set_text(const d::ElementTarget &, const std::string &,
                                     const d::InputLimits &, const d::CancelToken &) override {
        throw std::runtime_error("verification must not write");
    }
};
d::TextValueCondition condition() {
    d::TextValueCondition result;
    result.target.reference.id = "@e5";
    result.target.snapshot_scope = "scope:one";
    result.expected = "deepseek";
    return result;
}
} // namespace

int main() {
    Reader reader;
    auto c = condition();
    const auto verify = [&] { return d::verify_text_value(reader, c, {}, {}); };
    const auto matched = verify();
    MIRAGE_CHECK(matched.status == d::TextValueStatus::matched);
    MIRAGE_CHECK(matched.reference == c.target.reference.id &&
                 matched.snapshot_scope == c.target.snapshot_scope && matched.version == 1);
    reader.value = "deepseke";
    MIRAGE_CHECK(verify().status == d::TextValueStatus::mismatched);
    reader.value = "de epseek";
    MIRAGE_CHECK(verify().status == d::TextValueStatus::mismatched);
    reader.value = "Deepseek";
    MIRAGE_CHECK(verify().status == d::TextValueStatus::mismatched);
    reader.value = "deepseek\n";
    MIRAGE_CHECK(verify().status == d::TextValueStatus::mismatched);
    // Empty, UTF-8 and the exact boundary are full values, not missing fields.
    for (const auto &value : {std::string{}, std::string{"中文🌐"}, std::string(4096, 'x')}) {
        reader.value = c.expected = value;
        MIRAGE_CHECK(verify().status == d::TextValueStatus::matched);
    }
    c = condition();
    reader.value = c.expected;
    const auto reads = reader.reads;
    for (int i = 0; i < 8; ++i) {
        auto bad = c;
        d::TextValueReadLimits limits;
        if (i == 0)
            bad.target.reference.id.clear();
        if (i == 1)
            bad.target.snapshot_scope.clear();
        if (i == 2)
            bad.target.semantic.name = "same named field";
        if (i == 3)
            bad.expected.assign(4097, 'x');
        if (i == 4)
            bad.expected = "\xff";
        if (i == 5)
            limits.max_bytes = 0;
        if (i == 6)
            limits.max_bytes = 4097;
        if (i == 7)
            limits.timeout = std::chrono::milliseconds{0};
        const auto result = d::verify_text_value(reader, bad, limits, {});
        MIRAGE_CHECK(result.status == d::TextValueStatus::verification_unavailable);
        MIRAGE_CHECK(result.reason == "invalid_condition");
    }
    MIRAGE_CHECK(reader.reads == reads);
    reader.supported = false;
    MIRAGE_CHECK(verify().reason == "text_value_read_unsupported");
    MIRAGE_CHECK(reader.reads == reads);
    reader.supported = true;
    reader.throw_capability = true;
    MIRAGE_CHECK(verify().reason == "text_value_read_failed");
    MIRAGE_CHECK(reader.reads == reads);
    reader.throw_capability = false;
    d::CancelToken cancelled;
    cancelled.request_cancel();
    MIRAGE_CHECK(d::verify_text_value(reader, c, {}, cancelled).reason == "cancelled");
    MIRAGE_CHECK(reader.reads == reads);

    for (int i = 0; i < 13; ++i) {
        reader.alter = [i](auto &read, const auto &) {
            if (i == 0)
                read.reference = "@e6";
            if (i == 1)
                read.snapshot_scope = "scope:two";
            if (i == 2)
                read.read_started = {};
            if (i == 3)
                read.read_finished = read.read_started - std::chrono::seconds{1};
            if (i == 4)
                read.read_finished += std::chrono::hours{1};
            if (i == 5)
                read.complete = false;
            if (i == 6)
                read.committed = false;
            if (i == 7)
                read.protected_content = true;
            if (i == 8)
                read.source = d::TextValueSource::none;
            if (i == 9)
                read.utf8_text = "\xff";
            if (i == 10)
                read.utf8_text.assign(4097, 'x');
            if (i == 11)
                read.ok = false;
            if (i == 12)
                read.cancelled = true;
        };
        const auto before = reader.reads;
        const auto result = verify();
        MIRAGE_CHECK(reader.reads == before + 1);
        MIRAGE_CHECK(result.status == d::TextValueStatus::verification_unavailable);
        MIRAGE_CHECK(result.source == d::TextValueSource::none);
        MIRAGE_CHECK(result.read_started == std::chrono::steady_clock::time_point{});
    }
    reader.alter = [](auto &, const auto &) { throw std::runtime_error("private text"); };
    MIRAGE_CHECK(verify().reason == "text_value_read_failed");
    reader.alter = [&](auto &, const auto &) { cancelled.request_cancel(); };
    cancelled = {};
    MIRAGE_CHECK(d::verify_text_value(reader, c, {}, cancelled).reason == "cancelled");
    // Expiry during one synchronous read must never produce a match.
    reader.alter = [](auto &, const auto &cancel) {
        while (!cancel.cancelled()) {
        }
    };
    d::TextValueReadLimits tiny;
    tiny.timeout = std::chrono::milliseconds{1};
    MIRAGE_CHECK(d::verify_text_value(reader, c, tiny, {}).reason == "deadline_exceeded");
    // A fresh retry reads again; a previous mismatch never substitutes for it.
    reader.alter = {};
    reader.value = "wrong";
    MIRAGE_CHECK(verify().status == d::TextValueStatus::mismatched);
    reader.value = c.expected;
    MIRAGE_CHECK(verify().status == d::TextValueStatus::matched);
    return mirage::testing::finish("target_text_value_test");
}
