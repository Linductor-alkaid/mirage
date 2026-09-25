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
import type { ResponsePayload, ServerEvent, ServiceIdentity } from '../src/types.js';

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

    lastRequest(): ReturnType<typeof decodeRequest> {
        const last = this.queries[this.queries.length - 1];
        const decoded = decodeRequest(last.envelope);
        if (!decoded.ok) {
            throw new Error(`transport sent an undecodable request: ${decoded.error}`);
        }
        return decoded;
    }

    respondOk(id: number, payload: ResponsePayload): void {
        const last = this.queries[this.queries.length - 1];
        expect(last).toBeDefined();
        last.resolve(encodeResponse({ ok: true, id, payload }));
    }

    respondError(id: number, code: string, message: string): void {
        const last = this.queries[this.queries.length - 1];
        expect(last).toBeDefined();
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
        bridge.queries[0].fail(-2, 'renderer is gone');
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
});
