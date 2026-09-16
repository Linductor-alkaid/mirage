/// Contract tests for the 4-byte little-endian length-prefixed framing codec
/// (framing.ts), mirroring runtime/ipc framing.hpp/framing.cpp: 1 MiB cap,
/// no silent truncation, explicit protocol-error reasons.

import { describe, expect, it } from 'vitest';
import {
    FRAME_HEADER_BYTES,
    MAX_FRAME_BYTES,
    decodeUtf8,
    encodeUtf8,
    makeFrame,
    tryExtractFrame,
} from '../src/framing.js';

function concat(frames: Uint8Array[]): Uint8Array {
    const total = frames.reduce((sum, frame) => sum + frame.byteLength, 0);
    const out = new Uint8Array(total);
    let offset = 0;
    for (const frame of frames) {
        out.set(frame, offset);
        offset += frame.byteLength;
    }
    return out;
}

function headerOf(length: number): Uint8Array {
    const header = new Uint8Array(4);
    new DataView(header.buffer).setUint32(0, length, true);
    return header;
}

describe('constants', () => {
    it('pin the 1 MiB cap and 4-byte header', () => {
        expect(MAX_FRAME_BYTES).toBe(1048576);
        expect(FRAME_HEADER_BYTES).toBe(4);
    });
});

describe('makeFrame / tryExtractFrame round-trip', () => {
    it('ascii payload', () => {
        const extracted = tryExtractFrame(makeFrame('hello frames'));
        expect(extracted.status).toBe('message');
        if (extracted.status !== 'message') return;
        expect(decodeUtf8(extracted.message)).toBe('hello frames');
        expect(extracted.rest.byteLength).toBe(0);
    });

    it('multi-byte utf-8 payload (Chinese + emoji)', () => {
        const payload = '中文帧测试 🚀 ——长度按字节计';
        const extracted = tryExtractFrame(makeFrame(payload));
        expect(extracted.status).toBe('message');
        if (extracted.status !== 'message') return;
        expect(decodeUtf8(extracted.message)).toBe(payload);
        expect(extracted.message.byteLength).toBe(encodeUtf8(payload).byteLength);
    });

    it('empty payload', () => {
        const extracted = tryExtractFrame(makeFrame(''));
        expect(extracted.status).toBe('message');
        if (extracted.status !== 'message') return;
        expect(extracted.message.byteLength).toBe(0);
    });

    it('binary payload bytes survive verbatim', () => {
        const payload = new Uint8Array([0x00, 0xff, 0x80, 0x7f, 0x01, 0xfe]);
        const extracted = tryExtractFrame(makeFrame(payload));
        expect(extracted.status).toBe('message');
        if (extracted.status !== 'message') return;
        expect(Array.from(extracted.message)).toEqual([0x00, 0xff, 0x80, 0x7f, 0x01, 0xfe]);
    });

    it('writes a little-endian length header', () => {
        const frame = makeFrame('A');
        expect(Array.from(frame.subarray(0, 4))).toEqual([1, 0, 0, 0]);
    });
});

describe('need-more-data', () => {
    it('empty buffer', () => {
        expect(tryExtractFrame(new Uint8Array(0))).toEqual({ status: 'need-more-data' });
    });

    it('partial header', () => {
        expect(tryExtractFrame(new Uint8Array([5, 0, 0]))).toEqual({ status: 'need-more-data' });
    });

    it('complete header but partial payload', () => {
        const buffer = concat([headerOf(100), encodeUtf8('only five')]);
        expect(tryExtractFrame(buffer)).toEqual({ status: 'need-more-data' });
    });

    it('chunked delivery completes once the tail arrives', () => {
        const frame = makeFrame('split payload');
        const first = tryExtractFrame(frame.slice(0, 6));
        expect(first.status).toBe('need-more-data');
        const second = tryExtractFrame(concat([frame.slice(0, 6), frame.slice(6)]));
        expect(second.status).toBe('message');
        if (second.status !== 'message') return;
        expect(decodeUtf8(second.message)).toBe('split payload');
    });
});

describe('protocol errors', () => {
    it('declared length above the cap reports protocol-error with the stable reason', () => {
        const buffer = concat([headerOf(MAX_FRAME_BYTES + 1), new Uint8Array(64)]);
        const extracted = tryExtractFrame(buffer);
        expect(extracted).toEqual({
            status: 'protocol-error',
            reason: 'declared frame length 1048577 exceeds the 1048576 byte cap',
        });
    });

    it('reports the protocol error even when fewer bytes than the declaration arrived', () => {
        const extracted = tryExtractFrame(headerOf(0xffffffff));
        expect(extracted).toEqual({
            status: 'protocol-error',
            reason: 'declared frame length 4294967295 exceeds the 1048576 byte cap',
        });
    });
});

describe('cap boundary', () => {
    it('accepts a payload of exactly MAX_FRAME_BYTES on both sides', () => {
        const payload = new Uint8Array(MAX_FRAME_BYTES);
        payload[0] = 0x41;
        payload[MAX_FRAME_BYTES - 1] = 0x42;
        const frame = makeFrame(payload);
        expect(frame.byteLength).toBe(FRAME_HEADER_BYTES + MAX_FRAME_BYTES);
        const extracted = tryExtractFrame(frame);
        expect(extracted.status).toBe('message');
        if (extracted.status !== 'message') return;
        expect(extracted.message.byteLength).toBe(MAX_FRAME_BYTES);
    });

    it('makeFrame throws RangeError above the cap', () => {
        const payload = new Uint8Array(MAX_FRAME_BYTES + 1);
        expect(() => makeFrame(payload)).toThrow(RangeError);
        expect(() => makeFrame(payload)).toThrow(
            'frame payload of 1048577 bytes exceeds the 1048576 byte cap',
        );
        expect(() => makeFrame('x'.repeat(MAX_FRAME_BYTES + 1))).toThrow(RangeError);
    });
});

describe('multiple frames', () => {
    it('extracts consecutive frames from one buffer', () => {
        const buffer = concat([makeFrame('one'), makeFrame('two'), makeFrame('three')]);
        const first = tryExtractFrame(buffer);
        expect(first.status).toBe('message');
        if (first.status !== 'message') return;
        expect(decodeUtf8(first.message)).toBe('one');

        const second = tryExtractFrame(first.rest);
        expect(second.status).toBe('message');
        if (second.status !== 'message') return;
        expect(decodeUtf8(second.message)).toBe('two');

        const third = tryExtractFrame(second.rest);
        expect(third.status).toBe('message');
        if (third.status !== 'message') return;
        expect(decodeUtf8(third.message)).toBe('three');
        expect(third.rest.byteLength).toBe(0);
    });

    it('leaves trailing bytes in rest', () => {
        const junk = new Uint8Array([0xde, 0xad, 0xbe]);
        const extracted = tryExtractFrame(concat([makeFrame('a'), junk]));
        expect(extracted.status).toBe('message');
        if (extracted.status !== 'message') return;
        expect(decodeUtf8(extracted.message)).toBe('a');
        expect(Array.from(extracted.rest)).toEqual([0xde, 0xad, 0xbe]);
        expect(tryExtractFrame(extracted.rest)).toEqual({ status: 'need-more-data' });
    });

    it('does not consume a following frame beyond the declared length', () => {
        const extracted = tryExtractFrame(concat([makeFrame('x'), makeFrame('y')]));
        expect(extracted.status).toBe('message');
        if (extracted.status !== 'message') return;
        expect(decodeUtf8(extracted.message)).toBe('x');
        expect(decodeUtf8(tryExtractMessage(extracted.rest))).toBe('y');
    });
});

/** Small local convenience: extract-or-throw for readability in tests. */
function tryExtractMessage(buffer: Uint8Array): Uint8Array {
    const extracted = tryExtractFrame(buffer);
    if (extracted.status !== 'message') {
        throw new Error(`expected a complete frame, got ${extracted.status}`);
    }
    return extracted.message;
}
