#include "../../integration/mira/src/ime_text_input.hpp"
#include "../support/test.hpp"
#include <iostream>
#include <kairo/executor.hpp>
#include <stdexcept>
using namespace mirage;
using namespace mirage::integration::detail;
namespace {
struct Fixture {
    bool chinese = false, composed = false, done = false, stopped = false;
    int page = 0, reads = 0, actions = 0, candidate_reads = 0;
    std::string actual, fault;
    ImeIo io() {
        ImeIo value;
        value.stopped = [&] { return stopped; };
        value.send = [&](bool key, const std::string &text) {
            ++actions;
            if (fault == "throw")
                throw std::runtime_error("injected");
            if (fault == "cancel" && text == "ni") {
                stopped = true;
                return false;
            }
            if (key && text == "shift") {
                chinese = !chinese;
                return true;
            }
            if (text == "ctrl+a" || text == "backspace") {
                actual.clear();
                return true;
            }
            if (!key && text == "ni") {
                composed = true;
                page = 0;
                return true;
            }
            if (!key && !chinese) {
                actual += text;
                return true;
            }
            if (text == "=") {
                candidate_reads = 0;
                if (fault != "repeat")
                    ++page;
                return true;
            }
            if (text == "3" && page == 1) {
                actual = fault == "wrong_commit" ? "尼" : "你";
                composed = false;
                done = true;
                return true;
            }
            MIRAGE_CHECK(false);
            return false;
        };
        value.read = [&](ImeRoi roi) -> std::optional<integration::ImageOcrOutcome> {
            ++reads;
            integration::ImageOcrOutcome out;
            out.ok = true;
            std::string text;
            if (roi == ImeRoi::mode)
                text = fault == "unknown_mode" ? "?" : chinese ? "中" : "英";
            else if (roi == ImeRoi::target)
                text = actual;
            else if (composed || (done && fault == "not_closed"))
                text = page == 0                     ? "1. 尼 2. 泥"
                       : fault == "ambiguous"        ? "1. 你 3. 你"
                       : fault == "duplicate_labels" ? "3. 尼 3. 你"
                                                     : "1. 拟 3. 你";
            if (roi == ImeRoi::candidates && composed && fault == "render_delay" && page == 1 &&
                candidate_reads++ == 0)
                text = "1. 尼 2. 泥";
            if (!text.empty())
                out.regions = {{text, fault == "low_confidence" ? 0.5 : 0.99, 0, 0, 10, 10}};
            if (roi == ImeRoi::candidates && composed && page == 1 && fault == "split_boxes")
                out.regions = {{"3.", .99, 30, 15, 8, 10},
                               {"你", .99, 40, 10, 12, 20},
                               {"1. 拟 2. 18:50", .99, 0, 11, 28, 18},
                               {"< >", .2, 55, 10, 20, 20}};
            if (fault == "partial")
                out.total_region_count = out.regions.size() + 1;
            if (fault == "transient" && roi == ImeRoi::target && reads % 2)
                out.regions.clear();
            if (fault == "target_fragments" && roi == ImeRoi::target)
                out.regions = {{"a", .99, 0, 0, 10, 10}, {"b", .99, 20, 0, 10, 10}};
            if (fault == "target_spaces" && roi == ImeRoi::target)
                out.regions = {{" ab ", .99, 0, 0, 30, 10}};
            return out;
        };
        return value;
    }
};
} // namespace
int main() {
    ImePlan plan{{"你", "ni"}};
    MIRAGE_CHECK(valid_ime_plan("你", plan));
    MIRAGE_CHECK(valid_ime_plan("你好", {{"你好", "nihao"}}));
    for (auto bad : {ImePlan{{"你", "NI"}}, ImePlan{{"你好a", "nihao"}}, ImePlan{{"x", "ni"}},
                     ImePlan{{"😀", ""}}})
        MIRAGE_CHECK(!valid_ime_plan("你", bad));
    MIRAGE_CHECK(!valid_ime_plan("你你", plan));
    MIRAGE_CHECK(!valid_ime_plan(std::string(33, 'a'), {{std::string(33, 'a'), ""}}));
    unsigned successes = 0;
    for (unsigned i = 0; i < 100; ++i) {
        Fixture fixture;
        fixture.chinese = i % 2;
        const auto result = execute_ime_text(plan, fixture.io());
        MIRAGE_CHECK(result.ok && fixture.actual == "你" && fixture.done);
        MIRAGE_CHECK(result.verified_bytes == 3 && result.pages == 1);
        ++successes;
    }
    for (const auto *fault : {"repeat", "unknown_mode", "low_confidence", "wrong_commit",
                              "ambiguous", "duplicate_labels", "not_closed", "cancel", "partial"}) {
        Fixture fixture;
        fixture.fault = fault;
        const auto result = execute_ime_text(plan, fixture.io());
        MIRAGE_CHECK(!result.ok && result.verified_bytes == 0);
        MIRAGE_CHECK(fixture.actions < 12);
    }
    for (const auto *fault : {"split_boxes", "transient", "render_delay"}) {
        Fixture fixture;
        fixture.fault = fault;
        MIRAGE_CHECK(execute_ime_text(plan, fixture.io()).ok);
    }
    Fixture mixed;
    const auto mixed_result = execute_ime_text({{"你", "ni"}, {"ab 12", ""}}, mixed.io());
    MIRAGE_CHECK(mixed_result.ok && mixed.actual == "你ab 12" && !mixed.chinese);
    Fixture english;
    english.chinese = true;
    MIRAGE_CHECK(execute_ime_text({{"Hello 42", ""}}, english.io()).ok);
    MIRAGE_CHECK(english.actual == "Hello 42" && !english.chinese);
    for (const auto *fault : {"target_fragments", "target_spaces"}) {
        Fixture ambiguous_value;
        ambiguous_value.fault = fault;
        MIRAGE_CHECK(!execute_ime_text({{"ab", ""}}, ambiguous_value.io()).ok);
    }
    {
        ImePlan long_plan(32, {"你", "ni"});
        bool composed = false;
        unsigned page = 0;
        std::string actual;
        ImeIo bounded;
        bounded.stopped = [] { return false; };
        bounded.send = [&](bool, const std::string &value) {
            if (value == "ni") {
                composed = true;
                page = 0;
            }
            if (value == "=")
                ++page;
            if (value == "9") {
                composed = false;
                actual += "你";
            }
            return true;
        };
        bounded.read = [&](ImeRoi roi) -> std::optional<integration::ImageOcrOutcome> {
            integration::ImageOcrOutcome out;
            out.ok = true;
            std::string text = roi == ImeRoi::mode ? "中" : roi == ImeRoi::target ? actual : "";
            if (roi == ImeRoi::candidates && composed) {
                text = page == 7 ? "9. 你" : "1. 尼";
                if (page != 7)
                    for (unsigned n = 0; n < page; ++n)
                        text += "甲";
            }
            if (!text.empty())
                out.regions = {{text, .99, 0, 0, 20, 20}};
            return out;
        };
        const auto budget = execute_ime_text(long_plan, bounded);
        MIRAGE_CHECK(!budget.ok && budget.error == "roi_budget_exhausted" && budget.reads == 256);
        MIRAGE_CHECK(budget.actions <= 256 && budget.verified_bytes > 0);
    }
    Fixture initial_cancel;
    initial_cancel.stopped = true;
    MIRAGE_CHECK(!execute_ime_text(plan, initial_cancel.io()).ok && initial_cancel.actions == 0);
    kairo::Executor owner;
    kairo::ExecutorConfig config;
    config.min_threads = config.max_threads = 1;
    MIRAGE_CHECK(owner.initialize(config));
    bool exception = false;
    try {
        owner
            .submit_auto([&] {
                Fixture f;
                f.fault = "throw";
                return execute_ime_text(plan, f.io());
            })
            .get();
    } catch (const std::runtime_error &) {
        exception = true;
    }
    MIRAGE_CHECK(exception);
    owner.shutdown(true);
    bool rejected = false;
    try {
        owner.submit_auto([] {}).get();
    } catch (const std::exception &) {
        rejected = true;
    }
    MIRAGE_CHECK(rejected);
    std::cout << "controlled controller trials: " << successes
              << "/100; not real OCR/IME accuracy\n";
    return testing::finish("ime_text_input_test");
}
