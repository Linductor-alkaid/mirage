#pragma once

// Self-contained RFC 6455 server-side primitives for the development bridge
// (M1.5-03, DEC-012 decision 6). The pinned dependency set has no WebSocket
// implementation and devbridge must not introduce one, so the codec lives
// here: the opening handshake (SHA-1 + base64 accept key), frame parsing
// with mandatory client masking, and the server frame encoder. Pure byte
// machinery, unit-testable without sockets.

#include <cstddef>
#include <cstdint>
#include <string>

namespace mirage::devbridge::ws {

/// RFC 6455 section 1.3 connection-opening GUID, appended to the client key
/// before hashing.
inline constexpr char kWebSocketGuid[] = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

/// Largest WebSocket message the bridge accepts from the browser: exactly
/// one length-prefixed IPC frame (4-byte header + kMaxFrameBytes payload).
inline constexpr std::size_t kMaxMessageBytes = 1024 * 1024 + 4;

/// Cap on the HTTP opening-handshake request bytes; a client that has not
/// finished its request by then is rejected.
inline constexpr std::size_t kMaxHandshakeBytes = 8192;

/// Close codes this bridge sends (RFC 6455 section 7.4.1).
inline constexpr std::uint16_t kCloseNormal = 1000;
inline constexpr std::uint16_t kCloseGoingAway = 1001;
inline constexpr std::uint16_t kCloseProtocolError = 1002;
inline constexpr std::uint16_t kCloseUnsupportedData = 1003;
inline constexpr std::uint16_t kCloseMessageTooBig = 1009;
inline constexpr std::uint16_t kCloseInternalError = 1011;

/// RFC 3174 SHA-1 of `message` written as 20 raw bytes. Exposed for tests;
/// accept_key() is its only in-tree consumer.
void sha1(const std::string &message, unsigned char digest[20]);

/// Standard base64 (RFC 4648, with padding) of `size` bytes.
std::string base64_encode(const unsigned char *data, std::size_t size);

/// Decodes standard base64; false when the input is malformed or the padded
/// length is not `expected_size`. Used to validate the client handshake key
/// (16 decoded bytes per RFC 6455 section 4.2.1).
bool base64_decode(std::string_view text, std::size_t expected_size, std::string &out);

/// Computes the Sec-WebSocket-Accept value for a client Sec-WebSocket-Key.
std::string accept_key(const std::string &client_key);

enum class Opcode : std::uint8_t {
    continuation = 0x0,
    text = 0x1,
    binary = 0x2,
    close = 0x8,
    ping = 0x9,
    pong = 0xA,
};

bool is_control(Opcode opcode);

/// Encodes one unmasked server-to-client frame with FIN set.
std::string make_frame(Opcode opcode, const std::string &payload);

/// Encodes a close frame; `reason` is truncated to the 125-byte control
/// payload budget (code + reason).
std::string make_close_frame(std::uint16_t code, const std::string &reason);

struct Frame {
    Opcode opcode = Opcode::continuation;
    bool fin = true;
    std::string payload;
};

struct ParsedFrame {
    enum class Status { need_more_data, frame, protocol_error };
    Status status = Status::need_more_data;
    Frame frame;                  ///< meaningful only for frame
    std::uint16_t close_code = 0; ///< protocol_error: close code to answer with
    std::string reason;           ///< protocol_error
};

/// Extracts one complete client frame from the front of `buffer`, erasing
/// the consumed bytes. Server side: client frames must be masked, RSV bits
/// must be zero and control frames must be unfragmented and at most 125
/// payload bytes (RFC 6455 section 5.5); anything else is a protocol error
/// and the connection must be closed.
ParsedFrame try_parse_frame(std::string &buffer);

/// Reassembles fragmented data messages (RFC 6455 section 5.4) on top of
/// try_parse_frame. Control frames pass through unconsumed; the caller must
/// handle them itself and not feed them here.
class MessageAssembler {
  public:
    explicit MessageAssembler(std::size_t max_message_bytes);

    struct Result {
        enum class Status { need_more_data, message, protocol_error };
        Status status = Status::need_more_data;
        Opcode opcode = Opcode::binary; ///< message: text or binary
        std::string payload;            ///< message
        std::uint16_t close_code = 0;   ///< protocol_error
        std::string reason;             ///< protocol_error
    };

    Result consume(const Frame &frame);

  private:
    std::size_t max_message_bytes_;
    bool in_message_ = false;
    Opcode opcode_ = Opcode::binary;
    std::string payload_;
};

struct Handshake {
    enum class Status { need_more_data, ok, reject };
    Status status = Status::need_more_data;
    std::string accept_value; ///< ok: the 101 answer's Sec-WebSocket-Accept
    std::string reason;       ///< reject
};

/// Parses the HTTP opening handshake out of the front of `buffer`, erasing
/// exactly the consumed request bytes (any bytes after the blank line stay
/// in the buffer: a browser may pipeline its first frame behind the
/// handshake). `ok` requires a GET request with the RFC 6455 upgrade tokens,
/// a 16-byte base64 key and version 13.
Handshake try_parse_handshake(std::string &buffer);

/// The fixed 101 answer with `accept_value` already computed.
std::string make_handshake_response(const std::string &accept_value);

/// A plain HTTP/1.1 rejection used for non-WebSocket clients probing the
/// port; the caller closes the connection after writing it.
std::string make_http_rejection();

} // namespace mirage::devbridge::ws
