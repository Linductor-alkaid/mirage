/// RPA 工程式工作流编辑器（影刀/OpenRPA 范式映射到 Workflow IR v1）：
/// 顶部工具条（保存草稿/发布/运行/诊断）+ 中间顺序流程序列（拖拽排序、
/// 嵌套控制块）+ 右栏（动作库：可拖入的最小原子动作；属性：选中指令的
/// 参数编辑）。右栏两个 tab 按语义切换：拖入看动作库，点选指令看属性。
///
/// 纪律：IR v1 语义之外不出现（有序步骤 / 前置条件谓词 / control loop_head
/// 回跳 / {"$param"} 参数引用 / 副作用步骤 W-02 验证谓词——DEC-024 决策 5）；
/// 错误策略固定 fail-fast（IR 未承诺前不暴露其它策略）。目录来自 wire
/// `workflow.atom.catalog`（DEC-024：如实反映绑定环境），控制构造为编辑器
/// 固定提供，不在目录中宣称。

import { useMemo, useState } from 'react';
import {
  Clipboard,
  Eye,
  FileSearch,
  GripVertical,
  MousePointer,
  Play,
  Plus,
  Send,
  ShieldCheck,
  Terminal,
  Trash2,
  Workflow,
} from 'lucide-react';

import { useHarness } from '../../hooks.js';
import type { WorkflowAtom, WorkflowAtomParamSpec } from '../../state/workflow-backend.js';
import { newHexId } from '../../state/workflow-ir.js';
import type { WorkflowPredicateOpName, WorkflowPredicateView } from '../../state/model.js';

const CATEGORY_ICONS: Record<string, React.ReactNode> = {
  文件: <FileSearch size={13} />,
  命令: <Terminal size={13} />,
  桌面观察: <Eye size={13} />,
  窗口与输入: <MousePointer size={13} />,
  剪贴板: <Clipboard size={13} />,
  应用与通知: <Send size={13} />,
  其他: <Workflow size={13} />,
};

const MIME_ATOM = 'application/x-mir-atom';
const MIME_STEP = 'application/x-mir-step';

const PREDICATE_OPS: readonly WorkflowPredicateOpName[] = ['eq', 'ne', 'lt', 'le', 'gt', 'ge', 'contains', 'exists'];

/** IR 诊断（fail-closed 展示：错误逐条列出）。 */
function useDiagnostics(workflowId: string): { errors: string[]; warnings: string[] } {
  const { state } = useHarness();
  return useMemo(() => {
    const wf = state.workflows.find((w) => w.id === workflowId);
    if (wf === undefined) {
      return { errors: [], warnings: [] };
    }
    const errors: string[] = [];
    const warnings: string[] = [];
    if (wf.steps.length === 0) {
      warnings.push('流程为空：从右侧动作库拖入原子动作。');
    }
    const paramNames = new Set(wf.params.map((p) => p.name));
    wf.steps.forEach((s, i) => {
      for (const value of Object.values(s.params ?? {})) {
        for (const m of value.matchAll(/\{"\$([a-zA-Z0-9_-]+)"\}/g)) {
          if (!paramNames.has(m[1] ?? '')) {
            errors.push(`步骤 ${i + 1}：引用了未定义参数 {"$${m[1]}"}。`);
          }
        }
      }
      if (s.kind === 'control' && i === 0) {
        errors.push('步骤 1：循环回跳没有更早的回跳目标（IR v1 要求目标在序列中先于控制步）。');
      }
      if (s.kind === 'control' && (s.loopMax === undefined || s.loopMax < 1)) {
        errors.push(`步骤 ${i + 1}：循环回跳缺少 max_iterations（IR v1 要求 ≥ 1）。`);
      }
      if (s.kind === 'control' && i === wf.steps.length - 1) {
        warnings.push('循环回跳位于序列末尾：回跳目标为序列头部。');
      }
      const atom = state.atoms.find((a) => a.id === s.atomId);
      if (atom?.hasSideEffects === true) {
        warnings.push(`步骤 ${i + 1}（副作用）：已自动落 W-02 验证谓词；运行时需绑定参数 ok_${s.stepId.slice(0, 6)}=true，否则该步 fail closed。`);
      }
    });
    return { errors, warnings };
  }, [state.workflows, state.atoms, workflowId]);
}

function AtomIcon({ atom }: { atom: WorkflowAtom }): React.ReactElement {
  return (
    <span className="wf-atom-icon" aria-hidden>
      {CATEGORY_ICONS[atom.category] ?? <Workflow size={13} />}
    </span>
  );
}

/** 动作库面板：分类折叠 + 搜索 + 可拖入 + 加号插入（键盘/点选可达的替代拖拽）。 */
function AtomLibrary({
  onInsert,
}: {
  onInsert(atomId: string): void;
}): React.ReactElement {
  const { state } = useHarness();
  const [query, setQuery] = useState('');
  const wireAtomCount = useMemo(() => state.atoms.filter((a) => a.kind !== 'control').length, [state.atoms]);
  const filtered = useMemo(() => {
    const q = query.trim().toLowerCase();
    return state.atoms.filter(
      (a) => q.length === 0 || a.name.toLowerCase().includes(q) || a.description.toLowerCase().includes(q),
    );
  }, [state.atoms, query]);
  const categories = useMemo(() => {
    const map = new Map<string, WorkflowAtom[]>();
    for (const a of filtered) {
      const list = map.get(a.category) ?? [];
      list.push(a);
      map.set(a.category, list);
    }
    return map;
  }, [filtered]);

  return (
    <div className="wf-atomlib" data-testid="atom-library">
      <div className="sess-search" style={{ margin: '0 0 8px' }}>
        <input value={query} onChange={(e) => setQuery(e.target.value)} placeholder="搜索原子动作…" aria-label="搜索原子动作" />
      </div>
      {wireAtomCount === 0 && (
        <p className="muted" style={{ fontSize: 11, margin: '0 0 8px' }}>
          当前绑定环境未暴露原子能力（wire 目录为空）；流程控制构造始终可用。
        </p>
      )}
      {[...categories.entries()].map(([cat, atoms]) => (
        <details key={cat} className="wf-atom-cat" open>
          <summary className="g-label caps">
            {CATEGORY_ICONS[cat]} {cat}
            <span className="muted" style={{ letterSpacing: 0 }}>{atoms.length}</span>
          </summary>
          {atoms.map((a) => (
            <div
              key={a.id}
              className="wf-atom-card"
              draggable
              data-testid={`atom-${a.id}`}
              onDragStart={(e) => {
                e.dataTransfer.setData(MIME_ATOM, a.id);
                e.dataTransfer.effectAllowed = 'copy';
              }}
              onDoubleClick={() => onInsert(a.id)}
              title={`${a.description}（拖入 / 双击追加 / 点 +）`}
            >
              <AtomIcon atom={a} />
              <span className="wf-atom-name">{a.name}</span>
              {a.hasSideEffects && (
                <span className="sim-note" title="副作用原子：IR 构建时自动落 W-02 验证谓词">
                  副作用
                </span>
              )}
              <button
                type="button"
                className="sess-menu-btn"
                style={{ opacity: 1 }}
                aria-label={`追加 ${a.name} 到流程末尾`}
                onClick={() => onInsert(a.id)}
              >
                +
              </button>
            </div>
          ))}
        </details>
      ))}
      {filtered.length === 0 && <div className="palette-empty">没有匹配的原子动作</div>}
    </div>
  );
}

/** 前置条件谓词编辑（IR v1 结构化谓词：signal + op + 标量值）。 */
function SkipIfEditor({
  value,
  onChange,
  paramNames,
}: {
  value: WorkflowPredicateView | undefined;
  onChange(next: WorkflowPredicateView | undefined): void;
  paramNames: readonly string[];
}): React.ReactElement {
  const predicate = value ?? { signal: '', op: 'eq' as WorkflowPredicateOpName, value: '' };
  const commit = (patch: Partial<WorkflowPredicateView>): void => {
    const next = { ...predicate, ...patch };
    if (next.signal.trim().length === 0) {
      onChange(undefined);
      return;
    }
    onChange(next);
  };
  return (
    <div style={{ display: 'grid', gap: 4 }}>
      <input
        className="input"
        list="mir-param-signals"
        value={predicate.signal}
        placeholder="run_parameter:名称"
        aria-label="前置条件 signal"
        onChange={(e) => commit({ signal: e.target.value })}
      />
      <datalist id="mir-param-signals">
        {paramNames.map((name) => (
          <option key={name} value={`run_parameter:${name}`} />
        ))}
      </datalist>
      <div style={{ display: 'flex', gap: 4 }}>
        <select
          className="select"
          value={predicate.op}
          aria-label="前置条件算子"
          onChange={(e) => commit({ op: e.target.value as WorkflowPredicateOpName })}
        >
          {PREDICATE_OPS.map((op) => (
            <option key={op} value={op}>
              {op}
            </option>
          ))}
        </select>
        <input
          className="input"
          value={predicate.value}
          placeholder={predicate.op === 'exists' ? '（exists 无值）' : '标量值'}
          aria-label="前置条件值"
          disabled={predicate.op === 'exists'}
          onChange={(e) => commit({ value: e.target.value })}
        />
      </div>
    </div>
  );
}

/** 属性面板：选中指令的参数编辑（{"$param"} 引用 + 前置条件 + loop 上限）。 */
function StepProps({
  workflowId,
  stepIndex,
}: {
  workflowId: string;
  stepIndex: number;
}): React.ReactElement | null {
  const { state, setStepParams, setStepSkipIf, setStepLoopMax, mutateSteps } = useHarness();
  const wf = state.workflows.find((w) => w.id === workflowId);
  const step = wf?.steps[stepIndex];
  const atom = state.atoms.find((a) => a.id === step?.atomId);
  if (wf === undefined || step === undefined || atom === undefined) {
    return null;
  }
  const params = { ...(step.params ?? {}) };
  const setParam = (name: string, value: string): void => {
    setStepParams(workflowId, stepIndex, { ...params, [name]: value });
  };
  const renderParam = (spec: WorkflowAtomParamSpec): React.ReactElement => (
    <div className="form-row" key={spec.name} style={{ gridTemplateColumns: '96px 1fr' }}>
      <label htmlFor={`pp-${spec.name}`}>
        {spec.name}
        {spec.required ? ' *' : ''}
      </label>
      {spec.type === 'select' ? (
        <select
          id={`pp-${spec.name}`}
          className="select"
          value={params[spec.name] ?? spec.defaultValue ?? ''}
          onChange={(e) => setParam(spec.name, e.target.value)}
        >
          {(spec.options ?? []).map((o) => (
            <option key={o} value={o}>
              {o}
            </option>
          ))}
        </select>
      ) : (
        <input
          id={`pp-${spec.name}`}
          className="input"
          type={spec.type === 'number' ? 'number' : 'text'}
          value={params[spec.name] ?? spec.defaultValue ?? ''}
          placeholder={spec.type === 'text' ? '支持 {"$param"} 引用' : undefined}
          onChange={(e) => setParam(spec.name, e.target.value)}
        />
      )}
    </div>
  );

  return (
    <div className="wf-props" data-testid="step-props">
      <div className="props-head">
        <AtomIcon atom={atom} />
        <strong>{atom.name}</strong>
        <span className="mono muted" style={{ fontSize: 11 }}>#{stepIndex + 1}</span>
      </div>
      <p className="muted" style={{ fontSize: 12, marginBottom: 10 }}>{atom.description}</p>
      <div className="props-group caps">Input · 参数</div>
      {atom.params.length > 0 ? (
        <div className="form-grid">
          {atom.params.map(renderParam)}
        </div>
      ) : (
        <p className="muted" style={{ fontSize: 12 }}>该动作无参数。</p>
      )}
      {atom.hasSideEffects && (
        <div className="props-group caps" style={{ marginTop: 12 }}>
          <ShieldCheck size={11} /> 验证 · W-02
        </div>
      )}
      {atom.hasSideEffects && (
        <p className="muted" style={{ fontSize: 11 }}>
          副作用步骤自动携带验证谓词{' '}
          <span className="mono">{`run_parameter:ok_${step.stepId.slice(0, 6)} eq true`}</span>
          ，发布 DryRun 下按 NotEvaluable 计数（RULE-10）；Strict 运行需绑定参数{' '}
          <span className="mono">{`ok_${step.stepId.slice(0, 6)}`}</span>=true，否则该步 fail closed。
        </p>
      )}
      <div className="props-group caps" style={{ marginTop: 12 }}>Options · 执行条件</div>
      {atom.kind !== 'control' && (
        <div className="form-row" style={{ gridTemplateColumns: '96px 1fr' }}>
          <label htmlFor="pp-skipif">前置条件</label>
          <SkipIfEditor
            value={step.skipIf}
            onChange={(next) => setStepSkipIf(workflowId, stepIndex, next)}
            paramNames={wf.params.map((p) => p.name)}
          />
        </div>
      )}
      {step.atomId === 'ctl.loop' && (
        <div className="form-row" style={{ gridTemplateColumns: '96px 1fr' }}>
          <label htmlFor="pp-loopmax">最大迭代</label>
          <input
            id="pp-loopmax"
            className="input"
            type="number"
            min={1}
            value={step.loopMax ?? Number(params.maxIterations ?? 1)}
            onChange={(e) => setStepLoopMax(workflowId, stepIndex, Number(e.target.value) || undefined)}
          />
        </div>
      )}
      <p className="muted" style={{ fontSize: 11, marginTop: 10 }}>
        错误策略：<span className="mono">fail-fast</span>（IR v1 固定：失败即终止，后续跳过）
      </p>
      <button
        type="button"
        className="btn btn-danger"
        style={{ marginTop: 12 }}
        onClick={() => mutateSteps(workflowId, (steps) => steps.filter((_, i) => i !== stepIndex))}
      >
        <Trash2 size={13} /> 删除该指令
      </button>
    </div>
  );
}

/** 参数表：工作流级 {"$param"} 定义（诊断联动：未定义引用会报错）。
 * 副作用步骤的 W-02 验证绑定参数在保存时自动派生，此处只读展示。 */
function ParamsPane({ workflowId }: { workflowId: string }): React.ReactElement | null {
  const { state, setWorkflowParams } = useHarness();
  const [newName, setNewName] = useState('');
  const wf = state.workflows.find((w) => w.id === workflowId);
  if (wf === undefined) {
    return null;
  }
  const atomsById = new Map(state.atoms.map((a) => [a.id, a]));
  const derived = new Set(
    wf.steps
      .filter((s) => atomsById.get(s.atomId)?.hasSideEffects === true)
      .map((s) => `ok_${s.stepId.slice(0, 6)}`),
  );
  const declared = wf.params.filter((p) => !derived.has(p.name));
  const add = (): void => {
    const name = newName.trim();
    if (name.length === 0 || wf.params.some((p) => p.name === name)) {
      return;
    }
    setWorkflowParams(workflowId, [...wf.params, { name, required: false, description: '' }]);
    setNewName('');
  };
  return (
    <div className="wf-props" data-testid="wf-params">
      <div className="props-group caps">流程参数（{"$param"} 引用源）</div>
      {declared.length === 0 && (
        <p className="muted" style={{ fontSize: 12 }}>尚未定义参数。指令参数里写 {"$name"} 引用即可。</p>
      )}
      <div className="form-grid">
        {declared.map((p) => (
          <div key={p.name} className="wf-param-row">
            <span className="mono wf-param-name">{`{"$${p.name}"}`}</span>
            <input
              className="input"
              value={p.description}
              placeholder="说明"
              aria-label={`参数 ${p.name} 说明`}
              onChange={(ev) =>
                setWorkflowParams(
                  workflowId,
                  wf.params.map((x) => (x.name === p.name ? { ...x, description: ev.target.value } : x)),
                )
              }
            />
            <button
              type="button"
              className="sess-menu-btn"
              style={{ opacity: 1 }}
              aria-label={`删除参数 ${p.name}`}
              onClick={() => setWorkflowParams(workflowId, wf.params.filter((x) => x.name !== p.name))}
            >
              ×
            </button>
          </div>
        ))}
      </div>
      {[...derived].map((name) => (
        <div key={name} className="wf-param-row" title="副作用步骤的 W-02 验证谓词绑定参数（保存时自动生成，可选）">
          <span className="mono wf-param-name">{`{"$${name}"}`}</span>
          <span className="muted" style={{ fontSize: 11 }}>自动派生 · 验证谓词绑定（boolean）</span>
        </div>
      ))}
      <div style={{ display: 'flex', gap: 6, marginTop: 10 }}>
        <input
          className="input"
          value={newName}
          placeholder="新参数名"
          aria-label="新参数名"
          onChange={(ev) => setNewName(ev.target.value)}
          onKeyDown={(ev) => {
            if (ev.key === 'Enter') {
              add();
            }
          }}
        />
        <button type="button" className="btn" onClick={add}>
          <Plus size={13} /> 添加
        </button>
      </div>
    </div>
  );
}

export function WorkflowEditor({ workflowId }: { workflowId: string }): React.ReactElement | null {
  const { state, publishWorkflow, runWorkflow, mutateSteps, setWorkflowDescription } = useHarness();
  const wf = state.workflows.find((w) => w.id === workflowId);
  const [rightTab, setRightTab] = useState<'atoms' | 'props' | 'params'>('atoms');
  const [selected, setSelected] = useState<number | null>(null);
  const [dropIndex, setDropIndex] = useState<number | null>(null);
  const diagnostics = useDiagnostics(workflowId);
  if (wf === undefined) {
    return null;
  }

  const atomOf = (atomId: string): WorkflowAtom | undefined => state.atoms.find((a) => a.id === atomId);

  const insertAtom = (atomId: string, at: number): void => {
    const atom = atomOf(atomId);
    if (atom === undefined) {
      return;
    }
    const defaults: Record<string, string> = {};
    for (const p of atom.params) {
      if (p.defaultValue !== undefined) {
        defaults[p.name] = p.defaultValue;
      }
    }
    mutateSteps(workflowId, (steps) => {
      const next = [...steps];
      next.splice(Math.min(at, next.length), 0, {
        stepId: newHexId(),
        atomId: atom.id,
        title: atom.name,
        kind: atom.kind,
        detail: atom.description,
        params: defaults,
        ...(atom.id === 'ctl.loop' ? { loopMax: Number(defaults.maxIterations ?? 1) } : {}),
      });
      return next;
    });
    setSelected(Math.min(at, wf.steps.length));
    setRightTab('props');
  };

  const reorderStep = (from: number, to: number): void => {
    mutateSteps(workflowId, (steps) => {
      const next = [...steps];
      const [moved] = next.splice(from, 1);
      if (moved === undefined) {
        return steps;
      }
      next.splice(to > from ? to - 1 : to, 0, moved);
      return next;
    });
    setSelected(null);
  };

  const allowDrop = (e: React.DragEvent): void => {
    e.preventDefault();
    e.dataTransfer.dropEffect = e.dataTransfer.types.includes(MIME_ATOM) ? 'copy' : 'move';
  };

  const dropOn = (index: number) => (e: React.DragEvent): void => {
    e.preventDefault();
    const atomId = e.dataTransfer.getData(MIME_ATOM);
    const fromRaw = e.dataTransfer.getData(MIME_STEP);
    if (atomId.length > 0) {
      insertAtom(atomId, index);
    } else if (fromRaw.length > 0) {
      const from = Number(fromRaw);
      if (Number.isFinite(from) && from !== index) {
        reorderStep(from, index);
      }
    }
    setDropIndex(null);
  };

  const dragOverRow = (index: number) => (e: React.DragEvent<HTMLDivElement>): void => {
    allowDrop(e);
    const box = e.currentTarget.getBoundingClientRect();
    setDropIndex(e.clientY < box.top + box.height / 2 ? index : index + 1);
  };

  const rowDragStart = (index: number) => (e: React.DragEvent): void => {
    e.dataTransfer.setData(MIME_STEP, String(index));
    e.dataTransfer.effectAllowed = 'move';
  };

  return (
    <div className="wf-editor" data-testid="workflow-editor">
      <div className="wf-toolbar">
        <strong className="wf-name">{wf.name}</strong>
        <span className={`badge ${wf.published ? 'is-success' : 'is-warning'}`}>{wf.published ? wf.version : '草稿'}</span>
        {!wf.published && <span className="sim-note">草稿自动保存</span>}
        {!wf.runnable && <span className="badge is-muted" title="W-04：草稿版本不可被运行引用">不可运行</span>}
        {!wf.contentKnown && (
          <span className="badge is-info" title="定义内容尚未经 workflow.get 回读（DEC-026），回读失败时保持只读">
            只读 · 内容未回读
          </span>
        )}
        <input
          className="input"
          style={{ maxWidth: 280, height: 26 }}
          value={wf.description}
          placeholder="流程描述（可选）"
          aria-label="流程描述"
          disabled={!wf.contentKnown}
          onChange={(e) => setWorkflowDescription(workflowId, e.target.value)}
        />
        <span className="spacer" style={{ flex: 1 }} />
        {diagnostics.errors.length > 0 ? (
          <span className="badge is-danger" title={diagnostics.errors.join('\n')}>
            诊断 {diagnostics.errors.length}
          </span>
        ) : (
          <span className="badge is-muted">IR 校验通过</span>
        )}
        <button type="button" className="btn" disabled={!wf.contentKnown} onClick={() => publishWorkflow(workflowId)}>
          <Send size={13} /> 发布
        </button>
        <button type="button" className="btn btn-primary" disabled={!wf.runnable} onClick={() => runWorkflow(workflowId)}>
          <Play size={13} /> 运行
        </button>
      </div>

      {(diagnostics.errors.length > 0 || diagnostics.warnings.length > 0) && (
        <div className="wf-diagnostics" role="status">
          {[...diagnostics.errors.map((e) => ({ tone: 'is-error' as const, text: e })), ...diagnostics.warnings.map((w) => ({ tone: 'is-warn' as const, text: w }))].map((d, i) => (
            <div key={i} className={`system-line ${d.tone}`} style={{ alignSelf: 'flex-start' }}>
              {d.text}
            </div>
          ))}
        </div>
      )}

      <div className="wf-editor-body">
        <div className="wf-canvas" data-testid="wf-canvas">
          {wf.steps.length === 0 && (
            <div className="empty-hero" style={{ padding: 'var(--mir-space-6)' }}>
              <span className="big" style={{ fontSize: 28 }}>空流程</span>
              <p>从右侧动作库拖入原子动作，或点动作卡上的 + 追加到末尾。</p>
            </div>
          )}
          <div className="wf-seq" onDragOver={allowDrop} onDrop={dropOn(wf.steps.length)}>
            {wf.steps.map((s, i) => {
              const atom = atomOf(s.atomId);
              const paramChips = Object.entries(s.params ?? {});
              const isSel = selected === i;
              return (
                <div key={`${s.atomId}-${i}`}>
                  {dropIndex === i && <div className="wf-drop-line" aria-hidden />}
                  <div
                    className={`wf-step-row ${isSel ? 'is-selected' : ''} ${s.kind === 'control' ? 'is-control' : ''}`}
                    draggable
                    onDragStart={rowDragStart(i)}
                    onDragOver={dragOverRow(i)}
                    onDrop={dropOn(i)}
                    onClick={() => {
                      setSelected(i);
                      setRightTab('props');
                    }}
                    data-testid={`wf-step-${i}`}
                  >
                    <span className="wf-handle" aria-hidden>
                      <GripVertical size={13} />
                    </span>
                    <span className="wf-idx num">{String(i + 1).padStart(2, '0')}</span>
                    <span className="wf-atom-icon" aria-hidden>
                      {atom !== undefined ? CATEGORY_ICONS[atom.category] ?? <Workflow size={13} /> : <Workflow size={13} />}
                    </span>
                    <span className="wf-step-main">
                      <span className="wf-step-title">
                        {s.title}
                        {s.kind === 'control' && <span className="badge is-info" style={{ height: 16, fontSize: 10 }}>控制</span>}
                        {atom?.hasSideEffects && (
                          <span className="badge is-success" style={{ height: 16, fontSize: 10 }} title="副作用原子：携带 W-02 验证谓词">
                            验证
                          </span>
                        )}
                      </span>
                      <span className="wf-step-detail">
                        {paramChips.length > 0 ? (
                          paramChips.map(([k, v]) => (
                            <span key={k} className={`wf-param-chip mono ${v.startsWith('{"$') ? 'is-ref' : ''}`}>
                              {k}={v}
                            </span>
                          ))
                        ) : (
                          s.detail
                        )}
                      </span>
                    </span>
                    {s.skipIf !== undefined && (
                      <span className="wf-skip-badge" title={`前置条件：${s.skipIf.signal} ${s.skipIf.op} ${s.skipIf.value}`}>
                        skipIf
                      </span>
                    )}
                    {s.loopMax !== undefined && <span className="wf-loop-badge" title={`回跳上限：${s.loopMax}`}>×{s.loopMax}</span>}
                  </div>
                </div>
              );
            })}
            {dropIndex === wf.steps.length && wf.steps.length > 0 && <div className="wf-drop-line" aria-hidden />}
            <div className="wf-seq-end" onDragOver={allowDrop} onDrop={dropOn(wf.steps.length)}>
              拖入原子动作到此处追加
            </div>
          </div>
        </div>

        <aside className="wf-rightpane" aria-label="动作库与属性">
          <div className="composer-modes" style={{ margin: '0 0 8px', width: '100%' }}>
            <button type="button" role="tab" aria-selected={rightTab === 'atoms'} className={`mode-btn ${rightTab === 'atoms' ? 'is-active' : ''}`} style={{ flex: 1, justifyContent: 'center' }} onClick={() => setRightTab('atoms')}>
              动作库
            </button>
            <button type="button" role="tab" aria-selected={rightTab === 'props'} className={`mode-btn ${rightTab === 'props' ? 'is-active' : ''}`} style={{ flex: 1, justifyContent: 'center' }} onClick={() => setRightTab('props')}>
              属性{selected !== null ? ` · #${selected + 1}` : ''}
            </button>
            <button type="button" role="tab" aria-selected={rightTab === 'params'} className={`mode-btn ${rightTab === 'params' ? 'is-active' : ''}`} style={{ flex: 1, justifyContent: 'center' }} onClick={() => setRightTab('params')}>
              参数
            </button>
          </div>
          {rightTab === 'atoms' && (
            <AtomLibrary onInsert={(atomId) => insertAtom(atomId, selected !== null ? selected + 1 : wf.steps.length)} />
          )}
          {rightTab === 'props' &&
            (selected !== null && wf.steps[selected] !== undefined ? (
              <StepProps workflowId={workflowId} stepIndex={selected} />
            ) : (
              <div className="palette-empty">点选流程中的一条指令以编辑属性。</div>
            ))}
          {rightTab === 'params' && <ParamsPane workflowId={workflowId} />}
        </aside>
      </div>
    </div>
  );
}

