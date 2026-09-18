#include "websocket.hpp"

#include <cstring>

namespace mirage::devbridge::ws {
namespace {

/// RFC 3174 SHA-1 over a fully buffered message; 20 raw digest bytes.
class Sha1 {
  public:
    explicit Sha1(const std::string &message) {
        std::uint64_t total_bits = message.size() * 8;
        std::string padded(message);
        padded.push_back(static_cast<char>(0x80));
        while (padded.size() % 64 != 56) {
            padded.push_back('\0');
        }
        for (int shift = 56; shift >= 0; shift -= 8) {
            padded.push_back(static_cast<char>((total_bits >> shift) & 0xFF));
        }
        for (std::size_t offset = 0; offset < padded.size(); offset += 64) {
            process_block(padded.data() + offset);
        }
        for (int word = 0; word < 5; ++word) {
            for (int shift = 24; shift >= 0; shift -= 8) {
                digest_[word * 4 + (24 - shift) / 8] =
                    static_cast<unsigned char>((state_[word] >> shift) & 0xFF);
            }
        }
    }

    const unsigned char *digest() const { return digest_; }

  private:
    static std::uint32_t rotate_left(std::uint32_t value, int bits) {
        return (value << bits) | (value >> (32 - bits));
    }

    void process_block(const char *block) {
        std::uint32_t w[80];
        for (int index = 0; index < 16; ++index) {
            const auto *bytes = reinterpret_cast<const unsigned char *>(block + index * 4);
            w[index] = (static_cast<std::uint32_t>(bytes[0]) << 24) |
                       (static_cast<std::uint32_t>(bytes[1]) << 16) |
                       (static_cast<std::uint32_t>(bytes[2]) << 8) |
                       static_cast<std::uint32_t>(bytes[3]);
        }
        for (int index = 16; index < 80; ++index) {
            w[index] = rotate_left(w[index - 3] ^ w[index - 8] ^ w[index - 14] ^ w[index - 16], 1);
        }
        std::uint32_t a = state_[0];
        std::uint32_t b = state_[1];
        std::uint32_t c = state_[2];
        std::uint32_t d = state_[3];
        std::uint32_t e = state_[4];
        for (int index = 0; index < 80; ++index) {
            std::uint32_t f = 0;
            std::uint32_t k = 0;
            if (index < 20) {
                f = (b & c) | ((~b) & d);
                k = 0x5A827999;
            } else if (index < 40) {
                f = b ^ c ^ d;
                k = 0x6ED9EBA1;
            } else if (index < 60) {
                f = (b & c) | (b & d) | (c & d);
                k = 0x8F1BBCDC;
            } else {
                f = b ^ c ^ d;
                k = 0xCA62C1D6;
            }
            const std::uint32_t temp = rotate_left(a, 5) + f + e + k + w[index];
            e = d;
            d = c;
            c = rotate_left(b, 30);
            b = a;
            a = temp;
        }
        state_[0] += a;
        state_[1] += b;
        state_[2] += c;
        state_[3] += d;
        state_[4] += e;
    }

    std::uint32_t state_[5] = {0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0};
    unsigned char digest_[20] = {};
};

std::string to_lower(std::string text) {
    for (char &character : text) {
        if (character >= 'A' && character <= 'Z') {
            character = static_cast<char>(character - 'A' + 'a');
        }
    }
    return text;
}

/// True when `token` appears as one element of the comma-separated header
/// value `value` (case-insensitive, per RFC 7230 list rules).
bool token_list_contains(const std::string &value, const std::string &token) {
    std::size_t position = 0;
    const std::string lowered = to_lower(value);
    const std::string needle = to_lower(token);
    while (position <= lowered.size()) {
        const std::size_t comma = lowered.find(',', position);
        const std::size_t end = comma == std::string::npos ? lowered.size() : comma;
        std::size_t begin = position;
        while (begin < end && (lowered[begin] == ' ' || lowered[begin] == '\t')) {
            ++begin;
        }
        std::size_t stop = end;
        while (stop > begin && (lowered[stop - 1] == ' ' || lowered[stop - 1] == '\t')) {
            --stop;
        }
        if (lowered.compare(begin, stop - begin, needle) == 0) {
            return true;
        }
        if (comma == std::string::npos) {
            break;
        }
        position = comma + 1;
    }
    return false;
}

void append_be16(std::string &out, std::uint16_t value) {
    out.push_back(static_cast<char>((value >> 8) & 0xFF));
    out.push_back(static_cast<char>(value & 0xFF));
}

void append_be64(std::string &out, std::uint64_t value) {
    for (int shift = 56; shift >= 0; shift -= 8) {
        out.push_back(static_cast<char>((value >> shift) & 0xFF));
    }
}

} // namespace

void sha1(const std::string &message, unsigned char digest[20]) {
    const Sha1 hasher(message);
    std::memcpy(digest, hasher.digest(), 20);
}

std::string base64_encode(const unsigned char *data, std::size_t size) {
    static const char kAlphabet[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve(((size + 2) / 3) * 4);
    std::size_t index = 0;
    while (index + 3 <= size) {
        const std::uint32_t group = (static_cast<std::uint32_t>(data[index]) << 16) |
                                    (static_cast<std::uint32_t>(data[index + 1]) << 8) |
                                    static_cast<std::uint32_t>(data[index + 2]);
        out.push_back(kAlphabet[(group >> 18) & 0x3F]);
        out.push_back(kAlphabet[(group >> 12) & 0x3F]);
        out.push_back(kAlphabet[(group >> 6) & 0x3F]);
        out.push_back(kAlphabet[group & 0x3F]);
        index += 3;
    }
    const std::size_t remaining = size - index;
    if (remaining == 1) {
        const std::uint32_t group = static_cast<std::uint32_t>(data[index]) << 16;
        out.push_back(kAlphabet[(group >> 18) & 0x3F]);
        out.push_back(kAlphabet[(group >> 12) & 0x3F]);
        out.push_back('=');
        out.push_back('=');
    } else if (remaining == 2) {
        const std::uint32_t group = (static_cast<std::uint32_t>(data[index]) << 16) |
                                    (static_cast<std::uint32_t>(data[index + 1]) << 8);
        out.push_back(kAlphabet[(group >> 18) & 0x3F]);
        out.push_back(kAlphabet[(group >> 12) & 0x3F]);
        out.push_back(kAlphabet[(group >> 6) & 0x3F]);
        out.push_back('=');
    }
    return out;
}

bool base64_decode(std::string_view text, std::size_t expected_size, std::string &out) {
    auto value_of = [](char character) -> int {
        if (character >= 'A' && character <= 'Z') {
            return character - 'A';
        }
        if (character >= 'a' && character <= 'z') {
            return character - 'a' + 26;
        }
        if (character >= '0' && character <= '9') {
            return character - '0' + 52;
        }
        if (character == '+') {
            return 62;
        }
        if (character == '/') {
            return 63;
        }
        return -1;
    };
    out.clear();
    std::size_t index = 0;
    const std::size_t end = text.size();
    std::string bytes;
    while (index < end) {
        int group[4] = {-1, -1, -1, -1};
        int collected = 0;
        while (index < end && collected < 4) {
            const char character = text[index];
            if (character == '=') {
                break;
            }
            if (character == '\r' || character == '\n' || character == ' ') {
                ++index;
                continue;
            }
            const int value = value_of(character);
            if (value < 0) {
                return false;
            }
            group[collected++] = value;
            ++index;
        }
        if (collected == 4) {
            const std::uint32_t packed = (static_cast<std::uint32_t>(group[0]) << 18) |
                                         (static_cast<std::uint32_t>(group[1]) << 12) |
                                         (static_cast<std::uint32_t>(group[2]) << 6) |
                                         static_cast<std::uint32_t>(group[3]);
            bytes.push_back(static_cast<char>((packed >> 16) & 0xFF));
            bytes.push_back(static_cast<char>((packed >> 8) & 0xFF));
            bytes.push_back(static_cast<char>(packed & 0xFF));
            continue;
        }
        if (collected == 3) {
            const std::uint32_t packed = (static_cast<std::uint32_t>(group[0]) << 18) |
                                         (static_cast<std::uint32_t>(group[1]) << 12) |
                                         (static_cast<std::uint32_t>(group[2]) << 6);
            bytes.push_back(static_cast<char>((packed >> 16) & 0xFF));
            bytes.push_back(static_cast<char>((packed >> 8) & 0xFF));
            break;
        }
        if (collected == 2) {
            const std::uint32_t packed = (static_cast<std::uint32_t>(group[0]) << 18) |
                                         (static_cast<std::uint32_t>(group[1]) << 12);
            bytes.push_back(static_cast<char>((packed >> 16) & 0xFF));
            break;
        }
        return false;
    }
    // Only padding may follow, and it must complete the current quantum.
    while (index < end) {
        const char character = text[index++];
        if (character != '=' && character != '\r' && character != '\n') {
            return false;
        }
    }
    if (bytes.size() != expected_size) {
        return false;
    }
    out = std::move(bytes);
    return true;
}

std::string accept_key(const std::string &client_key) {
    unsigned char digest[20];
    sha1(client_key + kWebSocketGuid, digest);
    return base64_encode(digest, 20);
}

bool is_control(Opcode opcode) {
    return opcode == Opcode::close || opcode == Opcode::ping || opcode == Opcode::pong;
}

std::string make_frame(Opcode opcode, const std::string &payload) {
    std::string out;
    out.reserve(payload.size() + 10);
    out.push_back(static_cast<char>(0x80 | static_cast<std::uint8_t>(opcode)));
    if (payload.size() <= 125) {
        out.push_back(static_cast<char>(payload.size()));
    } else if (payload.size() <= 0xFFFF) {
        out.push_back(static_cast<char>(126));
        append_be16(out, static_cast<std::uint16_t>(payload.size()));
    } else {
        out.push_back(static_cast<char>(127));
        append_be64(out, payload.size());
    }
    out.append(payload);
    return out;
}

std::string make_close_frame(std::uint16_t code, const std::string &reason) {
    std::string payload;
    payload.reserve(2 + reason.size());
    payload.push_back(static_cast<char>((code >> 8) & 0xFF));
    payload.push_back(static_cast<char>(code & 0xFF));
    const std::size_t budget = 125 - payload.size();
    payload.append(reason.substr(0, budget));
    return make_frame(Opcode::close, payload);
}

ParsedFrame try_parse_frame(std::string &buffer) {
    ParsedFrame outcome;
    if (buffer.size() < 2) {
        return outcome;
    }
    const auto byte0 = static_cast<unsigned char>(buffer[0]);
    const auto byte1 = static_cast<unsigned char>(buffer[1]);
    const bool fin = (byte0 & 0x80) != 0;
    if ((byte0 & 0x70) != 0) {
        outcome.status = ParsedFrame::Status::protocol_error;
        outcome.close_code = kCloseProtocolError;
        outcome.reason = "nonzero RSV bits without a negotiated extension";
        return outcome;
    }
    const auto raw_opcode = static_cast<std::uint8_t>(byte0 & 0x0F);
    const bool known_opcode =
        raw_opcode <= 2 || raw_opcode == 8 || raw_opcode == 9 || raw_opcode == 10;
    if (!known_opcode) {
        outcome.status = ParsedFrame::Status::protocol_error;
        outcome.close_code = kCloseProtocolError;
        outcome.reason = "unknown opcode";
        return outcome;
    }
    const Opcode opcode = static_cast<Opcode>(raw_opcode);
    if ((byte1 & 0x80) == 0) {
        outcome.status = ParsedFrame::Status::protocol_error;
        outcome.close_code = kCloseProtocolError;
        outcome.reason = "unmasked client frame";
        return outcome;
    }
    const bool control = is_control(opcode);
    if (control && !fin) {
        outcome.status = ParsedFrame::Status::protocol_error;
        outcome.close_code = kCloseProtocolError;
        outcome.reason = "fragmented control frame";
        return outcome;
    }

    std::size_t cursor = 2;
    std::uint64_t length = byte1 & 0x7F;
    if (length == 126) {
        if (buffer.size() < cursor + 2) {
            return outcome;
        }
        const auto high = static_cast<unsigned char>(buffer[cursor]);
        const auto low = static_cast<unsigned char>(buffer[cursor + 1]);
        length = (static_cast<std::uint64_t>(high) << 8) | low;
        cursor += 2;
    } else if (length == 127) {
        if (buffer.size() < cursor + 8) {
            return outcome;
        }
        length = 0;
        for (int index = 0; index < 8; ++index) {
            length = (length << 8) | static_cast<unsigned char>(buffer[cursor + index]);
        }
        cursor += 8;
        if ((length >> 63) != 0) {
            outcome.status = ParsedFrame::Status::protocol_error;
            outcome.close_code = kCloseProtocolError;
            outcome.reason = "64-bit frame length with the top bit set";
            return outcome;
        }
    }
    if (control && length > 125) {
        outcome.status = ParsedFrame::Status::protocol_error;
        outcome.close_code = kCloseProtocolError;
        outcome.reason = "control frame payload exceeds 125 bytes";
        return outcome;
    }
    if (!control && length > kMaxMessageBytes) {
        outcome.status = ParsedFrame::Status::protocol_error;
        outcome.close_code = kCloseMessageTooBig;
        outcome.reason = "frame payload exceeds the bridge message cap";
        return outcome;
    }

    std::size_t mask_offset = cursor;
    cursor += 4;
    if (buffer.size() < cursor + length) {
        return outcome;
    }
    unsigned char mask[4];
    for (int index = 0; index < 4; ++index) {
        mask[index] = static_cast<unsigned char>(buffer[mask_offset + index]);
    }
    Frame frame;
    frame.opcode = opcode;
    frame.fin = fin;
    frame.payload.resize(static_cast<std::size_t>(length));
    for (std::size_t index = 0; index < static_cast<std::size_t>(length); ++index) {
        frame.payload[index] =
            static_cast<char>(static_cast<unsigned char>(buffer[cursor + index]) ^ mask[index % 4]);
    }
    buffer.erase(0, cursor + static_cast<std::size_t>(length));
    outcome.status = ParsedFrame::Status::frame;
    outcome.frame = std::move(frame);
    return outcome;
}

MessageAssembler::MessageAssembler(std::size_t max_message_bytes)
    : max_message_bytes_(max_message_bytes) {}

MessageAssembler::Result MessageAssembler::consume(const Frame &frame) {
    Result outcome;
    const bool control = is_control(frame.opcode);
    if (control) {
        outcome.status = Result::Status::protocol_error;
        outcome.close_code = kCloseProtocolError;
        outcome.reason = "control frame handed to the message assembler";
        return outcome;
    }
    if (!in_message_ && frame.opcode == Opcode::continuation) {
        outcome.status = Result::Status::protocol_error;
        outcome.close_code = kCloseProtocolError;
        outcome.reason = "continuation frame without a started message";
        return outcome;
    }
    if (in_message_ && frame.opcode != Opcode::continuation) {
        outcome.status = Result::Status::protocol_error;
        outcome.close_code = kCloseProtocolError;
        outcome.reason = "new data frame inside a fragmented message";
        return outcome;
    }
    if (!in_message_) {
        in_message_ = true;
        opcode_ = frame.opcode;
        payload_.clear();
    }
    if (payload_.size() + frame.payload.size() > max_message_bytes_) {
        in_message_ = false;
        payload_.clear();
        outcome.status = Result::Status::protocol_error;
        outcome.close_code = kCloseMessageTooBig;
        outcome.reason = "assembled message exceeds the bridge message cap";
        return outcome;
    }
    payload_.append(frame.payload);
    if (!frame.fin) {
        return outcome;
    }
    in_message_ = false;
    outcome.status = Result::Status::message;
    outcome.opcode = opcode_;
    outcome.payload = std::move(payload_);
    payload_.clear();
    return outcome;
}

Handshake try_parse_handshake(std::string &buffer) {
    Handshake outcome;
    const std::size_t header_end = buffer.find("\r\n\r\n");
    if (header_end == std::string::npos) {
        if (buffer.size() > kMaxHandshakeBytes) {
            outcome.status = Handshake::Status::reject;
            outcome.reason = "handshake request exceeds the header cap";
        }
        return outcome;
    }

    std::string upgrade;
    std::string connection;
    std::string key;
    std::string version;
    std::size_t line_start = 0;
    bool request_line = true;
    bool malformed = false;
    while (line_start <= header_end) {
        std::size_t line_end = buffer.find("\r\n", line_start);
        if (line_end == std::string::npos || line_end > header_end) {
            line_end = header_end;
        }
        const std::string line = buffer.substr(line_start, line_end - line_start);
        line_start = line_end + 2;
        if (line.empty()) {
            break;
        }
        if (request_line) {
            request_line = false;
            // RFC 7230 request-line: METHOD SP target SP HTTP-version.
            if (line.rfind("GET ", 0) != 0 || line.size() < 8 ||
                line.compare(line.size() - 8, 8, "HTTP/1.1") != 0) {
                malformed = true;
                break;
            }
            continue;
        }
        const std::size_t colon = line.find(':');
        if (colon == std::string::npos) {
            malformed = true;
            break;
        }
        const std::string name = to_lower(line.substr(0, colon));
        std::size_t value_begin = colon + 1;
        while (value_begin < line.size() &&
               (line[value_begin] == ' ' || line[value_begin] == '\t')) {
            ++value_begin;
        }
        std::size_t value_end = line.size();
        while (value_end > value_begin &&
               (line[value_end - 1] == ' ' || line[value_end - 1] == '\t')) {
            --value_end;
        }
        const std::string value = line.substr(value_begin, value_end - value_begin);
        if (name == "upgrade") {
            upgrade = value;
        } else if (name == "connection") {
            connection = value;
        } else if (name == "sec-websocket-key") {
            key = value;
        } else if (name == "sec-websocket-version") {
            version = value;
        }
    }
    if (malformed) {
        outcome.status = Handshake::Status::reject;
        outcome.reason = "malformed HTTP request";
        return outcome;
    }
    if (!token_list_contains(upgrade, "websocket") || !token_list_contains(connection, "upgrade")) {
        outcome.status = Handshake::Status::reject;
        outcome.reason = "not a WebSocket upgrade request";
        return outcome;
    }
    if (version != "13") {
        outcome.status = Handshake::Status::reject;
        outcome.reason = "unsupported WebSocket version";
        return outcome;
    }
    std::string key_bytes;
    if (!base64_decode(key, 16, key_bytes)) {
        outcome.status = Handshake::Status::reject;
        outcome.reason = "invalid Sec-WebSocket-Key";
        return outcome;
    }

    buffer.erase(0, header_end + 4);
    outcome.status = Handshake::Status::ok;
    outcome.accept_value = accept_key(key);
    return outcome;
}

std::string make_handshake_response(const std::string &accept_value) {
    std::string out;
    out.reserve(160);
    out += "HTTP/1.1 101 Switching Protocols\r\n";
    out += "Upgrade: websocket\r\n";
    out += "Connection: Upgrade\r\n";
    out += "Sec-WebSocket-Accept: ";
    out += accept_value;
    out += "\r\n\r\n";
    return out;
}

std::string make_http_rejection() {
    return "HTTP/1.1 400 Bad Request\r\nConnection: close\r\nContent-Length: 0\r\n\r\n";
}

} // namespace mirage::devbridge::ws
