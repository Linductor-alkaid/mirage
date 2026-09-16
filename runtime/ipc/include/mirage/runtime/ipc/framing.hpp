#pragma once

#include <cstddef>
#include <string>
#include <string_view>

namespace mirage::runtime::ipc {

/// Maximum wire payload of one IPC message (DEC-007). Larger payloads are a
/// protocol error and close the connection; there is no silent truncation.
inline constexpr std::size_t kMaxFrameBytes = 1024 * 1024;

/// Size of the fixed frame header: 4-byte little-endian unsigned payload
/// length followed by the payload bytes.
inline constexpr std::size_t kFrameHeaderBytes = 4;

/// Renders one payload as a length-prefixed frame. The payload must not
/// exceed kMaxFrameBytes; callers validate before encoding.
std::string make_frame(std::string_view payload);

enum class FrameExtract {
    NeedMoreData,  ///< buffer holds no complete frame yet
    Message,       ///< one frame extracted; `message` carries the payload
    ProtocolError, ///< header or declared length violates the framing rules;
                   ///< `reason` explains and the connection must be closed
};

struct FrameExtraction {
    FrameExtract status = FrameExtract::NeedMoreData;
    std::string message; ///< meaningful only for Message
    std::string reason;  ///< meaningful only for ProtocolError
};

/// Extracts one complete frame from the front of `buffer`, erasing the
/// consumed bytes. Stateless, so both the server loop and clients share the
/// exact framing rules.
FrameExtraction try_extract_frame(std::string &buffer);

} // namespace mirage::runtime::ipc
