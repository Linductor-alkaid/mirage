// Offline acceptance probe for MIRA-20261009-002: MiniMax-M3
// (anthropic.messages.v1) emits non-canonical SSE shapes when a turn is cut
// by max_tokens. Upstream mira PR#87 (merged as
// c75378ba1374450ae667af64561f86db1f90f4c4, consumed by this pin) makes the
// pinned AnthropicMessagesSseParser decode both cutoff shapes as bounded
// Incomplete/MaxOutputTokens completions; on the pre-fix pin (698fc77 and
// earlier) A/B failed closed with "invalid Messages SSE lifecycle or
// budget" instead.
//
// Shapes below were captured verbatim from https://api.minimaxi.com/anthropic
// (2026-10-09, synthetic body text substituted; no credentials or user
// content embedded):
//   A. orphan content_block_stop: cutoff lands between block scheduling and
//      the first content_block_start (observed at max_tokens=1, 4/4 runs).
//   B. truncated tool_use JSON: cutoff lands inside input_json_delta; the
//      block stop carries a partial argument object (observed with tools +
//      max_tokens=32..64, 2/2 runs).
//   C. control: a cutoff that lands inside an open thinking block decodes
//      fine (canonical behavior) — the parser only breaks on A/B.
//
// Build & run against the pinned mira build tree:
//   c++ -std=c++20 \
//     -Ithird_party/mira/include \
//     -Ibuild/native-release/third_party/mira/third_party/kairo \
//     docs/dependency_feedback/repro/minimax-cutoff-sse.cpp \
//     -Lbuild/native-release/third_party/mira \
//     -Lbuild/native-release/third_party/mira/third_party/kairo/src \
//     -lmira_core -lkairo -lpthread -o /tmp/minimax-cutoff-sse \
//   && /tmp/minimax-cutoff-sse
#include <mira/model_profile.hpp>
#include <mira/model_sse.hpp>

#include <iostream>
#include <string>

namespace {

constexpr const char *kMessageStart =
    "event: message_start\n"
    "data: {\"type\":\"message_start\",\"message\":{\"id\":\"probe\",\"type\":\"message\","
    "\"role\":\"assistant\",\"model\":\"MiniMax-M3\",\"content\":[],\"stop_reason\":null,"
    "\"usage\":{\"input_tokens\":0,\"output_tokens\":0}}}\n\n";
constexpr const char *kPing =
    "event: ping\ndata: {\"type\":\"ping\"}\n\n";
constexpr const char *kDeltaMaxTokens =
    "event: message_delta\n"
    "data: {\"type\":\"message_delta\",\"delta\":{\"stop_reason\":\"max_tokens\"},"
    "\"usage\":{\"input_tokens\":78,\"output_tokens\":1,"
    "\"cache_read_input_tokens\":128}}\n\n";
constexpr const char *kMessageStop =
    "event: message_stop\ndata: {\"type\":\"message_stop\"}\n\n";

std::string orphan_stop_stream() {
    // A: stop for a block that was never started.
    return std::string(kMessageStart) + kPing +
           "event: content_block_stop\ndata: {\"type\":\"content_block_stop\",\"index\":0}\n\n" +
           kDeltaMaxTokens + kMessageStop;
}

std::string truncated_tool_stream() {
    // B: tool_use arguments cut mid-JSON (partial "{" only).
    return std::string(kMessageStart) + kPing +
           "event: content_block_start\n"
           "data: {\"type\":\"content_block_start\",\"index\":0,"
           "\"content_block\":{\"type\":\"text\",\"text\":\"\"}}\n\n"
           "event: content_block_delta\n"
           "data: {\"type\":\"content_block_delta\",\"index\":0,"
           "\"delta\":{\"type\":\"text_delta\",\"text\":\"我将写入文件。\"}}\n\n"
           "event: content_block_stop\ndata: {\"type\":\"content_block_stop\",\"index\":0}\n\n"
           "event: content_block_start\n"
           "data: {\"type\":\"content_block_start\",\"index\":1,\"content_block\":"
           "{\"type\":\"tool_use\",\"id\":\"toolu_probe\",\"name\":\"desktop.file.write\","
           "\"input\":{}}}\n\n"
           "event: content_block_delta\n"
           "data: {\"type\":\"content_block_delta\",\"index\":1,"
           "\"delta\":{\"type\":\"input_json_delta\",\"partial_json\":\"{\\\"path\\\":\\\"/tmp\"}}\n\n"
           "event: content_block_stop\ndata: {\"type\":\"content_block_stop\",\"index\":1}\n\n" +
           kDeltaMaxTokens + kMessageStop;
}

std::string canonical_cutoff_stream() {
    // C: cutoff inside an open thinking block — canonical, must decode.
    return std::string(kMessageStart) + kPing +
           "event: content_block_start\n"
           "data: {\"type\":\"content_block_start\",\"index\":0,"
           "\"content_block\":{\"type\":\"thinking\",\"thinking\":\"\"}}\n\n"
           "event: content_block_delta\n"
           "data: {\"type\":\"content_block_delta\",\"index\":0,"
           "\"delta\":{\"type\":\"thinking_delta\",\"thinking\":\"先思考。\"}}\n\n"
           "event: content_block_stop\ndata: {\"type\":\"content_block_stop\",\"index\":0}\n\n" +
           kDeltaMaxTokens + kMessageStop;
}

mira::ModelProfile profile() {
    mira::ModelProfile record;
    record.dialect = mira::ProtocolDialect::AnthropicMessagesV1;
    record.model_selector = "MiniMax-M3";
    return record;
}

mira::ModelRequest request() {
    mira::ModelRequest request;
    request.output_contract.mode = mira::OutputMode::Text;
    request.generation.max_output_tokens = 8192;
    return request;
}

/// Feed one byte at a time: the transport boundary may cut anywhere.
std::string run(const std::string &name, const std::string &stream) {
    const auto record = profile();
    const auto model_request = request();
    mira::AnthropicMessagesSseParser parser(model_request, record);
    for (const char chunk : stream) {
        const auto status = parser.feed(std::string_view(&chunk, 1));
        if (!status)
            return name + ": FEED-ERROR: " + status.error().safe_message;
    }
    const auto finished = parser.finish();
    if (!finished)
        return name + ": FINISH-ERROR: " + finished.error().safe_message;
    return name + ": decoded (status " +
           std::to_string(static_cast<int>(finished.value().status)) + ")";
}

} // namespace

int main() {
    int failures = 0;
    const std::string results[] = {
        run("A orphan-stop      ", orphan_stop_stream()),
        run("B truncated-tool-js", truncated_tool_stream()),
        run("C canonical-cutoff ", canonical_cutoff_stream()),
    };
    for (const auto &line : results) {
        std::cout << line << "\n";
        if (line.find("decoded (status 1)") == std::string::npos)
            ++failures; // every cutoff shape must reach a bounded completion
    }
    return failures;
}
