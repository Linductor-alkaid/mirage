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
