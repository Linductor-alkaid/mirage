/// Shared helpers for the @mirage/contracts test suite. Not a test file
/// (vitest only picks up *.test.ts); strict-checked together with src.

import { expect } from 'vitest';
import type { ServerEvent, TaskProgress, HostStatus } from '../src/types.js';
import { IpcRequestError, TransportClosedError } from '../src/transport.js';
import type { EventListener } from '../src/transport.js';

/** Real-timer sleep; mock tests use small durations instead of fake timers
 * so queueMicrotask-based auto flush keeps working untouched. */
export function delay(ms: number): Promise<void> {
    return new Promise((resolve) => setTimeout(resolve, ms));
}

/** Asserts the call throws (synchronously or via rejection) an IpcRequestError
 * with the expected code, and optionally the exact stable message. */
export async function expectIpcError(
    fn: () => unknown,
    code: string,
    message?: string,
): Promise<IpcRequestError> {
    let caught: unknown;
    let threw = false;
    try {
        await fn();
    } catch (error) {
        threw = true;
        caught = error;
    }
    if (!threw) {
        throw new Error(`expected IpcRequestError('${code}') but the call succeeded`);
    }
    expect(caught).toBeInstanceOf(IpcRequestError);
    const error = caught as IpcRequestError;
    expect(error.code).toBe(code);
    if (message !== undefined) {
        expect(error.message).toBe(message);
    }
    return error;
}

/** Asserts the call throws (synchronously or via rejection) after close().
 * The transport interface documents "reject"; the mock throws synchronously,
 * and both are observable as an exception at the await point. */
export async function expectTransportClosed(fn: () => unknown): Promise<void> {
    let caught: unknown;
    let threw = false;
    try {
        await fn();
    } catch (error) {
        threw = true;
        caught = error;
    }
    if (!threw) {
        throw new Error('expected TransportClosedError but the call succeeded');
    }
    expect(caught).toBeInstanceOf(TransportClosedError);
}

export interface EventCollector {
    events: ServerEvent[];
    listener: EventListener;
    seqs(): number[];
    progressUpdates(): TaskProgress[];
    hostStatuses(): HostStatus[];
    overflowDrops(): number[];
}

export function createEventCollector(): EventCollector {
    const events: ServerEvent[] = [];
    return {
        events,
        listener: (event) => {
            events.push(event);
        },
        seqs: () => events.map((event) => event.seq),
        progressUpdates: () =>
            events
                .filter((event): event is Extract<ServerEvent, { event: 'task.updated' }> =>
                    event.event === 'task.updated')
                .map((event) => event.progress),
        hostStatuses: () =>
            events
                .filter((event): event is Extract<ServerEvent, { event: 'host.status' }> =>
                    event.event === 'host.status')
                .map((event) => event.status),
        overflowDrops: () =>
            events
                .filter((event): event is Extract<ServerEvent, { event: 'events.overflow' }> =>
                    event.event === 'events.overflow')
                .map((event) => event.dropped),
    };
}

/** Event builders for EventSequencer unit tests. */
export function hostEvent(seq: number, status: HostStatus): ServerEvent {
    return { v: 1, seq, event: 'host.status', status };
}

export function taskEvent(seq: number, progress: TaskProgress): ServerEvent {
    return {
        v: 1,
        seq,
        event: 'task.updated',
        task_id: 'task-0001',
        goal: 'g',
        progress,
        has_success: false,
        success: false,
    };
}

export function overflowEvent(seq: number, dropped: number): ServerEvent {
    return { v: 1, seq, event: 'events.overflow', dropped };
}
