/// Behaviour tests for the mock service's workflow face (DEC-023/DEC-024):
/// the save → list → publish → run → runs → cancel lifecycle, the W-04
/// draft-run rejection, the W-02 fail-closed verification outcome, the
/// delete guards, and `workflow.run_updated` event delivery through the
/// standard event surface. The mock simulates observable wire behaviour
/// only — structural IR validation stays the real service's job.

import { afterEach, describe, expect, it } from 'vitest';
import { createMockTransport } from '../src/mock/mock-service.js';
import type { MockMirageService, MockServiceOptions } from '../src/mock/mock-service.js';
import { EventSequencer } from '../src/events.js';
import type { WorkflowDefinition, WorkflowStartInput } from '../src/transport.js';
import type { WorkflowSummary } from '../src/types.js';
import { expectIpcError } from './helpers.js';

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

const WORKFLOW_ID = '0f9e8d7c6b5a4938271605948372615a';
const STEP_ID = '0f9e8d7c6b5a4938271605948372615b';

function readDefinition(): WorkflowDefinition {
    return {
        schema_version: { major: 1, minor: 0 },
        workflow_id: WORKFLOW_ID,
        name: '读取文本',
        summary: 'demo',
        parameters: [],
        steps: [
            {
                step_id: STEP_ID,
                name: 'read',
                kind: 'tool_call',
                arguments: { tool: 'desktop.filesystem.read_text', path: 'C:\\notes.txt' },
            },
        ],
        default_policy: 'strict',
        allowed_policies: ['strict', 'dry_run'],
    };
}

async function settleRun(
    transport: ReturnType<typeof createMockTransport>['transport'],
    runId: string,
    state: string,
): Promise<void> {
    for (let waited = 0; waited < 2000; waited += 10) {
        const runs = await transport.listWorkflowRuns();
        const run = runs.find((r) => r.run_id === runId);
        if (run !== undefined && run.state === state) {
            return;
        }
        await new Promise((resolve) => setTimeout(resolve, 10));
    }
    throw new Error(`run ${runId} did not reach ${state} within the wait budget`);
}

describe('workflow face', () => {
    it('serves the M1 reference binding catalog (two atoms, side-effect flag set on process)', async () => {
        const { transport } = makeService({ hostStartDelayMs: 0 });
        const tools = await transport.workflowAtomCatalog();
        expect(tools.map((t) => t.wire_name)).toEqual([
            'desktop.filesystem.read_text',
            'desktop.process.execute',
        ]);
        expect(tools[0]?.has_side_effects).toBe(false);
        expect(tools[1]?.has_side_effects).toBe(true);
    });

    it('saves drafts as not_validated and publishes through the gate as runnable', async () => {
        const { transport } = makeService({ hostStartDelayMs: 0 });
        const saved = await transport.saveWorkflow(readDefinition());
        expect(saved.workflow_id).toBe(WORKFLOW_ID);
        expect(saved.digest).toMatch(/^[0-9a-f]{64}$/);

        const findSaved = async (): Promise<WorkflowSummary> => {
            const workflows = await transport.listWorkflows();
            const entry = workflows.find((w) => w.workflow_id === WORKFLOW_ID);
            expect(entry, 'saved workflow in workflow.list').toBeDefined();
            return entry!;
        };

        expect(await findSaved()).toMatchObject({
            workflow_id: WORKFLOW_ID,
            name: '读取文本',
            validation: 'not_validated',
            runnable: false,
        });

        const published = await transport.publishWorkflow(readDefinition());
        expect(published.idempotent).toBe(false);
        expect(await findSaved()).toMatchObject({ validation: 'dry_run_passed', runnable: true });

        // Same content again: idempotent publish (content-addressed — W-03).
        const again = await transport.publishWorkflow(readDefinition());
        expect(again.idempotent).toBe(true);
        expect(again.digest).toBe(published.digest);
    });

    it('rejects malformed definitions and the byte budget with stable errors', async () => {
        const { transport } = makeService({ hostStartDelayMs: 0 });
        await expectIpcError(() => transport.saveWorkflow({ workflow_id: 'nope' }), 'invalid_argument');
        const oversized: WorkflowDefinition = {
            ...readDefinition(),
            name: 'x'.repeat(256 * 1024 + 1),
        };
        await expectIpcError(() => transport.saveWorkflow(oversized), 'invalid_argument');
    });

    it('refuses runs on drafts (W-04 passthrough) and drives published runs to completion', async () => {
        const { transport } = makeService({ hostStartDelayMs: 0, stepDurationMs: 15 });
        const saved = await transport.saveWorkflow(readDefinition());
        const draft: WorkflowStartInput = { workflow_id: WORKFLOW_ID, digest: saved.digest };
        await expectIpcError(() => transport.startWorkflowRun(draft), 'invalid_state');

        await transport.publishWorkflow(readDefinition());
        const started = await transport.startWorkflowRun({ workflow_id: WORKFLOW_ID });
        expect(started.run_id).toMatch(/^[0-9a-f]{32}$/);

        const runs = await transport.listWorkflowRuns();
        expect(runs).toHaveLength(1);
        expect(runs[0]).toMatchObject({ run_id: started.run_id, workflow_id: WORKFLOW_ID });
        await settleRun(transport, started.run_id, 'completed');
    });

    it('fails runs whose side-effect verification parameters are unbound (W-02 outcome)', async () => {
        const { transport } = makeService({ hostStartDelayMs: 0, stepDurationMs: 15 });
        const sideEffect: WorkflowDefinition = {
            ...readDefinition(),
            steps: [
                {
                    step_id: STEP_ID,
                    name: 'run',
                    kind: 'tool_call',
                    arguments: { tool: 'desktop.process.execute', command: 'echo hi' },
                    verification: { signal: 'run_parameter:confirmed', op: 'eq', value: true },
                },
            ],
            parameters: [{ name: 'confirmed', type: 'boolean', required: false }],
        };
        await transport.publishWorkflow(sideEffect);
        const unbound = await transport.startWorkflowRun({ workflow_id: WORKFLOW_ID });
        await settleRun(transport, unbound.run_id, 'failed');

        const bound = await transport.startWorkflowRun({
            workflow_id: WORKFLOW_ID,
            parameters: { confirmed: true },
        });
        await settleRun(transport, bound.run_id, 'completed');
    });

    it('cancels a driving run and answers idempotently on terminal runs', async () => {
        const { transport } = makeService({ hostStartDelayMs: 0, stepDurationMs: 5000 });
        await transport.publishWorkflow(readDefinition());
        const started = await transport.startWorkflowRun({ workflow_id: WORKFLOW_ID });
        const cancelled = await transport.cancelWorkflowRun(started.run_id);
        expect(cancelled).toMatchObject({ run_id: started.run_id, state: 'cancelled' });
        // Idempotent receipt on the settled run (pinned cancel_run semantics).
        const again = await transport.cancelWorkflowRun(started.run_id);
        expect(again.state).toBe('cancelled');
    });

    it('guards delete: unknown id is not_found, non-terminal runs are invalid_state', async () => {
        const { transport } = makeService({ hostStartDelayMs: 0, stepDurationMs: 5000 });
        await expectIpcError(() => transport.deleteWorkflow(WORKFLOW_ID), 'not_found');
        await transport.publishWorkflow(readDefinition());
        const started = await transport.startWorkflowRun({ workflow_id: WORKFLOW_ID });
        await expectIpcError(() => transport.deleteWorkflow(WORKFLOW_ID), 'invalid_state');
        await transport.cancelWorkflowRun(started.run_id);
        const deleted = await transport.deleteWorkflow(WORKFLOW_ID);
        expect(deleted.workflow_id).toBe(WORKFLOW_ID);
        // The catalog entry is gone; the seeded entries remain untouched.
        const workflows = await transport.listWorkflows();
        expect(workflows.find((w) => w.workflow_id === WORKFLOW_ID)).toBeUndefined();
    });

    it('delivers workflow.run_updated events through the subscribed event stream', async () => {
        const { transport } = makeService({ hostStartDelayMs: 0, stepDurationMs: 15 });
        const sequencer = new EventSequencer();
        const seen: string[] = [];
        await transport.subscribe((event) => {
            const verdict = sequencer.push(event);
            expect(verdict.kind).toBe('delivered');
            if (event.event === 'workflow.run_updated') {
                seen.push(event.state);
            }
        });
        await transport.publishWorkflow(readDefinition());
        const started = await transport.startWorkflowRun({ workflow_id: WORKFLOW_ID });
        await settleRun(transport, started.run_id, 'completed');
        expect(seen[0]).toBe('running');
        expect(seen).toContain('completed');
        await transport.unsubscribe();
    });
});
