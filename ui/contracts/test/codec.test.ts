/// Contract tests for the protocol v1 codec (codec.ts): encode/decode
/// round-trips for the full request/response/event surface plus strict
/// decoding negatives with stable reason strings mirrored from
/// runtime/ipc/src/protocol.cpp (M1.5-01 golden vectors).

import { describe, expect, it } from 'vitest';
import {
    classifyFrame,
    decodeEvent,
    decodeRequest,
    decodeResponse,
    encodeRequest,
    encodeResponse,
} from '../src/codec.js';
import { PROTOCOL_VERSION } from '../src/types.js';
import type {
    InspectTask,
    RequestBody,
    ResponseEnvelop,
    ResponsePayload,
    ServiceIdentity,
    TaskStep,
} from '../src/types.js';

const READ_STEP: TaskStep = { op: 'filesystem.read', arg: '/tmp/a.txt' };
const EXEC_STEP: TaskStep = { op: 'process.execute', arg: 'echo hi' };

function expectRequestOk(payload: string): { id: number; body: RequestBody } {
    const result = decodeRequest(payload);
    if (!result.ok) {
        throw new Error(`expected request decode ok, got: ${result.error}`);
    }
    return result;
}

function expectRequestFail(payload: string, error: string): void {
    expect(decodeRequest(payload)).toEqual({ ok: false, error });
}

function expectResponseOk(payload: string): ResponseEnvelop {
    const result = decodeResponse(payload);
    if (!result.ok) {
        throw new Error(`expected response decode ok, got: ${result.error}`);
    }
    return result.response;
}

function expectResponseFail(payload: string, error: string): void {
    expect(decodeResponse(payload)).toEqual({ ok: false, error });
}

function expectEventFail(payload: string, error: string): void {
    expect(decodeEvent(payload)).toEqual({ ok: false, error });
}

function roundTripRequest(id: number, body: RequestBody): void {
    expect(expectRequestOk(encodeRequest(id, body))).toEqual({ ok: true, id, body });
}

function roundTripResponse(response: ResponseEnvelop): void {
    expect(expectResponseOk(encodeResponse(response))).toEqual(response);
}

function payloadOf(response: ResponseEnvelop): ResponsePayload {
    if (!response.ok) {
        throw new Error('expected a successful response envelope');
    }
    return response.payload;
}

const IDENTITY: ServiceIdentity = {
    service: 'mirage-runtime',
    mirage_version: '0.1.0',
    mira_core_version: '0.1.0',
    host_status: 'running',
    protocol: 1,
};

const FULL_INSPECT: InspectTask = {
    id: 'task-0001',
    goal: 'open a terminal',
    progress: 'Completed',
    has_success: true,
    success: true,
    steps: [
        {
            index: 0,
            kind: 'filesystem.read',
            status: 'ok',
            operation_id: 'op-0001',
            permission: 'allowed',
            ok: true,
            exit_code: -1,
            result: 'file body',
            result_truncated: false,
            error: '',
        },
        {
            index: 1,
            kind: 'process.execute',
            status: 'ok',
            operation_id: 'op-0002',
            permission: 'allowed',
            ok: true,
            exit_code: 0,
            result: 'hi\n',
            result_truncated: false,
            error: '',
        },
    ],
};

describe('PROTOCOL_VERSION', () => {
    it('is pinned to 1', () => {
        expect(PROTOCOL_VERSION).toBe(1);
    });
});

describe('request round-trips', () => {
    it('hello', () => {
        roundTripRequest(0, { op: 'hello' });
    });

    it('task.submit with steps only', () => {
        roundTripRequest(7, { op: 'task.submit', goal: '清理缓存', steps: [READ_STEP, EXEC_STEP] });
    });

    it('task.submit with empty steps array', () => {
        roundTripRequest(1, { op: 'task.submit', goal: '', steps: [] });
    });

    it('task.submit with step_timeout_ms', () => {
        roundTripRequest(9, {
            op: 'task.submit',
            goal: 'g',
            steps: [EXEC_STEP],
            step_timeout_ms: 5000,
        });
    });

    it('task.list', () => {
        roundTripRequest(12, { op: 'task.list' });
    });

    it('task.inspect', () => {
        roundTripRequest(3, { op: 'task.inspect', task_id: 'task-0042' });
    });

    it('task.cancel', () => {
        roundTripRequest(4, { op: 'task.cancel', task_id: 'task-0042' });
    });

    it('service.shutdown', () => {
        roundTripRequest(5, { op: 'service.shutdown' });
    });
});

describe('request strict decoding', () => {
    it('rejects invalid JSON', () => {
        expectRequestFail('not json', 'payload is not valid JSON');
        expectRequestFail('', 'payload is not valid JSON');
    });

    it('rejects non-object payloads', () => {
        expectRequestFail('[]', 'request payload must be a JSON object');
        expectRequestFail('null', 'request payload must be a JSON object');
        expectRequestFail('42', 'request payload must be a JSON object');
    });

    it('rejects wrong protocol versions', () => {
        expectRequestFail('{}', 'unsupported protocol version');
        expectRequestFail('{"v":2,"id":0,"op":"hello"}', 'unsupported protocol version');
        expectRequestFail('{"v":"1","id":0,"op":"hello"}', 'unsupported protocol version');
        expectRequestFail('{"v":true,"id":0,"op":"hello"}', 'unsupported protocol version');
    });

    it('rejects missing or negative ids', () => {
        expectRequestFail('{"v":1,"op":"hello"}', "request is missing a non-negative 'id'");
        expectRequestFail('{"v":1,"id":-1,"op":"hello"}', "request is missing a non-negative 'id'");
        expectRequestFail('{"v":1,"id":1.5,"op":"hello"}', "request is missing a non-negative 'id'");
        expectRequestFail('{"v":1,"id":"7","op":"hello"}', "request is missing a non-negative 'id'");
    });

    it('rejects missing or unknown ops', () => {
        expectRequestFail('{"v":1,"id":0}', "request is missing 'op'");
        expectRequestFail('{"v":1,"id":0,"op":42}', "request is missing 'op'");
        expectRequestFail('{"v":1,"id":0,"op":"task.submit!"}', "unknown op 'task.submit!'");
    });

    it('task.submit requires a string goal', () => {
        expectRequestFail('{"v":1,"id":0,"op":"task.submit"}', "task.submit requires a 'goal' string");
        expectRequestFail(
            '{"v":1,"id":0,"op":"task.submit","goal":5}',
            "task.submit requires a 'goal' string",
        );
    });

    it('task.submit accepts an empty goal string (service-level policy)', () => {
        // The codec checks type only; non-empty policy lives in the service.
        const decoded = expectRequestOk('{"v":1,"id":0,"op":"task.submit","goal":""}');
        expect(decoded.body).toEqual({ op: 'task.submit', goal: '', steps: [] });
    });

    it("task.submit 'steps' must be an array", () => {
        expectRequestFail(
            '{"v":1,"id":0,"op":"task.submit","goal":"g","steps":5}',
            "task.submit 'steps' must be an array",
        );
        expectRequestFail(
            '{"v":1,"id":0,"op":"task.submit","goal":"g","steps":{"op":"filesystem.read"}}',
            "task.submit 'steps' must be an array",
        );
    });

    it('task.submit steps must be objects', () => {
        expectRequestFail(
            '{"v":1,"id":0,"op":"task.submit","goal":"g","steps":[5]}',
            'task.submit steps must be objects',
        );
        expectRequestFail(
            '{"v":1,"id":0,"op":"task.submit","goal":"g","steps":[null]}',
            'task.submit steps must be objects',
        );
    });

    it("task.submit step is missing 'op'", () => {
        expectRequestFail(
            '{"v":1,"id":0,"op":"task.submit","goal":"g","steps":[{}]}',
            "task.submit step is missing 'op'",
        );
        expectRequestFail(
            '{"v":1,"id":0,"op":"task.submit","goal":"g","steps":[{"op":7,"arg":"x"}]}',
            "task.submit step is missing 'op'",
        );
    });

    it('task.submit step has unsupported op', () => {
        expectRequestFail(
            '{"v":1,"id":0,"op":"task.submit","goal":"g","steps":[{"op":"filesystem.write","arg":"x"}]}',
            "task.submit step has unsupported op 'filesystem.write'",
        );
    });

    it("task.submit step requires a non-empty 'arg'", () => {
        expectRequestFail(
            '{"v":1,"id":0,"op":"task.submit","goal":"g","steps":[{"op":"filesystem.read"}]}',
            "task.submit step requires a non-empty 'arg'",
        );
        expectRequestFail(
            '{"v":1,"id":0,"op":"task.submit","goal":"g","steps":[{"op":"filesystem.read","arg":""}]}',
            "task.submit step requires a non-empty 'arg'",
        );
    });

    it("task.submit 'step_timeout_ms' must be positive", () => {
        for (const bad of ['0', '-1', '1.5', '"5000"', 'true']) {
            expectRequestFail(
                `{"v":1,"id":0,"op":"task.submit","goal":"g","step_timeout_ms":${bad}}`,
                "task.submit 'step_timeout_ms' must be positive",
            );
        }
    });

    it("task.inspect requires a non-empty 'task_id'", () => {
        expectRequestFail('{"v":1,"id":0,"op":"task.inspect"}', "task.inspect requires a non-empty 'task_id'");
        expectRequestFail(
            '{"v":1,"id":0,"op":"task.inspect","task_id":""}',
            "task.inspect requires a non-empty 'task_id'",
        );
    });

    it("task.cancel requires a non-empty 'task_id'", () => {
        expectRequestFail('{"v":1,"id":0,"op":"task.cancel"}', "task.cancel requires a non-empty 'task_id'");
        expectRequestFail(
            '{"v":1,"id":0,"op":"task.cancel","task_id":""}',
            "task.cancel requires a non-empty 'task_id'",
        );
    });
});

describe('response round-trips', () => {
    it('hello identity with events capability', () => {
        roundTripResponse({
            ok: true,
            id: 0,
            payload: { kind: 'identity', value: { ...IDENTITY, events: true } },
        });
    });

    it('hello identity with events false', () => {
        roundTripResponse({
            ok: true,
            id: 1,
            payload: { kind: 'identity', value: { ...IDENTITY, events: false } },
        });
    });

    it('hello identity without events member', () => {
        roundTripResponse({ ok: true, id: 2, payload: { kind: 'identity', value: { ...IDENTITY } } });
    });

    it('task submitted', () => {
        roundTripResponse({ ok: true, id: 3, payload: { kind: 'submitted', value: { task_id: 'task-0001' } } });
    });

    it('task list (empty and non-empty)', () => {
        roundTripResponse({ ok: true, id: 4, payload: { kind: 'list', value: { tasks: [] } } });
        roundTripResponse({
            ok: true,
            id: 5,
            payload: {
                kind: 'list',
                value: {
                    tasks: [
                        { id: 'task-0001', goal: 'first', progress: 'Completed' },
                        { id: 'task-0002', goal: 'second', progress: 'Active' },
                    ],
                },
            },
        });
    });

    it('task inspect (full)', () => {
        roundTripResponse({ ok: true, id: 6, payload: { kind: 'inspect', value: FULL_INSPECT } });
    });

    it('task inspect (minimal, no steps, no success)', () => {
        roundTripResponse({
            ok: true,
            id: 7,
            payload: {
                kind: 'inspect',
                value: { id: 'task-0002', goal: 'g', progress: 'Idle', has_success: false, steps: [] },
            },
        });
    });

    it('task cancelled', () => {
        roundTripResponse({
            ok: true,
            id: 8,
            payload: { kind: 'cancelled', value: { task_id: 'task-0001', progress: 'Cancelling' } },
        });
    });

    it('shutdown acknowledgement (no payload members)', () => {
        roundTripResponse({ ok: true, id: 9, payload: { kind: 'shutdown-accepted' } });
    });

    it('failed response with each representative code', () => {
        for (const code of ['protocol_error', 'unsupported', 'not_found', 'pinned_runtime'] as const) {
            roundTripResponse({
                ok: false,
                id: 10,
                error: { code, message: `task 'x' was not found` },
            });
        }
    });
});

describe('response strict decoding', () => {
    it('rejects invalid JSON and non-objects', () => {
        expectResponseFail('{', 'payload is not valid JSON');
        expectResponseFail('[]', 'response payload must be a JSON object');
    });

    it('rejects wrong version, missing id, missing ok', () => {
        expectResponseFail('{"id":0,"ok":true}', 'unsupported protocol version');
        expectResponseFail('{"v":1,"ok":true}', "response is missing a non-negative 'id'");
        expectResponseFail('{"v":1,"id":0}', "response is missing 'ok'");
        expectResponseFail('{"v":1,"id":0,"ok":1}', "response is missing 'ok'");
    });

    it('failed response requires an error object with code and message', () => {
        expectResponseFail('{"v":1,"id":0,"ok":false}', "failed response is missing an 'error' object");
        expectResponseFail(
            '{"v":1,"id":0,"ok":false,"error":"boom"}',
            "failed response is missing an 'error' object",
        );
        expectResponseFail(
            '{"v":1,"id":0,"ok":false,"error":{}}',
            "response error requires 'code' and 'message'",
        );
        expectResponseFail(
            '{"v":1,"id":0,"ok":false,"error":{"code":"internal"}}',
            "response error requires 'code' and 'message'",
        );
    });

    it('failure discrimination wins over payload members', () => {
        const decoded = expectResponseOk(
            '{"v":1,"id":3,"ok":false,"error":{"code":"internal","message":"boom"},"task_id":"x"}',
        );
        expect(decoded).toEqual({
            ok: false,
            id: 3,
            error: { code: 'internal', message: 'boom' },
        });
    });

    it('hello identity requires all identity members', () => {
        expectResponseFail('{"v":1,"id":0,"ok":true,"service":"mirage-runtime"}', 'hello response is missing identity members');
        expectResponseFail(
            '{"v":1,"id":0,"ok":true,"service":"s","mirage_version":"1","mira_core_version":"1","host_status":"running"}',
            'hello response is missing identity members',
        );
        expectResponseFail(
            '{"v":1,"id":0,"ok":true,"service":"s","mirage_version":"1","mira_core_version":"1","host_status":"running","protocol":true}',
            'hello response is missing identity members',
        );
    });

    it("hello 'events' must be a boolean when present", () => {
        expectResponseFail(
            '{"v":1,"id":0,"ok":true,"service":"s","mirage_version":"1","mira_core_version":"1","host_status":"running","protocol":1,"events":"yes"}',
            "hello response 'events' must be a boolean",
        );
    });

    it("task.submit response requires a non-empty 'task_id'", () => {
        expectResponseFail('{"v":1,"id":0,"ok":true,"task_id":""}', "task.submit response requires a non-empty 'task_id'");
        expectResponseFail('{"v":1,"id":0,"ok":true,"task_id":5}', "task.submit response requires a non-empty 'task_id'");
    });

    it("task.list 'tasks' must be an array of complete entries", () => {
        expectResponseFail('{"v":1,"id":0,"ok":true,"tasks":{}}', "task.list 'tasks' must be an array");
        expectResponseFail('{"v":1,"id":0,"ok":true,"tasks":[5]}', 'task.list entries must be objects');
        expectResponseFail('{"v":1,"id":0,"ok":true,"tasks":[{"id":"a"}]}', 'task.list entries require id, goal and progress');
        expectResponseFail(
            '{"v":1,"id":0,"ok":true,"tasks":[{"id":"a","goal":"g"}]}',
            'task.list entries require id, goal and progress',
        );
    });

    it("task.inspect 'task' must be an object", () => {
        expectResponseFail('{"v":1,"id":0,"ok":true,"task":5}', "task.inspect 'task' must be an object");
    });

    it('task.inspect requires id, goal and progress', () => {
        expectResponseFail(
            '{"v":1,"id":0,"ok":true,"task":{"goal":"g","progress":"Active"}}',
            'task.inspect requires id, goal and progress',
        );
    });

    it("task.inspect 'success' must be a boolean", () => {
        expectResponseFail(
            '{"v":1,"id":0,"ok":true,"task":{"id":"a","goal":"g","progress":"Completed","success":1}}',
            "task.inspect 'success' must be a boolean",
        );
    });

    it("task.inspect 'steps' must be an array of objects", () => {
        expectResponseFail(
            '{"v":1,"id":0,"ok":true,"task":{"id":"a","goal":"g","progress":"Active","steps":{}}}',
            "task.inspect 'steps' must be an array",
        );
        expectResponseFail(
            '{"v":1,"id":0,"ok":true,"task":{"id":"a","goal":"g","progress":"Active","steps":[null]}}',
            'task inspect steps must be objects',
        );
    });

    it("task.cancel 'task_cancelled' must be an object with task_id and progress", () => {
        expectResponseFail(
            '{"v":1,"id":0,"ok":true,"task_cancelled":"x"}',
            "task.cancel 'task_cancelled' must be an object",
        );
        expectResponseFail(
            '{"v":1,"id":0,"ok":true,"task_cancelled":{"task_id":"task-0001"}}',
            "task.cancel requires 'task_id' and 'progress'",
        );
    });

    it('payload discrimination follows member precedence service > task_id > tasks > task > task_cancelled > ack', () => {
        const base = '"v":1,"id":0,"ok":true';
        const service = '"service":"s","mirage_version":"1","mira_core_version":"1","host_status":"running","protocol":1';
        expect(payloadOf(expectResponseOk(`{${base},${service},"task_id":"x"}`))).toEqual({
            kind: 'identity',
            value: { service: 's', mirage_version: '1', mira_core_version: '1', host_status: 'running', protocol: 1 },
        });
        expect(payloadOf(expectResponseOk(`{${base},"task_id":"x","tasks":[]}`))).toEqual({
            kind: 'submitted',
            value: { task_id: 'x' },
        });
        expect(payloadOf(expectResponseOk(`{${base},"tasks":[],"task":{}}`))).toEqual({
            kind: 'list',
            value: { tasks: [] },
        });
        expect(payloadOf(expectResponseOk(`{${base},"task":{"id":"a","goal":"g","progress":"Idle"},"task_cancelled":{}}`)).kind)
            .toBe('inspect');
        expect(payloadOf(expectResponseOk(`{${base},"task_cancelled":{"task_id":"a","progress":"Idle"}}`))).toEqual({
            kind: 'cancelled',
            value: { task_id: 'a', progress: 'Idle' },
        });
        expect(payloadOf(expectResponseOk(`{${base}}`))).toEqual({ kind: 'shutdown-accepted' });
    });
});

describe('event decoding', () => {
    it('task.updated', () => {
        const payload = JSON.stringify({
            v: 1,
            seq: 4,
            event: 'task.updated',
            task_id: 'task-0001',
            goal: 'g',
            progress: 'Completed',
            has_success: true,
            success: true,
        });
        expect(decodeEvent(payload)).toEqual({
            ok: true,
            event: {
                v: 1,
                seq: 4,
                event: 'task.updated',
                task_id: 'task-0001',
                goal: 'g',
                progress: 'Completed',
                has_success: true,
                success: true,
            },
        });
    });

    it('host.status', () => {
        expect(decodeEvent('{"v":1,"seq":1,"event":"host.status","status":"starting"}')).toEqual({
            ok: true,
            event: { v: 1, seq: 1, event: 'host.status', status: 'starting' },
        });
    });

    it('events.overflow with dropped 0 and positive counts', () => {
        expect(decodeEvent('{"v":1,"seq":2,"event":"events.overflow","dropped":0}')).toEqual({
            ok: true,
            event: { v: 1, seq: 2, event: 'events.overflow', dropped: 0 },
        });
        expect(decodeEvent('{"v":1,"seq":3,"event":"events.overflow","dropped":9}')).toEqual({
            ok: true,
            event: { v: 1, seq: 3, event: 'events.overflow', dropped: 9 },
        });
    });

    it('rejects invalid JSON and non-objects', () => {
        expectEventFail('~', 'payload is not valid JSON');
        expectEventFail('"event"', 'event payload must be a JSON object');
    });

    it('rejects wrong version', () => {
        expectEventFail('{"v":2,"seq":1,"event":"host.status","status":"running"}', 'unsupported protocol version');
    });

    it("requires a positive 'seq'", () => {
        expectEventFail('{"v":1,"event":"host.status","status":"running"}', "event is missing a positive 'seq'");
        for (const seq of ['0', '-1', '1.5', 'true', '"3"']) {
            expectEventFail(
                `{"v":1,"seq":${seq},"event":"host.status","status":"running"}`,
                "event is missing a positive 'seq'",
            );
        }
    });

    it("requires 'event'", () => {
        expectEventFail('{"v":1,"seq":1}', "event is missing 'event'");
        expectEventFail('{"v":1,"seq":1,"event":7}', "event is missing 'event'");
    });

    it('rejects unknown events', () => {
        expectEventFail('{"v":1,"seq":1,"event":"task.deleted"}', "unknown event 'task.deleted'");
    });

    it('task.updated requires its five payload members', () => {
        const ok = {
            task_id: '"task-0001"',
            goal: '"g"',
            progress: '"Active"',
            has_success: 'false',
            success: 'false',
        };
        for (const missing of Object.keys(ok)) {
            const members = Object.entries(ok)
                .filter(([key]) => key !== missing)
                .map(([key, value]) => `"${key}":${value}`)
                .join(',');
            expectEventFail(
                `{"v":1,"seq":1,"event":"task.updated",${members}}`,
                "task.updated requires 'task_id', 'goal', 'progress', 'has_success' and 'success'",
            );
        }
        expectEventFail(
            '{"v":1,"seq":1,"event":"task.updated","task_id":"t","goal":"g","progress":"Active","has_success":1,"success":false}',
            "task.updated requires 'task_id', 'goal', 'progress', 'has_success' and 'success'",
        );
    });

    it("task.updated 'progress' must be a known progress name", () => {
        expectEventFail(
            '{"v":1,"seq":1,"event":"task.updated","task_id":"t","goal":"g","progress":"Running","has_success":false,"success":false}',
            "task.updated 'progress' is not a known progress name",
        );
    });

    it("host.status requires a known 'status'", () => {
        expectEventFail('{"v":1,"seq":1,"event":"host.status"}', "host.status requires 'status'");
        expectEventFail('{"v":1,"seq":1,"event":"host.status","status":"degraded"}', "host.status 'status' is not a known host status");
    });

    it("events.overflow requires a non-negative 'dropped'", () => {
        expectEventFail('{"v":1,"seq":1,"event":"events.overflow"}', "events.overflow requires a non-negative 'dropped'");
        expectEventFail('{"v":1,"seq":1,"event":"events.overflow","dropped":-1}', "events.overflow requires a non-negative 'dropped'");
        expectEventFail('{"v":1,"seq":1,"event":"events.overflow","dropped":0.5}', "events.overflow requires a non-negative 'dropped'");
    });
});

describe('classifyFrame', () => {
    it('classifies by discriminating member', () => {
        expect(classifyFrame(encodeRequest(0, { op: 'hello' }))).toBe('request');
        expect(classifyFrame(encodeResponse({ ok: true, id: 0, payload: { kind: 'shutdown-accepted' } }))).toBe('response');
        expect(
            classifyFrame(encodeResponse({ ok: false, id: 0, error: { code: 'internal', message: 'x' } })),
        ).toBe('response');
        expect(classifyFrame('{"v":1,"seq":1,"event":"host.status","status":"running"}')).toBe('event');
    });

    it('returns unknown for junk, non-objects and bare objects', () => {
        expect(classifyFrame('{{{')).toBe('unknown');
        expect(classifyFrame('null')).toBe('unknown');
        expect(classifyFrame('[1,2]')).toBe('unknown');
        expect(classifyFrame('{}')).toBe('unknown');
        expect(classifyFrame('{"v":1,"seq":1}')).toBe('unknown');
    });
});
