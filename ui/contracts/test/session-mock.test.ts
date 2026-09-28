/// Session-face tests for the mock service (DEC-021 wire behaviour, consumed
/// since DEC-025/M5-06): registry with primary session, capacity-bounded
/// session.open, the conversation journal (user/outcome with monotonic
/// sequence), the session.* event set at the same points the real service
/// publishes them, and the capability-off degradation.

import { afterEach, describe, expect, it } from 'vitest';
import { createMockTransport } from '../src/mock/mock-service.js';
import type { MockMirageService, MockServiceOptions } from '../src/mock/mock-service.js';
import { createEventCollector, delay, expectIpcError } from './helpers.js';

const openServices: MockMirageService[] = [];

function makeService(options: MockServiceOptions = {}): ReturnType<typeof createMockTransport> {
    const created = createMockTransport(options);
    openServices.push(created.service);
    return created;
}

afterEach(() => {
    for (const service of openServices.splice(0)) {
        service.close();
    }
});

describe('session registry', () => {
    it('registers the primary session up front and lists it as a snapshot', async () => {
        const { transport } = makeService({ hostStartDelayMs: 0 });
        const sessions = await transport.listSessions();
        expect(sessions).toHaveLength(1);
        expect(sessions[0]).toMatchObject({ state: 'autonomous' });
        expect(sessions[0]?.id).toMatch(/^[0-9a-f]{32}$/);
        expect(sessions[0]?.created_at_ms).toBeGreaterThan(0);
        expect(await transport.openSession()).toEqual({ session_id: expect.stringMatching(/^[0-9a-f]{32}$/) });
        expect((await transport.listSessions()).length).toBe(2);
    });

    it('refuses session.open at registry capacity with unavailable (DEC-021)', async () => {
        const { transport } = makeService({ hostStartDelayMs: 0 });
        // The primary session already occupies one slot.
        for (let i = 1; i < 16; i += 1) {
            await transport.openSession();
        }
        expect((await transport.listSessions()).length).toBe(16);
        await expectIpcError(() => transport.openSession(), 'unavailable', 'session capacity exhausted (16)');
    });

    it('publishes session.updated when a session is opened, never for the primary', async () => {
        const { transport } = makeService({ hostStartDelayMs: 0 });
        const collector = createEventCollector();
        await transport.subscribe(collector.listener);
        const opened = await transport.openSession();
        await delay(5);
        const updated = collector.events.filter((event) => event.event === 'session.updated');
        expect(updated).toHaveLength(1);
        expect(updated[0]).toMatchObject({ event: 'session.updated', session_id: opened.session_id, state: 'autonomous' });
    });
});

describe('conversation journal', () => {
    it('binds submit to the primary session by default and records the user goal', async () => {
        const { transport } = makeService({ hostStartDelayMs: 0, stepDurationMs: 5 });
        const primary = (await transport.listSessions())[0];
        const collector = createEventCollector();
        await transport.subscribe(collector.listener);
        await transport.submitTask({ goal: '读一下日志', steps: [] });
        await delay(30);

        const messages = collector.events.filter((event) => event.event === 'session.message');
        expect(messages).toHaveLength(2);
        expect(messages[0]).toMatchObject({
            event: 'session.message',
            session_id: primary?.id,
            task_id: 'task-0001',
            kind: 'user',
            text: '读一下日志',
            sequence: 1,
        });
        expect(messages[1]).toMatchObject({ event: 'session.message', kind: 'outcome', sequence: 2 });

        const history = await transport.sessionHistory({ session_id: primary?.id ?? '' });
        expect(history.truncated).toBe(false);
        expect(history.entries.map((entry) => entry.kind)).toEqual(['user', 'outcome']);
        expect(history.entries[0]).toMatchObject({
            kind: 'user',
            text: '读一下日志',
            sequence: 1,
        });
        expect(history.entries[1]?.text).toBe('loop settled: Completed (steps 0)');
        expect(history.entries[1]?.sequence).toBe(2);
    });

    it('rejects an explicit unknown binding with not_found and honours explicit ones', async () => {
        const { transport } = makeService({ hostStartDelayMs: 0, stepDurationMs: 5 });
        await expectIpcError(
            () => transport.submitTask({ goal: 'g', steps: [], session_id: 'ffffffffffffffffffffffffffffffff' }),
            'not_found',
            'unknown session id',
        );
        const opened = await transport.openSession();
        await transport.submitTask({ goal: 'bound', steps: [], session_id: opened.session_id });
        await delay(30);
        const history = await transport.sessionHistory({ session_id: opened.session_id });
        expect(history.entries.map((entry) => entry.kind)).toEqual(['user', 'outcome']);
        // The primary session stayed empty.
        const primary = (await transport.listSessions())[0];
        const primaryHistory = await transport.sessionHistory({ session_id: primary?.id ?? '' });
        expect(primaryHistory.entries).toEqual([]);
    });

    it('windows session.history with truncated marking and clamps the limit', async () => {
        const { transport } = makeService({ hostStartDelayMs: 0, stepDurationMs: 5 });
        const primary = (await transport.listSessions())[0];
        for (let i = 0; i < 4; i += 1) {
            await transport.submitTask({ goal: `g${i}`, steps: [] });
        }
        await delay(60);
        const full = await transport.sessionHistory({ session_id: primary?.id ?? '' });
        expect(full.entries).toHaveLength(8); // 4 × (user + outcome)
        expect(full.truncated).toBe(false);

        const windowed = await transport.sessionHistory({ session_id: primary?.id ?? '', limit: 3 });
        expect(windowed.entries).toHaveLength(3);
        expect(windowed.truncated).toBe(true);
        // Newest window in conversation order.
        expect(windowed.entries.map((entry) => entry.sequence)).toEqual([6, 7, 8]);
    });

    it('answers not_found for an unknown session history', async () => {
        const { transport } = makeService({ hostStartDelayMs: 0 });
        await expectIpcError(
            () => transport.sessionHistory({ session_id: 'ffffffffffffffffffffffffffffffff' }),
            'not_found',
            'unknown session id',
        );
    });
});

describe('session event set', () => {
    it('publishes one turn per settled step plus output for executed steps', async () => {
        const { transport } = makeService({ hostStartDelayMs: 0, stepDurationMs: 5 });
        const primary = (await transport.listSessions())[0];
        const collector = createEventCollector();
        await transport.subscribe(collector.listener);
        await transport.submitTask({
            goal: 'g',
            steps: [
                { op: 'filesystem.read', arg: 'a.txt' },
                { op: 'process.execute', arg: 'echo hi' },
            ],
        });
        await delay(60);

        const sid = primary?.id;
        const turns = collector.events.filter(
            (event) => event.event === 'session.turn' && event.session_id === sid,
        );
        expect(turns.map((t) => (t.event === 'session.turn' ? [t.step, t.kind, t.status] : []))).toEqual([
            [1, 'filesystem.read', 'ok'],
            [2, 'process.execute', 'ok'],
        ]);
        const outputs = collector.events.filter(
            (event) => event.event === 'session.output' && event.session_id === sid,
        );
        expect(outputs).toHaveLength(2);
        expect(outputs[0]).toMatchObject({ step: 1, truncated: false });
        expect(outputs[0]?.event === 'session.output' && outputs[0].chunk.startsWith('mock content of')).toBe(true);
    });

    it('publishes turns without output for steps that never executed', async () => {
        const { transport } = makeService({ hostStartDelayMs: 0, stepDurationMs: 5 });
        const primary = (await transport.listSessions())[0];
        const collector = createEventCollector();
        await transport.subscribe(collector.listener);
        await transport.submitTask({
            goal: 'g',
            steps: [
                { op: 'process.execute', arg: 'fail:boom' },
                { op: 'filesystem.read', arg: 'skipped' },
            ],
        });
        await delay(60);

        const sid = primary?.id;
        const turns = collector.events.filter(
            (event) => event.event === 'session.turn' && event.session_id === sid,
        );
        expect(turns.map((t) => (t.event === 'session.turn' ? t.status : ''))).toEqual(['failed', 'skipped']);
        const outputs = collector.events.filter(
            (event) => event.event === 'session.output' && event.session_id === sid,
        );
        // Only the failed step reached the action; the skipped one has no
        // output chunk (the wire omits it, matching the C++ null-result rule).
        expect(outputs).toHaveLength(1);
    });
});

describe('sessions capability disabled', () => {
    it('omits the sessions member from hello', async () => {
        const { transport } = makeService({ hostStartDelayMs: 0, sessionsCapability: false });
        const identity = await transport.hello();
        expect('sessions' in identity).toBe(false);
        expect(transport.sessionsSupported).toBe(false);
    });
});

describe('session.close management face (DEC-026 backlog item 2)', () => {
    it('closes an opened session, removes it from the list and publishes the closed notification', async () => {
        const { transport } = makeService({ hostStartDelayMs: 0 });
        const collector = createEventCollector();
        await transport.subscribe(collector.listener);

        const opened = await transport.openSession();
        await delay(5);
        expect((await transport.listSessions()).some((s) => s.id === opened.session_id)).toBe(true);

        const closed = await transport.closeSession(opened.session_id);
        expect(closed).toEqual({ session_id: opened.session_id, state: 'closed' });
        expect((await transport.listSessions()).some((s) => s.id === opened.session_id)).toBe(false);
        await delay(5);
        // The open notification (state autonomous) plus exactly one closed
        // notification are expected for this session.
        const closedEvents = collector.events.filter(
            (event) =>
                event.event === 'session.updated' &&
                event.session_id === opened.session_id &&
                event.state === 'closed',
        );
        expect(closedEvents).toHaveLength(1);
    });

    it('refuses the primary session with the stable invalid_state (task.submit default binding)', async () => {
        const { transport } = makeService({ hostStartDelayMs: 0 });
        const sessions = await transport.listSessions();
        expect(sessions).toHaveLength(1);
        await expect(transport.closeSession(sessions[0]!.id)).rejects.toMatchObject({
            code: 'invalid_state',
            message: 'the primary session cannot be closed',
        });
        // The refusal left the registry untouched.
        expect(await transport.listSessions()).toHaveLength(1);
    });

    it('answers not_found for unknown ids and keeps capacity freed after close', async () => {
        const { transport } = makeService({ hostStartDelayMs: 0 });
        await expect(transport.closeSession('ffffffffffffffffffffffffffffffff')).rejects.toMatchObject({
            code: 'not_found',
            message: 'unknown session id',
        });
        // Fill the registry, close one, and confirm the freed slot admits a
        // new session (the mock registry capacity is 16, primary included).
        const { transport: capacityTransport } = makeService({ hostStartDelayMs: 0 });
        const opened: string[] = [];
        for (let i = 0; i < 15; i += 1) {
            opened.push((await capacityTransport.openSession()).session_id);
        }
        await expect(capacityTransport.openSession()).rejects.toMatchObject({ code: 'unavailable' });
        await capacityTransport.closeSession(opened[0]!);
        const reopened = await capacityTransport.openSession();
        expect(reopened.session_id).not.toBe(opened[0]);
    });
});
