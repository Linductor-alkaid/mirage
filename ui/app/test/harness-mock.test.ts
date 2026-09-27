/// harness-mock 模拟域：seedSessions 形状与上界、exportSessionMarkdown
/// 导出内容（H1、用户/assistant 小节、引用行）。工作流种子已随 M5-05
/// 编辑器接真实面移除（工作流域走 DEC-023 契约路径）。

import { describe, expect, it } from 'vitest';

import {
    MAX_MESSAGES_PER_SESSION,
    MAX_SESSIONS,
    exportSessionMarkdown,
    seedContext,
    seedSessions,
} from '../src/state/harness-mock.js';
import type { ChatMessage } from '../src/state/model.js';

const MESSAGE_KINDS: readonly ChatMessage['kind'][] = [
    'user',
    'assistant',
    'activity',
    'step',
    'snapshot',
    'workflow-call',
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

    it('s-weekly thread carries the workflow-call tool card (m-w0)', () => {
        const seeded = seedSessions(NOW);
        const thread = seeded.messages.get('s-weekly');
        expect(thread).toBeDefined();
        const call = thread?.find((m) => m.kind === 'workflow-call');
        expect(call).toBeDefined();
        if (call?.kind === 'workflow-call') {
            expect(call.call).toMatchObject({
                workflowId: 'wf-shot-report',
                workflowName: '截图周报生成',
                version: 'v2',
                runId: 'r-2397',
                status: 'completed',
            });
            expect(call.call.params).toEqual({ days: '7', limit: '6' });
        }
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

    it('renders workflow-call tool cards as quote lines with name, version and status', () => {
        const md = exportSessionMarkdown('周报会话', [
            {
                id: 'mw',
                kind: 'workflow-call',
                at,
                call: {
                    workflowId: 'wf-shot-report',
                    workflowName: '截图周报生成',
                    version: 'v2',
                    params: { days: '7', limit: '6' },
                    runId: 'r-2397',
                    status: 'completed',
                },
            },
        ]);
        const quotes = md.split('\n').filter((l) => l.startsWith('> '));
        expect(quotes.length).toBe(1);
        expect(quotes[0]).toContain('调用工作流');
        expect(quotes[0]).toContain('截图周报生成（v2）');
        expect(quotes[0]).toContain('→ completed');
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
