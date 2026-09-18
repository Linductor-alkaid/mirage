/// WorkflowAtom 目录与 MockWorkflowBackend（RPA 编辑器后端缝，模拟域）：
/// 目录有界且形状合法；草稿/发布/删除/运行/取消的内存语义。

import { describe, expect, it } from 'vitest';

import { ATOM_CATALOG, MockWorkflowBackend } from '../src/state/workflow-backend.js';
import type { AtomCategory } from '../src/state/workflow-backend.js';
import type { WorkflowDraftPayload } from '../src/state/workflow-backend.js';

const NOW = 1_758_240_000_000;

const CATEGORIES: readonly AtomCategory[] = [
    '文件',
    '命令',
    '桌面观察',
    '窗口与输入',
    '剪贴板',
    '流程控制',
    '子流程',
];

describe('ATOM_CATALOG', () => {
    it('is a bounded non-empty catalog with unique ids', () => {
        expect(ATOM_CATALOG.length).toBeGreaterThan(0);
        expect(ATOM_CATALOG.length).toBeLessThanOrEqual(64); // 有界目录，非开放集合
        expect(new Set(ATOM_CATALOG.map((a) => a.id)).size).toBe(ATOM_CATALOG.length);
    });

    it('covers all 7 categories with at least one atom each, and nothing outside', () => {
        for (const category of CATEGORIES) {
            const inCategory = ATOM_CATALOG.filter((a) => a.category === category);
            expect(inCategory.length, category).toBeGreaterThanOrEqual(1);
        }
        const known = new Set<string>(CATEGORIES);
        for (const atom of ATOM_CATALOG) {
            expect(known.has(atom.category), atom.id).toBe(true);
        }
    });

    it('availability enum and atom shape are well-formed', () => {
        for (const atom of ATOM_CATALOG) {
            expect(['available', 'planned'], atom.id).toContain(atom.availability);
            expect(atom.name.length, atom.id).toBeGreaterThan(0);
            expect(atom.description.length, atom.id).toBeGreaterThan(0);
            expect(atom.params.length, atom.id).toBeLessThanOrEqual(8); // 参数表有界
            for (const spec of atom.params) {
                expect(['text', 'number', 'select', 'boolean'], `${atom.id}:${spec.name}`).toContain(spec.type);
                expect(spec.name.length, atom.id).toBeGreaterThan(0);
                expect(spec.description.length, atom.id).toBeGreaterThan(0);
                if (spec.type === 'select') {
                    expect(spec.options?.length ?? 0, `${atom.id}:${spec.name}`).toBeGreaterThan(0);
                }
            }
        }
    });

    it('atom kind stays inside the WorkflowStepDef kind set (control included)', () => {
        for (const atom of ATOM_CATALOG) {
            expect(
                ['filesystem.read', 'process.execute', 'display.observe', 'control'],
                atom.id,
            ).toContain(atom.kind);
        }
        // 流程控制类原子必须落为 control kind
        for (const atom of ATOM_CATALOG.filter((a) => a.category === '流程控制')) {
            if (atom.id !== 'ctl.delay') {
                expect(atom.kind, atom.id).toBe('control');
            }
        }
    });
});

describe('MockWorkflowBackend', () => {
    function draftPayload(id: string, name = '草稿流程'): WorkflowDraftPayload {
        return {
            id,
            name,
            version: 'v1',
            description: '测试草稿',
            params: [],
            steps: [{ atomId: 'cmd.run', title: '执行命令', kind: 'process.execute', detail: 'echo hi' }],
        };
    }

    it('saveDraft creates a new unpublished draft with fresh-workflow defaults', async () => {
        const backend = new MockWorkflowBackend(NOW);
        expect((await backend.listDefs()).some((d) => d.id === 'wf-x')).toBe(false);

        const saved = await backend.saveDraft(draftPayload('wf-x', '我的流程'));

        expect(saved.published).toBe(false);
        expect(saved.name).toBe('我的流程');
        expect(saved.version).toBe('v1');
        expect(saved.steps).toHaveLength(1);
        expect(saved.successRate).toBe(1); // 新流程缺省满成功率
        expect(saved.lastRunAt).toBeUndefined();
        expect(saved.updatedAt).toBeGreaterThan(0);
        expect((await backend.listDefs()).some((d) => d.id === 'wf-x')).toBe(true);
    });

    it('saveDraft updates an existing def in place and flips it back to draft', async () => {
        const backend = new MockWorkflowBackend(NOW);
        const original = (await backend.listDefs()).find((d) => d.id === 'wf-shot-report');
        expect(original?.published).toBe(true);
        expect(original?.version).toBe('v2');

        const saved = await backend.saveDraft(draftPayload('wf-shot-report', '截图周报生成 PRO'));

        expect(saved.published).toBe(false); // 编辑即回草稿（保存强制 unpublished）
        expect(saved.name).toBe('截图周报生成 PRO');
        expect(saved.version).toBe('v2'); // 版本不受载荷影响，见下一用例
        expect(saved.lastRunAt).toBe(original?.lastRunAt); // 运行履历保留
        expect((await backend.listDefs()).filter((d) => d.id === 'wf-shot-report')).toHaveLength(1);
    });

    it('draft saves never change the version; publish is the only version carrier', async () => {
        const backend = new MockWorkflowBackend(NOW);

        // 更新：载荷携带过期/任意版本都不会回退既有定义的版本
        const stale = await backend.saveDraft({ ...draftPayload('wf-shot-report', '改名'), version: 'v9' });
        expect(stale.version).toBe('v2');

        // 发布是唯一的版本进位通道：v2 -> v3
        const published = await backend.publish('wf-shot-report');
        expect(published.version).toBe('v3');
        expect(published.published).toBe(true);

        // 发布后再保存草稿：版本保持 v3
        const after = await backend.saveDraft({ ...draftPayload('wf-shot-report', '再改'), version: 'v1' });
        expect(after.version).toBe('v3');
        expect(after.published).toBe(false);
    });

    it('publish bumps vN -> vN+1 and marks the def published', async () => {
        const backend = new MockWorkflowBackend(NOW);
        const published = await backend.publish('wf-shot-report');

        expect(published.version).toBe('v3');
        expect(published.published).toBe(true);
        const stored = (await backend.listDefs()).find((d) => d.id === 'wf-shot-report');
        expect(stored?.version).toBe('v3');
        expect(stored?.published).toBe(true);
    });

    it('publish rejects unknown ids', async () => {
        const backend = new MockWorkflowBackend(NOW);
        await expect(backend.publish('wf-nope')).rejects.toThrow(/not found/);
    });

    it('remove drops the def and tolerates unknown ids', async () => {
        const backend = new MockWorkflowBackend(NOW);
        await backend.remove('wf-downloads');
        expect((await backend.listDefs()).some((d) => d.id === 'wf-downloads')).toBe(false);
        await expect(backend.remove('wf-nope')).resolves.toBeUndefined();
    });

    it('run creates a pending running run and lists it first', async () => {
        const backend = new MockWorkflowBackend(NOW);
        const run = await backend.run('wf-shot-report');

        expect(run.status).toBe('running');
        expect(run.workflowId).toBe('wf-shot-report');
        expect(run.workflowName).toBe('截图周报生成');
        expect(run.steps).toHaveLength(4);
        expect(run.steps.every((s) => s.status === 'pending')).toBe(true);

        const runs = await backend.listRuns();
        expect(runs[0]?.id).toBe(run.id); // 最新在前
        expect(runs).toHaveLength(6); // seed 5 + 新 1
    });

    it('cancelRun terminalizes the running run and skips pending steps (idempotent)', async () => {
        const backend = new MockWorkflowBackend(NOW);
        const run = await backend.run('wf-daily-standup');

        await backend.cancelRun(run.id);
        let stored = (await backend.listRuns()).find((r) => r.id === run.id);
        expect(stored?.status).toBe('cancelled');
        expect(stored?.steps.every((s) => s.status === 'skipped')).toBe(true);

        await expect(backend.cancelRun(run.id)).resolves.toBeUndefined(); // 终态幂等
        stored = (await backend.listRuns()).find((r) => r.id === run.id);
        expect(stored?.status).toBe('cancelled');
    });

    it('cancelRun tolerates unknown ids', async () => {
        const backend = new MockWorkflowBackend(NOW);
        await expect(backend.cancelRun('r-nope')).resolves.toBeUndefined();
    });

    it('atomCatalog serves the constant catalog; listDefs/listRuns are snapshots', async () => {
        const backend = new MockWorkflowBackend(NOW);
        expect(await backend.atomCatalog()).toBe(ATOM_CATALOG);

        const defsA = await backend.listDefs();
        const defsB = await backend.listDefs();
        expect(defsA).not.toBe(defsB); // 快照数组，不共享引用
        expect(defsA).toEqual(defsB);

        const runsA = await backend.listRuns();
        const runsB = await backend.listRuns();
        expect(runsA).not.toBe(runsB);
        expect(runsA).toEqual(runsB);
    });
});
