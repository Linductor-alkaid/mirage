/// M1.5-08 验收：状态语义封闭映射（§2.6 门槛 3 / §2.5-7）。

import { describe, expect, it } from 'vitest';

import {
    hostBadgeTone,
    stepBadgeTone,
    taskBadgeTone,
    type BadgeTone,
} from '../src/components/status-badge.js';

const VALID_TONES: readonly BadgeTone[] = ['success', 'destructive', 'primary', 'warning', 'info', 'muted'];

const TASK_STATES = ['Idle', 'Active', 'Paused', 'Cancelling', 'Completed', 'Failed', 'Cancelled'] as const;
const STEP_STATES = ['pending', 'running', 'ok', 'failed', 'skipped', 'cancelled'] as const;
const HOST_STATES = ['stopped', 'starting', 'running', 'stopping', 'failed'] as const;

describe('§2.6-3 closed status-tone mapping', () => {
    it('every mapped tone is one of the six semantic tones', () => {
        for (const s of TASK_STATES) expect(VALID_TONES).toContain(taskBadgeTone(s));
        for (const s of STEP_STATES) expect(VALID_TONES).toContain(stepBadgeTone(s));
        for (const s of HOST_STATES) expect(VALID_TONES).toContain(hostBadgeTone(s));
    });

    it('task semantics: Failed->destructive, Active->primary, Paused->info, Cancelling->warning, Completed->success', () => {
        expect(taskBadgeTone('Failed')).toBe('destructive');
        expect(taskBadgeTone('Active')).toBe('primary');
        expect(taskBadgeTone('Paused')).toBe('info');
        expect(taskBadgeTone('Cancelling')).toBe('warning');
        expect(taskBadgeTone('Completed')).toBe('success');
        expect(taskBadgeTone('Idle')).toBe('muted');
        expect(taskBadgeTone('Cancelled')).toBe('muted');
    });

    it('step semantics: running->primary, ok->success, failed->destructive, pending/skipped/cancelled->muted', () => {
        expect(stepBadgeTone('running')).toBe('primary');
        expect(stepBadgeTone('ok')).toBe('success');
        expect(stepBadgeTone('failed')).toBe('destructive');
        expect(stepBadgeTone('pending')).toBe('muted');
        expect(stepBadgeTone('skipped')).toBe('muted');
        expect(stepBadgeTone('cancelled')).toBe('muted');
    });

    it('host semantics: running->success, starting/stopping->warning, failed->destructive, stopped->muted', () => {
        expect(hostBadgeTone('running')).toBe('success');
        expect(hostBadgeTone('starting')).toBe('warning');
        expect(hostBadgeTone('stopping')).toBe('warning');
        expect(hostBadgeTone('failed')).toBe('destructive');
        expect(hostBadgeTone('stopped')).toBe('muted');
    });
});
