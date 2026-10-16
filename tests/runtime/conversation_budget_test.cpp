#include "../support/test.hpp"
#include <cstdio>
#include <mirage/conversation.hpp>

// DEC-093: categorized budget allocation verification.
namespace mirage::conversation {
namespace {

void test_category_budgets() {
    // Verify the budget constants match DEC-093.
    MIRAGE_CHECK(max_tool_budget == 40 * 1024);
    MIRAGE_CHECK(max_text_budget == 16 * 1024);
    MIRAGE_CHECK(max_thinking_budget == 8 * 1024);
    MIRAGE_CHECK(max_tool_budget + max_text_budget + max_thinking_budget == max_content_bytes);
}

void test_thinking_does_not_block_tools() {
    // Simulate BUG-20261015-001: long thinking should not prevent tool records.
    std::vector<Part> parts;

    // Add a long thinking part (8 KiB).
    Part thinking;
    thinking.id = "1.0";
    thinking.kind = "thinking";
    thinking.status = "complete"; // Required by valid_parts.
    thinking.text = std::string(max_thinking_budget, 'x');
    parts.push_back(thinking);

    MIRAGE_CHECK(valid_parts(parts));

    // Tool records should still have their full budget (40 KiB).
    Part tool;
    tool.id = "1.1";
    tool.kind = "tool";
    tool.name = "desktop.screen.capture";
    tool.status = "complete";
    tool.input = "{\"display_id\":\"0\"}";
    tool.output = std::string(10 * 1024, 'y'); // 10 KiB output
    parts.push_back(tool);

    MIRAGE_CHECK(valid_parts(parts));

    // Verify thinking + tool is within total budget.
    std::size_t total = 0;
    for (const auto &p : parts) {
        total += p.text.size() + p.input.size() + p.output.size();
    }
    MIRAGE_CHECK(total <= max_content_bytes);
    std::printf("  thinking + tool total: %zu bytes (max: %zu)\n", total, max_content_bytes);
}

void test_tools_have_priority_over_thinking() {
    std::vector<Part> parts;

    // Add multiple tool calls (should consume up to 40 KiB).
    for (int i = 0; i < 4; ++i) {
        Part tool;
        tool.id = "1." + std::to_string(i);
        tool.kind = "tool";
        tool.name = "desktop.vision.ocr";
        tool.status = "complete";
        tool.input = "{\"display_id\":\"0\"}";
        // Each tool has 9 KiB output (4 * 9 = 36 KiB < 40 KiB).
        tool.output = std::string(9 * 1024, 'a' + i);
        parts.push_back(tool);
    }

    MIRAGE_CHECK(valid_parts(parts));

    // Thinking budget (8 KiB) is independent.
    Part thinking;
    thinking.id = "1.10";
    thinking.kind = "thinking";
    thinking.status = "complete"; // Required by valid_parts.
    thinking.text = std::string(max_thinking_budget, 'z');
    parts.push_back(thinking);

    MIRAGE_CHECK(valid_parts(parts));

    // Total should be tool (36 KiB) + thinking (8 KiB) = 44 KiB < 64 KiB.
    std::size_t total = 0;
    for (const auto &p : parts) {
        total += p.text.size() + p.input.size() + p.output.size();
    }
    MIRAGE_CHECK(total <= max_content_bytes);
    MIRAGE_CHECK(total > 40 * 1024); // More than just tool budget.
    std::printf("  multi-tool + thinking total: %zu bytes\n", total);
}

void test_final_text_budget() {
    std::vector<Part> parts;

    // Tools consume their budget.
    Part tool;
    tool.id = "1.0";
    tool.kind = "tool";
    tool.name = "desktop.screen.capture";
    tool.status = "complete";
    tool.input = "{\"display_id\":\"0\"}";
    tool.output = std::string(30 * 1024, 'x');
    parts.push_back(tool);

    // Final text has its own 16 KiB budget.
    Part text;
    text.id = "1.1";
    text.kind = "text";
    text.status = "complete"; // Required by valid_parts.
    text.text = std::string(max_text_budget, 'y');
    parts.push_back(text);

    std::printf("  tool: input=%zu, output=%zu\n", tool.input.size(), tool.output.size());
    std::printf("  text: text=%zu\n", text.text.size());
    std::printf("  max_tool_budget=%zu, max_text_budget=%zu\n", max_tool_budget, max_text_budget);
    MIRAGE_CHECK(valid_parts(parts));

    // Total: 30 KiB (tool) + 16 KiB (text) = 46 KiB < 64 KiB.
    std::size_t total = 0;
    for (const auto &p : parts) {
        total += p.text.size() + p.input.size() + p.output.size();
    }
    MIRAGE_CHECK(total <= max_content_bytes);
    std::printf("  tool + final text total: %zu bytes\n", total);
}

void test_truncate_text() {
    std::string text = "Hello, 世界! This is a test string.";
    std::string original = text;

    // Truncate to 10 bytes (should be UTF-8 safe).
    bool truncated = truncate_text(text, 10);
    MIRAGE_CHECK(truncated);
    MIRAGE_CHECK(text.size() <= 10);

    // Verify UTF-8 validity (no broken multi-byte characters).
    // The function should not cut in the middle of "世界".
    for (std::size_t i = 0; i < text.size();) {
        unsigned char c = text[i];
        if ((c & 0x80) == 0) {
            ++i; // ASCII
        } else if ((c & 0xE0) == 0xC0) {
            if (i + 1 >= text.size()) {
                std::fprintf(stderr, "ERROR: incomplete 2-byte UTF-8 at end\n");
                testing::failure_count()++;
                break;
            }
            i += 2;
        } else if ((c & 0xF0) == 0xE0) {
            if (i + 2 >= text.size()) {
                std::fprintf(stderr, "ERROR: incomplete 3-byte UTF-8 at end\n");
                testing::failure_count()++;
                break;
            }
            i += 3;
        } else if ((c & 0xF8) == 0xF0) {
            if (i + 3 >= text.size()) {
                std::fprintf(stderr, "ERROR: incomplete 4-byte UTF-8 at end\n");
                testing::failure_count()++;
                break;
            }
            i += 4;
        } else {
            std::fprintf(stderr, "ERROR: invalid UTF-8 sequence at byte %zu\n", i);
            testing::failure_count()++;
            break;
        }
    }
    std::printf("  truncated: '%s' (%zu bytes)\n", text.c_str(), text.size());
}

} // namespace
} // namespace mirage::conversation

int main() {
    using namespace mirage::conversation;

    std::printf("=== DEC-093 Categorized Budget Allocation Test ===\n");

    std::printf("[TEST] Category budgets\n");
    test_category_budgets();

    std::printf("[TEST] Thinking does not block tools\n");
    test_thinking_does_not_block_tools();

    std::printf("[TEST] Tools have priority over thinking\n");
    test_tools_have_priority_over_thinking();

    std::printf("[TEST] Final text budget\n");
    test_final_text_budget();

    std::printf("[TEST] Truncate text (UTF-8 safe)\n");
    test_truncate_text();

    return mirage::testing::finish("conversation_budget_test");
}
