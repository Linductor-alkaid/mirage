#include "../support/test.hpp"

#include <websocket.hpp>

#include <cstdint>
#include <string>

// M1.5-03 unit tests for the self-contained RFC 6455 codec in
// apps/devbridge/websocket.hpp: known SHA-1 / base64 / accept-key vectors,
// frame encoding shapes, masked client frame parsing with its protocol-error
// matrix, message reassembly and the HTTP opening handshake. Pure byte
// machinery: no sockets, no executor, no concurrency.

namespace {

namespace ws = mirage::devbridge::ws;

std::string hex_of(const unsigned char *data, std::size_t size) {
    static const char kDigits[] = "0123456789abcdef";
    std::string out;
    out.reserve(size * 2);
    for (std::size_t index = 0; index < size; ++index) {
        out.push_back(kDigits[(data[index] >> 4) & 0x0F]);
        out.push_back(kDigits[data[index] & 0x0F]);
    }
    return out;
}

/// Builds a client-to-server frame with the mandatory masking applied, so
/// ws::try_parse_frame (server side) accepts it.
std::string make_masked_frame(std::uint8_t opcode, const std::string &payload, bool fin = true,
                              std::uint32_t mask = 0x37FA213DU) {
    std::string out;
    out.push_back(static_cast<char>((fin ? 0x80u : 0x00u) | opcode));
    const std::size_t size = payload.size();
    if (size <= 125) {
        out.push_back(static_cast<char>(0x80u | size));
    } else if (size <= 0xFFFFu) {
        out.push_back(static_cast<char>(0x80u | 126u));
        out.push_back(static_cast<char>((size >> 8) & 0xFFu));
        out.push_back(static_cast<char>(size & 0xFFu));
    } else {
        out.push_back(static_cast<char>(0x80u | 127u));
        for (int shift = 56; shift >= 0; shift -= 8) {
            out.push_back(static_cast<char>((static_cast<std::uint64_t>(size) >> shift) & 0xFFu));
        }
    }
    const unsigned char mask_bytes[4] = {static_cast<unsigned char>((mask >> 24) & 0xFFu),
                                         static_cast<unsigned char>((mask >> 16) & 0xFFu),
                                         static_cast<unsigned char>((mask >> 8) & 0xFFu),
                                         static_cast<unsigned char>(mask & 0xFFu)};
    for (const unsigned char mask_byte : mask_bytes) {
        out.push_back(static_cast<char>(mask_byte));
    }
    for (std::size_t index = 0; index < size; ++index) {
        out.push_back(payload[index] ^ static_cast<char>(mask_bytes[index % 4]));
    }
    return out;
}

/// Parses `bytes` on a scratch buffer and reports the close code of a
/// protocol error, or 0 when the outcome was not a protocol error.
std::uint16_t protocol_error_code(const std::string &bytes) {
    std::string buffer = bytes;
    const ws::ParsedFrame parsed = ws::try_parse_frame(buffer);
    return parsed.status == ws::ParsedFrame::Status::protocol_error ? parsed.close_code : 0;
}

/// Parses `bytes` and checks the full frame outcome (status, opcode, fin,
/// payload) in one shot; also verifies the buffer was fully consumed.
bool parses_exactly(const std::string &bytes, ws::Opcode opcode, const std::string &payload,
                    bool fin = true) {
    std::string buffer = bytes;
    const ws::ParsedFrame parsed = ws::try_parse_frame(buffer);
    return parsed.status == ws::ParsedFrame::Status::frame && parsed.frame.opcode == opcode &&
           parsed.frame.fin == fin && parsed.frame.payload == payload && buffer.empty();
}

// --- SHA-1 / base64 / accept key ---------------------------------------------

void test_sha1_known_vectors() {
    unsigned char digest[20];
    ws::sha1("abc", digest);
    MIRAGE_CHECK(hex_of(digest, 20) == "a9993e364706816aba3e25717850c26c9cd0d89d");

    ws::sha1("", digest);
    MIRAGE_CHECK(hex_of(digest, 20) == "da39a3ee5e6b4b0d3255bfef95601890afd80709");

    // RFC 3174 / NIST vectors; the 56-byte message sits exactly on the
    // padding boundary (message + 0x80 fills the length word's block).
    ws::sha1("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq", digest);
    MIRAGE_CHECK(hex_of(digest, 20) == "84983e441c3bd26ebaae4aa1f95129e5e54670f1");

    // 64-byte message: two more padding blocks than the 55-byte case.
    ws::sha1(std::string(64, 'a'), digest);
    MIRAGE_CHECK(hex_of(digest, 20) == "0098ba824b5c16427bd7a1122a5a442a25ec644d");
}

void test_accept_key_rfc_example() {
    // RFC 6455 section 1.3.
    MIRAGE_CHECK(ws::accept_key("dGhlIHNhbXBsZSBub25jZQ==") == "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=");
}

void test_base64_encode() {
    const unsigned char *empty = reinterpret_cast<const unsigned char *>("");
    MIRAGE_CHECK(ws::base64_encode(empty, 0).empty());
    MIRAGE_CHECK(ws::base64_encode(reinterpret_cast<const unsigned char *>("f"), 1) == "Zg==");
    MIRAGE_CHECK(ws::base64_encode(reinterpret_cast<const unsigned char *>("fo"), 2) == "Zm8=");
    MIRAGE_CHECK(ws::base64_encode(reinterpret_cast<const unsigned char *>("foo"), 3) == "Zm9v");
    MIRAGE_CHECK(ws::base64_encode(reinterpret_cast<const unsigned char *>("foob"), 4) ==
                 "Zm9vYg==");
    MIRAGE_CHECK(ws::base64_encode(reinterpret_cast<const unsigned char *>("fooba"), 5) ==
                 "Zm9vYmE=");
    MIRAGE_CHECK(ws::base64_encode(reinterpret_cast<const unsigned char *>("foobar"), 6) ==
                 "Zm9vYmFy");
}

void test_base64_round_trip_and_rejects() {
    std::string bytes;
    for (int value = 0; value < 256; ++value) {
        bytes.push_back(static_cast<char>(value));
    }
    const std::string encoded =
        ws::base64_encode(reinterpret_cast<const unsigned char *>(bytes.data()), bytes.size());
    std::string decoded;
    MIRAGE_CHECK(ws::base64_decode(encoded, bytes.size(), decoded));
    MIRAGE_CHECK(decoded == bytes);

    // A handshake key is exactly 16 decoded bytes.
    const std::string key =
        ws::base64_encode(reinterpret_cast<const unsigned char *>("0123456789abcdef"), 16);
    MIRAGE_CHECK(key.size() == 24);
    decoded.clear();
    MIRAGE_CHECK(ws::base64_decode(key, 16, decoded));
    MIRAGE_CHECK(decoded == "0123456789abcdef");

    // Unpadded input of the right size is accepted; the padded length must
    // match the expectation exactly.
    decoded.clear();
    MIRAGE_CHECK(ws::base64_decode("Zm9v", 3, decoded));
    MIRAGE_CHECK(decoded == "foo");
    MIRAGE_CHECK(!ws::base64_decode("Zg==", 2, decoded));
    MIRAGE_CHECK(!ws::base64_decode("Zm9v", 4, decoded));

    // Invalid alphabet, embedded padding, and padding-only input.
    MIRAGE_CHECK(!ws::base64_decode("Zm9*", 3, decoded));
    MIRAGE_CHECK(!ws::base64_decode("Zm9vZm8=Zm8=", 6, decoded));
    MIRAGE_CHECK(!ws::base64_decode("====", 0, decoded));
}

// --- frame encoding ------------------------------------------------------------

void test_make_frame_length_shapes() {
    // 7-bit length form.
    const std::string small = ws::make_frame(ws::Opcode::binary, std::string(125, 'x'));
    MIRAGE_CHECK(small.size() == 125 + 2);
    MIRAGE_CHECK(static_cast<unsigned char>(small[0]) == 0x82);
    MIRAGE_CHECK(static_cast<unsigned char>(small[1]) == 125);

    // 16-bit length form starts at 126.
    const std::string medium = ws::make_frame(ws::Opcode::binary, std::string(126, 'y'));
    MIRAGE_CHECK(medium.size() == 126 + 4);
    MIRAGE_CHECK(static_cast<unsigned char>(medium[0]) == 0x82);
    MIRAGE_CHECK(static_cast<unsigned char>(medium[1]) == 126);
    MIRAGE_CHECK(static_cast<unsigned char>(medium[2]) == 0x00);
    MIRAGE_CHECK(static_cast<unsigned char>(medium[3]) == 126);

    const std::string large16 = ws::make_frame(ws::Opcode::text, std::string(65535, 'z'));
    MIRAGE_CHECK(large16.size() == 65535 + 4);
    MIRAGE_CHECK(static_cast<unsigned char>(large16[1]) == 126);
    MIRAGE_CHECK(static_cast<unsigned char>(large16[2]) == 0xFF);
    MIRAGE_CHECK(static_cast<unsigned char>(large16[3]) == 0xFF);

    // 64-bit length form starts at 65536; be64(65536) = 00*5 01 00 00.
    const std::string large64 = ws::make_frame(ws::Opcode::binary, std::string(65536, 'w'));
    MIRAGE_CHECK(large64.size() == 65536 + 10);
    MIRAGE_CHECK(static_cast<unsigned char>(large64[0]) == 0x82);
    MIRAGE_CHECK(static_cast<unsigned char>(large64[1]) == 127);
    for (int index = 2; index <= 6; ++index) {
        MIRAGE_CHECK(static_cast<unsigned char>(large64[index]) == 0x00);
    }
    MIRAGE_CHECK(static_cast<unsigned char>(large64[7]) == 0x01);
    MIRAGE_CHECK(static_cast<unsigned char>(large64[8]) == 0x00);
    MIRAGE_CHECK(static_cast<unsigned char>(large64[9]) == 0x00);
    MIRAGE_CHECK(static_cast<unsigned char>(large64[10]) == 'w');
}

void test_make_close_frame() {
    const std::string normal = ws::make_close_frame(ws::kCloseNormal, "");
    MIRAGE_CHECK(normal.size() == 4);
    MIRAGE_CHECK(static_cast<unsigned char>(normal[0]) == 0x88);
    MIRAGE_CHECK(static_cast<unsigned char>(normal[1]) == 0x02);
    MIRAGE_CHECK(static_cast<unsigned char>(normal[2]) == 0x03);
    MIRAGE_CHECK(static_cast<unsigned char>(normal[3]) == 0xE8);

    const std::string with_reason = ws::make_close_frame(ws::kCloseGoingAway, "bye");
    MIRAGE_CHECK(with_reason.size() == 2 + 2 + 3);
    MIRAGE_CHECK(with_reason.substr(2) == std::string("\x03\xE9", 2) + "bye");

    // A 200-byte reason must be truncated into the 125-byte control budget.
    const std::string truncated =
        ws::make_close_frame(ws::kCloseInternalError, std::string(200, 'r'));
    MIRAGE_CHECK(truncated.size() == 2 + 125);
    MIRAGE_CHECK(truncated[1] == static_cast<char>(125));
    MIRAGE_CHECK(truncated.substr(4, 123) == std::string(123, 'r'));
}

// --- frame parsing ---------------------------------------------------------------

void test_parse_round_trip() {
    MIRAGE_CHECK(parses_exactly(make_masked_frame(0x2, "hello"), ws::Opcode::binary, "hello"));
    MIRAGE_CHECK(parses_exactly(make_masked_frame(0x1, ""), ws::Opcode::text, ""));

    // 16-bit length round trip with binary bytes (incl. 0x00 / 0xFF / 0x80).
    std::string binary;
    for (int value = 0; value < 256; ++value) {
        binary.push_back(static_cast<char>(value));
    }
    std::string big;
    while (big.size() < 200) {
        big += binary;
    }
    big.resize(200);
    MIRAGE_CHECK(parses_exactly(make_masked_frame(0x2, big), ws::Opcode::binary, big));

    // Control frames.
    MIRAGE_CHECK(parses_exactly(make_masked_frame(0x9, "hb"), ws::Opcode::ping, "hb"));
    MIRAGE_CHECK(parses_exactly(make_masked_frame(0xA, ""), ws::Opcode::pong, ""));
    const std::string close_payload = std::string("\x03\xE8", 2);
    MIRAGE_CHECK(
        parses_exactly(make_masked_frame(0x8, close_payload), ws::Opcode::close, close_payload));
}

void test_parse_fragmented_delivery() {
    const std::string frame = make_masked_frame(0x2, "payload-42");
    for (std::size_t cut = 1; cut < frame.size(); ++cut) {
        std::string buffer = frame.substr(0, cut);
        const ws::ParsedFrame partial = ws::try_parse_frame(buffer);
        MIRAGE_CHECK(partial.status == ws::ParsedFrame::Status::need_more_data);
        // A partial frame must not consume bytes.
        MIRAGE_CHECK(buffer == frame.substr(0, cut));
        buffer += frame.substr(cut);
        const ws::ParsedFrame whole = ws::try_parse_frame(buffer);
        MIRAGE_CHECK(whole.status == ws::ParsedFrame::Status::frame);
        MIRAGE_CHECK(whole.frame.payload == "payload-42");
        MIRAGE_CHECK(buffer.empty());
    }
}

void test_parse_two_frames_in_one_buffer() {
    const std::string first = make_masked_frame(0x2, "one", true, 0x11111111u);
    const std::string second = make_masked_frame(0x9, "pp", true, 0x22222222u);
    std::string buffer = first + second;
    const ws::ParsedFrame parsed_first = ws::try_parse_frame(buffer);
    MIRAGE_CHECK(parsed_first.status == ws::ParsedFrame::Status::frame);
    MIRAGE_CHECK(parsed_first.frame.payload == "one");
    const ws::ParsedFrame parsed_second = ws::try_parse_frame(buffer);
    MIRAGE_CHECK(parsed_second.status == ws::ParsedFrame::Status::frame);
    MIRAGE_CHECK(parsed_second.frame.opcode == ws::Opcode::ping);
    MIRAGE_CHECK(parsed_second.frame.payload == "pp");
    MIRAGE_CHECK(buffer.empty());
}

void test_parse_protocol_error_matrix() {
    // Unmasked client frame.
    MIRAGE_CHECK(protocol_error_code(std::string("\x82\x05", 2) + "hello") == 1002);
    // Nonzero RSV bits.
    MIRAGE_CHECK(protocol_error_code(std::string("\xC2\x85", 2) + "\x01\x02\x03\x04\x05") == 1002);
    // Unknown opcodes 0x3 and 0xB.
    MIRAGE_CHECK(protocol_error_code(make_masked_frame(0x3, "")) == 1002);
    MIRAGE_CHECK(protocol_error_code(make_masked_frame(0xB, "")) == 1002);
    // Fragmented control frame.
    MIRAGE_CHECK(protocol_error_code(std::string("\x09\x85", 2) + "\x01\x02\x03\x04\x05") == 1002);
    // Control frame with a 16-bit length (127-byte payload).
    MIRAGE_CHECK(protocol_error_code(make_masked_frame(0x8, std::string(127, 'c'))) == 1002);
    // Single frame above the 1 MiB message cap.
    MIRAGE_CHECK(protocol_error_code(
                     make_masked_frame(0x2, std::string(ws::kMaxMessageBytes + 1, 'd'))) == 1009);
    // The 1009 rejection happens on the header alone, before the payload.
    {
        std::string header = std::string("\x82\xFF", 2);
        const std::uint64_t too_big = static_cast<std::uint64_t>(ws::kMaxMessageBytes) + 5;
        for (int shift = 56; shift >= 0; shift -= 8) {
            header.push_back(static_cast<char>((too_big >> shift) & 0xFFu));
        }
        header += std::string("\x01\x02\x03\x04", 4);
        MIRAGE_CHECK(protocol_error_code(header) == 1009);
    }
    // 64-bit length with the top bit set.
    {
        std::string header = std::string("\x82\xFF", 2);
        const std::uint64_t huge = 0x8000000000000000ULL;
        for (int shift = 56; shift >= 0; shift -= 8) {
            header.push_back(static_cast<char>((huge >> shift) & 0xFFu));
        }
        header += std::string("\x01\x02\x03\x04", 4);
        MIRAGE_CHECK(protocol_error_code(header) == 1002);
    }
}

void test_parse_message_cap_boundary() {
    // Exactly one maximal IPC frame (1 MiB + 4 header bytes) is acceptable.
    const std::string maximal(ws::kMaxMessageBytes, 'm');
    std::string buffer = make_masked_frame(0x2, maximal);
    const ws::ParsedFrame parsed = ws::try_parse_frame(buffer);
    MIRAGE_CHECK(parsed.status == ws::ParsedFrame::Status::frame);
    MIRAGE_CHECK(parsed.frame.payload.size() == ws::kMaxMessageBytes);
    MIRAGE_CHECK(buffer.empty());

    // One payload byte more is rejected with 1009.
    const std::string over(maximal.size() + 1, 'm');
    MIRAGE_CHECK(protocol_error_code(make_masked_frame(0x2, over)) == 1009);
}

// --- message reassembly ----------------------------------------------------------

void test_assembler_fragmented_message() {
    ws::MessageAssembler assembler(1024);
    const auto first = assembler.consume(ws::Frame{ws::Opcode::binary, false, "Hello "});
    MIRAGE_CHECK(first.status == ws::MessageAssembler::Result::Status::need_more_data);
    const auto second = assembler.consume(ws::Frame{ws::Opcode::continuation, false, "frag"});
    MIRAGE_CHECK(second.status == ws::MessageAssembler::Result::Status::need_more_data);
    const auto third = assembler.consume(ws::Frame{ws::Opcode::continuation, true, "mented"});
    MIRAGE_CHECK(third.status == ws::MessageAssembler::Result::Status::message);
    MIRAGE_CHECK(third.opcode == ws::Opcode::binary);
    MIRAGE_CHECK(third.payload == "Hello fragmented");

    // A second message may start right after.
    const auto next = assembler.consume(ws::Frame{ws::Opcode::text, true, "txt"});
    MIRAGE_CHECK(next.status == ws::MessageAssembler::Result::Status::message);
    MIRAGE_CHECK(next.opcode == ws::Opcode::text);
    MIRAGE_CHECK(next.payload == "txt");
}

void test_assembler_error_matrix() {
    ws::MessageAssembler assembler(16);

    // Continuation without a started message.
    auto stray = assembler.consume(ws::Frame{ws::Opcode::continuation, true, "x"});
    MIRAGE_CHECK(stray.status == ws::MessageAssembler::Result::Status::protocol_error);
    MIRAGE_CHECK(stray.close_code == 1002);

    // New data frame inside a fragmented message.
    auto started = assembler.consume(ws::Frame{ws::Opcode::binary, false, "seed"});
    MIRAGE_CHECK(started.status == ws::MessageAssembler::Result::Status::need_more_data);
    auto intruder = assembler.consume(ws::Frame{ws::Opcode::text, true, "boom"});
    MIRAGE_CHECK(intruder.status == ws::MessageAssembler::Result::Status::protocol_error);
    MIRAGE_CHECK(intruder.close_code == 1002);

    // Control frames never reach the assembler.
    auto control = assembler.consume(ws::Frame{ws::Opcode::ping, true, ""});
    MIRAGE_CHECK(control.status == ws::MessageAssembler::Result::Status::protocol_error);
    MIRAGE_CHECK(control.close_code == 1002);
}

void test_assembler_size_cap() {
    ws::MessageAssembler assembler(16);
    // Exactly 16 assembled bytes pass.
    auto exact = assembler.consume(ws::Frame{ws::Opcode::binary, true, std::string(16, 'a')});
    MIRAGE_CHECK(exact.status == ws::MessageAssembler::Result::Status::message);
    MIRAGE_CHECK(exact.payload.size() == 16);

    // 16 + 1 across fragments trips the cap with 1009.
    auto head = assembler.consume(ws::Frame{ws::Opcode::binary, false, std::string(16, 'b')});
    MIRAGE_CHECK(head.status == ws::MessageAssembler::Result::Status::need_more_data);
    auto tail = assembler.consume(ws::Frame{ws::Opcode::continuation, true, "!"});
    MIRAGE_CHECK(tail.status == ws::MessageAssembler::Result::Status::protocol_error);
    MIRAGE_CHECK(tail.close_code == 1009);

    // The assembler returns to idle after the error.
    auto after = assembler.consume(ws::Frame{ws::Opcode::binary, true, "ok"});
    MIRAGE_CHECK(after.status == ws::MessageAssembler::Result::Status::message);
    MIRAGE_CHECK(after.payload == "ok");
}

// --- opening handshake -----------------------------------------------------------

namespace {

const char kRfcExampleRequest[] = "GET /chat HTTP/1.1\r\n"
                                  "Host: example.com:8000\r\n"
                                  "Upgrade: websocket\r\n"
                                  "Connection: Upgrade\r\n"
                                  "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
                                  "Sec-WebSocket-Origin: http://example.com\r\n"
                                  "Sec-WebSocket-Version: 13\r\n"
                                  "\r\n";

} // namespace

void test_handshake_rfc_example() {
    std::string buffer = kRfcExampleRequest;
    const ws::Handshake handshake = ws::try_parse_handshake(buffer);
    MIRAGE_CHECK(handshake.status == ws::Handshake::Status::ok);
    MIRAGE_CHECK(handshake.accept_value == "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=");
    MIRAGE_CHECK(buffer.empty());
}

void test_handshake_byte_by_byte() {
    const std::string request = kRfcExampleRequest;
    std::string buffer;
    ws::Handshake handshake;
    for (std::size_t index = 0; index + 1 < request.size(); ++index) {
        buffer.push_back(request[index]);
        handshake = ws::try_parse_handshake(buffer);
        if (handshake.status != ws::Handshake::Status::need_more_data) {
            break;
        }
    }
    // All but the final byte: still waiting for the request terminator.
    MIRAGE_CHECK(handshake.status == ws::Handshake::Status::need_more_data);
    buffer.push_back(request.back());
    handshake = ws::try_parse_handshake(buffer);
    MIRAGE_CHECK(handshake.status == ws::Handshake::Status::ok);
    MIRAGE_CHECK(handshake.accept_value == "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=");
    MIRAGE_CHECK(buffer.empty());
}

void test_handshake_keeps_pipelined_frame_bytes() {
    const std::string pipelined = make_masked_frame(0x9, "ping", true, 0x0Au);
    std::string buffer = std::string(kRfcExampleRequest) + pipelined;
    const ws::Handshake handshake = ws::try_parse_handshake(buffer);
    MIRAGE_CHECK(handshake.status == ws::Handshake::Status::ok);
    MIRAGE_CHECK(buffer == pipelined);
    // The retained bytes parse as a frame on the next call.
    const ws::ParsedFrame parsed = ws::try_parse_frame(buffer);
    MIRAGE_CHECK(parsed.status == ws::ParsedFrame::Status::frame);
    MIRAGE_CHECK(parsed.frame.opcode == ws::Opcode::ping);
    MIRAGE_CHECK(parsed.frame.payload == "ping");
}

void test_handshake_header_case_and_token_list() {
    std::string buffer = "GET / HTTP/1.1\r\n"
                         "HOST: localhost\r\n"
                         "upgrade: WebSocket\r\n"
                         "CONNECTION: keep-alive, Upgrade\r\n"
                         "sec-websocket-key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
                         "SEC-WEBSOCKET-VERSION: 13\r\n"
                         "\r\n";
    const ws::Handshake handshake = ws::try_parse_handshake(buffer);
    MIRAGE_CHECK(handshake.status == ws::Handshake::Status::ok);
}

void test_handshake_rejects() {
    const auto rejects = [](const std::string &request) {
        std::string buffer = request;
        const ws::Handshake handshake = ws::try_parse_handshake(buffer);
        return handshake.status == ws::Handshake::Status::reject;
    };

    // Not a GET / not HTTP/1.1.
    MIRAGE_CHECK(rejects("POST /chat HTTP/1.1\r\nUpgrade: websocket\r\n"
                         "Connection: Upgrade\r\nSec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
                         "Sec-WebSocket-Version: 13\r\n\r\n"));
    MIRAGE_CHECK(rejects("GET /chat HTTP/1.0\r\nUpgrade: websocket\r\n"
                         "Connection: Upgrade\r\nSec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
                         "Sec-WebSocket-Version: 13\r\n\r\n"));
    // Missing / wrong upgrade token.
    MIRAGE_CHECK(rejects("GET /chat HTTP/1.1\r\n"
                         "Connection: Upgrade\r\nSec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
                         "Sec-WebSocket-Version: 13\r\n\r\n"));
    MIRAGE_CHECK(rejects("GET /chat HTTP/1.1\r\nUpgrade: h2c\r\n"
                         "Connection: Upgrade\r\nSec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
                         "Sec-WebSocket-Version: 13\r\n\r\n"));
    // Connection header without the upgrade token.
    MIRAGE_CHECK(rejects("GET /chat HTTP/1.1\r\nUpgrade: websocket\r\n"
                         "Connection: keep-alive\r\nSec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
                         "Sec-WebSocket-Version: 13\r\n\r\n"));
    // Wrong version.
    MIRAGE_CHECK(rejects("GET /chat HTTP/1.1\r\nUpgrade: websocket\r\n"
                         "Connection: Upgrade\r\nSec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
                         "Sec-WebSocket-Version: 12\r\n\r\n"));
    // Missing key.
    MIRAGE_CHECK(rejects("GET /chat HTTP/1.1\r\nUpgrade: websocket\r\n"
                         "Connection: Upgrade\r\nSec-WebSocket-Version: 13\r\n\r\n"));
    // Key with an invalid character.
    MIRAGE_CHECK(rejects("GET /chat HTTP/1.1\r\nUpgrade: websocket\r\n"
                         "Connection: Upgrade\r\nSec-WebSocket-Key: ****\r\n"
                         "Sec-WebSocket-Version: 13\r\n\r\n"));
    // Key that does not decode to 16 bytes.
    MIRAGE_CHECK(rejects("GET /chat HTTP/1.1\r\nUpgrade: websocket\r\n"
                         "Connection: Upgrade\r\nSec-WebSocket-Key: Zm9v\r\n"
                         "Sec-WebSocket-Version: 13\r\n\r\n"));
    // Header block above the 8 KiB cap without a terminator.
    MIRAGE_CHECK(rejects("GET /chat HTTP/1.1\r\n" + std::string(9000, 'a')));
}

void test_handshake_response_and_rejection_shapes() {
    const std::string response = ws::make_handshake_response("s3pPLMBiTxaQ9kYGzzhZRbK+xOo=");
    MIRAGE_CHECK(response == "HTTP/1.1 101 Switching Protocols\r\n"
                             "Upgrade: websocket\r\n"
                             "Connection: Upgrade\r\n"
                             "Sec-WebSocket-Accept: s3pPLMBiTxaQ9kYGzzhZRbK+xOo=\r\n\r\n");

    const std::string rejection = ws::make_http_rejection();
    MIRAGE_CHECK(rejection.rfind("HTTP/1.1 400 ", 0) == 0);
    MIRAGE_CHECK(rejection.find("Connection: close") != std::string::npos);
}

} // namespace

int main() {
    test_sha1_known_vectors();
    test_accept_key_rfc_example();
    test_base64_encode();
    test_base64_round_trip_and_rejects();
    test_make_frame_length_shapes();
    test_make_close_frame();
    test_parse_round_trip();
    test_parse_fragmented_delivery();
    test_parse_two_frames_in_one_buffer();
    test_parse_protocol_error_matrix();
    test_parse_message_cap_boundary();
    test_assembler_fragmented_message();
    test_assembler_error_matrix();
    test_assembler_size_cap();
    test_handshake_rfc_example();
    test_handshake_byte_by_byte();
    test_handshake_keeps_pipelined_frame_bytes();
    test_handshake_header_case_and_token_list();
    test_handshake_rejects();
    test_handshake_response_and_rejection_shapes();
    return mirage::testing::finish("websocket_test");
}
