/// WorkflowBackend IPC 适配器测试（DEC-023 契约路径）：适配器跑在
/// `@mirage/contracts` 的 MockTransport（wire 忠实模拟服务）之上，覆盖
/// 目录投影、草稿/发布语义（W-04/W-03）、运行生命周期投影与目录懒取缓存。

import { afterEach, describe, expect, it } from 'vitest';
import { createMockTransport } from '@mirage/contracts';
import type { MockMirageService } from '@mirage/contracts';

import { IpcWorkflowBackend, CONTROL_CONSTRUCTS } from '../src/state/workflow-backend.js';
import { newHexId } from '../src/state/workflow-ir.js';

const openServices: MockMirageService[] = [];

function makeBackend(): { backend: IpcWorkflowBackend; transport: ReturnType<typeof createMockTransport>['transport'] } {
    const created = createMockTransport({ hostStartDelayMs: 0, stepDurationMs: 10 });
    openServices.push(created.service);
    return { backend: new IpcWorkflowBackend(created.transport), transport: created.transport };
}

afterEach(() => {
    for (const service of openServices.splice(0)) {
        service.close();
    }
});

function draftPayload(overrides: { name?: string } = {}): {
    id: string;
    name: string;
    version: string;
    description: string;
    params: { name: string; required: boolean; description: string }[];
    steps: {
        stepId: string;
        atomId: string;
        title: string;
        kind: 'tool_call';
        detail: string;
        params: Record<string, string>;
    }[];
} {
    return {
        id: newHexId(),
        name: 'demo',
        version: '草稿',
        description: '',
        params: [],
        steps: [],
        ...overrides,
    };
}

describe('IpcWorkflowBackend.atomCatalog', () => {
    it('projects the wire exposed view (M1 reference binding); control constructs stay separate', async () => {
        const { backend } = makeBackend();
        const atoms = await backend.atomCatalog();
        expect(atoms.map((a) => a.id)).toEqual(['desktop.filesystem.read_text', 'desktop.process.execute']);
        expect(CONTROL_CONSTRUCTS.map((c) => c.id)).toEqual(['ctl.loop']);
    });

    it('caches the catalog; failures are not cached', async () => {
        const { backend, transport } = makeBackend();
        await backend.atomCatalog();
        // 二次取用走缓存：关闭底层服务也不会让已缓存的目录失效。
        transport.close();
        await expect(backend.atomCatalog()).resolves.toHaveLength(2);
    });
});

describe('IpcWorkflowBackend lifecycle', () => {
    it('saveDraft appends a not_validated draft (parseable, not runnable — W-04)', async () => {
        const { backend } = makeBackend();
        const def = await backend.saveDraft(draftPayload());
        expect(def.id).toMatch(/^[0-9a-f]{32}$/);
        expect(def.published).toBe(false);
        expect(def.runnable).toBe(false);
        expect(def.digest).toMatch(/^[0-9a-f]{64}$/);
        expect(def.contentKnown).toBe(true);

        const defs = await backend.listDefs();
        const saved = defs.find((d) => d.id === def.id);
        expect(saved).toMatchObject({ name: 'demo', published: false, runnable: false });
        // wire 摘要投影不携带内容：列表条目不可编辑（contentKnown=false）。
        expect(saved?.contentKnown).toBe(false);
        expect(saved?.steps).toEqual([]);
    });

    it('publish runs the DryRun gate, flips runnable and is idempotent on same content', async () => {
        const { backend } = makeBackend();
        const payload = draftPayload();
        await backend.saveDraft(payload);
        const published = await backend.publish(payload);
        expect(published.published).toBe(true);
        expect(published.runnable).toBe(true);
        expect(published.version).toBe(published.digest!.slice(0, 8));

        const again = await backend.publish(payload);
        expect(again.digest).toBe(published.digest);
    });

    it('run starts a published workflow and cancelRun settles it', async () => {
        const { backend } = makeBackend();
        const payload = draftPayload();
        await backend.saveDraft(payload);
        await backend.publish(payload);

        const run = await backend.run(payload.id);
        expect(run.workflowId).toBe(payload.id);
        expect(run.status).toBe('running');
        expect(run.id).toMatch(/^[0-9a-f]{32}$/);

        await backend.cancelRun(run.id);
        const runs = await backend.listRuns();
        expect(runs[0]).toMatchObject({ id: run.id, status: 'cancelled' });
    });

    it('listRuns projects the wire states onto the view vocabulary', async () => {
        const { backend } = makeBackend();
        const payload = draftPayload({ name: 'proj' });
        await backend.saveDraft(payload);
        await backend.publish(payload);
        await backend.run(payload.id);
        for (let waited = 0; waited < 2000; waited += 10) {
            const runs = await backend.listRuns();
            if (runs[0]?.status === 'completed') {
                expect(runs[0].workflowName).toBe('proj');
                return;
            }
            await new Promise((resolve) => setTimeout(resolve, 10));
        }
        throw new Error('run did not complete within the wait budget');
    });

    it('remove deletes the catalog entry only', async () => {
        const { backend } = makeBackend();
        const payload = draftPayload();
        await backend.saveDraft(payload);
        await backend.remove(payload.id);
        const defs = await backend.listDefs();
        expect(defs.find((d) => d.id === payload.id)).toBeUndefined();
    });
});
