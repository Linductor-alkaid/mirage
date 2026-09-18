/// HarnessStore 纯函数面：hash 路由解析/生成往返、settings 分类白名单回退、
/// 展示步骤合并（displayStepsOf）、任务终态判定（isTerminalProgress）。
/// 纯 Node 环境：不构造 HarnessStore（其构造读取 window）。

import { describe, expect, it } from 'vitest';
import type { InspectTask, StepView, TaskProgress } from '@mirage/contracts';

import {
    SETTINGS_CATEGORIES,
    displayStepsOf,
    isTerminalProgress,
    parseRoute,
    routeToHash,
} from '../src/state/store.js';
import type { Route, SubmitStepInput } from '../src/state/store.js';

// ---- fixtures -------------------------------------------------------------

let opSeq = 0;
function stepOf(partial: Partial<StepView> & Pick<StepView, 'index'>): StepView {
    opSeq += 1;
    return {
        kind: 'filesystem.read',
        status: 'ok',
        operation_id: `op-fix-${opSeq}`,
        permission: 'allowed',
        ok: true,
        exit_code: 0,
        result: '',
        result_truncated: false,
        error: '',
        ...partial,
    };
}

function taskOf(progress: TaskProgress, steps: StepView[]): InspectTask {
    return {
        id: 't-fix',
        goal: 'fixture goal',
        progress,
        has_success: progress === 'Completed',
        steps,
    };
}

// ---- parseRoute -----------------------------------------------------------

describe('parseRoute', () => {
    const CASES: readonly { hash: string; route: Route }[] = [
        { hash: '#/chat', route: { view: 'chat' } },
        { hash: '#/chat/s-abc', route: { view: 'chat', sessionId: 's-abc' } },
        { hash: '#/chat/', route: { view: 'chat' } },
        { hash: '#/chat/a%20b%2Fc', route: { view: 'chat', sessionId: 'a b/c' } },
        { hash: '#/chat/s-1/extra', route: { view: 'chat', sessionId: 's-1' } },
        { hash: '#/workflows', route: { view: 'workflows' } },
        { hash: '#/workflows/wf-shot', route: { view: 'workflow-editor', workflowId: 'wf-shot' } },
        { hash: '#/workflows/wf-shot/runs', route: { view: 'workflow-editor', workflowId: 'wf-shot' } },
        {
            hash: '#/workflows/wf-shot/runs/r-2401',
            route: { view: 'workflow-run', workflowId: 'wf-shot', runId: 'r-2401' },
        },
        {
            hash: '#/workflows/wf-shot/runs/r-2401/extra',
            route: { view: 'workflow-run', workflowId: 'wf-shot', runId: 'r-2401' },
        },
        { hash: '#/resources', route: { view: 'resources' } },
        { hash: '#/settings', route: { view: 'settings', category: 'general' } },
        { hash: '#/settings/', route: { view: 'settings', category: 'general' } },
        { hash: '#/settings/appearance', route: { view: 'settings', category: 'appearance' } },
        { hash: '#/settings/appearance/extra', route: { view: 'settings', category: 'appearance' } },
        // 白名单外（大小写敏感、任意串）回退 general
        { hash: '#/settings/bogus', route: { view: 'settings', category: 'general' } },
        { hash: '#/settings/Appearance', route: { view: 'settings', category: 'general' } },
        { hash: '#/settings/..', route: { view: 'settings', category: 'general' } },
        // 未知/空路径回退 chat
        { hash: '', route: { view: 'chat' } },
        { hash: '#', route: { view: 'chat' } },
        { hash: '#/', route: { view: 'chat' } },
        { hash: '#/nope/x/y', route: { view: 'chat' } },
    ];

    for (const { hash, route } of CASES) {
        it(`parseRoute('${hash || '<empty>'}') -> ${JSON.stringify(route)}`, () => {
            expect(parseRoute(hash)).toEqual(route);
        });
    }

    it('whitelists exactly the 8 settings categories', () => {
        expect(SETTINGS_CATEGORIES).toEqual([
            'general',
            'appearance',
            'models',
            'memory',
            'skills',
            'mcp',
            'permissions',
            'runtime',
        ]);
    });

    for (const category of SETTINGS_CATEGORIES) {
        it(`accepts whitelisted category '${category}'`, () => {
            expect(parseRoute(`#/settings/${category}`)).toEqual({
                view: 'settings',
                category,
            });
        });
    }
});

// ---- routeToHash 与往返 -----------------------------------------------------

describe('routeToHash / round-trip', () => {
    const CANONICAL: readonly { route: Route; hash: string }[] = [
        { route: { view: 'chat' }, hash: '#/chat' },
        { route: { view: 'chat', sessionId: 's-1' }, hash: '#/chat/s-1' },
        { route: { view: 'chat', sessionId: 'a b/c' }, hash: '#/chat/a%20b%2Fc' },
        { route: { view: 'workflows' }, hash: '#/workflows' },
        { route: { view: 'workflow-editor', workflowId: 'wf a' }, hash: '#/workflows/wf%20a' },
        {
            route: { view: 'workflow-run', workflowId: 'wf a', runId: 'r/1' },
            hash: '#/workflows/wf%20a/runs/r%2F1',
        },
        { route: { view: 'resources' }, hash: '#/resources' },
        ...SETTINGS_CATEGORIES.map((category) => ({
            route: { view: 'settings', category } as Route,
            hash: `#/settings/${category}`,
        })),
    ];

    it('routeToHash serializes each canonical route', () => {
        for (const { route, hash } of CANONICAL) {
            expect(routeToHash(route), JSON.stringify(route)).toBe(hash);
        }
    });

    it('parseRoute ∘ routeToHash is identity on canonical routes', () => {
        for (const { route } of CANONICAL) {
            expect(parseRoute(routeToHash(route)), JSON.stringify(route)).toEqual(route);
        }
    });

    it('routeToHash ∘ parseRoute is identity on canonical hashes', () => {
        for (const { hash } of CANONICAL) {
            expect(routeToHash(parseRoute(hash))).toBe(hash);
        }
    });
});

// ---- displayStepsOf ---------------------------------------------------------

describe('displayStepsOf', () => {
    it('without inputs every displayed argument defaults to empty string', () => {
        const detail = taskOf('Active', [stepOf({ index: 0 }), stepOf({ index: 1 })]);
        const display = displayStepsOf(detail);
        expect(display.map((s) => s.argument)).toEqual(['', '']);
        expect(display.map((s) => s.operationId)).toEqual(detail.steps.map((s) => s.operation_id));
        expect(display.map((s) => s.index)).toEqual([0, 1]);
    });

    it('merges local submit inputs by step index', () => {
        const detail = taskOf('Active', [
            stepOf({ index: 0, status: 'ok' }),
            stepOf({ index: 1, status: 'running' }),
            stepOf({ index: 2, status: 'pending' }),
        ]);
        const inputs: SubmitStepInput[] = [
            { kind: 'filesystem.read', argument: 'ci/logs/nightly.log' },
            { kind: 'process.execute', argument: 'npm test' },
        ];
        const display = displayStepsOf(detail, inputs);
        expect(display.map((s) => s.argument)).toEqual(['ci/logs/nightly.log', 'npm test', '']);
        expect(display.map((s) => s.status)).toEqual(['ok', 'running', 'pending']);
    });

    it('keys the input lookup on step.index, not on array position', () => {
        const swapped = [stepOf({ index: 1 }), stepOf({ index: 0 })];
        const inputs: SubmitStepInput[] = [
            { kind: 'filesystem.read', argument: 'zero' },
            { kind: 'filesystem.read', argument: 'one' },
        ];
        const display = displayStepsOf(taskOf('Active', swapped), inputs);
        expect(display.map((s) => s.argument)).toEqual(['one', 'zero']);
    });

    it('inputs shorter than steps leave the tail arguments empty', () => {
        const detail = taskOf('Active', [
            stepOf({ index: 0 }),
            stepOf({ index: 1 }),
            stepOf({ index: 2 }),
        ]);
        const display = displayStepsOf(detail, [{ kind: 'filesystem.read', argument: 'only-first' }]);
        expect(display.map((s) => s.argument)).toEqual(['only-first', '', '']);
    });

    it('carries terminal fields (exit code / result / error / truncation) through', () => {
        const detail = taskOf('Failed', [
            stepOf({
                index: 0,
                status: 'failed',
                exit_code: 3,
                error: 'exit 3',
                result: 'partial output',
                result_truncated: true,
                kind: 'process.execute',
            }),
        ]);
        const display = displayStepsOf(detail, [{ kind: 'process.execute', argument: 'boom' }]);
        expect(display).toHaveLength(1);
        expect(display[0]).toMatchObject({
            index: 0,
            kind: 'process.execute',
            status: 'failed',
            operationId: detail.steps[0]?.operation_id,
            argument: 'boom',
            exitCode: 3,
            result: 'partial output',
            resultTruncated: true,
            error: 'exit 3',
        });
    });
});

// ---- isTerminalProgress ------------------------------------------------------

describe('isTerminalProgress', () => {
    for (const progress of ['Completed', 'Failed', 'Cancelled'] as const) {
        it(`'${progress}' is terminal`, () => {
            expect(isTerminalProgress(progress)).toBe(true);
        });
    }

    for (const progress of ['Idle', 'Active', 'Paused', 'Cancelling', 'Unknown'] as const) {
        it(`'${progress}' is not terminal`, () => {
            expect(isTerminalProgress(progress)).toBe(false);
        });
    }

    it('accepts plain strings (defensive signature)', () => {
        expect(isTerminalProgress('completed')).toBe(false);
        expect(isTerminalProgress('Completed')).toBe(true);
    });
});
