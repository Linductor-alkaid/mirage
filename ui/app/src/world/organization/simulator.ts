/// Organization 模拟器（确定性 seed）。
///
/// 目的：Mira 暂无 multi-agent Runtime 语义（依赖反馈 MIRA-20260921-001），
/// World Projection 与 Projector 需要可重复、可视化、可在浏览器内运行的事件
/// 流；UI 标注「模拟」。接入真实 Runtime 时替换 `OrganizationEventSource`
/// 即可，Projector 与 Renderer 不感知。
///
/// 设计：
/// - 内部维护 deterministic RNG（seed），保证每次 replay 形状一致；
/// - 默认提供 30 Agent / 4 Team / 6 Task / 3 Collaboration 的种子剧本；
/// - 提供 `pause()` / `resume()` / `setSpeed()` 用于 React 控件；
/// - 不引用 Three.js / React / DOM。

import type { Activity, Agent, OrganizationState, Role, Task, Team } from './types.js';
import type { OrganizationEvent } from './events.js';
import type {
    AgentId,
    CollaborationId,
    LogicalTime,
    OrganizationId,
    RoleId,
    TaskId,
    TeamId,
} from '../model/types.js';
import type { OrganizationEventSource } from './source.js';
import {
    newActivityId,
    newAgentId,
    newCollaborationId,
    newOrganizationId,
    newTeamId,
    resetIdCounterForTests,
} from '../model/identity.js';

interface SimulatedAgent {
    readonly id: AgentId;
    readonly displayName: string;
    readonly roleId: RoleId;
    readonly teamId: TeamId;
    readonly accent: string;
}

interface SimulatorOptions {
    /** 模拟起点时间。 */
    readonly now?: LogicalTime;
    /** tick 频率（默认 1 tick / 200ms）。 */
    readonly tickMs?: number;
    /** 播放速度倍率。 */
    readonly speed?: number;
    /** 启用角色集合；首期内置 4 个。 */
    readonly roles?: ReadonlyArray<Role>;
    /** 自定义 seed；默认 'mirage-demo'。 */
    readonly seed?: string;
}

/** 简单 deterministic LCG，便于在测试中做快照断言。 */
function makeRng(seed: string): () => number {
    let state = 0x9e3779b9;
    for (let i = 0; i < seed.length; i += 1) {
        state ^= seed.charCodeAt(i);
        state = Math.imul(state, 0x85ebca6b) >>> 0;
    }
    return () => {
        state = Math.imul(state ^ (state >>> 15), 0x2c1b3c6d) >>> 0;
        state = Math.imul(state ^ (state >>> 12), 0x297a2d39) >>> 0;
        state ^= state >>> 15;
        return (state >>> 0) / 0x100000000;
    };
}

const DEFAULT_ROLES: ReadonlyArray<Role> = [
    {
        id: 'role-engineer' as RoleId,
        name: 'Engineer',
        description: '负责功能实现与单元测试。',
    },
    {
        id: 'role-architect' as RoleId,
        name: 'Architect',
        description: '负责系统设计、跨模块契约。',
    },
    {
        id: 'role-qa' as RoleId,
        name: 'QA',
        description: '回归与质量验证。',
    },
    {
        id: 'role-pm' as RoleId,
        name: 'Product Manager',
        description: '任务规划与外部对接。',
    },
];

interface PlannedActivity {
    readonly agentId: AgentId;
    readonly kind: Activity['kind'];
    readonly delayMs: number;
    readonly durationMs: number;
    readonly taskId: TaskId | null;
    readonly collaborationId: CollaborationId | null;
}

interface PlannedCollaboration {
    readonly id: CollaborationId;
    readonly participants: ReadonlyArray<AgentId>;
    readonly taskId: TaskId | null;
    readonly delayMs: number;
    readonly durationMs: number;
}

interface MutableTeam {
    readonly id: TeamId;
    readonly name: string;
    readonly productLine: string;
    readonly agents: SimulatedAgent[];
    /** 临时累加；seed() 时写入最终 Team。 */
    memberIds: AgentId[];
}

interface SeedPlan {
    readonly teams: ReadonlyArray<{ team: Team; agents: ReadonlyArray<SimulatedAgent> }>;
    readonly tasks: ReadonlyArray<Task>;
    readonly activities: ReadonlyArray<PlannedActivity>;
    readonly collaborations: ReadonlyArray<PlannedCollaboration>;
}

/** 确定性 seed 数据：4 Team × 30 Agent。 */
function planSeed(): SeedPlan {
    const engineer = DEFAULT_ROLES[0]!.id;
    const architect = DEFAULT_ROLES[1]!.id;
    const qa = DEFAULT_ROLES[2]!.id;
    const pm = DEFAULT_ROLES[3]!.id;

    const productA = newTeamId();
    const productB = newTeamId();
    const platformTeam = newTeamId();
    const designOps = newTeamId();

    const accents = ['#E8A33D', '#7FB3D5', '#34D399', '#F472B6', '#FBBF24', '#60A5FA', '#A78BFA'];
    const pickAccent = (i: number): string => accents[i % accents.length]!;

    const teamDefs: ReadonlyArray<{ id: TeamId; name: string; productLine: string }> = [
        { id: productA, name: 'Product A', productLine: 'product-a' },
        { id: productB, name: 'Product B', productLine: 'product-b' },
        { id: platformTeam, name: 'Platform', productLine: 'platform' },
        { id: designOps, name: 'Design & Ops', productLine: 'ops' },
    ];

    const teams: MutableTeam[] = teamDefs.map((t) => ({ ...t, agents: [], memberIds: [] }));

    const samples: ReadonlyArray<{ teamIdx: number; role: RoleId; count: number; prefix: string }> = [
        { teamIdx: 0, role: pm, count: 2, prefix: 'PM' },
        { teamIdx: 0, role: architect, count: 2, prefix: 'Arch' },
        { teamIdx: 0, role: engineer, count: 4, prefix: 'EngA' },
        { teamIdx: 0, role: qa, count: 2, prefix: 'QAA' },
        { teamIdx: 1, role: pm, count: 1, prefix: 'PM' },
        { teamIdx: 1, role: engineer, count: 4, prefix: 'EngB' },
        { teamIdx: 1, role: qa, count: 2, prefix: 'QAB' },
        { teamIdx: 2, role: engineer, count: 5, prefix: 'Plt' },
        { teamIdx: 2, role: architect, count: 1, prefix: 'PltA' },
        { teamIdx: 2, role: qa, count: 1, prefix: 'PltQ' },
        { teamIdx: 3, role: engineer, count: 3, prefix: 'Ops' },
        { teamIdx: 3, role: pm, count: 1, prefix: 'OpsPM' },
    ];

    const rng = makeRng('mirage-demo-agents');
    let idx = 0;
    for (const s of samples) {
        for (let n = 0; n < s.count; n += 1) {
            const id = newAgentId();
            const displayName = `${s.prefix}-${(n + 1).toString().padStart(2, '0')}`;
            teams[s.teamIdx]!.agents.push({
                id,
                displayName,
                roleId: s.role,
                teamId: teams[s.teamIdx]!.id,
                accent: pickAccent(idx + Math.floor(rng() * 7)),
            });
            teams[s.teamIdx]!.memberIds.push(id);
            idx += 1;
        }
    }

    const seedTeams: { team: Team; agents: ReadonlyArray<SimulatedAgent> }[] = teams.map((t) => ({
        team: {
            id: t.id,
            name: t.name,
            parentTeamId: null,
            productLine: t.productLine,
            memberIds: t.memberIds.slice(),
            createdAt: 0,
        },
        agents: t.agents.slice(),
    }));

    // 任务：6 个，覆盖 pending → completed 各种状态
    const tasks: Task[] = [
        taskOf('t-001', '梳理 Organization Layer 数据模型', 'pending', 0),
        taskOf('t-002', '实现 WorldProjector reducer', 'active', 1),
        taskOf('t-003', 'Three.js 渲染骨架', 'active', 2),
        taskOf('t-004', 'Agent placeholder 视觉与状态机', 'pending', 3),
        taskOf('t-005', 'Avatar 抽象与未来接入规划', 'pending', 4),
        taskOf('t-006', 'Activity 可视化与交互设计', 'blocked', 5),
    ];

    // 把 assignee 填上：让每个任务挂到具体 team 的前 2 个 agent
    const activeTaskByTeam: Record<number, Task> = {};
    for (let t = 0; t < tasks.length; t += 1) {
        const task = tasks[t]!;
        const teamIdx = t % teams.length;
        const team = teams[teamIdx]!;
        const pick = Math.min(3, team.agents.length);
        const assignees: AgentId[] = [];
        for (let n = 0; n < pick; n += 1) {
            assignees.push(team.agents[n]!.id);
        }
        const merged: Task = { ...task, assigneeAgentIds: assignees };
        tasks[t] = merged;
        if (task.status === 'active' || task.status === 'blocked') {
            activeTaskByTeam[teamIdx] = merged;
        }
    }

    // 协作：3 个（演示 gathering）
    const collaborations: PlannedCollaboration[] = [
        {
            id: newCollaborationId(),
            participants: [
                teams[0]!.agents[0]!.id,
                teams[0]!.agents[2]!.id,
                teams[0]!.agents[3]!.id,
            ],
            taskId: tasks[1]!.id,
            delayMs: 6_000,
            durationMs: 8_000,
        },
        {
            id: newCollaborationId(),
            participants: [
                teams[2]!.agents[0]!.id,
                teams[2]!.agents[5]!.id,
                teams[1]!.agents[1]!.id,
            ],
            taskId: tasks[2]!.id,
            delayMs: 12_000,
            durationMs: 6_000,
        },
        {
            id: newCollaborationId(),
            participants: [teams[3]!.agents[3]!.id, teams[0]!.agents[5]!.id],
            taskId: tasks[4]!.id,
            delayMs: 22_000,
            durationMs: 5_000,
        },
    ];

    // Activity 计划
    const activities: PlannedActivity[] = [];
    const kinds: Activity['kind'][] = ['working', 'waiting', 'reviewing', 'testing', 'reporting'];
    for (let t = 0; t < tasks.length; t += 1) {
        const task = tasks[t]!;
        const startOffset = t < 2 ? 2_000 : 5_000 + t * 1_500;
        for (let r = 0; r < task.assigneeAgentIds.length; r += 1) {
            const agentId = task.assigneeAgentIds[r]!;
            activities.push({
                agentId,
                kind: kinds[(t + r) % kinds.length]!,
                delayMs: startOffset + r * 800,
                durationMs: 14_000 + ((t + r) * 600) % 6_000,
                taskId: task.id,
                collaborationId: null,
            });
        }
    }
    for (let i = 0; i < teams.length; i += 1) {
        const idleAgents = teams[i]!.agents.slice(0, 2);
        for (let j = 0; j < idleAgents.length; j += 1) {
            activities.push({
                agentId: idleAgents[j]!.id,
                kind: 'idle',
                delayMs: 4_000 + j * 1_500,
                durationMs: 30_000,
                taskId: null,
                collaborationId: null,
            });
        }
    }

    return { teams: seedTeams, tasks, activities, collaborations };
}

function taskOf(raw: string, title: string, status: Task['status'], _idx: number): Task {
    return {
        id: raw as TaskId,
        title,
        goal: title,
        status,
        assigneeAgentIds: [],
        parentTaskId: null,
        createdAt: 0,
        startedAt: status === 'active' ? 0 : null,
        completedAt: status === 'completed' ? 0 : null,
    };
}

/** 用一个 EventSource 把模拟剧本展开为 OrganizationEvent 序列。 */
export class OrganizationSimulator implements OrganizationEventSource {
    readonly id: string;
    readonly source = 'simulator' as const;

    private readonly opts: { now: LogicalTime; tickMs: number; speed: number; roles: ReadonlyArray<Role>; seed: string };
    private readonly plan: SeedPlan;
    private readonly listeners = new Set<(event: OrganizationEvent) => void>();
    private state: OrganizationState;
    private playing = false;
    private cursor = 0;
    private rafId: ReturnType<typeof setTimeout> | null = null;
    private disposed = false;
    private alreadyEmittedStarts = new Set<string>();
    private alreadyEmittedEnds = new Set<string>();
    private alreadyEmittedCollabStarts = new Set<string>();
    private alreadyEmittedCollabEnds = new Set<string>();

    constructor(
        organizationId: OrganizationId = newOrganizationId(),
        opts: SimulatorOptions = {},
    ) {
        this.id = `sim:${organizationId}`;
        this.opts = {
            now: opts.now ?? 0,
            tickMs: opts.tickMs ?? 200,
            speed: opts.speed ?? 1,
            roles: opts.roles ?? DEFAULT_ROLES,
            seed: opts.seed ?? 'mirage-demo',
        };
        void makeRng(this.opts.seed); // future expansion; keeping deterministic
        this.plan = planSeed();
        this.state = {
            id: organizationId,
            name: 'Mira Demo Org',
            agents: {},
            teams: {},
            roles: Object.fromEntries(this.opts.roles.map((r) => [r.id, r])),
            tasks: {},
            activities: {},
            collaborations: {},
            logicalTime: this.opts.now,
            source: this.source,
        };
        // seed 与监听器注册顺序：构造函数内立即 seed；
        // 监听器通过 `subscribe()` 重新拉取历史事件（replayTo）。
        this.seed();
    }

    snapshot(): OrganizationState {
        return this.state;
    }

    subscribe(listener: (event: OrganizationEvent) => void): () => void {
        // 新订阅者立刻收到一份 snapshot_sync，包含已经 emit 过的全部事件结果。
        const initial: OrganizationEvent = {
            type: 'organization.snapshot_sync',
            at: this.state.logicalTime,
            source: this.state.source,
            organizationId: this.state.id,
            agents: this.state.agents,
            teams: this.state.teams,
            roles: this.state.roles,
            tasks: this.state.tasks,
            activities: this.state.activities,
            collaborations: this.state.collaborations,
        };
        listener(initial);
        this.listeners.add(listener);
        return () => {
            this.listeners.delete(listener);
        };
    }

    start(): void {
        if (this.playing || this.disposed) {
            return;
        }
        this.playing = true;
        this.scheduleTick();
    }

    pause(): void {
        this.playing = false;
        if (this.rafId !== null) {
            clearTimeout(this.rafId);
            this.rafId = null;
        }
    }

    setSpeed(speed: number): void {
        this.opts.speed = speed > 0 ? speed : 1;
    }

    stop(): void {
        this.pause();
    }

    /** 仅测试：把计数器清零（避免 planSeed 内部 newXxxId 累积）。 */
    static resetForTests(): void {
        resetIdCounterForTests();
    }

    dispose(): void {
        this.disposed = true;
        this.pause();
        this.listeners.clear();
    }

    // ----- internal --------------------------------------------------------

    private emit(event: OrganizationEvent): void {
        for (const listener of this.listeners) {
            listener(event);
        }
    }

    private scheduleTick(): void {
        if (!this.playing || this.disposed) {
            return;
        }
        this.rafId = setTimeout(
            () => this.tick(),
            Math.max(20, this.opts.tickMs / this.opts.speed),
        );
    }

    private tick(): void {
        if (!this.playing || this.disposed) {
            return;
        }
        this.cursor = Math.min(60_000, this.cursor + this.opts.tickMs);
        const now = this.opts.now + this.cursor;
        this.state = { ...this.state, logicalTime: now };

        // 触发计划内的 activity
        for (let i = 0; i < this.plan.activities.length; i += 1) {
            const act = this.plan.activities[i]!;
            const startKey = `${i}:${act.agentId}:start`;
            if (
                act.delayMs <= this.cursor &&
                act.delayMs > this.cursor - this.opts.tickMs &&
                !this.alreadyEmittedStarts.has(startKey)
            ) {
                this.alreadyEmittedStarts.add(startKey);
                const id = newActivityId();
                this.emit({
                    type: 'agent.activity_started',
                    at: now,
                    source: this.source,
                    activity: {
                        id,
                        agentId: act.agentId,
                        kind: act.kind,
                        taskId: act.taskId,
                        collaborationId: null,
                        startedAt: now,
                        endedAt: null,
                    },
                });
            }
            const endBoundary = act.delayMs + act.durationMs;
            const endKey = `${i}:${act.agentId}:end`;
            if (
                endBoundary <= this.cursor &&
                endBoundary > this.cursor - this.opts.tickMs &&
                !this.alreadyEmittedEnds.has(endKey)
            ) {
                this.alreadyEmittedEnds.add(endKey);
                const activity = Object.values(this.state.activities).find(
                    (a) =>
                        a.agentId === act.agentId &&
                        a.taskId === act.taskId &&
                        a.kind === act.kind &&
                        a.endedAt === null,
                );
                if (activity) {
                    this.emit({
                        type: 'agent.activity_ended',
                        at: now,
                        source: this.source,
                        agentId: act.agentId,
                        activityId: activity.id,
                    });
                }
            }
        }
        for (let i = 0; i < this.plan.collaborations.length; i += 1) {
            const col = this.plan.collaborations[i]!;
            const startKey = `collab:${i}:start`;
            if (
                col.delayMs <= this.cursor &&
                col.delayMs > this.cursor - this.opts.tickMs &&
                !this.alreadyEmittedCollabStarts.has(startKey)
            ) {
                this.alreadyEmittedCollabStarts.add(startKey);
                this.emit({
                    type: 'collaboration.started',
                    at: now,
                    source: this.source,
                    collaboration: {
                        id: col.id,
                        participants: col.participants,
                        taskId: col.taskId,
                        meetingSpaceId: null,
                        startedAt: now,
                        endedAt: null,
                    },
                });
            }
            const endBoundary = col.delayMs + col.durationMs;
            const endKey = `collab:${i}:end`;
            if (
                endBoundary <= this.cursor &&
                endBoundary > this.cursor - this.opts.tickMs &&
                !this.alreadyEmittedCollabEnds.has(endKey)
            ) {
                this.alreadyEmittedCollabEnds.add(endKey);
                this.emit({
                    type: 'collaboration.ended',
                    at: now,
                    source: this.source,
                    collaborationId: col.id,
                });
            }
        }

        this.scheduleTick();
    }

    private seed(): void {
        for (const { team } of this.plan.teams) {
            this.state = { ...this.state, teams: { ...this.state.teams, [team.id]: team } };
            this.emit({
                type: 'organization.team_created',
                at: this.opts.now,
                source: this.source,
                team,
            });
        }
        for (const { agents } of this.plan.teams) {
            for (const agent of agents) {
                const a: Agent = {
                    id: agent.id,
                    displayName: agent.displayName,
                    roleId: agent.roleId,
                    teamId: agent.teamId,
                    lifecycle: 'active',
                    currentActivityId: null,
                    currentTaskId: null,
                    accent: agent.accent,
                    joinedAt: this.opts.now,
                };
                this.state = { ...this.state, agents: { ...this.state.agents, [a.id]: a } };
                this.emit({
                    type: 'agent.created',
                    at: this.opts.now,
                    source: this.source,
                    agent: a,
                });
            }
        }
        for (const task of this.plan.tasks) {
            const seeded: Task = {
                ...task,
                startedAt: task.status === 'active' ? this.opts.now : null,
                completedAt: task.status === 'completed' ? this.opts.now : null,
            };
            this.state = { ...this.state, tasks: { ...this.state.tasks, [seeded.id]: seeded } };
            this.emit({
                type: 'task.created',
                at: this.opts.now,
                source: this.source,
                task: seeded,
            });
        }
        for (const task of this.plan.tasks) {
            if (task.assigneeAgentIds.length > 0) {
                this.state = {
                    ...this.state,
                    tasks: {
                        ...this.state.tasks,
                        [task.id]: { ...this.state.tasks[task.id]!, assigneeAgentIds: task.assigneeAgentIds },
                    },
                };
                this.emit({
                    type: 'task.assigned',
                    at: this.opts.now,
                    source: this.source,
                    taskId: task.id,
                    assigneeAgentIds: task.assigneeAgentIds,
                });
            }
        }
    }
}