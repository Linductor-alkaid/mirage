/// Client-side event-stream discipline (DEC-012 draft, decisions 2/4):
/// per-connection `seq` tracking with resync on gap/overflow, and the
/// bounded drop-oldest queue shape the service uses per connection
/// (mirrored here so mock and tests share one implementation).

import type { ServerEvent } from './types.js';

export type ResyncReason = 'seq-gap' | 'overflow';

export type SequencerResult =
    | { kind: 'delivered' }
    | { kind: 'resync'; reason: ResyncReason; dropped?: number };

/** Tracks the per-connection `seq` (starts at 1, strictly monotonic). A gap
 * or an `events.overflow` marker means the stream is incomplete: the client
 * must resync from `task.list` / `task.inspect` and must not treat the event
 * stream as full state (DEC-012 decision 4). After a resync the next normal
 * event is accepted as the new baseline — the peer's seq counter never
 * resets, so comparing against the old baseline would loop forever. */
export class EventSequencer {
    private lastSeq = 0;
    private awaitBaseline = false;

    push(event: ServerEvent): SequencerResult {
        if (event.event === 'events.overflow') {
            this.awaitBaseline = true;
            return { kind: 'resync', reason: 'overflow', dropped: event.dropped };
        }
        if (this.awaitBaseline) {
            this.lastSeq = event.seq;
            this.awaitBaseline = false;
            return { kind: 'delivered' };
        }
        if (event.seq !== this.lastSeq + 1) {
            this.awaitBaseline = true;
            return { kind: 'resync', reason: 'seq-gap' };
        }
        this.lastSeq = event.seq;
        return { kind: 'delivered' };
    }
}

/** Fixed-capacity FIFO with a drop-oldest overflow policy (DEC-012 decision
 * 5). `push` returns the number of dropped entries so the owner can surface
 * an explicit `events.overflow` marker instead of losing events silently. */
export class BoundedEventQueue<T> {
    private items: T[] = [];

    constructor(readonly capacity: number) {
        if (!Number.isInteger(capacity) || capacity < 1) {
            throw new RangeError('BoundedEventQueue capacity must be a positive integer');
        }
    }

    get size(): number {
        return this.items.length;
    }

    /** Entries in arrival order (oldest first). */
    drain(): T[] {
        const drained = this.items;
        this.items = [];
        return drained;
    }

    /** Pushes one entry; when full, drops the oldest and reports 1. */
    push(item: T): number {
        if (this.items.length >= this.capacity) {
            this.items.shift();
            this.items.push(item);
            return 1;
        }
        this.items.push(item);
        return 0;
    }
}
