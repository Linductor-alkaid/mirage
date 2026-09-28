/// Unit tests for WsBridgeTransport (M1.5-05): a scripted fake BridgeSocket
/// drives the wire with no network and no global WebSocket. Every frame is
/// asserted with the shared framing/codec rules from the same package
/// (makeFrame / tryExtractFrame / decodeRequest / encodeResponse), covering:
/// the DEC-007 single-outstanding discipline, per-request timeouts with late
/// response dropping, id routing, payload-kind mismatches, the event surface,
/// frame boundary reassembly across WebSocket messages, the protocol error
/// matrix, close() idempotence and the onConnectionLost contract.

import { describe, expect, it, vi } from 'vitest';
import type { Mock } from 'vitest';
import { decodeRequest, encodeResponse } from '../src/codec.js';
import type { RequestDecode } from '../src/codec.js';
import {
    MAX_FRAME_BYTES,
    decodeUtf8,
    encodeUtf8,
    makeFrame,
    tryExtractFrame,
} from '../src/framing.js';
import { IpcRequestError, TransportClosedError } from '../src/transport.js';
import { TransportTimeoutError, WsBridgeTransport } from '../src/ws-transport.js';
import type { BridgeSocket } from '../src/ws-transport.js';
import type {
    IpcError,
    InspectTask,
    RequestBody,
    ResponsePayload,
    ServerEvent,
    ServiceIdentity,
} from '../src/types.js';
import { createEventCollector, expectIpcError, expectTransportClosed } from './helpers.js';

// ---- scripted fake socket (no network, no global WebSocket) ----------------

class FakeSocket implements BridgeSocket {
    binaryType: 'blob' | 'arraybuffer' = 'blob';
    onopen: ((event: unknown) => void) | null = null;
    onmessage: ((event: { data: unknown }) => void) | null = null;
    onerror: ((event: unknown) => void) | null = null;
    onclose: ((event: unknown) => void) | null = null;

    /** Frames written by the transport, one entry per send() call. */
    readonly sent: Uint8Array[] = [];
    /** Arguments of every close() call made by the transport. */
    readonly closeCalls: Array<{ code?: number; reason?: string }> = [];

    send(data: Uint8Array): void {
        this.sent.push(data.slice());
    }

    close(code?: number, reason?: string): void {
        this.closeCalls.push({ code, reason });
    }

    // -- test drivers ---------------------------------------------------------

    open(): void {
        this.onopen?.({});
    }

    deliver(data: Uint8Array | string): void {
        this.onmessage?.({ data });
    }

    peerClose(): void {
        this.onclose?.({});
    }
}

interface Harness {
    transport: WsBridgeTransport;
    socket: FakeSocket;
    lost: Mock<() => void>;
}

function makeHarness(options: { requestTimeoutMs?: number } = {}): Harness {
    const socket = new FakeSocket();
    const transport = new WsBridgeTransport('ws://bridge.test/', {
        socketFactory: () => socket,
        ...options,
    });
    const lost = vi.fn();
    transport.onConnectionLost(lost);
    return { transport, socket, lost };
}

// -- wire helpers -------------------------------------------------------------

/** Drains the transport's microtask chains (queue tail, opening promise). */
async function flush(): Promise<void> {
    await new Promise<void>((resolve) => setTimeout(resolve, 0));
}

/** Decodes one sent message as exactly one well-formed request frame. */
function decodeSentRequest(bytes: Uint8Array): RequestDecode & { singleFrame: boolean } {
    const extract = tryExtractFrame(bytes);
    if (extract.status !== 'message') {
        throw new Error(`sent bytes are not one complete frame (status: ${extract.status})`);
    }
    const rest = extract.rest;
    return { ...decodeRequest(decodeUtf8(extract.message)), singleFrame: rest.length === 0 };
}

function sentBody(bytes: Uint8Array): RequestBody {
    const decode = decodeSentRequest(bytes);
    if (!decode.ok) {
        throw new Error(`sent frame failed to decode as a request: ${decode.error}`);
    }
    expect(decode.singleFrame).toBe(true);
    return decode.body;
}

function respond(socket: FakeSocket, id: number, payload: ResponsePayload): void {
    socket.deliver(makeFrame(encodeResponse({ ok: true, id, payload })));
}

function respondError(socket: FakeSocket, id: number, code: IpcError['code'], message: string): void {
    socket.deliver(makeFrame(encodeResponse({ ok: false, id, error: { code, message } })));
}

function eventFrame(event: ServerEvent): Uint8Array {
    return makeFrame(encodeUtf8(JSON.stringify(event)));
}

function hostEvent(seq: number, status: Extract<ServerEvent, { event: 'host.status' }>['status']): ServerEvent {
    return { v: 1, seq, event: 'host.status', status };
}

function identityValue(events?: boolean): ServiceIdentity {
    const value: ServiceIdentity = {
        service: 'mirage-runtime',
        mirage_version: '0.1.0',
        mira_core_version: '0.1.0',
        host_status: 'running',
        protocol: 1,
    };
    if (events !== undefined) {
        value.events = events;
    }
    return value;
}

function inspectValue(taskId: string): InspectTask {
    return {
        id: taskId,
        goal: 'fixture goal',
        progress: 'Active',
        has_success: false,
        steps: [],
    };
}

/** Opens the socket, completes the hello round trip (asserting the hello
 * frame on the wire) and returns the identity. */
async function handshake(harness: Harness, events?: boolean): Promise<ServiceIdentity> {
    const pending = harness.transport.hello();
    harness.socket.open();
    await flush();
    const body = sentBody(harness.socket.sent[0]!);
    if (body.op !== 'hello') {
        throw new Error(`expected the first frame to be hello, got '${body.op}'`);
    }
    respond(harness.socket, 1, { kind: 'identity', value: identityValue(events) });
    return pending;
}

// ---- hello round trip --------------------------------------------------------

describe('hello round trip', () => {
    it('sends a decodable hello frame (id 1) and resolves with the identity payload', async () => {
        const harness = makeHarness();
        const pending = harness.transport.hello();
        harness.socket.open();
        await flush();

        expect(harness.socket.sent).toHaveLength(1);
        expect(decodeSentRequest(harness.socket.sent[0]!)).toEqual({
            ok: true,
            id: 1,
            body: { op: 'hello' },
            singleFrame: true,
        });

        respond(harness.socket, 1, { kind: 'identity', value: identityValue(true) });
        const identity = await pending;
        expect(identity).toEqual(identityValue(true));
        expect(harness.transport.eventsSupported).toBe(true);
        expect(harness.lost).not.toHaveBeenCalled();
    });

    it('exposes eventsSupported=true after hello advertised the events capability', async () => {
        const harness = makeHarness();
        const pending = harness.transport.hello();
        harness.socket.open();
        await flush();
        respond(harness.socket, 1, { kind: 'identity', value: identityValue(true) });
        await pending;
        expect(harness.transport.eventsSupported).toBe(true);
    });

    it('reports eventsSupported=false when the identity has no events member', async () => {
        const harness = makeHarness();
        const pending = harness.transport.hello();
        harness.socket.open();
        await flush();
        // Wire truth: encodeResponse omits the member when undefined.
        const payload = decodeUtf8(
            (() => {
                const frame = makeFrame(encodeResponse({ ok: true, id: 1, payload: { kind: 'identity', value: identityValue() } }));
                return frame.slice(4);
            })(),
        );
        expect(payload.includes('events')).toBe(false);
        respond(harness.socket, 1, { kind: 'identity', value: identityValue() });

        await pending;
        expect(harness.transport.eventsSupported).toBe(false);
    });
});

// ---- DEC-007 single-outstanding discipline ------------------------------------

describe('single-outstanding request discipline (DEC-007)', () => {
    it('holds the second request until the first response settles it', async () => {
        const harness = makeHarness();
        await handshake(harness, true);
        harness.socket.sent.length = 0;

        const first = harness.transport.listTasks();
        const second = harness.transport.inspectTask('task-0001');
        await flush();

        expect(harness.socket.sent).toHaveLength(1);
        expect(sentBody(harness.socket.sent[0]!)).toEqual({ op: 'task.list' });

        respond(harness.socket, 2, { kind: 'list', value: { tasks: [] } });
        await expect(first).resolves.toEqual([]);
        await flush();

        expect(harness.socket.sent).toHaveLength(2);
        expect(sentBody(harness.socket.sent[1]!)).toEqual({ op: 'task.inspect', task_id: 'task-0001' });
        respond(harness.socket, 3, { kind: 'inspect', value: inspectValue('task-0001') });
        await expect(second).resolves.toEqual(inspectValue('task-0001'));
    });

    it('times out a stalled request with TransportTimeoutError, drops the late response and keeps serving', async () => {
        const harness = makeHarness({ requestTimeoutMs: 20 });
        await handshake(harness, true);
        harness.socket.sent.length = 0;

        const stalled = harness.transport.listTasks();
        await flush();
        expect(harness.socket.sent).toHaveLength(1);

        await expect(stalled).rejects.toThrow(TransportTimeoutError);

        // The late response must be dropped as an unknown id: no resolution of
        // any later request, no connection failure.
        respond(harness.socket, 2, {
            kind: 'list',
            value: { tasks: [{ id: 'task-0001', goal: 'late', progress: 'Active' }] },
        });
        await flush();
        expect(harness.lost).not.toHaveBeenCalled();

        const after = harness.transport.listTasks();
        await flush();
        expect(harness.socket.sent).toHaveLength(2);
        expect(sentBody(harness.socket.sent[1]!)).toEqual({ op: 'task.list' });
        respond(harness.socket, 3, { kind: 'list', value: { tasks: [] } });
        await expect(after).resolves.toEqual([]);
    });

    it('surfaces a failed response as IpcRequestError with the stable wire code', async () => {
        const harness = makeHarness();
        await handshake(harness, true);

        const pending = harness.transport.inspectTask('task-404');
        await flush();
        respondError(harness.socket, 2, 'not_found', "task 'task-404' was not found");
        const error = await expectIpcError(
            () => pending,
            'not_found',
            "task 'task-404' was not found",
        );
        expect(error).toBeInstanceOf(IpcRequestError);

        // An error response is a request-level failure; the connection stays usable.
        const after = harness.transport.listTasks();
        await flush();
        respond(harness.socket, 3, { kind: 'list', value: { tasks: [] } });
        await expect(after).resolves.toEqual([]);
    });

    it('fails the connection when the response payload kind does not match the request', async () => {
        const harness = makeHarness();
        const pending = harness.transport.hello();
        harness.socket.open();
        await flush();
        // A submitted-shaped response to hello is a protocol violation.
        respond(harness.socket, 1, { kind: 'submitted', value: { task_id: 'task-0001' } });

        await expect(pending).rejects.toThrow(TransportClosedError);
        await expectTransportClosed(() => harness.transport.listTasks());
        await expectTransportClosed(() => harness.transport.hello());
    });
});

// ---- event surface -------------------------------------------------------------

describe('event surface', () => {
    it('dispatches event frames to the listener only after the subscribe ack', async () => {
        const harness = makeHarness();
        await handshake(harness, true);
        const collector = createEventCollector();

        // Before subscribe there is no listener: the frame must be swallowed
        // without crashing and without failing the connection.
        harness.socket.deliver(eventFrame(hostEvent(1, 'running')));
        await flush();
        expect(collector.events).toHaveLength(0);
        expect(harness.lost).not.toHaveBeenCalled();

        const subscribed = harness.transport.subscribe(collector.listener);
        await flush();
        expect(sentBody(harness.socket.sent.at(-1)!)).toEqual({ op: 'events.subscribe' });
        respond(harness.socket, 2, { kind: 'shutdown-accepted' });
        await subscribed;

        harness.socket.deliver(
            eventFrame({
                v: 1,
                seq: 1,
                event: 'task.updated',
                task_id: 'task-0001',
                goal: 'g',
                progress: 'Active',
                has_success: false,
                success: false,
            }),
        );
        harness.socket.deliver(eventFrame(hostEvent(2, 'stopping')));
        await flush();

        expect(collector.progressUpdates()).toEqual(['Active']);
        expect(collector.hostStatuses()).toEqual(['stopping']);
    });

    it('unsubscribe() before subscribing sends no frame; after subscribing it sends events.unsubscribe', async () => {
        const harness = makeHarness();
        await handshake(harness, true);
        harness.socket.sent.length = 0;

        await harness.transport.unsubscribe();
        expect(harness.socket.sent).toHaveLength(0);

        const collector = createEventCollector();
        const subscribed = harness.transport.subscribe(collector.listener);
        await flush();
        respond(harness.socket, 2, { kind: 'shutdown-accepted' });
        await subscribed;

        const unsubscribed = harness.transport.unsubscribe();
        await flush();
        expect(sentBody(harness.socket.sent.at(-1)!)).toEqual({ op: 'events.unsubscribe' });
        respond(harness.socket, 3, { kind: 'shutdown-accepted' });
        await unsubscribed;

        harness.socket.deliver(eventFrame(hostEvent(3, 'stopped')));
        await flush();
        expect(collector.events).toHaveLength(0);
    });
});

// ---- frame boundaries ------------------------------------------------------------

describe('frame boundaries over the WebSocket mapping', () => {
    it('reassembles a frame delivered across two WebSocket messages', async () => {
        const harness = makeHarness();
        await handshake(harness, true);

        const pending = harness.transport.listTasks();
        await flush();
        const frame = makeFrame(
            encodeResponse({ ok: true, id: 2, payload: { kind: 'list', value: { tasks: [] } } }),
        );
        let settled = false;
        void pending.then(() => {
            settled = true;
        });

        harness.socket.deliver(frame.slice(0, 3));
        await flush();
        expect(settled).toBe(false);

        harness.socket.deliver(frame.slice(3));
        await expect(pending).resolves.toEqual([]);
    });

    it('processes two frames concatenated in one message (identity + event during handshake)', async () => {
        const harness = makeHarness();
        const pending = harness.transport.hello();
        harness.socket.open();
        await flush();

        const identityFrame = makeFrame(
            encodeResponse({ ok: true, id: 1, payload: { kind: 'identity', value: identityValue(true) } }),
        );
        const event = hostEvent(1, 'running');
        const secondFrame = eventFrame(event);
        const merged = new Uint8Array(identityFrame.byteLength + secondFrame.byteLength);
        merged.set(identityFrame, 0);
        merged.set(secondFrame, identityFrame.byteLength);

        harness.socket.deliver(merged);
        const identity = await pending;
        expect(identity).toEqual(identityValue(true));
        expect(harness.transport.eventsSupported).toBe(true);

        // The connection stays healthy after the merged message.
        const after = harness.transport.listTasks();
        await flush();
        respond(harness.socket, 2, { kind: 'list', value: { tasks: [] } });
        await expect(after).resolves.toEqual([]);
    });

    it('dispatches two subscribed event frames carried by one message in order', async () => {
        const harness = makeHarness();
        await handshake(harness, true);
        const collector = createEventCollector();
        const subscribed = harness.transport.subscribe(collector.listener);
        await flush();
        respond(harness.socket, 2, { kind: 'shutdown-accepted' });
        await subscribed;

        const first = eventFrame(hostEvent(1, 'starting'));
        const second = eventFrame(hostEvent(2, 'running'));
        const merged = new Uint8Array(first.byteLength + second.byteLength);
        merged.set(first, 0);
        merged.set(second, first.byteLength);
        harness.socket.deliver(merged);
        await flush();

        expect(collector.hostStatuses()).toEqual(['starting', 'running']);
    });
});

// ---- protocol error matrix ---------------------------------------------------------

describe('protocol error matrix', () => {
    it('a text WebSocket message fails the connection and rejects the pending request', async () => {
        const harness = makeHarness();
        await handshake(harness, true);
        const pending = harness.transport.listTasks();
        await flush();

        harness.socket.deliver('the dev bridge must carry binary frames only');

        await expect(pending).rejects.toThrow(TransportClosedError);
        await expectTransportClosed(() => harness.transport.listTasks());
        expect(harness.lost).toHaveBeenCalledTimes(1);
    });

    it('a declared frame length above the 1 MiB cap fails the connection', async () => {
        const harness = makeHarness();
        await handshake(harness, true);
        const pending = harness.transport.listTasks();
        await flush();

        const header = new Uint8Array(4);
        new DataView(header.buffer).setUint32(0, MAX_FRAME_BYTES + 1, true);
        harness.socket.deliver(header);

        await expect(pending).rejects.toThrow(TransportClosedError);
        await expectTransportClosed(() => harness.transport.listTasks());
        expect(harness.lost).toHaveBeenCalledTimes(1);
    });

    it('a frame whose payload is not valid JSON fails the connection', async () => {
        const harness = makeHarness();
        await handshake(harness, true);
        const pending = harness.transport.listTasks();
        await flush();

        harness.socket.deliver(makeFrame(encodeUtf8('{{{ nope')));

        await expect(pending).rejects.toThrow(TransportClosedError);
        await expectTransportClosed(() => harness.transport.listTasks());
    });

    it('an ok response frame missing the id member fails the connection', async () => {
        const harness = makeHarness();
        await handshake(harness, true);
        const pending = harness.transport.listTasks();
        await flush();

        harness.socket.deliver(makeFrame(encodeUtf8(JSON.stringify({ v: 1, ok: true }))));

        await expect(pending).rejects.toThrow(TransportClosedError);
        await expectTransportClosed(() => harness.transport.listTasks());
    });

    it('a malformed event frame (unknown event name) fails the connection', async () => {
        const harness = makeHarness();
        await handshake(harness, true);
        const collector = createEventCollector();
        const subscribed = harness.transport.subscribe(collector.listener);
        await flush();
        respond(harness.socket, 2, { kind: 'shutdown-accepted' });
        await subscribed;

        harness.socket.deliver(
            eventFrame({ v: 1, seq: 1, event: 'unknown.kind' } as unknown as ServerEvent),
        );
        await flush();
        expect(collector.events).toHaveLength(0);
        await expectTransportClosed(() => harness.transport.listTasks());
        expect(harness.lost).toHaveBeenCalledTimes(1);
    });

    it('reports onConnectionLost exactly once per connection when the socket drops after hello', async () => {
        const harness = makeHarness();
        await handshake(harness, true);

        harness.socket.peerClose();
        harness.socket.peerClose();

        expect(harness.lost).toHaveBeenCalledTimes(1);
        await expectTransportClosed(() => harness.transport.listTasks());
    });

    it('does not report connection loss when the drop happens before hello completes', async () => {
        const harness = makeHarness();
        const pending = harness.transport.hello();
        harness.socket.peerClose();

        await expect(pending).rejects.toThrow(TransportClosedError);
        expect(harness.lost).not.toHaveBeenCalled();
    });

    // 回归锁定（M1.5-05 复验）：mirage-ipc-protocol-v1.md §1 要求检测到协议
    // 违规即「连接必须关闭」——传输层除了拆除自身状态外，还必须对底层
    // socket 发起 close（failConnection 以 1002/'protocol error' 关闭；
    // 主动 close() 走 1000/'client close'，二者互不混淆）。
    it('closes the underlying socket when the client detects a protocol error', async () => {
        const harness = makeHarness();
        await handshake(harness, true);
        harness.socket.deliver('text frame triggers the protocol error path');
        await flush();
        expect(harness.socket.closeCalls).toEqual([{ code: 1002, reason: 'protocol error' }]);
    });
});

// ---- close lifecycle -----------------------------------------------------------------

describe('close lifecycle', () => {
    it('close() is idempotent, rejects pending and later requests, and never fires onConnectionLost', async () => {
        const harness = makeHarness();
        await handshake(harness, true);
        const pending = harness.transport.listTasks();
        await flush();

        await harness.transport.close();
        await expect(pending).rejects.toThrow(TransportClosedError);
        expect(harness.lost).not.toHaveBeenCalled();

        await harness.transport.close();
        await expectTransportClosed(() => harness.transport.hello());
        await expectTransportClosed(() => harness.transport.listTasks());
        expect(harness.socket.closeCalls).toEqual([{ code: 1000, reason: 'client close' }]);
    });

    it('rejects with TransportClosedError when the socket factory throws', async () => {
        const socket = new FakeSocket();
        const transport = new WsBridgeTransport('ws://bridge.test/', {
            socketFactory: () => {
                throw new Error('no bridge process');
            },
        });
        void socket;
        await expect(transport.hello()).rejects.toThrow(TransportClosedError);
        await expectTransportClosed(() => transport.listTasks());
    });
});

// ---- per-op request frames and response routing ------------------------------------------

describe('request frames and response routing per op', () => {
    it('task.submit maps goal, steps and optional step_timeout_ms and routes the submitted payload', async () => {
        const harness = makeHarness();
        await handshake(harness, true);

        const pending = harness.transport.submitTask({
            goal: '整理速查卡',
            steps: [
                { op: 'filesystem.read', arg: 'docs/a.md' },
                { op: 'process.execute', arg: 'make card' },
            ],
            step_timeout_ms: 1500,
        });
        await flush();
        expect(sentBody(harness.socket.sent.at(-1)!)).toEqual({
            op: 'task.submit',
            goal: '整理速查卡',
            steps: [
                { op: 'filesystem.read', arg: 'docs/a.md' },
                { op: 'process.execute', arg: 'make card' },
            ],
            step_timeout_ms: 1500,
        });
        respond(harness.socket, 2, { kind: 'submitted', value: { task_id: 'task-0001' } });
        await expect(pending).resolves.toEqual({ task_id: 'task-0001' });
    });

    it('task.submit omits step_timeout_ms when the caller did not provide one', async () => {
        const harness = makeHarness();
        await handshake(harness, true);

        const pending = harness.transport.submitTask({
            goal: 'g',
            steps: [{ op: 'filesystem.read', arg: 'a' }],
        });
        await flush();
        expect(sentBody(harness.socket.sent.at(-1)!)).toEqual({
            op: 'task.submit',
            goal: 'g',
            steps: [{ op: 'filesystem.read', arg: 'a' }],
        });
        respond(harness.socket, 2, { kind: 'submitted', value: { task_id: 'task-0002' } });
        await expect(pending).resolves.toEqual({ task_id: 'task-0002' });
    });

    it('task.list sends the bare op and routes the list payload', async () => {
        const harness = makeHarness();
        await handshake(harness, true);

        const pending = harness.transport.listTasks();
        await flush();
        expect(sentBody(harness.socket.sent.at(-1)!)).toEqual({ op: 'task.list' });
        respond(harness.socket, 2, {
            kind: 'list',
            value: {
                tasks: [
                    { id: 'task-0001', goal: 'first', progress: 'Active' },
                    { id: 'task-0002', goal: 'second', progress: 'Completed' },
                ],
            },
        });
        await expect(pending).resolves.toEqual([
            { id: 'task-0001', goal: 'first', progress: 'Active' },
            { id: 'task-0002', goal: 'second', progress: 'Completed' },
        ]);
    });

    it('task.inspect carries task_id and routes the inspect payload', async () => {
        const harness = makeHarness();
        await handshake(harness, true);

        const pending = harness.transport.inspectTask('task-0007');
        await flush();
        expect(sentBody(harness.socket.sent.at(-1)!)).toEqual({
            op: 'task.inspect',
            task_id: 'task-0007',
        });
        respond(harness.socket, 2, { kind: 'inspect', value: inspectValue('task-0007') });
        await expect(pending).resolves.toEqual(inspectValue('task-0007'));
    });

    it('task.cancel carries task_id and routes the cancelled payload', async () => {
        const harness = makeHarness();
        await handshake(harness, true);

        const pending = harness.transport.cancelTask('task-0007');
        await flush();
        expect(sentBody(harness.socket.sent.at(-1)!)).toEqual({
            op: 'task.cancel',
            task_id: 'task-0007',
        });
        respond(harness.socket, 2, {
            kind: 'cancelled',
            value: { task_id: 'task-0007', progress: 'Cancelling' },
        });
        await expect(pending).resolves.toEqual({ task_id: 'task-0007', progress: 'Cancelling' });
    });

    it('service.shutdown sends the bare op and resolves on the plain ack', async () => {
        const harness = makeHarness();
        await handshake(harness, true);

        const pending = harness.transport.shutdown();
        await flush();
        expect(sentBody(harness.socket.sent.at(-1)!)).toEqual({ op: 'service.shutdown' });
        respond(harness.socket, 2, { kind: 'shutdown-accepted' });
        await expect(pending).resolves.toBeUndefined();
    });
});

// ---- workflow face (DEC-023) -----------------------------------------------

const WORKFLOW_ID = '0f9e8d7c6b5a4938271605948372615a';
const RUN_ID = '00000001000000010000000000000001';
const DIGEST = 'ab'.repeat(32);

function readDefinition(): Record<string, unknown> {
    return {
        schema_version: { major: 1, minor: 0 },
        workflow_id: WORKFLOW_ID,
        name: '读取文本',
        parameters: [],
        steps: [],
        default_policy: 'strict',
        allowed_policies: ['strict'],
    };
}

describe('workflow face (DEC-023)', () => {
    it('workflow.list sends the bare op and routes the workflow-list payload', async () => {
        const harness = makeHarness();
        await handshake(harness, true);

        const pending = harness.transport.listWorkflows();
        await flush();
        expect(sentBody(harness.socket.sent.at(-1)!)).toEqual({ op: 'workflow.list' });
        respond(harness.socket, 2, {
            kind: 'workflow-list',
            value: {
                workflows: [
                    {
                        workflow_id: WORKFLOW_ID,
                        name: '读取文本',
                        head_digest: DIGEST,
                        validation: 'dry_run_passed',
                        runnable: true,
                        updated_at_ms: 1_000,
                    },
                ],
            },
        });
        await expect(pending).resolves.toEqual([
            {
                workflow_id: WORKFLOW_ID,
                name: '读取文本',
                head_digest: DIGEST,
                validation: 'dry_run_passed',
                runnable: true,
                updated_at_ms: 1_000,
            },
        ]);
    });

    it('workflow.save / workflow.publish carry the definition object and route their payloads', async () => {
        const harness = makeHarness();
        await handshake(harness, true);

        const save = harness.transport.saveWorkflow(readDefinition());
        await flush();
        expect(sentBody(harness.socket.sent.at(-1)!)).toEqual({
            op: 'workflow.save',
            definition: readDefinition(),
        });
        respond(harness.socket, 2, {
            kind: 'workflow-saved',
            value: { workflow_id: WORKFLOW_ID, digest: DIGEST },
        });
        await expect(save).resolves.toEqual({ workflow_id: WORKFLOW_ID, digest: DIGEST });

        const publish = harness.transport.publishWorkflow(readDefinition());
        await flush();
        expect(sentBody(harness.socket.sent.at(-1)!)).toEqual({
            op: 'workflow.publish',
            definition: readDefinition(),
        });
        respond(harness.socket, 3, {
            kind: 'workflow-published',
            value: { workflow_id: WORKFLOW_ID, digest: DIGEST, dry_run_id: 'dry-1', idempotent: false },
        });
        await expect(publish).resolves.toEqual({
            workflow_id: WORKFLOW_ID,
            digest: DIGEST,
            dry_run_id: 'dry-1',
            idempotent: false,
        });
    });

    it('workflow.delete carries workflow_id and routes the workflow-deleted payload', async () => {
        const harness = makeHarness();
        await handshake(harness, true);

        const pending = harness.transport.deleteWorkflow(WORKFLOW_ID);
        await flush();
        expect(sentBody(harness.socket.sent.at(-1)!)).toEqual({
            op: 'workflow.delete',
            workflow_id: WORKFLOW_ID,
        });
        respond(harness.socket, 2, { kind: 'workflow-deleted', value: { workflow_id: WORKFLOW_ID } });
        await expect(pending).resolves.toEqual({ workflow_id: WORKFLOW_ID });
    });

    it('workflow.atom.catalog routes the exposed tools payload', async () => {
        const harness = makeHarness();
        await handshake(harness, true);

        const pending = harness.transport.workflowAtomCatalog();
        await flush();
        expect(sentBody(harness.socket.sent.at(-1)!)).toEqual({ op: 'workflow.atom.catalog' });
        respond(harness.socket, 2, {
            kind: 'workflow-atom-catalog',
            value: {
                tools: [
                    {
                        wire_name: 'desktop.filesystem.read_text',
                        version: '1.0.0',
                        description: 'Reads a UTF-8 text file.',
                        has_side_effects: false,
                        parameters_schema: {
                            type: 'object',
                            properties: { path: { type: 'string' } },
                            required: ['path'],
                        },
                    },
                ],
            },
        });
        const tools = await pending;
        expect(tools).toHaveLength(1);
        expect(tools[0]).toMatchObject({ wire_name: 'desktop.filesystem.read_text', has_side_effects: false });
    });

    it('workflow.run maps optional digest/parameters/policy members and routes the started payload', async () => {
        const harness = makeHarness();
        await handshake(harness, true);

        const bare = harness.transport.startWorkflowRun({ workflow_id: WORKFLOW_ID });
        await flush();
        expect(sentBody(harness.socket.sent.at(-1)!)).toEqual({ op: 'workflow.run', workflow_id: WORKFLOW_ID });
        respond(harness.socket, 2, { kind: 'workflow-run-started', value: { run_id: RUN_ID } });
        await expect(bare).resolves.toEqual({ run_id: RUN_ID });

        const full = harness.transport.startWorkflowRun({
            workflow_id: WORKFLOW_ID,
            digest: DIGEST,
            parameters: { confirmed: true },
            policy: 'strict',
        });
        await flush();
        expect(sentBody(harness.socket.sent.at(-1)!)).toEqual({
            op: 'workflow.run',
            workflow_id: WORKFLOW_ID,
            digest: DIGEST,
            parameters: { confirmed: true },
            policy: 'strict',
        });
        respond(harness.socket, 3, { kind: 'workflow-run-started', value: { run_id: RUN_ID } });
        await expect(full).resolves.toEqual({ run_id: RUN_ID });
    });

    it('workflow.cancel carries run_id and surfaces the stable pinned error on rejection', async () => {
        const harness = makeHarness();
        await handshake(harness, true);

        const pending = harness.transport.cancelWorkflowRun(RUN_ID);
        await flush();
        expect(sentBody(harness.socket.sent.at(-1)!)).toEqual({ op: 'workflow.cancel', run_id: RUN_ID });
        respond(harness.socket, 2, {
            kind: 'workflow-run-cancelled',
            value: { run_id: RUN_ID, state: 'cancelled' },
        });
        await expect(pending).resolves.toEqual({ run_id: RUN_ID, state: 'cancelled' });

        const failing = harness.transport.listWorkflows();
        await flush();
        respondError(harness.socket, 3, 'not_found', 'unknown workflow id');
        await expectIpcError(() => failing, 'not_found');
    });
});

// ---- session face (DEC-021, consumed since DEC-025/M5-06) -------------------

const SESSION_ID = '0f9e8d7c6b5a4938271605948372615b';

describe('session face (DEC-021)', () => {
    it('session.list sends the bare op and routes the session-list payload', async () => {
        const harness = makeHarness();
        await handshake(harness, true);

        const pending = harness.transport.listSessions();
        await flush();
        expect(sentBody(harness.socket.sent.at(-1)!)).toEqual({ op: 'session.list' });
        respond(harness.socket, 2, {
            kind: 'session-list',
            value: { sessions: [{ id: SESSION_ID, state: 'autonomous', created_at_ms: 1_000 }] },
        });
        await expect(pending).resolves.toEqual([
            { id: SESSION_ID, state: 'autonomous', created_at_ms: 1_000 },
        ]);
    });

    it('session.open sends the bare op and routes the session-opened payload', async () => {
        const harness = makeHarness();
        await handshake(harness, true);

        const pending = harness.transport.openSession();
        await flush();
        expect(sentBody(harness.socket.sent.at(-1)!)).toEqual({ op: 'session.open' });
        respond(harness.socket, 2, { kind: 'session-opened', value: { session_id: SESSION_ID } });
        await expect(pending).resolves.toEqual({ session_id: SESSION_ID });

        const failing = harness.transport.openSession();
        await flush();
        respondError(harness.socket, 3, 'unavailable', 'session capacity exhausted (16)');
        await expectIpcError(() => failing, 'unavailable');
    });

    it('session.history carries session_id with optional limit and routes the snapshot', async () => {
        const harness = makeHarness();
        await handshake(harness, true);

        const bare = harness.transport.sessionHistory({ session_id: SESSION_ID });
        await flush();
        expect(sentBody(harness.socket.sent.at(-1)!)).toEqual({ op: 'session.history', session_id: SESSION_ID });
        respond(harness.socket, 2, {
            kind: 'session-history',
            value: {
                session_id: SESSION_ID,
                entries: [{ kind: 'user', text: 'goal', sequence: 1, recorded_at_ms: 5 }],
                truncated: false,
            },
        });
        await expect(bare).resolves.toEqual({
            session_id: SESSION_ID,
            entries: [{ kind: 'user', text: 'goal', sequence: 1, recorded_at_ms: 5 }],
            truncated: false,
        });

        const windowed = harness.transport.sessionHistory({ session_id: SESSION_ID, limit: 10 });
        await flush();
        expect(sentBody(harness.socket.sent.at(-1)!)).toEqual({
            op: 'session.history',
            session_id: SESSION_ID,
            limit: 10,
        });
        respond(harness.socket, 3, {
            kind: 'session-history',
            value: { session_id: SESSION_ID, entries: [], truncated: true },
        });
        await expect(windowed).resolves.toEqual({ session_id: SESSION_ID, entries: [], truncated: true });
    });

    it('task.submit maps the optional session_id member', async () => {
        const harness = makeHarness();
        await handshake(harness, true);

        const pending = harness.transport.submitTask({
            goal: 'g',
            steps: [{ op: 'filesystem.read', arg: 'a' }],
            session_id: SESSION_ID,
        });
        await flush();
        expect(sentBody(harness.socket.sent.at(-1)!)).toEqual({
            op: 'task.submit',
            goal: 'g',
            steps: [{ op: 'filesystem.read', arg: 'a' }],
            session_id: SESSION_ID,
        });
        respond(harness.socket, 2, {
            kind: 'submitted',
            value: { task_id: 'task-0001', session_id: SESSION_ID },
        });
        await expect(pending).resolves.toEqual({ task_id: 'task-0001', session_id: SESSION_ID });
    });
});

// ---- DEC-026 faces: workflow.get + desktop.observe ----------------------------

describe('DEC-026 faces over the WebSocket mapping', () => {
    it('getWorkflow sends workflow.get and resolves with the definition view', async () => {
        const harness = makeHarness();
        await handshake(harness, true);
        harness.socket.sent.length = 0;

        const pending = harness.transport.getWorkflow('5a4b3c2d1e0f4938576a5b4c3d2e1f0a');
        await flush();
        expect(sentBody(harness.socket.sent[0]!)).toEqual({
            op: 'workflow.get',
            workflow_id: '5a4b3c2d1e0f4938576a5b4c3d2e1f0a',
        });

        const definition = { schema_version: { major: 1, minor: 0 }, workflow_id: '5a4b3c2d1e0f4938576a5b4c3d2e1f0a', name: 'x', steps: [] };
        respond(harness.socket, 2, {
            kind: 'workflow-get',
            value: { workflow_id: '5a4b3c2d1e0f4938576a5b4c3d2e1f0a', digest: 'ab'.repeat(32), definition },
        });
        const view = await pending;
        expect(view.digest).toBe('ab'.repeat(32));
        expect(view.definition).toEqual(definition);
    });

    it('getWorkflow surfaces the stable not_found for unknown ids', async () => {
        const harness = makeHarness();
        await handshake(harness, true);
        harness.socket.sent.length = 0;

        const pending = harness.transport.getWorkflow('ff');
        await flush();
        respondError(harness.socket, 2, 'not_found', 'unknown workflow id');
        await expect(pending).rejects.toMatchObject({ code: 'not_found', message: 'unknown workflow id' });
    });

    it('desktopObserve sends the explicit semantic/visual flags and resolves with the observation view', async () => {
        const harness = makeHarness();
        await handshake(harness, true);
        harness.socket.sent.length = 0;

        const pending = harness.transport.desktopObserve({ visual: true });
        await flush();
        expect(sentBody(harness.socket.sent[0]!)).toEqual({ op: 'desktop.observe', semantic: true, visual: true });

        respond(harness.socket, 2, {
            kind: 'observation-view',
            value: {
                active_application: 'Code',
                active_window: 'main.rs',
                window_geometry: { x: 0, y: 0, width: 1920, height: 1080 },
                window_focused: true,
                focused_element: '@e3',
                pointer_x: 10,
                pointer_y: 20,
                environment_state: 'x11',
                visual_snapshot_ref: '@vs7',
                visual_regions: [
                    { ref: '@v1', source: 'ocr', geometry: { x: 1, y: 2, width: 3, height: 4 }, text: 'Run', template_id: '' },
                ],
            },
        });
        const view = await pending;
        expect(view.visual_snapshot_ref).toBe('@vs7');
        expect(view.visual_regions).toHaveLength(1);
        expect(view.semantic).toBeUndefined();
    });

    it('desktopObserve without input writes the pinned defaults (semantic on, visual off)', async () => {
        const harness = makeHarness();
        await handshake(harness, true);
        harness.socket.sent.length = 0;

        const pending = harness.transport.desktopObserve();
        await flush();
        // The canonical form always carries both flags: absent input falls
        // back to the request defaults, never to an abbreviated body.
        expect(sentBody(harness.socket.sent[0]!)).toEqual({ op: 'desktop.observe', semantic: true, visual: false });

        respond(harness.socket, 2, {
            kind: 'observation-view',
            value: {
                active_application: 'Code',
                active_window: 'main.rs',
                window_geometry: { x: 0, y: 0, width: 1920, height: 1080 },
                window_focused: true,
                focused_element: '@e3',
                pointer_x: 10,
                pointer_y: 20,
                environment_state: 'x11',
            },
        });
        const view = await pending;
        expect(view.semantic).toBeUndefined();
        expect(view.visual_snapshot_ref).toBeUndefined();
        expect(view.visual_regions).toBeUndefined();
    });
});

// ---- session.close management face (DEC-026 backlog item 2) -------------------

describe('session.close over the WebSocket mapping', () => {
    it('closeSession sends session.close and routes the closed reply with state', async () => {
        const harness = makeHarness();
        await handshake(harness, true);
        harness.socket.sent.length = 0;

        const pending = harness.transport.closeSession('5a4b3c2d1e0f4938576a5b4c3d2e1f0a');
        await flush();
        expect(sentBody(harness.socket.sent[0]!)).toEqual({
            op: 'session.close',
            session_id: '5a4b3c2d1e0f4938576a5b4c3d2e1f0a',
        });

        respond(harness.socket, 2, {
            kind: 'session-closed',
            value: { session_id: '5a4b3c2d1e0f4938576a5b4c3d2e1f0a', state: 'closed' },
        });
        const closed = await pending;
        expect(closed).toEqual({ session_id: '5a4b3c2d1e0f4938576a5b4c3d2e1f0a', state: 'closed' });
    });

    it('surfaces the stable invalid_state for the primary session guard', async () => {
        const harness = makeHarness();
        await handshake(harness, true);
        harness.socket.sent.length = 0;

        const pending = harness.transport.closeSession('s-primary');
        await flush();
        respondError(harness.socket, 2, 'invalid_state', 'the primary session cannot be closed');
        await expect(pending).rejects.toMatchObject({
            code: 'invalid_state',
            message: 'the primary session cannot be closed',
        });
    });
});

// ---- dialog face (DEC-027): session.chat / session.chat.history ---------------

describe('session.chat over the WebSocket mapping', () => {
    it('sessionChat sends the chat request and routes the accepted turn id', async () => {
        const harness = makeHarness();
        await handshake(harness, true);
        harness.socket.sent.length = 0;

        const pending = harness.transport.sessionChat('5a4b3c2d1e0f4938576a5b4c3d2e1f0a', '列出当前应用');
        await flush();
        expect(sentBody(harness.socket.sent[0]!)).toEqual({
            op: 'session.chat',
            session_id: '5a4b3c2d1e0f4938576a5b4c3d2e1f0a',
            text: '列出当前应用',
        });

        respond(harness.socket, 2, { kind: 'session-chat-accepted', value: { turn_id: 'a'.repeat(32) } });
        const accepted = await pending;
        expect(accepted).toEqual({ turn_id: 'a'.repeat(32) });
    });

    it('sessionChat surfaces the in-flight invalid_state latch', async () => {
        const harness = makeHarness();
        await handshake(harness, true);
        harness.socket.sent.length = 0;

        const pending = harness.transport.sessionChat('s', 'hi');
        await flush();
        respondError(harness.socket, 2, 'invalid_state', 'a dialog turn is already in flight for this session');
        await expect(pending).rejects.toMatchObject({
            code: 'invalid_state',
            message: 'a dialog turn is already in flight for this session',
        });
    });

    it('sessionChatHistory sends the snapshot request and routes the dialog thread', async () => {
        const harness = makeHarness();
        await handshake(harness, true);
        harness.socket.sent.length = 0;

        const pending = harness.transport.sessionChatHistory('5a4b3c2d1e0f4938576a5b4c3d2e1f0a', 5);
        await flush();
        expect(sentBody(harness.socket.sent[0]!)).toEqual({
            op: 'session.chat.history',
            session_id: '5a4b3c2d1e0f4938576a5b4c3d2e1f0a',
            limit: 5,
        });

        respond(harness.socket, 2, {
            kind: 'session-chat-history',
            value: {
                session_id: '5a4b3c2d1e0f4938576a5b4c3d2e1f0a',
                turns: [
                    {
                        turn_id: 'a'.repeat(32),
                        status: 'ok',
                        user_text: 'q',
                        reply_text: '当前有两个应用窗口：编辑器与终端。',
                        sequence: 1,
                        recorded_at_ms: 1700000000000,
                    },
                ],
                truncated: false,
            },
        });
        const history = await pending;
        expect(history.session_id).toBe('5a4b3c2d1e0f4938576a5b4c3d2e1f0a');
        expect(history.truncated).toBe(false);
        expect(history.turns[0]!.status).toBe('ok');
        expect(history.turns[0]!.reply_text).toBe('当前有两个应用窗口：编辑器与终端。');
    });
});
