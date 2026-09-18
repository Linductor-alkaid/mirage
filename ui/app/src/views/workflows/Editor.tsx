/// RPA 工程式工作流编辑器（影刀/OpenRPA 范式映射到 Workflow IR v1）：
/// 顶部工具条（保存草稿/发布/运行/诊断）+ 中间顺序流程序列（拖拽排序、
/// 嵌套控制块）+ 右栏（动作库：可拖入的最小原子动作；属性：选中指令的
/// 参数编辑）。右栏两个 tab 按语义切换：拖入看动作库，点选指令看属性。
///
/// 纪律：IR v1 语义之外不出现（有序步骤 / skipIf 前置跳过 / loop_head 回跳 /
/// {"$param"} 参数引用）；错误策略固定 fail-fast（IR 未承诺前不暴露其它策略）。

import { useMemo, useState } from 'react';
import {
  Clipboard,
  Eye,
  FileSearch,
  GripVertical,
  MousePointer,
  Play,
  Plus,
  Repeat,
  Send,
  Terminal,
  Trash2,
  Workflow,
} from 'lucide-react';

import { useHarness } from '../../hooks.js';
import type { WorkflowAtom, WorkflowAtomParamSpec } from '../../state/workflow-backend.js';

const CATEGORY_ICONS: Record<string, React.ReactNode> = {
  文件: <FileSearch size={13} />,
  命令: <Terminal size={13} />,
  桌面观察: <Eye size={13} />,
  窗口与输入: <MousePointer size={13} />,
  剪贴板: <Clipboard size={13} />,
  流程控制: <Repeat size={13} />,
  子流程: <Workflow size={13} />,
};

const MIME_ATOM = 'application/x-mir-atom';
const MIME_STEP = 'application/x-mir-step';

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
      if (s.kind === 'control' && s.atomId === 'ctl.loop' && (s.loopMax === undefined || s.loopMax < 1)) {
        errors.push(`步骤 ${i + 1}：循环回跳缺少 max_iterations（IR v1 要求 ≥ 1）。`);
      }
      if (s.kind === 'control' && s.atomId === 'ctl.loop' && i === wf.steps.length - 1) {
        warnings.push('循环回跳位于序列末尾：回跳目标为序列头部。');
      }
    });
    return { errors, warnings };
  }, [state.workflows, workflowId]);
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
              {a.availability === 'planned' && <span className="sim-note" title="依赖 M2+ Platform Backend，运行面待交付">M2+</span>}
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

/** 属性面板：选中指令的参数编辑（{"$param"} 引用 + skipIf + loop 上限）。 */
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
      <label htmlFor={`pp-${spec.name}`}>{spec.name}</label>
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
      {atom.produces !== undefined && (
        <>
          <div className="props-group caps" style={{ marginTop: 12 }}>Output · 输出</div>
          <p className="muted" style={{ fontSize: 11 }}>
            结果存入 <span className="mono">{`{${atom.produces}}`}</span>，后续指令可用
          </p>
        </>
      )}
      <div className="props-group caps" style={{ marginTop: 12 }}>Options · 执行条件</div>
      {atom.kind !== 'control' && (
        <div className="form-row" style={{ gridTemplateColumns: '96px 1fr' }}>
          <label htmlFor="pp-skipif">跳过条件</label>
          <input
            id="pp-skipif"
            className="input"
            value={step.skipIf ?? ''}
            placeholder="谓词 DSL（IR v1 skipIf；留空不跳过）"
            onChange={(e) => setStepSkipIf(workflowId, stepIndex, e.target.value.length > 0 ? e.target.value : undefined)}
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

/** 参数表：工作流级 {"$param"} 定义（诊断联动：未定义引用会报错）。 */
function ParamsPane({ workflowId }: { workflowId: string }): React.ReactElement | null {
  const { state, setWorkflowParams } = useHarness();
  const [newName, setNewName] = useState('');
  const wf = state.workflows.find((w) => w.id === workflowId);
  if (wf === undefined) {
    return null;
  }
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
      {wf.params.length === 0 && (
        <p className="muted" style={{ fontSize: 12 }}>尚未定义参数。指令参数里写 {"$name"} 引用即可。</p>
      )}
      <div className="form-grid">
        {wf.params.map((p) => (
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
        <span className={`badge ${wf.published ? 'is-success' : 'is-warning'}`}>{wf.version}</span>
        {!wf.published && <span className="sim-note">草稿自动保存</span>}
        <input
          className="input"
          style={{ maxWidth: 280, height: 26 }}
          value={wf.description}
          placeholder="流程描述（可选）"
          aria-label="流程描述"
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
        <button type="button" className="btn" onClick={() => publishWorkflow(workflowId)}>
          <Send size={13} /> 发布
        </button>
        <button type="button" className="btn btn-primary" onClick={() => runWorkflow(workflowId)}>
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
                        {atom?.availability === 'planned' && <span className="sim-note">M2+</span>}
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
                    {s.skipIf !== undefined && <span className="wf-skip-badge" title={`前置条件：${s.skipIf}`}>skipIf</span>}
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

