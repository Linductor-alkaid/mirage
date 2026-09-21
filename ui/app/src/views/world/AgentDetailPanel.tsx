/// Agent Detail Panel — 当 Three.js Canvas 中点击 Agent 时，浮出右侧面板。
///
/// 现阶段：从 Coordinator + World Model + OrganizationStore 派生只读展示。
/// 不写入状态（M8 之后才能让面板控制 camera focus 等命令）。

import type { AgentEntity } from '../../world/model/agentEntity.js';
import type { Agent, Activity, OrganizationState, Task } from '../../world/organization/types.js';
import type { VisualState } from '../../world/model/index.js';

export interface AgentDetailPanelProps {
    readonly entity: AgentEntity;
    readonly agent: Agent | undefined;
    readonly organization: OrganizationState;
    readonly onFocus: () => void;
    readonly onClose: () => void;
}

const VISUAL_LABEL: Readonly<Record<VisualState, string>> = {
    idle: '空闲',
    walking: '移动中',
    working: '工作中',
    collaborating: '协作中',
    waiting: '等待',
    blocked: '阻塞',
    reviewing: '评审',
    testing: '测试',
    reporting: '汇报',
    completed: '已完成',
    hibernated: '休眠',
};

const ROLE_LABELS: Readonly<Record<string, string>> = {
    'role-engineer': '工程师',
    'role-architect': '架构师',
    'role-qa': '测试',
    'role-pm': '产品经理',
};

export function AgentDetailPanel({
    entity,
    agent,
    organization,
    onFocus,
    onClose,
}: AgentDetailPanelProps): React.ReactElement {
    const team = entity.teamId ? organization.teams[entity.teamId] : undefined;
    const currentActivity = agent?.currentActivityId
        ? (organization.activities[agent.currentActivityId] as Activity | undefined)
        : undefined;
    const currentTask = agent?.currentTaskId
        ? (organization.tasks[agent.currentTaskId] as Task | undefined)
        : undefined;
    const roleLabel = agent ? (ROLE_LABELS[agent.roleId] ?? agent.roleId) : '—';

    return (
        <aside className="agent-detail" data-testid="agent-detail">
            <header className="agent-detail__head">
                <div className="agent-detail__title">
                    <span
                        className="agent-detail__dot"
                        style={{
                            background: `rgb(${Math.round(entity.accent.r * 255)}, ${Math.round(
                                entity.accent.g * 255,
                            )}, ${Math.round(entity.accent.b * 255)})`,
                        }}
                    />
                    <h2>{entity.displayName}</h2>
                </div>
                <button type="button" className="agent-detail__close" onClick={onClose} aria-label="关闭">
                    ×
                </button>
            </header>
            <dl className="agent-detail__grid">
                <dt>角色</dt>
                <dd>{roleLabel}</dd>
                <dt>团队</dt>
                <dd>{team?.name ?? '—'}</dd>
                <dt>视觉状态</dt>
                <dd>
                    <span className={`agent-detail__state is-${entity.visualState}`}>
                        {VISUAL_LABEL[entity.visualState]}
                    </span>
                </dd>
                <dt>生命周期</dt>
                <dd>{agent?.lifecycle ?? '—'}</dd>
                <dt>当前任务</dt>
                <dd>{currentTask ? currentTask.title : '—'}</dd>
                <dt>当前活动</dt>
                <dd>
                    {currentActivity
                        ? `${VISUAL_LABEL[mapActivityKindToVisual(currentActivity.kind)]} · 已开始 ${formatTime(currentActivity.startedAt)}`
                        : '—'}
                </dd>
            </dl>
            <footer className="agent-detail__foot">
                <button type="button" onClick={onFocus} className="agent-detail__focus">
                    镜头聚焦
                </button>
            </footer>
        </aside>
    );
}

function mapActivityKindToVisual(
    kind: Activity['kind'],
): VisualState {
    switch (kind) {
        case 'working':
            return 'working';
        case 'collaborating':
            return 'collaborating';
        case 'reviewing':
            return 'reviewing';
        case 'testing':
            return 'testing';
        case 'reporting':
            return 'reporting';
        case 'waiting':
            return 'waiting';
        case 'blocked':
            return 'blocked';
        case 'idle':
            return 'idle';
    }
}

function formatTime(ms: number): string {
    if (ms < 0) {
        return '—';
    }
    const s = Math.floor(ms / 1000);
    if (s < 60) {
        return `${s}s`;
    }
    const m = Math.floor(s / 60);
    return `${m}m${s % 60}s`;
}