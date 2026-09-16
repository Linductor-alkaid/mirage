/// Unit tests for client-side event discipline (events.ts): per-connection
/// seq tracking with resync on gap/overflow (DEC-012 decisions 2/4) and the
/// bounded drop-oldest queue shape (decision 5).

import { describe, expect, it } from 'vitest';
import { BoundedEventQueue, EventSequencer } from '../src/events.js';
import { hostEvent, overflowEvent, taskEvent } from './helpers.js';

describe('EventSequencer', () => {
    it('delivers a strictly consecutive stream starting at 1', () => {
        const sequencer = new EventSequencer();
        expect(sequencer.push(hostEvent(1, 'starting'))).toEqual({ kind: 'delivered' });
        expect(sequencer.push(hostEvent(2, 'running'))).toEqual({ kind: 'delivered' });
        expect(sequencer.push(taskEvent(3, 'Active'))).toEqual({ kind: 'delivered' });
    });

    it('rejects a first event whose seq is not 1', () => {
        const sequencer = new EventSequencer();
        expect(sequencer.push(hostEvent(2, 'running'))).toEqual({
            kind: 'resync',
            reason: 'seq-gap',
        });
    });

    it('flags a seq gap as resync and re-baselines without repeat reports', () => {
        const sequencer = new EventSequencer();
        sequencer.push(hostEvent(1, 'starting'));
        sequencer.push(hostEvent(2, 'running'));
        expect(sequencer.push(hostEvent(5, 'running'))).toEqual({
            kind: 'resync',
            reason: 'seq-gap',
        });
        // After a resync the next normal event is accepted as the new
        // baseline (the peer counter never resets).
        expect(sequencer.push(hostEvent(6, 'running'))).toEqual({ kind: 'delivered' });
        expect(sequencer.push(taskEvent(7, 'Active'))).toEqual({ kind: 'delivered' });
    });

    it('re-baselines onto a lower seq after a gap without looping', () => {
        const sequencer = new EventSequencer();
        // First event is not seq 1: immediate gap.
        expect(sequencer.push(hostEvent(4, 'running'))).toEqual({ kind: 'resync', reason: 'seq-gap' });
        // The next normal event is accepted as the new baseline whatever its seq.
        expect(sequencer.push(hostEvent(9, 'running'))).toEqual({ kind: 'delivered' });
        // A jump backwards after baselining is a fresh gap.
        expect(sequencer.push(hostEvent(2, 'running'))).toEqual({ kind: 'resync', reason: 'seq-gap' });
        expect(sequencer.push(hostEvent(3, 'running'))).toEqual({ kind: 'delivered' });
        expect(sequencer.push(hostEvent(10, 'running'))).toEqual({ kind: 'resync', reason: 'seq-gap' });
    });

    it('treats events.overflow as a resync carrying the dropped count', () => {
        const sequencer = new EventSequencer();
        sequencer.push(hostEvent(1, 'running'));
        expect(sequencer.push(overflowEvent(2, 7))).toEqual({
            kind: 'resync',
            reason: 'overflow',
            dropped: 7,
        });
    });

    it('accepts the next normal event after overflow as the new baseline', () => {
        const sequencer = new EventSequencer();
        sequencer.push(hostEvent(1, 'running'));
        sequencer.push(overflowEvent(2, 3));
        expect(sequencer.push(taskEvent(50, 'Active'))).toEqual({ kind: 'delivered' });
        expect(sequencer.push(taskEvent(51, 'Completed'))).toEqual({ kind: 'delivered' });
    });

    it('reports consecutive overflow markers and keeps waiting for a baseline', () => {
        const sequencer = new EventSequencer();
        sequencer.push(hostEvent(1, 'running'));
        expect(sequencer.push(overflowEvent(2, 1))).toEqual({ kind: 'resync', reason: 'overflow', dropped: 1 });
        expect(sequencer.push(overflowEvent(3, 2))).toEqual({ kind: 'resync', reason: 'overflow', dropped: 2 });
        expect(sequencer.push(taskEvent(4, 'Active'))).toEqual({ kind: 'delivered' });
    });
});

describe('BoundedEventQueue', () => {
    it('rejects capacities below 1 and non-integers', () => {
        for (const capacity of [0, -1, -100, 1.5, Number.NaN, Number.POSITIVE_INFINITY]) {
            expect(() => new BoundedEventQueue(capacity)).toThrow(RangeError);
        }
    });

    it('drains in FIFO arrival order and empties', () => {
        const queue = new BoundedEventQueue<string>(3);
        expect(queue.size).toBe(0);
        expect(queue.push('a')).toBe(0);
        expect(queue.push('b')).toBe(0);
        expect(queue.push('c')).toBe(0);
        expect(queue.size).toBe(3);
        expect(queue.drain()).toEqual(['a', 'b', 'c']);
        expect(queue.size).toBe(0);
        expect(queue.drain()).toEqual([]);
    });

    it('drops the oldest entry when full and reports 1', () => {
        const queue = new BoundedEventQueue<number>(2);
        queue.push(1);
        queue.push(2);
        expect(queue.push(3)).toBe(1);
        expect(queue.push(4)).toBe(1);
        expect(queue.drain()).toEqual([3, 4]);
    });

    it('capacity 1 keeps only the newest entry', () => {
        const queue = new BoundedEventQueue<string>(1);
        expect(queue.push('x')).toBe(0);
        expect(queue.push('y')).toBe(1);
        expect(queue.drain()).toEqual(['y']);
    });
});
