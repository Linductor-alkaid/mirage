// @vitest-environment jsdom
/// Unit tests for DesktopBridgeTransport (M5-02): a scripted fake bridge
/// drives the controlled CEF query surface with no browser and no globals.
/// Coverage: the hello/identity round trip, correlation-id echo checking,
/// wire-shaped error surfacing (IpcRequestError), bridge failure and
/// malformed response handling, per-request timeouts with late answer
/// dropping, the event hook surface (subscribe/unsubscribe), the
/// connection-lost hook lifecycle (install once, removed on close) and
/// close() idempotence — all against the shared codec rules.

import { describe, expect, it, vi } from 'vitest';
import type { Mock } from 'vitest';
import { decodeRequest, encodeResponse } from '../src/codec.js';
import { TransportClosedError } from '../src/transport.js';
import {
    DesktopBridgeTransport,
    DesktopBridgeTimeoutError,
} from '../src/desktop-transport.js';
import type { DesktopBridgeQuery } from '../src/desktop-transport.js';
import type { RequestBody, ResponsePayload, ServerEvent, ServiceIdentity } from '../src/types.js';

const IDENTITY: ServiceIdentity = {
    service: 'mirage-runtime',
    mirage_version: '0.4.0-test',
    mira_core_version: '1.0.0',
    host_status: 'running',
    protocol: 1,
    events: true,
};

interface Query {
    envelope: string;
    resolve: (response: string) => void;
    fail: (code: number, message: string) => void;
}

/// Scripted fake bridge: records every query, hands the test the answerers.
class FakeBridge {
    readonly queries: Query[] = [];
    private nextQueryId = 1;

    /** The object handed to the transport as its bridge function (the CEF
     * query function takes one object argument). */
    readonly query: DesktopBridgeQuery = (query: {
        request: string;
        onSuccess: (response: string) => void;
        onFailure: (errorCode: number, errorMessage: string) => void;
    }): number => {
        const queryId = this.nextQueryId++;
        this.queries.push({
            envelope: query.request,
            resolve: (response) => query.onSuccess(response),
            fail: (code, message) => query.onFailure(code, message),
        });
        return queryId;
    };

    /** Decodes the last sent envelope as a request or throws (the transport
     * must only ever send well-formed requests). */
    lastRequest(): { id: number; body: RequestBody } {
        const last = this.queries[this.queries.length - 1];
        if (last === undefined) {
            throw new Error('no query was sent');
        }
        const decoded = decodeRequest(last.envelope);
        if (!decoded.ok) {
            throw new Error(`transport sent an undecodable request: ${decoded.error}`);
        }
        return { id: decoded.id, body: decoded.body };
    }

    respondOk(id: number, payload: ResponsePayload): void {
        const last = this.queries[this.queries.length - 1];
        if (last === undefined) {
            throw new Error('no query was sent');
        }
        last.resolve(encodeResponse({ ok: true, id, payload }));
    }

    respondError(id: number, code: string, message: string): void {
        const last = this.queries[this.queries.length - 1];
        if (last === undefined) {
            throw new Error('no query was sent');
        }
        last.resolve(
            encodeResponse({ ok: false, id, error: { code: code as never, message } }),
        );
    }
}

interface Harness {
    transport: DesktopBridgeTransport;
    bridge: FakeBridge;
    lost: Mock<() => void>;
}

function makeHarness(requestTimeoutMs = 200): Harness {
    const bridge = new FakeBridge();
    const transport = new DesktopBridgeTransport(bridge.query, requestTimeoutMs);
    const lost = vi.fn();
    transport.onConnectionLost(lost);
    return { transport, bridge, lost };
}

async function flush(): Promise<void> {
    await new Promise<void>((resolve) => setTimeout(resolve, 0));
}

describe('DesktopBridgeTransport', () => {
    it('performs the hello exchange and reads the identity', async () => {
        const { transport, bridge } = makeHarness();
        const pending = transport.hello();
        expect(bridge.lastRequest().body.op).toBe('hello');
        bridge.respondOk(1, { kind: 'identity', value: IDENTITY });
        const identity = await pending;
        expect(identity.mirage_version).toBe('0.4.0-test');
        expect(transport.eventsSupported).toBe(true);
        await transport.close();
    });

    it('rejects a response that does not echo the correlation id', async () => {
        const { transport, bridge } = makeHarness();
        const pending = transport.hello();
        bridge.respondOk(99, { kind: 'identity', value: IDENTITY });
        await expect(pending).rejects.toBeInstanceOf(TransportClosedError);
        await transport.close();
    });

    it('surfaces wire errors as IpcRequestError with the stable code', async () => {
        const { transport, bridge } = makeHarness();
        const pending = transport.listTasks();
        bridge.respondError(1, 'unavailable', 'no live session at svc');
        await expect(pending).rejects.toMatchObject({
            name: 'IpcRequestError',
            code: 'unavailable',
        });
        await transport.close();
    });

    it('answers bridge-side query failures as a closed transport', async () => {
        const { transport, bridge } = makeHarness();
        const pending = transport.listTasks();
        const first = bridge.queries[0];
        if (first === undefined) {
            throw new Error('no query was sent');
        }
        first.fail(-2, 'renderer is gone');
        await expect(pending).rejects.toBeInstanceOf(TransportClosedError);
        await transport.close();
    });

    it('drops a late answer after the request timeout', async () => {
        vi.useFakeTimers();
        try {
            const { transport, bridge } = makeHarness(50);
            const pending = transport.listTasks();
            const expectation = expect(pending).rejects.toBeInstanceOf(DesktopBridgeTimeoutError);
            vi.advanceTimersByTime(60);
            // The late answer must not resolve the rejected query.
            bridge.respondOk(1, { kind: 'list', value: { tasks: [] } });
            await expectation;
            await transport.close();
        } finally {
            vi.useRealTimers();
        }
    });

    it('delivers events through the hook after subscribe and drops the hook on unsubscribe', async () => {
        const { transport, bridge } = makeHarness();
        const received: ServerEvent[] = [];
        const pendingSubscribe = transport.subscribe((event) => received.push(event));
        expect(bridge.lastRequest().body.op).toBe('events.subscribe');
        bridge.respondOk(1, { kind: 'shutdown-accepted' });
        await pendingSubscribe;

        const eventJson = JSON.stringify({
            v: 1,
            seq: 4,
            event: 'task.updated',
            task_id: 'task-1',
            goal: 'g',
            progress: 'Completed',
            has_success: true,
            success: true,
        });
        window.__mirageOnEvent?.(eventJson);
        expect(received).toHaveLength(1);
        expect(received[0]).toMatchObject({ event: 'task.updated', seq: 4 });

        // A malformed envelope is dropped, not thrown, into the listener.
        window.__mirageOnEvent?.('{not json');

        const pendingUnsubscribe = transport.unsubscribe();
        expect(bridge.lastRequest().body.op).toBe('events.unsubscribe');
        bridge.respondOk(2, { kind: 'shutdown-accepted' });
        await pendingUnsubscribe;
        expect(window.__mirageOnEvent).toBeUndefined();
        await transport.close();
    });

    it('reports connection loss through the process hook and removes it on close', async () => {
        const { transport, lost } = makeHarness();
        window.__mirageConnectionLost?.();
        expect(lost).toHaveBeenCalledTimes(1);

        await transport.close();
        expect(window.__mirageConnectionLost).toBeUndefined();
        window.__mirageConnectionLost?.();
        expect(lost).toHaveBeenCalledTimes(1); // no hook, no second report
    });

    it('closes idempotently and rejects further requests', async () => {
        const { transport } = makeHarness();
        await transport.close();
        await expect(transport.close()).resolves.toBeUndefined();
        await expect(transport.hello()).rejects.toBeInstanceOf(TransportClosedError);
    });

    it('fails pending requests when the transport closes mid-flight', async () => {
        const { transport } = makeHarness();
        const pending = transport.listTasks();
        await transport.close();
        await expect(pending).rejects.toBeInstanceOf(TransportClosedError);
        await flush();
    });

    it('sends the session face requests and routes their payloads (DEC-021)', async () => {
        const SESSION_ID = '0f9e8d7c6b5a4938271605948372615b';
        const { transport, bridge } = makeHarness();

        const list = transport.listSessions();
        expect(bridge.lastRequest().body.op).toBe('session.list');
        bridge.respondOk(1, {
            kind: 'session-list',
            value: { sessions: [{ id: SESSION_ID, state: 'autonomous', created_at_ms: 7 }] },
        });
        await expect(list).resolves.toEqual([{ id: SESSION_ID, state: 'autonomous', created_at_ms: 7 }]);

        const open = transport.openSession();
        expect(bridge.lastRequest().body.op).toBe('session.open');
        bridge.respondOk(2, { kind: 'session-opened', value: { session_id: SESSION_ID } });
        await expect(open).resolves.toEqual({ session_id: SESSION_ID });

        const history = transport.sessionHistory({ session_id: SESSION_ID, limit: 10 });
        const historyRequest = bridge.lastRequest().body;
        expect(historyRequest).toEqual({
            op: 'session.history',
            session_id: SESSION_ID,
            limit: 10,
        });
        bridge.respondOk(3, {
            kind: 'session-history',
            value: {
                session_id: SESSION_ID,
                entries: [{ kind: 'user', text: 'goal', sequence: 1, recorded_at_ms: 9 }],
                truncated: false,
            },
        });
        await expect(history).resolves.toEqual({
            session_id: SESSION_ID,
            entries: [{ kind: 'user', text: 'goal', sequence: 1, recorded_at_ms: 9 }],
            truncated: false,
        });

        const failing = transport.sessionHistory({ session_id: SESSION_ID });
        expect(bridge.lastRequest().body).toEqual({ op: 'session.history', session_id: SESSION_ID });
        bridge.respondError(4, 'not_found', 'unknown session id');
        await expect(failing).rejects.toMatchObject({ name: 'IpcRequestError', code: 'not_found' });

        await transport.close();
    });

    it('carries the optional session_id member on task.submit', async () => {
        const SESSION_ID = '0f9e8d7c6b5a4938271605948372615b';
        const { transport, bridge } = makeHarness();
        const pending = transport.submitTask({
            goal: 'g',
            steps: [{ op: 'filesystem.read', arg: 'a' }],
            session_id: SESSION_ID,
        });
        expect(bridge.lastRequest().body).toEqual({
            op: 'task.submit',
            goal: 'g',
            steps: [{ op: 'filesystem.read', arg: 'a' }],
            session_id: SESSION_ID,
        });
        bridge.respondOk(1, { kind: 'submitted', value: { task_id: 'task-0001', session_id: SESSION_ID } });
        await expect(pending).resolves.toEqual({ task_id: 'task-0001', session_id: SESSION_ID });
        await transport.close();
    });
});

// -- DEC-026 faces: workflow.get + desktop.observe -----------------------------

describe('DesktopBridgeTransport DEC-026 faces', () => {
    it('sends workflow.get and routes the definition view', async () => {
        const { transport, bridge } = makeHarness();
        const pending = transport.getWorkflow('5a4b3c2d1e0f4938576a5b4c3d2e1f0a');
        expect(bridge.lastRequest().body).toEqual({
            op: 'workflow.get',
            workflow_id: '5a4b3c2d1e0f4938576a5b4c3d2e1f0a',
        });
        bridge.respondOk(1, {
            kind: 'workflow-get',
            value: {
                workflow_id: '5a4b3c2d1e0f4938576a5b4c3d2e1f0a',
                digest: 'cd'.repeat(32),
                definition: { workflow_id: '5a4b3c2d1e0f4938576a5b4c3d2e1f0a', name: 'x', steps: [] },
            },
        });
        const view = await pending;
        expect(view.digest).toBe('cd'.repeat(32));
        expect(view.definition.name).toBe('x');
        await transport.close();
    });

    it('sends desktop.observe with explicit flags and routes the observation view', async () => {
        const { transport, bridge } = makeHarness();
        const pending = transport.desktopObserve({ semantic: false, visual: true });
        expect(bridge.lastRequest().body).toEqual({ op: 'desktop.observe', semantic: false, visual: true });
        bridge.respondOk(1, {
            kind: 'observation-view',
            value: {
                active_application: 'Code',
                active_window: 'main.rs',
                window_geometry: { x: 0, y: 0, width: 1920, height: 1080 },
                window_focused: false,
                focused_element: '',
                pointer_x: 0,
                pointer_y: 0,
                environment_state: 'x11',
            },
        });
        const view = await pending;
        expect(view.active_window).toBe('main.rs');
        expect(view.visual_snapshot_ref).toBeUndefined();
        await transport.close();
    });
});
