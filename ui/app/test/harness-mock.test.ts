/// harness-mock 模拟域：seedSessions / seedWorkflows 形状与上界、
/// exportSessionMarkdown 导出内容（H1、用户/assistant 小节、引用行）。

import { describe, expect, it } from 'vitest';

import {
    MAX_MESSAGES_PER_SESSION,
    MAX_SESSIONS,
    exportSessionMarkdown,
    seedContext,
    seedSessions,
    seedWorkflows,
} from '../src/state/harness-mock.js';
import type { ChatMessage } from '../src/state/model.js';

const MESSAGE_KINDS: readonly ChatMessage['kind'][] = [
    'user',
    'assistant',
    'activity',
    'step',
    'snapshot',
    'approval',
    'system',
];

const NOW = 1_758_240_000_000; // 固定时间原点，shape 断言与墙钟无关

describe('seedSessions', () => {
    it('seeds 7 sessions with unique ids', () => {
        const seeded = seedSessions(NOW);
        expect(seeded.sessions).toHaveLength(7);
        expect(new Set(seeded.sessions.map((s) => s.id)).size).toBe(7);
    });

    it('every session carries the meta shape (id/title/pinned/timestamps/mode)', () => {
        const seeded = seedSessions(NOW);
        for (const session of seeded.sessions) {
            expect(session.id, session.title).toBeTypeOf('string');
            expect(session.title.length).toBeGreaterThan(0);
            expect(['chat', 'exec']).toContain(session.mode);
            expect(session.createdAt).toBeLessThanOrEqual(session.updatedAt);
            expect(session.createdAt).toBeLessThanOrEqual(NOW);
            expect(typeof session.pinned).toBe('boolean');
        }
    });

    it('exec sessions reference a protocol task; exactly one pinned session', () => {
        const seeded = seedSessions(NOW);
        for (const session of seeded.sessions) {
            if (session.mode === 'exec') {
                expect(session.taskId, session.id).toBeTypeOf('string');
            }
        }
        expect(seeded.sessions.filter((s) => s.pinned)).toHaveLength(1);
    });

    it('every session has a thread, keyed exactly by session ids', () => {
        const seeded = seedSessions(NOW);
        expect([...seeded.messages.keys()].sort()).toEqual(seeded.sessions.map((s) => s.id).sort());
        for (const [id, thread] of seeded.messages) {
            expect(thread.length, id).toBeGreaterThanOrEqual(2);
        }
    });

    it('threads only use known message kinds and open with a user message', () => {
        const seeded = seedSessions(NOW);
        for (const [id, thread] of seeded.messages) {
            expect(MESSAGE_KINDS, id).toEqual(expect.arrayContaining(thread.map((m) => m.kind)));
            expect(new Set(thread.map((m) => m.kind)).size, id).toBeLessThanOrEqual(MESSAGE_KINDS.length);
            expect(thread[0]?.kind, id).toBe('user');
            for (const message of thread) {
                expect(typeof message.at, message.id).toBe('number');
            }
        }
    });

    it('message threads respect the per-session cap constant', () => {
        expect(MAX_MESSAGES_PER_SESSION).toBe(200);
        const seeded = seedSessions(NOW);
        for (const [, thread] of seeded.messages) {
            expect(thread.length).toBeLessThanOrEqual(MAX_MESSAGES_PER_SESSION);
        }
    });
});

describe('seedWorkflows', () => {
    it('seeds 4 workflows and 5 runs', () => {
        const seeded = seedWorkflows(NOW);
        expect(seeded.workflows).toHaveLength(4);
        expect(seeded.runs).toHaveLength(5);
    });

    it('workflow defs carry the full shape with known step kinds', () => {
        const seeded = seedWorkflows(NOW);
        for (const wf of seeded.workflows) {
            expect(wf.name.length).toBeGreaterThan(0);
            expect(wf.version).toMatch(/^v\d+$/);
            expect(wf.params.length).toBeGreaterThanOrEqual(0);
            expect(wf.steps.length).toBeGreaterThan(0);
            expect(wf.successRate).toBeGreaterThanOrEqual(0);
            expect(wf.successRate).toBeLessThanOrEqual(1);
            for (const step of wf.steps) {
                expect(['filesystem.read', 'process.execute', 'display.observe']).toContain(step.kind);
                expect(step.title.length).toBeGreaterThan(0);
            }
        }
    });

    it('every run references a seeded workflow with matching name and known status', () => {
        const seeded = seedWorkflows(NOW);
        for (const run of seeded.runs) {
            const wf = seeded.workflows.find((w) => w.id === run.workflowId);
            expect(wf, run.id).toBeDefined();
            expect(run.workflowName, run.id).toBe(wf?.name);
            expect(['running', 'completed', 'failed', 'cancelled']).toContain(run.status);
            expect(run.steps.length, run.id).toBeGreaterThan(0);
        }
    });

    it('seeded run status distribution: 1 running, 2 completed, 1 failed, 1 cancelled', () => {
        const seeded = seedWorkflows(NOW);
        const counts: Record<string, number> = {};
        for (const run of seeded.runs) {
            counts[run.status] = (counts[run.status] ?? 0) + 1;
        }
        expect(counts).toEqual({ running: 1, completed: 2, failed: 1, cancelled: 1 });
    });
});

describe('seedContext', () => {
    it('breakdown sums to used tokens within budget', () => {
        const context = seedContext();
        expect(context.usedTokens).toBeLessThanOrEqual(context.budgetTokens);
        const sum = context.breakdown.system + context.breakdown.history + context.breakdown.tools;
        expect(sum).toBe(context.usedTokens);
    });
});

describe('exportSessionMarkdown', () => {
    const at = NOW;
    const messages: ChatMessage[] = [
        { id: 'm1', kind: 'user', at, text: '把构建日志按失败原因归组。' },
        { id: 'm2', kind: 'assistant', at: at + 1, text: '**已完成** 归组，共 3 类。' },
        {
            id: 'm3',
            kind: 'approval',
            at: at + 2,
            approval: { id: 'ap-1', kind: 'process.execute', summary: '重跑失败用例', status: 'approved' },
        },
    ];

    it('starts with an H1 title', () => {
        const md = exportSessionMarkdown('构建日志整理', messages);
        expect(md.split('\n')[0]).toBe('# 构建日志整理');
    });

    it('emits user and assistant sections (H2) containing their texts', () => {
        const md = exportSessionMarkdown('构建日志整理', messages);
        const lines = md.split('\n');
        const userHeader = lines.find((l) => l.startsWith('## ') && l.includes('· 用户'));
        const assistantHeader = lines.find((l) => l.startsWith('## ') && l.includes('· Mirage'));
        expect(userHeader).toBeDefined();
        expect(assistantHeader).toBeDefined();
        expect(md).toContain('把构建日志按失败原因归组。');
        expect(md).toContain('**已完成** 归组，共 3 类。');
    });

    it('renders approvals as quote lines with kind, summary and verdict', () => {
        const md = exportSessionMarkdown('构建日志整理', messages);
        const quotes = md.split('\n').filter((l) => l.startsWith('> '));
        expect(quotes.length).toBeGreaterThanOrEqual(1);
        expect(quotes[0]).toContain('权限请求（process.execute）');
        expect(quotes[0]).toContain('重跑失败用例');
        expect(quotes[0]).toContain('已放行');
    });

    it('maps denial and pending verdicts distinctly', () => {
        const base = { id: 'ap-x', kind: 'filesystem.write' as const, summary: '移动安装包' };
        const denied = exportSessionMarkdown('t', [
            { id: 'm1', kind: 'approval', at, approval: { ...base, status: 'denied' } },
        ]);
        const pending = exportSessionMarkdown('t', [
            { id: 'm1', kind: 'approval', at, approval: { ...base, status: 'pending' } },
        ]);
        expect(denied).toContain('已拒止');
        expect(pending).toContain('待处理');
    });

    it('renders system notes and activity as quote lines', () => {
        const md = exportSessionMarkdown('t', [
            { id: 'm1', kind: 'system', at, tone: 'warn', text: '任务已取消' },
            { id: 'm2', kind: 'activity', at, taskId: 't-0107', progress: 'Cancelled', note: '用户取消' },
        ]);
        expect(md).toContain('> ');
        expect(md).toContain('任务已取消');
        expect(md).toContain('任务 t-0107：Cancelled（用户取消）');
    });
});

describe('capacity constants', () => {
    it('session and message caps are the documented bounds', () => {
        expect(MAX_SESSIONS).toBe(64);
        expect(MAX_MESSAGES_PER_SESSION).toBe(200);
    });
});
