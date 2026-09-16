/// Length-prefixed frame codec mirroring `runtime/ipc/include/mirage/runtime/ipc/framing.hpp`
/// and `framing.cpp`: 4-byte little-endian unsigned payload length followed by
/// the payload bytes, 1 MiB payload cap, no silent truncation. Consumed by the
/// browser/devbridge side once real transports land (M1.5-03/M1.5-05); kept in
/// the contract package so both ends share the exact framing rules.

export const MAX_FRAME_BYTES = 1024 * 1024;
export const FRAME_HEADER_BYTES = 4;

const textEncoder = new TextEncoder();
const textDecoder = new TextDecoder('utf-8', { fatal: false });

export function encodeUtf8(payload: string): Uint8Array {
    return textEncoder.encode(payload);
}

export function decodeUtf8(bytes: Uint8Array): string {
    return textDecoder.decode(bytes);
}

/** Renders one payload as a length-prefixed frame. Throws when the payload
 * exceeds MAX_FRAME_BYTES (callers validate before encoding). */
export function makeFrame(payload: string | Uint8Array): Uint8Array {
    const bytes = typeof payload === 'string' ? encodeUtf8(payload) : payload;
    if (bytes.byteLength > MAX_FRAME_BYTES) {
        throw new RangeError(
            `frame payload of ${bytes.byteLength} bytes exceeds the ${MAX_FRAME_BYTES} byte cap`,
        );
    }
    const frame = new Uint8Array(FRAME_HEADER_BYTES + bytes.byteLength);
    const view = new DataView(frame.buffer);
    view.setUint32(0, bytes.byteLength, true);
    frame.set(bytes, FRAME_HEADER_BYTES);
    return frame;
}

export type FrameExtract =
    | { status: 'need-more-data' }
    | { status: 'message'; message: Uint8Array; rest: Uint8Array }
    | { status: 'protocol-error'; reason: string };

/** Extracts one complete frame from the front of `buffer`. Stateless, shared
 * by both ends; a protocol error means the connection must be closed. */
export function tryExtractFrame(buffer: Uint8Array): FrameExtract {
    if (buffer.byteLength < FRAME_HEADER_BYTES) {
        return { status: 'need-more-data' };
    }
    const view = new DataView(buffer.buffer, buffer.byteOffset, buffer.byteLength);
    const length = view.getUint32(0, true);
    if (length > MAX_FRAME_BYTES) {
        return {
            status: 'protocol-error',
            reason: `declared frame length ${length} exceeds the ${MAX_FRAME_BYTES} byte cap`,
        };
    }
    if (buffer.byteLength < FRAME_HEADER_BYTES + length) {
        return { status: 'need-more-data' };
    }
    const end = FRAME_HEADER_BYTES + length;
    return {
        status: 'message',
        message: buffer.slice(FRAME_HEADER_BYTES, end),
        rest: buffer.slice(end),
    };
}
