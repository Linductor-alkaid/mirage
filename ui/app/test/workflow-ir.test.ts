/// Workflow IR v1 映射测试（DEC-013 对齐验收）：编辑器模型 → pinned
/// `workflow_ir.hpp` IR v1 JSON 的换算纪律——封闭词汇、{"$param"} 引用、
/// 结构化谓词、W-02 副作用验证谓词（DEC-024 决策 5）与 loop_head 回跳标注。

import { describe, expect, it } from 'vitest';

import type { WorkflowDef } from '../src/state/model.js';
import { CONTROL_CONSTRUCTS, atomFromExposedTool } from '../src/state/workflow-backend.js';
import { derivedVerificationParams, irToWorkflowDef, isSideEffectStep, predicateValue, workflowDefToIr } from '../src/state/workflow-ir.js';

const ATOMS = new Map(
    [
        {
            wire_name: 'desktop.filesystem.read_text',
            version: '1.0.0',
            description: 'Reads a UTF-8 text file.',
            has_side_effects: false,
            parameters_schema: {
                type: 'object',
                properties: { path: { type: 'string', description: 'File path' } },
                required: ['path'],
            },
        },
        {
            wire_name: 'desktop.process.execute',
            version: '1.0.0',
            description: 'Executes a command line.',
            has_side_effects: true,
            parameters_schema: {
                type: 'object',
                properties: {
                    command: { type: 'string', description: 'Command line' },
                    timeout_ms: { type: 'integer', minimum: 1000, maximum: 120000 },
                },
                required: ['command'],
            },
        },
    ].map((tool) => [tool.wire_name, atomFromExposedTool(tool)]),
);

function baseDef(overrides: Partial<WorkflowDef> = {}): WorkflowDef {
    return {
        id: '0f9e8d7c6b5a4938271605948372615a',
        name: 'demo',
        version: 'abcd1234',
        description: '演示流程',
        params: [{ name: 'path', required: true, description: '文件路径' }],
        steps: [],
        successRate: 1,
        published: false,
        updatedAt: 0,
        runnable: false,
        contentKnown: true,
        ...overrides,
    };
}

describe('atomFromExposedTool', () => {
    it('projects wire name, label, category, schema params and the side-effect flag', () => {
        const atom = atomFromExposedTool({
            wire_name: 'desktop.clipboard.write_text',
            version: '1.0.0',
            description: 'Writes text to the clipboard.',
            has_side_effects: true,
            parameters_schema: {
                type: 'object',
                properties: { text: { type: 'string', description: 'Text' } },
                required: ['text'],
            },
        });
        expect(atom.id).toBe('desktop.clipboard.write_text');
        expect(atom.name).toBe('写入剪贴板');
        expect(atom.category).toBe('剪贴板');
        expect(atom.kind).toBe('tool_call');
        expect(atom.hasSideEffects).toBe(true);
        expect(atom.params).toEqual([
            { name: 'text', type: 'text', required: true, description: 'Text' },
        ]);
    });

    it('falls back to the wire name and 其他 category for unknown atoms', () => {
        const atom = atomFromExposedTool({
            wire_name: 'desktop.custom.thing',
            version: '2.0.0',
            description: '',
            has_side_effects: false,
            parameters_schema: {},
        });
        expect(atom.name).toBe('desktop.custom.thing');
        expect(atom.category).toBe('其他');
    });

    it('maps integer schema params to number inputs', () => {
        const atom = ATOMS.get('desktop.process.execute');
        expect(atom?.params.find((p) => p.name === 'timeout_ms')).toMatchObject({ type: 'number', required: false });
    });
});

describe('workflowDefToIr', () => {
    it('builds a minimal IR v1 document with closed vocabulary', () => {
        const def = baseDef({
            steps: [
                {
                    stepId: 'b1000000000000000000000000000001',
                    atomId: 'desktop.filesystem.read_text',
                    title: '读取',
                    kind: 'tool_call',
                    detail: '',
                    params: { path: '{"$path"}' },
                },
            ],
        });
        const ir = workflowDefToIr(def, ATOMS);
        expect(ir).toMatchObject({
            schema_version: { major: 1, minor: 0 },
            workflow_id: def.id,
            name: 'demo',
            summary: '演示流程',
            default_policy: 'strict',
            allowed_policies: ['strict', 'dry_run'],
        });
        expect(ir.steps).toEqual([
            {
                step_id: 'b1000000000000000000000000000001',
                name: '读取',
                kind: 'tool_call',
                arguments: { tool: 'desktop.filesystem.read_text', path: { $param: 'path' } },
            },
        ]);
        expect(ir.parameters).toEqual([{ name: 'path', type: 'string', required: true, summary: '文件路径' }]);
    });

    it('adds the W-02 verification predicate and its derived parameter for side-effect atoms', () => {
        const stepId = 'b1000000000000000000000000000042';
        const def = baseDef({
            steps: [
                {
                    stepId,
                    atomId: 'desktop.process.execute',
                    title: '执行',
                    kind: 'tool_call',
                    detail: '',
                    params: { command: 'echo hi', timeout_ms: '30000' },
                },
            ],
        });
        const ir = workflowDefToIr(def, ATOMS);
        expect(isSideEffectStep(def.steps[0]!, ATOMS)).toBe(true);
        expect(ir.steps).toEqual([
            {
                step_id: stepId,
                name: '执行',
                kind: 'tool_call',
                arguments: { tool: 'desktop.process.execute', command: 'echo hi', timeout_ms: 30000 },
                verification: { signal: 'run_parameter:ok_b10000', op: 'eq', value: true },
            },
        ]);
        // 派生参数：可选 boolean、无默认值 → 发布 DryRun（空参绑定）NotEvaluable 通过。
        expect(ir.parameters).toEqual([
            { name: 'path', type: 'string', required: true, summary: '文件路径' },
            {
                name: 'ok_b10000',
                type: 'boolean',
                required: false,
                summary: '步骤「执行」副作用验证谓词绑定（运行时置 true 以通过验证）',
            },
        ]);
        expect(derivedVerificationParams(def.steps, ATOMS)).toHaveLength(1);
    });

    it('marks the first step loop_head and emits the control jump for the loop construct', () => {
        const def = baseDef({
            params: [],
            steps: [
                {
                    stepId: 'b1000000000000000000000000000001',
                    atomId: 'desktop.filesystem.read_text',
                    title: '读取',
                    kind: 'tool_call',
                    detail: '',
                    params: { path: 'C:\\a.txt' },
                },
                {
                    stepId: 'b1000000000000000000000000000002',
                    atomId: 'ctl.loop',
                    title: '循环回跳',
                    kind: 'control',
                    detail: '',
                    loopMax: 5,
                },
            ],
        });
        const atoms = new Map([...ATOMS, ...CONTROL_CONSTRUCTS.map((c) => [c.id, c] as const)]);
        const ir = workflowDefToIr(def, atoms);
        expect(ir.steps).toEqual([
            {
                step_id: 'b1000000000000000000000000000001',
                name: '读取',
                kind: 'tool_call',
                loop_head: true,
                arguments: { tool: 'desktop.filesystem.read_text', path: 'C:\\a.txt' },
            },
            {
                step_id: 'b1000000000000000000000000000002',
                name: '循环回跳',
                kind: 'control',
                jump_to: 'b1000000000000000000000000000001',
                max_iterations: 5,
            },
        ]);
        expect(ir.parameters).toEqual([]);
    });

    it('emits structured preconditions and skips the jump when the loop is the first step', () => {
        const def = baseDef({
            params: [],
            steps: [
                {
                    stepId: 'b1000000000000000000000000000001',
                    atomId: 'desktop.filesystem.read_text',
                    title: '读取',
                    kind: 'tool_call',
                    detail: '',
                    params: { path: 'C:\\a.txt' },
                    skipIf: { signal: 'run_parameter:skip', op: 'eq', value: 'true' },
                },
                {
                    stepId: 'b1000000000000000000000000000002',
                    atomId: 'ctl.loop',
                    title: '循环回跳',
                    kind: 'control',
                    detail: '',
                    loopMax: 3,
                },
                {
                    stepId: 'b1000000000000000000000000000003',
                    atomId: 'desktop.filesystem.read_text',
                    title: '读取 2',
                    kind: 'tool_call',
                    detail: '',
                    params: { path: 'C:\\b.txt' },
                },
            ],
        });
        const ir = workflowDefToIr(def, ATOMS);
        const steps = ir.steps as Record<string, unknown>[];
        expect(steps[0]).toMatchObject({
            precondition: { signal: 'run_parameter:skip', op: 'eq', value: true },
        });
        expect(steps[1]).toMatchObject({ kind: 'control', max_iterations: 3, jump_to: steps[0]!.step_id });
    });

    it('parses predicate scalars: true/false/null, numbers, plain strings, exists → null', () => {
        expect(predicateValue('eq', 'true')).toBe(true);
        expect(predicateValue('eq', 'false')).toBe(false);
        expect(predicateValue('eq', 'null')).toBe(null);
        expect(predicateValue('le', '3.5')).toBe(3.5);
        expect(predicateValue('eq', ' hello ')).toBe('hello');
        expect(predicateValue('exists', '')).toBe(null);
    });
});

describe('irToWorkflowDef (DEC-026 definition read face)', () => {
    it('rebuilds an editable def from the IR the write path produced (round trip)', () => {
        const atomsById = new Map([
            ...CONTROL_CONSTRUCTS.map((a) => [a.id, a] as const),
        ]);
        const atom = atomFromExposedTool({
            wire_name: 'desktop.filesystem.read_text',
            version: '1.0.0',
            description: 'read',
            has_side_effects: false,
            parameters_schema: {
                type: 'object',
                properties: { path: { type: 'string', description: 'path' } },
                required: ['path'],
            },
        });
        atomsById.set(atom.id, atom);

        const def: WorkflowDef = {
            id: 'aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa',
            name: 'round-trip',
            version: '草稿',
            description: 'desc',
            params: [{ name: 'path', required: true, description: '目标路径', type: 'string' }],
            steps: [
                {
                    stepId: '11111111111111111111111111111111',
                    atomId: 'desktop.filesystem.read_text',
                    title: 'readme',
                    kind: 'tool_call',
                    detail: '',
                    params: { path: 'README.md' },
                    skipIf: { signal: 'run_parameter:skip', op: 'eq', value: 'true' },
                },
                {
                    stepId: '22222222222222222222222222222222',
                    atomId: 'ctl.loop',
                    title: 'loop',
                    kind: 'control',
                    detail: '',
                    loopMax: 4,
                },
            ],
            successRate: 1,
            published: false,
            updatedAt: 0,
            runnable: false,
            contentKnown: true,
        };
        const ir = workflowDefToIr(def, atomsById);
        const view = { workflow_id: def.id, digest: 'ab'.repeat(32), definition: ir };
        const rebuilt = irToWorkflowDef(view);

        expect(rebuilt.id).toBe(def.id);
        expect(rebuilt.name).toBe(def.name);
        expect(rebuilt.description).toBe(def.description);
        expect(rebuilt.digest).toBe('ab'.repeat(32));
        expect(rebuilt.contentKnown).toBe(true);
        // 摘要投影字段以 workflow.list 为准，读取面不伪造。
        expect(rebuilt.published).toBe(false);
        expect(rebuilt.runnable).toBe(false);
        expect(rebuilt.updatedAt).toBe(0);

        // 参数：IR 参数原样回读（读取步骤无副作用，本例无派生谓词参数）。
        const names = rebuilt.params.map((p) => p.name);
        expect(names).toContain('path');

        // 步骤：id / 标题 / 参数 / 谓词 / 回跳上限逐一还原。
        expect(rebuilt.steps).toHaveLength(2);
        const first = rebuilt.steps[0]!;
        expect(first.stepId).toBe('11111111111111111111111111111111');
        expect(first.atomId).toBe('desktop.filesystem.read_text');
        expect(first.params).toEqual({ path: 'README.md' });
        expect(first.skipIf).toEqual({ signal: 'run_parameter:skip', op: 'eq', value: 'true' });
        const second = rebuilt.steps[1]!;
        expect(second.kind).toBe('control');
        expect(second.loopMax).toBe(4);
    });

    it('skips steps outside the IR v1 closed vocabulary instead of inventing content', () => {
        const view = {
            workflow_id: 'aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa',
            digest: 'ab'.repeat(32),
            definition: {
                name: 'x',
                steps: [
                    { step_id: '1', name: 'alien', kind: 'teleport' },
                    { step_id: '2', name: 'ok', kind: 'tool_call', arguments: { tool: 'desktop.process.execute', command: 'echo hi' } },
                ],
            },
        };
        const rebuilt = irToWorkflowDef(view);
        expect(rebuilt.steps).toHaveLength(1);
        expect(rebuilt.steps[0]!.stepId).toBe('2');
        expect(rebuilt.steps[0]!.params).toEqual({ command: 'echo hi' });
    });
});
