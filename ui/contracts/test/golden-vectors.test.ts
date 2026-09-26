/// M1.5-01 golden-vector consistency gate: reads the shared
/// tests/runtime/data/ipc_protocol_golden.json — the very same file the C++
/// gate (tests/runtime/ipc_protocol_golden_test.cpp) consumes — and locks the
/// TypeScript mirror byte-for-byte against the canonical wire forms, per
/// docs/design/mirage-ipc-protocol-v1.md section 8. Unlike the C++ side, this
/// suite also consumes the DEC-012 `events` / `event_failures` sections (the
/// C++ event codec lands in M1.5-02); there is no event encoder on either
/// side yet, so events assert the decode direction plus a byte-level
/// JSON.stringify lock.

import { readFileSync } from 'node:fs';

import { describe, expect, it } from 'vitest';

import { decodeEvent, decodeRequest, decodeResponse, encodeRequest, encodeResponse } from '../src/codec.js';
import { decodeUtf8, makeFrame, tryExtractFrame } from '../src/framing.js';
import type {
    HostStatus,
    IpcError,
    InspectTask,
    PendingPermission,
    RequestBody,
    ResponseEnvelop,
    ServerEvent,
    StepView,
    TaskProgress,
    TaskSummary,
} from '../src/types.js';

// --- shared vectors file (single source of truth, never copied) --------------

interface GoldenRequestVector {
    name: string;
    id: number;
    body: RequestBody;
    canonical: string;
}

interface GoldenFailureVector {
    name: string;
    payload: string;
    error?: string;
    error_prefix?: string;
}

/** Inspect value as authored in the vectors file: member presence of
 * `success` *is* the has_success semantics (schema doc section 6.2). */
interface GoldenInspectValue {
    id: string;
    goal: string;
    progress: TaskProgress;
    success?: boolean;
    steps: StepView[];
}

type GoldenPayload =
    | { kind: 'identity'; value: { service: string; mirage_version: string; mira_core_version: string; host_status: HostStatus; protocol: number; events?: boolean; permissions?: boolean } }
    | { kind: 'submitted'; value: { task_id: string } }
    | { kind: 'list'; value: { tasks: TaskSummary[] } }
    | { kind: 'inspect'; value: GoldenInspectValue }
    | { kind: 'cancelled'; value: { task_id: string; progress: TaskProgress } }
    | { kind: 'shutdown-accepted' }
    | { kind: 'permission-responded'; value: { request_id: string } }
    | { kind: 'permission-list'; value: { pending: PendingPermission[] } };

type GoldenEnvelop =
    | { id: number; ok: true; payload: GoldenPayload }
    | { id: number; ok: false; error: { code: IpcError['code']; message: string } };

interface GoldenResponseVector {
    name: string;
    response: GoldenEnvelop;
    canonical: string;
    /** Set (hello-capability) while the C++ encoder cannot write the `events`
     * identity member; the TypeScript mirror has no such gap. */
    encode_pending?: string;
}

interface GoldenEventVector {
    name: string;
    event: ServerEvent;
    canonical: string;
}

interface GoldenFramingVector {
    name: string;
    payload: string;
    frame_hex: string;
}

interface GoldenFramingFailureVector {
    name: string;
    declared_length: number;
    reason: string;
}

interface GoldenVectorsFile {
    meta: { schema: string; version: number; protocol_version: number };
    requests: GoldenRequestVector[];
    request_failures: GoldenFailureVector[];
    responses: GoldenResponseVector[];
    response_failures: GoldenFailureVector[];
    events: GoldenEventVector[];
    event_failures: GoldenFailureVector[];
    framing: GoldenFramingVector[];
    framing_failures: GoldenFramingFailureVector[];
}

// ui/contracts/test/ -> ui/contracts/ -> ui/ -> repository root.
const VECTORS_URL = new URL('../../../tests/runtime/data/ipc_protocol_golden.json', import.meta.url);
const vectors = JSON.parse(readFileSync(VECTORS_URL, 'utf8')) as GoldenVectorsFile;

// --- helpers -----------------------------------------------------------------

/** Maps a vector's structured response onto the codec's ResponseEnvelop:
 * member presence of `success` becomes has_success, exactly like both
 * decoders do. */
function expectedEnvelop(vector: GoldenResponseVector): ResponseEnvelop {
    const response = vector.response;
    if (!response.ok) {
        return { ok: false, id: response.id, error: response.error };
    }
    switch (response.payload.kind) {
        case 'identity':
            return { ok: true, id: response.id, payload: { kind: 'identity', value: { ...response.payload.value } } };
        case 'submitted':
            return { ok: true, id: response.id, payload: { kind: 'submitted', value: { ...response.payload.value } } };
        case 'list':
            return { ok: true, id: response.id, payload: { kind: 'list', value: { tasks: response.payload.value.tasks } } };
        case 'cancelled':
            return { ok: true, id: response.id, payload: { kind: 'cancelled', value: { ...response.payload.value } } };
        case 'inspect': {
            const value = response.payload.value;
            const hasSuccess = value.success !== undefined;
            const task: InspectTask = {
                id: value.id,
                goal: value.goal,
                progress: value.progress,
                has_success: hasSuccess,
                success: hasSuccess ? value.success : undefined,
                steps: value.steps,
            };
            return { ok: true, id: response.id, payload: { kind: 'inspect', value: task } };
        }
        case 'permission-responded':
            return {
                ok: true,
                id: response.id,
                payload: { kind: 'permission-responded', value: { ...response.payload.value } },
            };
        case 'permission-list':
            return {
                ok: true,
                id: response.id,
                payload: {
                    kind: 'permission-list',
                    value: { pending: response.payload.value.pending },
                },
            };
        case 'shutdown-accepted':
            return { ok: true, id: response.id, payload: { kind: 'shutdown-accepted' } };
    }
}

/** Asserts a failure vector: `error` matches exactly, `error_prefix` by
 * prefix (the C++ parser appends its own diagnostic after "payload is not
 * valid JSON"; the TS decoder emits exactly the prefix). */
function expectGoldenFailure(
    result: { ok: false; error: string } | { ok: true },
    vector: GoldenFailureVector,
    surface: string,
): void {
    if (result.ok) {
        throw new Error(`golden vector '${vector.name}' (${surface}) decoded but must fail`);
    }
    if (vector.error !== undefined) {
        if (result.error !== vector.error) {
            throw new Error(
                `golden vector '${vector.name}' (${surface}) error mismatch\n  actual:   ${result.error}\n  expected: ${vector.error}`,
            );
        }
        expect(result.error).toBe(vector.error);
        return;
    }
    if (vector.error_prefix !== undefined) {
        if (!result.error.startsWith(vector.error_prefix)) {
            throw new Error(
                `golden vector '${vector.name}' (${surface}) error '${result.error}' does not start with '${vector.error_prefix}'`,
            );
        }
        expect(result.error.startsWith(vector.error_prefix)).toBe(true);
        return;
    }
    throw new Error(`golden vector '${vector.name}' carries neither 'error' nor 'error_prefix'`);
}

function toHex(bytes: Uint8Array): string {
    return Array.from(bytes, (byte) => byte.toString(16).padStart(2, '0')).join('');
}

// --- suites ------------------------------------------------------------------

describe('golden vectors meta', () => {
    it('is the mirage ipc golden vector schema at protocol v1', () => {
        expect(vectors.meta.schema).toBe('mirage-ipc-protocol-golden-vectors');
        expect(vectors.meta.version).toBeGreaterThanOrEqual(1);
        expect(vectors.meta.protocol_version).toBe(1);
    });
});

describe('golden requests (encode byte-exact + decode restore)', () => {
    it.each(vectors.requests)('$name', (vector) => {
        expect(encodeRequest(vector.id, vector.body)).toBe(vector.canonical);
        const decoded = decodeRequest(vector.canonical);
        if (!decoded.ok) {
            throw new Error(`golden vector '${vector.name}' failed to decode: ${decoded.error}`);
        }
        expect(decoded.id).toBe(vector.id);
        expect(decoded.body).toEqual(vector.body);
    });
});

describe('golden request failures (stable decode error strings)', () => {
    it.each(vectors.request_failures)('$name', (vector) => {
        expectGoldenFailure(decodeRequest(vector.payload), vector, 'decodeRequest');
    });
});

describe('golden responses (encode byte-exact + decode restore)', () => {
    it.each(vectors.responses)('$name', (vector) => {
        const expected = expectedEnvelop(vector);
        // No vector is encode_pending for the TypeScript mirror (it already
        // writes the hello `events` capability member); the C++ side skips
        // encoding hello-capability until M1.5-02.
        expect(encodeResponse(expected)).toBe(vector.canonical);
        const decoded = decodeResponse(vector.canonical);
        if (!decoded.ok) {
            throw new Error(`golden vector '${vector.name}' failed to decode: ${decoded.error}`);
        }
        expect(decoded.response).toEqual(expected);
    });
});

describe('golden response failures (stable decode error strings)', () => {
    it.each(vectors.response_failures)('$name', (vector) => {
        expectGoldenFailure(decodeResponse(vector.payload), vector, 'decodeResponse');
    });
});

describe('golden events (TS-only in M1.5-01: decode + stringify lock, no encoder)', () => {
    it.each(vectors.events)('$name', (vector) => {
        const decoded = decodeEvent(vector.canonical);
        if (!decoded.ok) {
            throw new Error(`golden vector '${vector.name}' failed to decode: ${decoded.error}`);
        }
        expect(decoded.event).toEqual(vector.event);
        // Byte-level lock without an encoder: the decoded event re-serializes
        // to the canonical string (member insertion order is the wire order).
        expect(JSON.stringify(decoded.event)).toBe(vector.canonical);
    });
});

describe('golden event failures (stable decode error strings)', () => {
    it.each(vectors.event_failures)('$name', (vector) => {
        expectGoldenFailure(decodeEvent(vector.payload), vector, 'decodeEvent');
    });
});

describe('golden framing (little-endian length prefix)', () => {
    it.each(vectors.framing)('$name', (vector) => {
        const frame = makeFrame(vector.payload);
        expect(toHex(frame)).toBe(vector.frame_hex);
        const extraction = tryExtractFrame(frame);
        if (extraction.status !== 'message') {
            throw new Error(`golden vector '${vector.name}' did not extract a frame`);
        }
        expect(decodeUtf8(extraction.message)).toBe(vector.payload);
        expect(extraction.rest.byteLength).toBe(0);
    });
});

describe('golden framing failures (cap violations are protocol errors)', () => {
    it.each(vectors.framing_failures)('$name', (vector) => {
        const header = new Uint8Array(4);
        new DataView(header.buffer).setUint32(0, vector.declared_length, true);
        const extraction = tryExtractFrame(header);
        if (extraction.status !== 'protocol-error') {
            throw new Error(`golden vector '${vector.name}' was not rejected as a protocol error`);
        }
        expect(extraction.reason).toBe(vector.reason);
    });
});
