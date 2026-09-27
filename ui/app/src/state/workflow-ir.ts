/// Workflow IR v1 映射（DEC-013 对齐验收）：编辑器模型 → pinned
/// `workflow_ir.hpp` 定义的 IR v1 JSON 文档（`workflow.save` /
/// `workflow.publish` 的 `definition`，以及「导出 IR JSON」的输出）。
///
/// 本模块是编辑器语义与 pinned IR 的唯一换算点，纪律：
/// - 只产出 IR v1 封闭词汇（`tool_call`/`control` 步骤、`{"$param": name}`
///   引用、结构化谓词、`strict` 缺省策略）；未知形态不出现。
/// - W-02（DEC-024 决策 5）：副作用原子的步骤必须携带 verification 谓词。
///   编辑器按 C++ 侧已验证的形态落谓词——读取一个自动生成的可选
///   boolean 运行参数（无默认值）：发布 DryRun（空参绑定）下该谓词
///   NotEvaluable 计数通过（RULE-10），运行以参数满足或 fail closed。
/// - 控制构造（循环回跳）映射为 `control` 步骤 + 序列首步的 `loop_head`
///   标注；回跳目标不存在时由编辑器诊断报错，不产出非法 IR。

import type {
    WorkflowDef,
    WorkflowPredicateOpName,
    WorkflowPredicateView,
    WorkflowStepDef,
} from './model.js';
import type { WorkflowAtom } from './workflow-backend.js';

/** 32-hex 身份（IR workflow_id / step_id），与 pinned `WorkflowId` /
 * `StepId` 的 `to_string()` 形态一致。 */
export function newHexId(): string {
    if (typeof crypto !== 'undefined' && typeof crypto.randomUUID === 'function') {
        return crypto.randomUUID().replace(/-/g, '');
    }
    // 非安全上下文回退：时间 + 随机的 32 hex（仅测试环境可达）。
    let out = '';
    while (out.length < 32) {
        out += Math.floor(Math.random() * 16).toString(16);
    }
    return out;
}

const HEX_RE = /^[0-9a-f]{32}$/;

export function isHexId(value: string): boolean {
    return HEX_RE.test(value);
}

/** 副作用步骤的验证谓词绑定参数名（稳定：派生自 step_id 前缀）。 */
export function verificationParamName(stepId: string): string {
    return `ok_${stepId.slice(0, 6)}`;
}

/** 步骤对应的原子是否副作用（控制构造恒否；未知原子按否处理——
 * 目录缺席时不虚构门禁需求）。 */
export function isSideEffectStep(step: WorkflowStepDef, atomsById: ReadonlyMap<string, WorkflowAtom>): boolean {
    return atomsById.get(step.atomId)?.hasSideEffects === true;
}

/** W-02 派生参数：每个副作用步骤一个可选 boolean 参数（无默认值）。 */
export function derivedVerificationParams(
    steps: readonly WorkflowStepDef[],
    atomsById: ReadonlyMap<string, WorkflowAtom>,
): { name: string; required: false; description: string; type: 'boolean' }[] {
    const seen = new Set<string>();
    const params: { name: string; required: false; description: string; type: 'boolean' }[] = [];
    for (const step of steps) {
        if (!isSideEffectStep(step, atomsById)) {
            continue;
        }
        const name = verificationParamName(step.stepId);
        if (seen.has(name)) {
            continue;
        }
        seen.add(name);
        params.push({
            name,
            required: false,
            description: `步骤「${step.title}」副作用验证谓词绑定（运行时置 true 以通过验证）`,
            type: 'boolean',
        });
    }
    return params;
}

/** 把编辑器的参数值转换为 IR 实参值：`{"$name"}` 引用 → 引用对象；
 * 数值型参数（schema integer）的纯数字文本 → 数值；其余保持字符串。 */
function argumentValue(value: string, specType: WorkflowAtom['params'][number]['type'] | undefined): string | number | { $param: string } {
    const trimmed = value.trim();
    const ref = /^(?:\{"\$([A-Za-z0-9_-]+)"\})$/.exec(trimmed);
    if (ref !== null && ref[1] !== undefined) {
        return { $param: ref[1] };
    }
    if (specType === 'number' && /^-?\d+$/.test(trimmed)) {
        return Number(trimmed);
    }
    return value;
}

/** 谓词值文本 → JSON 标量（exists 恒 null；true/false/null/数字按
 * JSON 字面量，其余按字符串）。 */
export function predicateValue(op: WorkflowPredicateOpName, text: string): string | number | boolean | null {
    if (op === 'exists') {
        return null;
    }
    const trimmed = text.trim();
    if (trimmed === 'true') {
        return true;
    }
    if (trimmed === 'false') {
        return false;
    }
    if (trimmed === 'null') {
        return null;
    }
    if (/^-?\d+(\.\d+)?$/.test(trimmed)) {
        return Number(trimmed);
    }
    return trimmed;
}

function predicateToJson(predicate: WorkflowPredicateView): Record<string, unknown> {
    return {
        signal: predicate.signal,
        op: predicate.op,
        value: predicateValue(predicate.op, predicate.value),
    };
}

/** 编辑器模型 → IR v1 JSON 文档。`atomsById` 来自 wire 目录 + 控制构造。 */
export function workflowDefToIr(def: WorkflowDef, atomsById: ReadonlyMap<string, WorkflowAtom>): Record<string, unknown> {
    const steps = def.steps;
    const hasLoop = steps.some((s) => s.kind === 'control');
    const headStep = steps[0];
    const derived = derivedVerificationParams(steps, atomsById);
    const derivedNames = new Set(derived.map((p) => p.name));

    const parameters = [
        ...def.params
            .filter((p) => !derivedNames.has(p.name))
            .map((p) => ({
                name: p.name,
                type: p.type ?? 'string',
                required: p.required,
                ...(p.description.length > 0 ? { summary: p.description } : {}),
            })),
        ...derived.map((p) => ({
            name: p.name,
            type: p.type,
            required: false,
            summary: p.description,
        })),
    ];

    return {
        schema_version: { major: 1, minor: 0 },
        workflow_id: def.id,
        name: def.name,
        ...(def.description.length > 0 ? { summary: def.description } : {}),
        parameters,
        steps: steps.map((step, index) => {
            const atom = atomsById.get(step.atomId);
            const base: Record<string, unknown> = {
                step_id: step.stepId,
                name: step.title,
                kind: step.kind,
            };
            if (index === 0 && hasLoop) {
                // 循环回跳目标必须是更早的 loop_head 步骤：编辑器固定把
                // 序列首步标注为 loop_head（IR 回跳语义，DEC-013）。
                base.loop_head = true;
            }
            if (step.kind === 'control') {
                if (headStep !== undefined && index > 0) {
                    base.jump_to = headStep.stepId;
                }
                base.max_iterations = Math.max(1, Math.trunc(step.loopMax ?? 1));
                return base;
            }
            const args: Record<string, unknown> = { tool: step.atomId };
            for (const [key, value] of Object.entries(step.params ?? {})) {
                args[key] = argumentValue(value, atom?.params.find((p) => p.name === key)?.type);
            }
            base.arguments = args;
            if (atom?.hasSideEffects === true) {
                base.verification = {
                    signal: `run_parameter:${verificationParamName(step.stepId)}`,
                    op: 'eq',
                    value: true,
                };
            }
            if (step.skipIf !== undefined) {
                base.precondition = predicateToJson(step.skipIf);
            }
            return base;
        }),
        default_policy: 'strict',
        allowed_policies: ['strict', 'dry_run'],
    };
}
