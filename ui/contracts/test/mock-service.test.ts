/// Behaviour tests for the M1.5-04 mock service (mock/mock-service.ts):
/// host lifecycle, scripted step progression, cancel semantics, stable error
/// shapes, and the DEC-012-draft event surface (per-subscription seq,
/// bounded drop-oldest queues, overflow markers, manual flush hook).
///
/// Timing strategy: real timers with small durations (5-200 ms) and generous
/// awaits instead of fake timers, so the queueMicrotask auto-flush path stays
/// untouched. Cancellation races are avoided by acting synchronously right
/// after submit while the step timer is still pending.

import { afterEach, describe, expect, it } from 'vitest';
import { createMockTransport } from '../src/mock/mock-service.js';
import type { MockMirageService, MockServiceOptions } from '../src/mock/mock-service.js';
import { EventSequencer } from '../src/events.js';
import type { TaskProgress, TaskStep } from '../src/types.js';
import { TransportClosedError } from '../src/transport.js';
import { createEventCollector, delay, expectIpcError, expectTransportClosed } from './helpers.js';

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

describe('hello identity', () => {
    it('advertises the service identity, protocol 1 and the events capability', async () => {
        const { transport } = makeService({ hostStartDelayMs: 0 });
        const identity = await transport.hello();
        expect(identity).toEqual({
            service: 'mirage-runtime',
            mirage_version: '0.1.0',
            mira_core_version: '0.1.0',
            host_status: 'running',
            protocol: 1,
            events: true,
        });
        expect(identity.events).toBe(true);
        expect(transport.eventsSupported).toBe(true);
        expect(transport.label).toBe('Mock');
    });
});

describe('host lifecycle', () => {
    it('walks starting -> running -> stopping -> stopped and publishes each transition', async () => {
        const { transport, service } = makeService({ hostStartDelayMs: 15, shutdownDelayMs: 15 });
        const collector = createEventCollector();
        transport.subscribe(collector.listener);

        // Immediately after construction the host is still starting.
        expect((await transport.hello()).host_status).toBe('starting');
        await expectIpcError(
            () => transport.submitTask({ goal: 'g', steps: [] }),
            'invalid_state',
            'mira host is not running (status: starting)',
        );

        await delay(50);
        expect((await transport.hello()).host_status).toBe('running');
        expect(await transport.submitTask({ goal: 'g', steps: [] })).toEqual({ task_id: 'task-0001' });

        transport.shutdown();
        expect((await transport.hello()).host_status).toBe('stopping');
        await expectIpcError(
            () => transport.submitTask({ goal: 'g', steps: [] }),
            'invalid_state',
            'mira host is not running (status: stopping)',
        );
        // shutdown is idempotent while stopping: no second transition event.
        transport.shutdown();

        await delay(50);
        expect((await transport.hello()).host_status).toBe('stopped');
        await expectIpcError(
            () => transport.submitTask({ goal: 'g', steps: [] }),
            'invalid_state',
            'mira host is not running (status: stopped)',
        );
        // shutdown on a stopped host is a no-op as well.
        transport.shutdown();
        await delay(5);

        expect(collector.hostStatuses()).toEqual(['starting', 'running', 'stopping', 'stopped']);
        // seq order: starting(1), running(2), task Active(3), task Completed(4),
        // stopping(5), stopped(6).
        expect(collector.seqs()).toEqual([1, 2, 3, 4, 5, 6]);
        // The task submitted while running completed before shutdown.
        expect(service.list()).toEqual([{ id: 'task-0001', goal: 'g', progress: 'Completed' }]);
    });

    it('converges to stopped when shutdown is called while starting, without resurrecting running', async () => {
        // Regression: shutdown() must clear the pending starting timer, or the
        // stale constructor timer flips the host back to 'running' after the
        // shutdown already settled to 'stopped'.
        const { transport, service } = makeService({ hostStartDelayMs: 400, shutdownDelayMs: 5 });
        const collector = createEventCollector();
        transport.subscribe(collector.listener);

        expect((await transport.hello()).host_status).toBe('starting');
        transport.shutdown();
        expect((await transport.hello()).host_status).toBe('stopping');
        await expectIpcError(
            () => transport.submitTask({ goal: 'g', steps: [] }),
            'invalid_state',
            'mira host is not running (status: stopping)',
        );

        await delay(30);
        expect((await transport.hello()).host_status).toBe('stopped');
        // Outlive the original starting delay: the stale timer must not fire.
        await delay(450);
        expect((await transport.hello()).host_status).toBe('stopped');
        await delay(5);
        await expectIpcError(
            () => transport.submitTask({ goal: 'g', steps: [] }),
            'invalid_state',
            'mira host is not running (status: stopped)',
        );
        expect(service.list()).toEqual([]);
        expect(collector.hostStatuses()).toEqual(['starting', 'stopping', 'stopped']);
        expect(collector.seqs()).toEqual([1, 2, 3]);
    });
});

describe('submit and step progression', () => {
    it('runs the full success flow with step-level states and terminal Completed', async () => {
        const { transport } = makeService({ hostStartDelayMs: 0, stepDurationMs: 200 });
        const collector = createEventCollector();
        transport.subscribe(collector.listener);

        const steps: TaskStep[] = [
            { op: 'filesystem.read', arg: '/tmp/a.txt' },
            { op: 'process.execute', arg: 'echo hi' },
        ];
        expect(await transport.submitTask({ goal: 'open a terminal', steps })).toEqual({
            task_id: 'task-0001',
        });

        // Mid-flight: the first step is running, the second still pending.
        await delay(60);
        const running = await transport.inspectTask('task-0001');
        expect(running.progress).toBe('Active');
        expect(running.steps.map((step) => step.status)).toEqual(['running', 'pending']);
        expect(running.steps[0]?.operation_id).toBe('op-0001');
        expect(running.has_success).toBe(false);
        expect(running.success).toBeUndefined();

        await delay(500);
        const done = await transport.inspectTask('task-0001');
        expect(done.progress).toBe('Completed');
        expect(done.has_success).toBe(true);
        expect(done.success).toBe(true);
        expect(done.steps[0]).toEqual({
            index: 0,
            kind: 'filesystem.read',
            status: 'ok',
            operation_id: 'op-0001',
            permission: 'allowed',
            ok: true,
            exit_code: -1,
            result: "mock content of '/tmp/a.txt'",
            result_truncated: false,
            error: '',
        });
        expect(done.steps[1]).toEqual({
            index: 1,
            kind: 'process.execute',
            status: 'ok',
            operation_id: 'op-0002',
            permission: 'allowed',
            ok: true,
            exit_code: 0,
            result: "mock output of 'echo hi'",
            result_truncated: false,
            error: '',
        });
        expect(await transport.listTasks()).toEqual([
            { id: 'task-0001', goal: 'open a terminal', progress: 'Completed' },
        ]);

        // baseline + Active + running(step0) + ok(step0) + running(step1)
        // + ok(step1) + Completed
        expect(collector.seqs()).toEqual([1, 2, 3, 4, 5, 6, 7]);
        expect(collector.progressUpdates()).toEqual([
            'Active',
            'Active',
            'Active',
            'Active',
            'Active',
            'Completed',
        ]);
        const terminal = collector.events.at(-1);
        expect(terminal).toMatchObject({
            event: 'task.updated',
            task_id: 'task-0001',
            goal: 'open a terminal',
            progress: 'Completed',
            has_success: true,
            success: true,
        });
    });

    it('assigns incrementing task-NNNN ids and completes step-less tasks at once', async () => {
        const { transport } = makeService({ hostStartDelayMs: 0, stepDurationMs: 5 });
        expect(await transport.submitTask({ goal: 'first', steps: [] })).toEqual({ task_id: 'task-0001' });
        expect(await transport.submitTask({ goal: 'second', steps: [] })).toEqual({ task_id: 'task-0002' });
        await delay(20);
        const done = await transport.inspectTask('task-0002');
        expect(done.progress).toBe('Completed');
        expect(done.has_success).toBe(true);
        expect(done.success).toBe(true);
        expect(done.steps).toEqual([]);
        expect((await transport.listTasks()).map((task) => task.id)).toEqual(['task-0001', 'task-0002']);
    });
});

describe('cancellation', () => {
    it('cancels a running task: steps cancelled/skipped, terminal Cancelled, pinned_runtime afterwards', async () => {
        const { transport } = makeService({ hostStartDelayMs: 0, stepDurationMs: 10000 });
        const collector = createEventCollector();
        transport.subscribe(collector.listener);

        const steps: TaskStep[] = [
            { op: 'process.execute', arg: 'long-running' },
            { op: 'filesystem.read', arg: 'never-started' },
            { op: 'process.execute', arg: 'also-never-started' },
        ];
        await transport.submitTask({ goal: 'to be cancelled', steps });

        // Cancel synchronously while the first step timer is still pending.
        const result = await transport.cancelTask('task-0001');
        expect(result.task_id).toBe('task-0001');
        expect(['Cancelling', 'Cancelled']).toContain(result.progress);

        const inspect = await transport.inspectTask('task-0001');
        expect(inspect.progress).toBe('Cancelled');
        expect(inspect.has_success).toBe(false);
        expect(inspect.success).toBeUndefined();
        expect(inspect.steps.map((step) => step.status)).toEqual(['cancelled', 'skipped', 'skipped']);
        expect(inspect.steps.map((step) => step.operation_id)).toEqual(['op-0001', '', '']);
        expect(inspect.steps.every((step) => !step.ok)).toBe(true);

        await delay(5);
        expect(collector.progressUpdates()).toEqual([
            'Active',
            'Active',
            'Cancelling',
            'Cancelling',
            'Cancelled',
        ]);
        expect(collector.seqs()).toEqual([1, 2, 3, 4, 5, 6]);

        // Cancelling a settled task is the pinned passthrough rejection.
        await expectIpcError(
            () => transport.cancelTask('task-0001'),
            'pinned_runtime',
            "invalid_state: task 'task-0001' already settled as Cancelled",
        );
    });

    it('rejects cancelling an unknown task with not_found', async () => {
        const { transport } = makeService({ hostStartDelayMs: 0 });
        await expectIpcError(
            () => transport.cancelTask('task-9999'),
            'not_found',
            "task 'task-9999' was not found",
        );
    });
});

describe('failure injection', () => {
    it('fails a process.execute fail: step, skips the rest and settles Failed', async () => {
        const { transport } = makeService({ hostStartDelayMs: 0, stepDurationMs: 5 });
        const collector = createEventCollector();
        transport.subscribe(collector.listener);

        await transport.submitTask({
            goal: 'doomed',
            steps: [
                { op: 'process.execute', arg: 'fail:boom' },
                { op: 'filesystem.read', arg: '/etc/hostname' },
            ],
        });
        await delay(80);

        const inspect = await transport.inspectTask('task-0001');
        expect(inspect.progress).toBe('Failed');
        expect(inspect.has_success).toBe(false);
        expect(inspect.success).toBeUndefined();
        const failed = inspect.steps[0];
        expect(failed?.status).toBe('failed');
        expect(failed?.exit_code).toBe(1);
        expect(failed?.error).toBe('mock step failure: boom');
        expect(failed?.ok).toBe(false);
        expect(inspect.steps[1]?.status).toBe('skipped');
        expect(inspect.steps[1]?.operation_id).toBe('');
        expect(collector.progressUpdates().at(-1)).toBe('Failed');
        const terminal = collector.events.at(-1);
        expect(terminal).toMatchObject({ event: 'task.updated', progress: 'Failed', has_success: false, success: false });
    });

    it('uses the injected-failure placeholder for a bare fail: argument', async () => {
        const { transport } = makeService({ hostStartDelayMs: 0, stepDurationMs: 5 });
        await transport.submitTask({
            goal: 'g',
            steps: [{ op: 'process.execute', arg: 'fail:   ' }],
        });
        await delay(60);
        const inspect = await transport.inspectTask('task-0001');
        expect(inspect.steps[0]?.error).toBe('mock step failure: injected failure');
    });

    it('does not treat fail: in a filesystem.read argument as an injection', async () => {
        const { transport } = makeService({ hostStartDelayMs: 0, stepDurationMs: 5 });
        await transport.submitTask({
            goal: 'g',
            steps: [{ op: 'filesystem.read', arg: 'fail:not-an-injection' }],
        });
        await delay(60);
        const inspect = await transport.inspectTask('task-0001');
        expect(inspect.progress).toBe('Completed');
        expect(inspect.has_success).toBe(true);
        expect(inspect.success).toBe(true);
        expect(inspect.steps[0]?.status).toBe('ok');
    });
});

describe('submit validation and capacity', () => {
    it('rejects a whitespace-only goal', async () => {
        const { transport } = makeService({ hostStartDelayMs: 0 });
        await expectIpcError(
            () => transport.submitTask({ goal: '   ', steps: [] }),
            'invalid_argument',
            "task.submit requires a non-empty 'goal'",
        );
    });

    it('rejects empty step arguments without registering the task', async () => {
        const { transport } = makeService({ hostStartDelayMs: 0 });
        await expectIpcError(
            () =>
                transport.submitTask({
                    goal: 'g',
                    steps: [
                        { op: 'filesystem.read', arg: 'ok' },
                        { op: 'process.execute', arg: '' },
                    ],
                }),
            'invalid_argument',
            "task.submit step requires a non-empty 'arg'",
        );
        expect(await transport.listTasks()).toEqual([]);
        // The failed submit did not consume a task id.
        expect(await transport.submitTask({ goal: 'next', steps: [] })).toEqual({ task_id: 'task-0001' });
    });

    it('refuses submits once the registry is at capacity', async () => {
        const { transport } = makeService({ hostStartDelayMs: 0, stepDurationMs: 10000, taskCapacity: 1 });
        expect(await transport.submitTask({ goal: 'first', steps: [] })).toEqual({ task_id: 'task-0001' });
        await expectIpcError(
            () => transport.submitTask({ goal: 'second', steps: [] }),
            'invalid_state',
            'task registry at capacity (1)',
        );
        expect((await transport.listTasks()).length).toBe(1);
    });
});

describe('inspect and list snapshots', () => {
    it('returns not_found for unknown ids and reflects state at call time', async () => {
        const { transport } = makeService({ hostStartDelayMs: 0, stepDurationMs: 10000 });
        await expectIpcError(
            () => transport.inspectTask('task-4242'),
            'not_found',
            "task 'task-4242' was not found",
        );

        await transport.submitTask({ goal: 'one', steps: [{ op: 'filesystem.read', arg: 'a' }] });
        await transport.submitTask({ goal: 'two', steps: [{ op: 'filesystem.read', arg: 'b' }] });
        expect(await transport.listTasks()).toEqual([
            { id: 'task-0001', goal: 'one', progress: 'Active' },
            { id: 'task-0002', goal: 'two', progress: 'Active' },
        ]);

        await transport.cancelTask('task-0001');
        // list is a live snapshot: only the cancelled task changed state.
        expect((await transport.listTasks()).map((task) => task.progress as TaskProgress)).toEqual([
            'Cancelled',
            'Active',
        ]);
        expect((await transport.inspectTask('task-0002')).goal).toBe('two');
    });
});

describe('event surface', () => {
    it('sends a host.status baseline at subscribe time and keeps seq strictly monotonic', async () => {
        const { transport } = makeService({ hostStartDelayMs: 300 });
        const collector = createEventCollector();
        transport.subscribe(collector.listener);
        await delay(5);
        expect(collector.events).toHaveLength(1);
        expect(collector.events[0]).toMatchObject({ v: 1, seq: 1, event: 'host.status', status: 'starting' });
    });

    it('keeps per-subscription seq independent between two subscribers', async () => {
        const { transport, service } = makeService({ hostStartDelayMs: 0 });
        const a = createEventCollector();
        const b = createEventCollector();
        transport.subscribe(a.listener);
        transport.subscribe(b.listener);
        service.publishFrame({ v: 1, event: 'host.status', status: 'stopping' });
        await delay(5);
        for (const collector of [a, b]) {
            expect(collector.seqs()).toEqual([1, 2]);
            expect(collector.hostStatuses()).toEqual(['running', 'stopping']);
        }
    });

    it('stops delivery after unsubscribe (idempotent)', async () => {
        const { transport, service } = makeService({ hostStartDelayMs: 0 });
        const collector = createEventCollector();
        transport.subscribe(collector.listener);
        await delay(5);
        // The subscribe-time baseline was delivered before unsubscribing.
        expect(collector.events).toHaveLength(1);
        await transport.unsubscribe();
        await transport.unsubscribe();
        service.publishFrame({ v: 1, event: 'host.status', status: 'stopping' });
        await delay(5);
        expect(collector.events).toHaveLength(1);
    });

    it('holds events until flush() in manual mode and never redelivers', async () => {
        const { transport, service } = makeService({ hostStartDelayMs: 0, flushMode: 'manual' });
        const collector = createEventCollector();
        transport.subscribe(collector.listener);
        // Nothing is delivered while the queue is unflushed.
        await delay(5);
        expect(collector.events).toEqual([]);

        service.publishFrame({
            v: 1,
            event: 'task.updated',
            task_id: 'task-0001',
            goal: 'g',
            progress: 'Active',
            has_success: false,
            success: false,
        });
        await delay(5);
        expect(collector.events).toEqual([]);

        service.flush();
        expect(collector.seqs()).toEqual([1, 2]);
        expect(collector.hostStatuses()).toEqual(['running']);
        expect(collector.progressUpdates()).toEqual(['Active']);

        // A drained queue is not redelivered by a second flush.
        service.flush();
        expect(collector.seqs()).toEqual([1, 2]);
    });

    it('synthesizes events.overflow with a counted drop on burst, without duplicate seqs', async () => {
        const { transport, service } = makeService({
            hostStartDelayMs: 0,
            flushMode: 'manual',
            eventQueueCapacity: 4,
        });
        const collector = createEventCollector();
        transport.subscribe(collector.listener);

        for (const goal of ['g1', 'g2', 'g3', 'g4', 'g5', 'g6']) {
            service.publishFrame({
                v: 1,
                event: 'task.updated',
                task_id: 'task-0001',
                goal,
                progress: 'Active',
                has_success: false,
                success: false,
            });
        }
        service.flush();

        const seqs = collector.seqs();
        expect(new Set(seqs).size).toBe(seqs.length);
        expect([...seqs].sort((x, y) => x - y)).toEqual(seqs);
        // Capacity 4 with a 6-event burst: the queued baseline host.status
        // event, g1 and g2 were evicted (drop-oldest); the single marker
        // reports the exact total of 3 dropped events.
        expect(collector.overflowDrops()).toEqual([3]);
        // Delivered updates preserve publish order (g3..g6 survive this burst).
        expect(collector.progressUpdates()).toEqual(['Active', 'Active', 'Active', 'Active']);
        const goals = collector.events
            .filter((event) => event.event === 'task.updated')
            .map((event) => (event.event === 'task.updated' ? event.goal : ''));
        expect(goals).toEqual(['g3', 'g4', 'g5', 'g6']);
        expect(collector.events.at(-1)?.event).toBe('events.overflow');

        // The client-side sequencer must flag the incomplete stream.
        const sequencer = new EventSequencer();
        const verdicts = collector.events.map((event) => sequencer.push(event));
        expect(verdicts.some((verdict) => verdict.kind === 'resync' && verdict.reason === 'overflow')).toBe(true);
    });

    it('reports the exact dropped count in a single trailing marker and lets the sequencer re-baseline', async () => {
        // Regression: the marker is delivered after the queue drains (never
        // enqueued), so it cannot evict events and `dropped` accumulates the
        // exact number of evicted entries — including the subscribe-time
        // baseline event.
        const { transport, service } = makeService({
            hostStartDelayMs: 0,
            flushMode: 'manual',
            eventQueueCapacity: 3,
        });
        const collector = createEventCollector();
        transport.subscribe(collector.listener);

        const publish = (goal: string) =>
            service.publishFrame({
                v: 1,
                event: 'task.updated',
                task_id: 'task-0001',
                goal,
                progress: 'Active',
                has_success: false,
                success: false,
            });
        // baseline(1) + f1..f5(2..6) into capacity 3: baseline, f1 and f2 are
        // evicted; exactly 3 losses must be reported by exactly one marker.
        for (const goal of ['f1', 'f2', 'f3', 'f4', 'f5']) {
            publish(goal);
        }
        service.flush();

        const events = collector.events;
        // Delivered: f3, f4, f5 and the marker (the evicted baseline, f1 and
        // f2 are never delivered).
        expect(events.length).toBe(4);
        // The marker is the last delivery of the batch, never mid-queue.
        expect(events.at(-1)).toEqual({ v: 1, seq: 7, event: 'events.overflow', dropped: 3 });
        expect(events.slice(0, -1).every((event) => event.event !== 'events.overflow')).toBe(true);
        // Delivered events keep publish order; no duplicate seqs.
        expect(collector.seqs()).toEqual([4, 5, 6, 7]);
        expect(collector.progressUpdates()).toEqual(['Active', 'Active', 'Active']);
        const goals = events
            .filter((event) => event.event === 'task.updated')
            .map((event) => (event.event === 'task.updated' ? event.goal : ''));
        expect(goals).toEqual(['f3', 'f4', 'f5']);
        // Exact accounting: published 6 (baseline + 5), delivered 3, lost 3.
        expect(6 - goals.length).toBe(3);

        // A follow-up burst that does not overflow emits no marker at all.
        publish('f6');
        publish('f7');
        service.flush();
        expect(collector.seqs()).toEqual([4, 5, 6, 7, 8, 9]);
        expect(collector.overflowDrops()).toEqual([3]);
        expect(collector.events.at(-1)).toMatchObject({ event: 'task.updated', goal: 'f7' });

        // Client discipline: gap resync on the first survivor, overflow resync
        // on the marker, then the new baseline continues without misreports.
        const sequencer = new EventSequencer();
        const verdicts = collector.events.map((event) => sequencer.push(event));
        expect(verdicts).toEqual([
            { kind: 'resync', reason: 'seq-gap' },
            { kind: 'delivered' },
            { kind: 'delivered' },
            { kind: 'resync', reason: 'overflow', dropped: 3 },
            { kind: 'delivered' },
            { kind: 'delivered' },
        ]);
    });

    it('accumulates per-flush drop totals across bursts into separate exact markers', async () => {
        const { transport, service } = makeService({
            hostStartDelayMs: 0,
            flushMode: 'manual',
            eventQueueCapacity: 2,
        });
        const collector = createEventCollector();
        transport.subscribe(collector.listener);
        const publish = () =>
            service.publishFrame({
                v: 1,
                event: 'task.updated',
                task_id: 'task-0001',
                goal: 'g',
                progress: 'Active',
                has_success: false,
                success: false,
            });

        // Burst 1: baseline evicted (1 loss), flushed.
        publish();
        publish();
        service.flush();
        // Burst 2: queue empty again, third publish evicts the first (1 loss).
        publish();
        publish();
        publish();
        service.flush();

        expect(collector.overflowDrops()).toEqual([1, 1]);
        // Each marker trails its own flush batch.
        expect(collector.events.map((event) => event.event)).toEqual([
            'task.updated',
            'task.updated',
            'events.overflow',
            'task.updated',
            'task.updated',
            'events.overflow',
        ]);
        expect(collector.seqs()).toEqual([2, 3, 4, 6, 7, 8]);
    });
});

describe('events capability disabled', () => {
    it('omits the events member from hello and refuses subscribe', async () => {
        const { transport } = makeService({ hostStartDelayMs: 0, eventsCapability: false });
        const identity = await transport.hello();
        expect('events' in identity).toBe(false);
        expect(identity.protocol).toBe(1);
        expect(identity.service).toBe('mirage-runtime');
        expect(transport.eventsSupported).toBe(false);
        await expectIpcError(
            () => transport.subscribe(() => {}),
            'unsupported',
            'peer did not advertise the events capability',
        );
    });
});

describe('close semantics', () => {
    it('makes every request fail after close and is idempotent', async () => {
        const { transport, service } = makeService({ hostStartDelayMs: 0 });
        const collector = createEventCollector();
        transport.subscribe(collector.listener);

        await transport.close();
        await transport.close();
        await expectTransportClosed(() => transport.hello());
        await expectTransportClosed(() => transport.submitTask({ goal: 'g', steps: [] }));
        await expectTransportClosed(() => transport.listTasks());
        await expectTransportClosed(() => transport.inspectTask('task-0001'));
        await expectTransportClosed(() => transport.cancelTask('task-0001'));
        await expectTransportClosed(() => transport.shutdown());
        await expectTransportClosed(() => transport.subscribe(collector.listener));
        await expectTransportClosed(() => service.list());

        // Subscriptions were dropped: nothing beyond the pre-close baseline
        // is ever delivered.
        service.publishFrame({ v: 1, event: 'host.status', status: 'running' });
        await delay(5);
        expect(collector.events).toHaveLength(1);
        expect(collector.events[0]).toMatchObject({ event: 'host.status', status: 'running' });
    });

    it('rejects (never throws synchronously) for every request after close', async () => {
        // Regression: the transport contract is promise rejection, so the
        // promises must be obtainable without executing a sync throw. If a
        // method regressed to throwing synchronously, the assignment below
        // would raise before expect() ever runs and fail this test.
        const { transport } = makeService({ hostStartDelayMs: 0 });
        await transport.close();
        await transport.close();

        const pHello = transport.hello();
        const pSubmit = transport.submitTask({ goal: 'g', steps: [] });
        const pList = transport.listTasks();
        const pInspect = transport.inspectTask('task-0001');
        const pCancel = transport.cancelTask('task-0001');
        const pShutdown = transport.shutdown();
        const pSubscribe = transport.subscribe(() => {});

        const pending: Promise<unknown>[] = [
            pHello,
            pSubmit,
            pList,
            pInspect,
            pCancel,
            pShutdown,
            pSubscribe,
        ];
        for (const promise of pending) {
            expect(promise).toBeInstanceOf(Promise);
            await expect(promise).rejects.toBeInstanceOf(TransportClosedError);
            await expect(promise).rejects.toThrow('mock service is closed');
        }
    });
});
