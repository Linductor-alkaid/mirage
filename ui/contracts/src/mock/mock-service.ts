/// In-memory mock of the M1 Runtime Service (M1.5-04): a scripted task state
/// machine plus a DEC-012-draft event surface, letting the UI run the full
/// submit -> step-level progress -> cancel -> terminal flow in the browser
/// with no backend. Scope discipline: this simulates observable *wire*
/// behaviour (states, stable error shapes, event seq/overflow semantics);
/// it does not reimplement service internals. The `fail:` step convention
/// below is mock-only, documented, and never produced by the real service.

import { BoundedEventQueue } from '../events.js';
import type { DesktopObserveInput, EventListener, MirageTransport, SessionHistoryInput, SubmitTaskInput, WorkflowDefinition, WorkflowStartInput } from '../transport.js';
import { IpcRequestError, TransportClosedError } from '../transport.js';
import type {
    ExposedTool,
    HostStatus,
    InspectTask,
    ObservationView,
    PendingPermission,
    PolicyView,
    ServerEvent,
    ServiceIdentity,
    SessionHistoryEntry,
    SessionState,
    SessionSummary,
    StepView,
    TaskProgress,
    TaskSummary,
    WorkflowDefinitionView,
    WorkflowRunState,
    WorkflowRunSummary,
    WorkflowSummary,
} from '../types.js';
import { PROTOCOL_VERSION } from '../types.js';

export interface MockServiceOptions {
    /** Wall time per step, milliseconds (default 700; 0 advances immediately). */
    stepDurationMs?: number;
    /** starting -> running delay, milliseconds (default 500). */
    hostStartDelayMs?: number;
    /** stopping -> stopped delay on shutdown, milliseconds (default 300). */
    shutdownDelayMs?: number;
    /** Task registry capacity; submit refuses when full (default 256). */
    taskCapacity?: number;
    /** Per-subscription event queue capacity, drop-oldest (default 64). */
    eventQueueCapacity?: number;
    /** When false, hello omits the `events` capability and subscribe()
     * fails with `unsupported` — drives the UI degradation path (M1.5-05). */
    eventsCapability?: boolean;
    /** When false, hello omits the `workflows` capability (DEC-023) —
     * drives the UI's workflow-face fallback path. */
    workflowsCapability?: boolean;
    /** When false, hello omits the `sessions` capability (DEC-021) —
     * drives the UI's session-face fallback path (DEC-025). */
    sessionsCapability?: boolean;
    /** When false, hello omits the `observation` capability (DEC-026) and
     * desktop.observe fails with the old-server unknown-op shape — drives
     * the UI's observation-face fallback path. */
    observationCapability?: boolean;
    /** When false, hello omits the `chat` capability (DEC-027) and
     * session.chat fails with the old-server unknown-op shape — drives the
     * UI's dialog-face fallback path. */
    chatCapability?: boolean;
    /** When false, hello omits the `policy` capability (M5-07) and the
     * policy face fails with the old-server unknown-op shape. The mock's
     * in-memory policy seeds from the DEC-010 defaults. */
    policyCapability?: boolean;
    /** When false, hello omits the `permissions` capability (DEC-020) and
     * the approval center goes dark — mirrors a headless service without a
     * confirmation hub. */
    permissionsCapability?: boolean;
    /** 'auto' (default) flushes queues via microtasks; 'manual' only
     * enqueues until flush() is called — the deterministic hook for
     * overflow tests. */
    flushMode?: 'auto' | 'manual';
}

/** Event frame without the per-subscription seq, which is assigned at
 * enqueue time so delivery order matches seq order. */
type DistributiveOmit<T, K extends keyof never> = T extends unknown ? Omit<T, K> : never;
type EventFrame = DistributiveOmit<ServerEvent, 'seq'>;

interface MockStep {
    kind: StepView['kind'];
    argument: string;
    status: StepView['status'];
    operationId: string;
    exitCode: number;
    result: string;
    resultTruncated: boolean;
    error: string;
}

interface MockTask {
    id: string;
    goal: string;
    steps: MockStep[];
    progress: TaskProgress;
    hasSuccess: boolean;
    success: boolean;
    cancelRequested: boolean;
    timer: ReturnType<typeof setTimeout> | null;
    /** DEC-021 conversation owner (resolved at submit; empty = pre-face). */
    sessionId: string;
}

// ---------------------------------------------------------------------------
// Session face (DEC-021, consumed since DEC-025/M5-06): a wire-faithful
// mirror of the observable behaviour — a registry holding the primary
// session, capacity-bounded session.open, a per-session conversation journal
// (user/outcome entries with monotonic sequence numbers) and the session.*
// event set published at the same points the real service publishes them.
// The pinned projection internals are NOT re-implemented; the outcome
// wording mirrors the conversation view's settled sentence shape.
// ---------------------------------------------------------------------------

interface MockJournalEntry {
    kind: 'user' | 'outcome';
    text: string;
    sequence: number;
    recorded_at_ms: number;
}

interface MockChatTurn {
    turn_id: string;
    status: 'pending' | 'ok' | 'failed';
    user_text: string;
    reply_text?: string;
    error?: string;
    sequence: number;
    recorded_at_ms: number;
}

interface MockSession {
    id: string;
    state: 'autonomous';
    created_at_ms: number;
    journal: MockJournalEntry[];
    /** DEC-027 dialog thread: bounded turn log + in-flight latch. */
    chatTurns: MockChatTurn[];
    nextChatSequence: number;
    inFlightChatTurnId: string;
}

const SESSION_REGISTRY_CAPACITY = 16;
const HISTORY_DEFAULT_LIMIT = 50;

let mockTurnCounter = 0;

/** Deterministic 32-hex-shaped turn identity suffix for the mock. */
function mockTurnSuffix(): string {
    mockTurnCounter += 1;
    const base = mockTurnCounter.toString(16).padStart(8, '0');
    return `${base}${base}${base}${base}`;
}

function mockSessionId(number: number): string {
    const tail = number.toString(16).padStart(8, '0');
    return `${tail}${tail}${tail}${tail}`;
}

/** The conversation view's settled sentence shape (DEC-021): the outcome
 * wording the pinned projection composes. */
function outcomeSentence(progress: string, steps: number): string {
    return `loop settled: ${progress} (steps ${steps})`;
}

interface Subscription {
    listener: EventListener;
    queue: BoundedEventQueue<ServerEvent>;
    nextSeq: number;
    droppedSinceOverflow: number;
    flushScheduled: boolean;
}

// ---------------------------------------------------------------------------
// Workflow face (DEC-023/DEC-024): in-memory simulation of the observable
// wire behaviour — append-style draft/publish with validation states,
// content-addressed digests, a closed atom catalog (the M1 reference
// binding's two atoms, mirroring what a live test service exposes) and
// run lifecycle events. It deliberately does NOT re-implement the pinned
// IR parser: definitions are checked for the members the wire contract
// pins, everything structural is the real service's job.
// ---------------------------------------------------------------------------

interface MockWorkflowEntry {
    definition: WorkflowDefinition;
    digest: string;
    validation: 'not_validated' | 'dry_run_passed';
    updated_at_ms: number;
}

interface MockWorkflowRun {
    run_id: string;
    workflow_id: string;
    state: WorkflowRunState;
    run_epoch: number;
    created_at_ms: number;
    timer: ReturnType<typeof setTimeout> | null;
}

const MAX_WORKFLOW_DEFINITION_BYTES = 256 * 1024;
const WORKFLOW_REGISTRY_CAPACITY = 128;
const WORKFLOW_RUN_CAPACITY = 256;

function canonicalDefinition(value: WorkflowDefinition): string {
    return JSON.stringify(value);
}

/** Mock content digest: stable per content, shaped like the pinned
 * sha-256 hex (64 chars) so the UI's display paths are exercised. */
function mockDigest(content: string): string {
    let h1 = 0x811c9dc5;
    let h2 = 0x1000193;
    for (let i = 0; i < content.length; i += 1) {
        const ch = content.charCodeAt(i);
        h1 = Math.imul(h1 ^ ch, 0x01000193) >>> 0;
        h2 = Math.imul(h2 + ch + i, 0x85ebca6b) >>> 0;
    }
    return (h1.toString(16).padStart(8, '0') + h2.toString(16).padStart(8, '0')).repeat(4);
}

function isHexId(value: unknown, length: number): value is string {
    return typeof value === 'string' && value.length === length && /^[0-9a-f]+$/.test(value);
}

const TERMINAL_RUN_STATES: readonly WorkflowRunState[] = ['completed', 'failed', 'cancelled'];

function isTerminalRunState(state: WorkflowRunState): boolean {
    return TERMINAL_RUN_STATES.includes(state);
}

/** Deterministic mock run id shaped like the pinned 32-hex identities. */
function mockRunId(number: number): string {
    const tail = number.toString(16).padStart(8, '0');
    return `${tail}${tail}${tail}${tail}`;
}

/** True when a definition carries a side-effect step whose verification
 * reads a run_parameter that the caller did not bind (mirrors the pinned
 * fail-closed outcome: NotEvaluable verification fails dispatching runs). */
function mockVerificationUnbound(definition: WorkflowDefinition, parameters: Record<string, unknown>): boolean {
    const steps = (definition as { steps?: Array<{ kind?: unknown; verification?: { signal?: unknown } }> })
        .steps;
    if (!Array.isArray(steps)) {
        return false;
    }
    for (const step of steps) {
        const verification = step.verification;
        if (step.kind !== 'tool_call' || verification === undefined) {
            continue;
        }
        const signal = typeof verification.signal === 'string' ? verification.signal : '';
        if (!signal.startsWith('run_parameter:')) {
            continue;
        }
        if (!(signal.slice('run_parameter:'.length) in parameters)) {
            return true;
        }
    }
    return false;
}

/** The M1 reference binding's exposed atoms: what a service with the test
 * desktop environment actually reports (DEC-024; the live-service catalog
 * carries exactly these two atoms until more providers bind). */
export const MOCK_ATOM_CATALOG: readonly ExposedTool[] = [
    {
        wire_name: 'desktop.filesystem.read_text',
        version: '1.0.0',
        description: 'Reads a UTF-8 text file (permission-gated read).',
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
        description: 'Executes a command line and captures its output (gated side effect).',
        has_side_effects: true,
        parameters_schema: {
            type: 'object',
            properties: {
                command: { type: 'string', description: 'Command line' },
                timeout_ms: {
                    type: 'integer',
                    minimum: 1000,
                    maximum: 120000,
                    description: 'Wall-clock budget in milliseconds (default 30000)',
                },
            },
            required: ['command'],
        },
    },
];

/** Draft seed: a filesystem read with one {"$param"} reference (parseable,
 * not runnable — W-04 semantics observable through workflow.list). */
function seedReadDraft(): WorkflowDefinition {
    return {
        schema_version: { major: 1, minor: 0 },
        workflow_id: 'a1000000000000000000000000000001',
        name: '读取说明文件',
        summary: '读取桌面说明文本（草稿种子，演示 W-04 草稿态）',
        parameters: [
            { name: 'path', type: 'string', required: true, summary: '说明文件路径' },
        ],
        steps: [
            {
                step_id: 'b1000000000000000000000000000001',
                name: '读取文本',
                kind: 'tool_call',
                arguments: { tool: 'desktop.filesystem.read_text', path: { $param: 'path' } },
            },
        ],
        default_policy: 'strict',
        allowed_policies: ['strict', 'dry_run'],
    };
}

/** Published seed: the same read, through the DryRun gate; a run completes. */
function seedReadPublished(): WorkflowDefinition {
    const def = seedReadDraft();
    return {
        ...def,
        workflow_id: 'a1000000000000000000000000000002',
        name: '读取系统信息',
        summary: '已发布的读取流程（种子，可运行）',
        steps: [
            {
                step_id: 'b1000000000000000000000000000002',
                name: '读取文本',
                kind: 'tool_call',
                arguments: { tool: 'desktop.filesystem.read_text', path: 'C:\\mirage\\README.md' },
            },
        ],
    };
}

/** Published seed with a side effect: W-02 forces the verification
 * predicate; a run without the bound parameter fails (NotEvaluable is
 * fail closed on dispatching policies — the mock mirrors that outcome). */
function seedSideEffect(): WorkflowDefinition {
    return {
        schema_version: { major: 1, minor: 0 },
        workflow_id: 'a1000000000000000000000000000003',
        name: '清理临时目录',
        summary: '带副作用原子的发布流程（W-02 验证谓词种子）',
        parameters: [
            { name: 'confirmed', type: 'boolean', required: false, summary: '副作用验证谓词绑定' },
        ],
        steps: [
            {
                step_id: 'b1000000000000000000000000000003',
                name: '执行清理',
                kind: 'tool_call',
                arguments: { tool: 'desktop.process.execute', command: 'echo demo' },
                verification: { signal: 'run_parameter:confirmed', op: 'eq', value: true },
            },
        ],
        default_policy: 'strict',
        allowed_policies: ['strict', 'dry_run'],
    };
}

const TERMINAL_PROGRESS: readonly TaskProgress[] = ['Completed', 'Failed', 'Cancelled'];

function isTerminal(progress: TaskProgress): boolean {
    return TERMINAL_PROGRESS.includes(progress);
}

/** Mock-only failure convention: a process.execute step whose argument
 * starts with `fail:` fails after its simulated run (exit code 1). */
function failureReason(step: MockStep): string | null {
    if (step.kind === 'process.execute' && step.argument.startsWith('fail:')) {
        return `mock step failure: ${step.argument.slice('fail:'.length).trim() || 'injected failure'}`;
    }
    return null;
}

/**
 * Semantic core of the mock: host lifecycle, task registry, scripted step
 * driver and the DEC-012-draft event bus (per-subscription seq, bounded
 * drop-oldest queues, `events.overflow` markers).
 */
export class MockMirageService {
    private hostStatus: HostStatus = 'stopped';
    private hostTimer: ReturnType<typeof setTimeout> | null = null;
    private readonly tasks = new Map<string, MockTask>();
    private readonly subscriptions = new Set<Subscription>();
    private readonly workflows = new Map<string, MockWorkflowEntry>();
    private readonly workflowRuns = new Map<string, MockWorkflowRun>();
    private readonly sessions = new Map<string, MockSession>();
    /** DEC-021 主会话 id（构造时入册）；session.close 的产品侧守卫锚点。 */
    private primarySessionId = '';
    /** M5-07 policy face：内存规则集（DEC-010 默认 + policy.set 覆盖）。 */
    private readonly policyRules = new Map<string, string>([
        ['filesystem.read', 'allow'],
        ['filesystem.write', 'deny'],
        ['process.execute', 'allow'],
        ['window.activate', 'allow'],
        ['screen.capture', 'allow'],
        ['input.inject', 'allow'],
        ['clipboard.read', 'allow'],
        ['clipboard.write', 'allow'],
        ['application.launch', 'allow'],
        ['application.terminate', 'allow'],
        ['notification.post', 'allow'],
    ]);
    private policyReadRoots: string[] = [];
    /** DEC-020 挂起确认（mock-only demo 钩子产生）。 */
    private readonly pendingApprovals = new Map<
        string,
        {
            request_id: string;
            capability: string;
            resource: string;
            task_id: string;
            expires_at: number;
            timer: ReturnType<typeof setTimeout> | null;
        }
    >();
    private nextPermissionNumber = 1;
    private nextTaskNumber = 1;
    private nextOperationNumber = 1;
    private nextRunNumber = 1;
    private nextDryRunNumber = 1;
    private nextSessionNumber = 1;
    private closed = false;
    private readonly options: Required<
        Pick<MockServiceOptions, 'stepDurationMs' | 'hostStartDelayMs' | 'shutdownDelayMs' | 'taskCapacity' | 'eventQueueCapacity' | 'eventsCapability' | 'workflowsCapability' | 'sessionsCapability' | 'observationCapability' | 'chatCapability' | 'policyCapability' | 'permissionsCapability' | 'flushMode'>
    >;

    constructor(options: MockServiceOptions = {}) {
        this.options = {
            stepDurationMs: options.stepDurationMs ?? 700,
            hostStartDelayMs: options.hostStartDelayMs ?? 500,
            shutdownDelayMs: options.shutdownDelayMs ?? 300,
            taskCapacity: options.taskCapacity ?? 256,
            eventQueueCapacity: options.eventQueueCapacity ?? 64,
            eventsCapability: options.eventsCapability ?? true,
            workflowsCapability: options.workflowsCapability ?? true,
            sessionsCapability: options.sessionsCapability ?? true,
            observationCapability: options.observationCapability ?? true,
            chatCapability: options.chatCapability ?? true,
            policyCapability: options.policyCapability ?? true,
            permissionsCapability: options.permissionsCapability ?? true,
            flushMode: options.flushMode ?? 'auto',
        };
        // DEC-021: the primary session enters the registry up front, so
        // session.list always reports at least one entry. Its id anchors
        // session.close's primary guard (DEC-026).
        this.primarySessionId = this.registerSession(Date.now() - 3_600_000).id;
        const seed = (definition: WorkflowDefinition, validation: MockWorkflowEntry['validation']): void => {
            const id = (definition as { workflow_id: string }).workflow_id;
            this.workflows.set(id, {
                definition,
                digest: mockDigest(canonicalDefinition(definition)),
                validation,
                updated_at_ms: Date.now() - 3_600_000,
            });
        };
        seed(seedReadDraft(), 'not_validated');
        seed(seedReadPublished(), 'dry_run_passed');
        seed(seedSideEffect(), 'dry_run_passed');
        this.setHostStatus('starting');
        if (this.options.hostStartDelayMs === 0) {
            this.setHostStatus('running');
        } else {
            this.hostTimer = setTimeout(() => {
                this.hostTimer = null;
                this.setHostStatus('running');
            }, this.options.hostStartDelayMs);
        }
    }

    // -- lifecycle ---------------------------------------------------------

    /** Releases the mock: further requests are refused, subscriptions are
     * dropped, in-flight timers are cancelled. Idempotent. */
    close(): void {
        this.closed = true;
        if (this.hostTimer !== null) {
            clearTimeout(this.hostTimer);
            this.hostTimer = null;
        }
        for (const task of this.tasks.values()) {
            if (task.timer !== null) {
                clearTimeout(task.timer);
                task.timer = null;
            }
        }
        for (const run of this.workflowRuns.values()) {
            if (run.timer !== null) {
                clearTimeout(run.timer);
                run.timer = null;
            }
        }
        this.subscriptions.clear();
    }

    private assertOpen(): void {
        if (this.closed) {
            throw new TransportClosedError('mock service is closed');
        }
    }

    // -- request surface ----------------------------------------------------

    hello(): ServiceIdentity {
        this.assertOpen();
        const identity: ServiceIdentity = {
            service: 'mirage-runtime',
            mirage_version: '0.1.0',
            mira_core_version: '0.1.0',
            host_status: this.hostStatus,
            protocol: PROTOCOL_VERSION,
        };
        if (this.options.eventsCapability) {
            identity.events = true;
        }
        if (this.options.workflowsCapability) {
            identity.workflows = true;
        }
        if (this.options.sessionsCapability) {
            identity.sessions = true;
        }
        if (this.options.observationCapability) {
            identity.observation = true;
        }
        if (this.options.chatCapability) {
            identity.chat = true;
        }
        if (this.options.policyCapability) {
            identity.policy = true;
        }
        if (this.options.permissionsCapability) {
            identity.permissions = true;
        }
        return identity;
    }

    submit(request: SubmitTaskInput): { task_id: string } {
        this.assertOpen();
        if (this.hostStatus !== 'running') {
            throw new IpcRequestError(
                'invalid_state',
                `mira host is not running (status: ${this.hostStatus})`,
            );
        }
        const goal = request.goal.trim();
        if (goal.length === 0) {
            throw new IpcRequestError('invalid_argument', "task.submit requires a non-empty 'goal'");
        }
        if (this.tasks.size >= this.options.taskCapacity) {
            throw new IpcRequestError(
                'invalid_state',
                `task registry at capacity (${this.options.taskCapacity})`,
            );
        }
        const session = this.resolveSubmitSession(request.session_id);
        const steps: MockStep[] = request.steps.map((step) => {
            if (step.arg.length === 0) {
                throw new IpcRequestError(
                    'invalid_argument',
                    "task.submit step requires a non-empty 'arg'",
                );
            }
            return {
                kind: step.op,
                argument: step.arg,
                status: 'pending',
                operationId: '',
                exitCode: -1,
                result: '',
                resultTruncated: false,
                error: '',
            };
        });
        const id = `task-${String(this.nextTaskNumber).padStart(4, '0')}`;
        this.nextTaskNumber += 1;
        const task: MockTask = {
            id,
            goal,
            steps,
            progress: 'Active',
            hasSuccess: false,
            success: false,
            cancelRequested: false,
            timer: null,
            sessionId: session.id,
        };
        this.tasks.set(id, task);
        // DEC-021: the goal joins the session journal before the submit
        // response; the notification rides the same ordering.
        this.appendJournalMessage(session, 'user', goal, id);
        this.publishTaskUpdated(task);
        this.advance(task);
        return { task_id: id };
    }

    list(): TaskSummary[] {
        this.assertOpen();
        return [...this.tasks.values()].map((task) => ({
            id: task.id,
            goal: task.goal,
            progress: task.progress,
        }));
    }

    inspect(taskId: string): InspectTask {
        this.assertOpen();
        const task = this.tasks.get(taskId);
        if (task === undefined) {
            throw new IpcRequestError('not_found', `task '${taskId}' was not found`);
        }
        return {
            id: task.id,
            goal: task.goal,
            progress: task.progress,
            has_success: task.hasSuccess,
            success: task.hasSuccess ? task.success : undefined,
            steps: task.steps.map((step, index) => ({
                index,
                kind: step.kind,
                status: step.status,
                operation_id: step.operationId,
                permission: 'allowed',
                ok: step.status === 'ok',
                exit_code: step.exitCode,
                result: step.result,
                result_truncated: step.resultTruncated,
                error: step.error,
            })),
        };
    }

    cancel(taskId: string): { task_id: string; progress: TaskProgress } {
        this.assertOpen();
        const task = this.tasks.get(taskId);
        if (task === undefined) {
            throw new IpcRequestError('not_found', `task '${taskId}' was not found`);
        }
        if (isTerminal(task.progress)) {
            // Same passthrough shape as the real service: the pinned
            // rejection is surfaced verbatim instead of reviving the task.
            throw new IpcRequestError(
                'pinned_runtime',
                `invalid_state: task '${taskId}' already settled as ${task.progress}`,
            );
        }
        if (task.progress === 'Cancelling') {
            throw new IpcRequestError(
                'pinned_runtime',
                `invalid_state: task '${taskId}' is already cancelling`,
            );
        }
        task.cancelRequested = true;
        task.progress = 'Cancelling';
        this.publishTaskUpdated(task);
        if (task.timer !== null) {
            clearTimeout(task.timer);
            task.timer = null;
            this.unwindCancelled(task);
        }
        // A step boundary was reached concurrently; advance() observes the
        // flag and unwinds there.
        return { task_id: task.id, progress: task.progress };
    }

    shutdown(): void {
        this.assertOpen();
        if (this.hostStatus === 'stopped' || this.hostStatus === 'stopping') {
            return;
        }
        // Shutdown converges to stopped from any non-terminal state: a
        // pending starting timer must not resurrect 'running' afterwards.
        if (this.hostTimer !== null) {
            clearTimeout(this.hostTimer);
            this.hostTimer = null;
        }
        this.setHostStatus('stopping');
        const settle = () => this.setHostStatus('stopped');
        if (this.options.shutdownDelayMs === 0) {
            settle();
        } else {
            setTimeout(settle, this.options.shutdownDelayMs);
        }
    }

    // -- workflow face (DEC-023/DEC-024) -------------------------------------
    //
    // Simulates the observable wire behaviour: registry capacity + byte
    // budget rejections, content-addressed digests, W-04 draft semantics,
    // DryRun-gated idempotent publish, delete guards, and run lifecycle
    // events. Structural IR validation is NOT re-implemented — the mock
    // checks the members the wire contract pins and lets the UI's real
    // failures come from the real service.

    /** Members the wire contract pins on every definition; everything else
     * (limits, predicate shapes, control jumps) is the service's job. */
    private checkDefinition(definition: WorkflowDefinition): void {
        if (canonicalDefinition(definition).length > MAX_WORKFLOW_DEFINITION_BYTES) {
            throw new IpcRequestError(
                'invalid_argument',
                `workflow definition exceeds the ${MAX_WORKFLOW_DEFINITION_BYTES} byte budget`,
            );
        }
        const id = (definition as { workflow_id?: unknown }).workflow_id;
        if (!isHexId(id, 32)) {
            throw new IpcRequestError('invalid_argument', 'malformed workflow identity');
        }
        const record = definition as {
            name?: unknown;
            parameters?: unknown;
            steps?: unknown;
            default_policy?: unknown;
            allowed_policies?: unknown;
        };
        if (typeof record.name !== 'string' || record.name.length === 0) {
            throw new IpcRequestError('invalid_argument', 'definition requires a name');
        }
        if (!Array.isArray(record.steps)) {
            throw new IpcRequestError('invalid_argument', 'definition requires steps');
        }
        for (const step of record.steps) {
            const stepId = (step as { step_id?: unknown }).step_id;
            if (!isHexId(stepId, 32)) {
                throw new IpcRequestError('invalid_argument', 'step requires a step_id');
            }
        }
        if (record.default_policy !== undefined && typeof record.default_policy !== 'string') {
            throw new IpcRequestError('invalid_argument', 'default_policy must be a policy name');
        }
        if (record.allowed_policies !== undefined && !Array.isArray(record.allowed_policies)) {
            throw new IpcRequestError('invalid_argument', 'allowed_policies must be an array');
        }
    }

    workflowList(): WorkflowSummary[] {
        this.assertOpen();
        const summaries: WorkflowSummary[] = [];
        for (const [workflow_id, entry] of this.workflows) {
            summaries.push({
                workflow_id,
                name: (entry.definition as { name: string }).name,
                head_digest: entry.digest,
                validation: entry.validation,
                runnable: entry.validation === 'dry_run_passed',
                updated_at_ms: entry.updated_at_ms,
            });
        }
        return summaries.sort((a, b) => a.workflow_id.localeCompare(b.workflow_id));
    }

    workflowSave(definition: WorkflowDefinition): { workflow_id: string; digest: string } {
        this.assertOpen();
        this.checkDefinition(definition);
        const workflowId = definition.workflow_id as string;
        if (!this.workflows.has(workflowId) && this.workflows.size >= WORKFLOW_REGISTRY_CAPACITY) {
            throw new IpcRequestError(
                'unavailable',
                `workflow registry capacity exhausted (${WORKFLOW_REGISTRY_CAPACITY})`,
            );
        }
        const digest = mockDigest(canonicalDefinition(definition));
        this.workflows.set(workflowId, {
            definition,
            digest,
            validation: 'not_validated',
            updated_at_ms: Date.now(),
        });
        return { workflow_id: workflowId, digest };
    }

    workflowPublish(
        definition: WorkflowDefinition,
    ): { workflow_id: string; digest: string; dry_run_id: string; idempotent: boolean } {
        this.assertOpen();
        this.checkDefinition(definition);
        const workflowId = definition.workflow_id as string;
        if (!this.workflows.has(workflowId) && this.workflows.size >= WORKFLOW_REGISTRY_CAPACITY) {
            throw new IpcRequestError(
                'unavailable',
                `workflow registry capacity exhausted (${WORKFLOW_REGISTRY_CAPACITY})`,
            );
        }
        const digest = mockDigest(canonicalDefinition(definition));
        const head = this.workflows.get(workflowId);
        const idempotent = head !== undefined && head.digest === digest && head.validation === 'dry_run_passed';
        this.workflows.set(workflowId, {
            definition,
            digest,
            validation: 'dry_run_passed',
            updated_at_ms: Date.now(),
        });
        const dry_run_id = `dry-${String(this.nextDryRunNumber).padStart(4, '0')}`;
        this.nextDryRunNumber += 1;
        return { workflow_id: workflowId, digest, dry_run_id, idempotent };
    }

    workflowDelete(workflowId: string): { workflow_id: string } {
        this.assertOpen();
        if (!this.workflows.has(workflowId)) {
            throw new IpcRequestError('not_found', 'unknown workflow id');
        }
        for (const run of this.workflowRuns.values()) {
            if (run.workflow_id === workflowId && !isTerminalRunState(run.state)) {
                throw new IpcRequestError('invalid_state', 'workflow has non-terminal runs');
            }
        }
        this.workflows.delete(workflowId);
        return { workflow_id: workflowId };
    }

    /** The definition read face (DEC-026): the mock registry already keeps
     * the definition content it was given, so the read projects it verbatim
     * — the same product-catalog shape the real service serves. */
    workflowGet(workflowId: string): WorkflowDefinitionView {
        this.assertOpen();
        const entry = this.workflows.get(workflowId);
        if (entry === undefined) {
            throw new IpcRequestError('not_found', 'unknown workflow id');
        }
        return { workflow_id: workflowId, digest: entry.digest, definition: entry.definition };
    }

    /** The observation face (DEC-026): a deterministic simulated-desktop
     * capture with the exact wire shape — frame members always, the
     * semantic snapshot per request, and the co-present visual pair per
     * request (a published generation exists in the mock topology). No
     * event form exists on the wire, so none is simulated. */
    desktopObserve(input: DesktopObserveInput = {}): ObservationView {
        this.assertOpen();
        if (!this.options.observationCapability) {
            throw new IpcRequestError('protocol_error', "unknown op 'desktop.observe'");
        }
        if (this.hostStatus !== 'running') {
            throw new IpcRequestError('unavailable', 'no desktop environment bound');
        }
        const semantic = input.semantic ?? true;
        const visual = input.visual ?? false;
        const view: ObservationView = {
            active_application: 'Mock Studio',
            active_window: 'simulated desktop — session workbench',
            window_geometry: { x: 0, y: 0, width: 1280, height: 800 },
            window_focused: true,
            focused_element: '@e2',
            pointer_x: 640,
            pointer_y: 400,
            environment_state: 'mock',
        };
        if (semantic) {
            view.semantic = {
                application: 'Mock Studio',
                window_title: 'simulated desktop — session workbench',
                nodes: [
                    {
                        ref: '@e1',
                        role: 'panel',
                        name: 'sidebar',
                        description: '',
                        parent: -1,
                        geometry: { x: 0, y: 0, width: 220, height: 800 },
                        focused: false,
                        enabled: true,
                    },
                    {
                        ref: '@e2',
                        role: 'editor',
                        name: 'task composer',
                        description: 'simulated composer surface',
                        parent: -1,
                        geometry: { x: 240, y: 32, width: 800, height: 480 },
                        focused: true,
                        enabled: true,
                    },
                    {
                        ref: '@e3',
                        role: 'button',
                        name: 'submit',
                        description: '',
                        parent: -1,
                        geometry: { x: 960, y: 520, width: 96, height: 32 },
                        focused: false,
                        enabled: true,
                    },
                ],
                truncated: false,
            };
        }
        if (visual) {
            view.visual_snapshot_ref = '@vs1';
            view.visual_regions = [
                {
                    ref: '@v1',
                    source: 'ocr',
                    geometry: { x: 240, y: 32, width: 320, height: 28 },
                    text: 'session workbench',
                    template_id: '',
                },
                {
                    ref: '@v2',
                    source: 'template',
                    geometry: { x: 960, y: 520, width: 96, height: 32 },
                    text: '',
                    template_id: 'cache:submit',
                },
            ];
        }
        return view;
    }

    workflowAtomCatalog(): ExposedTool[] {
        this.assertOpen();
        return [...MOCK_ATOM_CATALOG];
    }

    workflowRunList(): WorkflowRunSummary[] {
        this.assertOpen();
        const runs: WorkflowRunSummary[] = [];
        for (const run of this.workflowRuns.values()) {
            runs.push({
                run_id: run.run_id,
                workflow_id: run.workflow_id,
                state: run.state,
                run_epoch: run.run_epoch,
                created_at_ms: run.created_at_ms,
            });
        }
        return runs.sort((a, b) => b.created_at_ms - a.created_at_ms);
    }

    workflowRun(input: WorkflowStartInput): { run_id: string } {
        this.assertOpen();
        if (this.hostStatus !== 'running') {
            throw new IpcRequestError('invalid_state', `mira host is not running (status: ${this.hostStatus})`);
        }
        const entry = this.workflows.get(input.workflow_id);
        if (entry === undefined) {
            throw new IpcRequestError('not_found', 'unknown workflow id');
        }
        const digest = input.digest ?? entry.digest;
        if (!isHexId(digest, 64)) {
            throw new IpcRequestError('invalid_argument', 'malformed workflow digest');
        }
        if (digest !== entry.digest || entry.validation !== 'dry_run_passed') {
            // W-04 passthrough: only the runnable head version admits runs.
            throw new IpcRequestError('invalid_state', 'workflow version is not runnable');
        }
        if (this.workflowRuns.size >= WORKFLOW_RUN_CAPACITY) {
            throw new IpcRequestError(
                'unavailable',
                `workflow run registry capacity exhausted (${WORKFLOW_RUN_CAPACITY})`,
            );
        }
        const runId = mockRunId(this.nextRunNumber);
        this.nextRunNumber += 1;
        const run: MockWorkflowRun = {
            run_id: runId,
            workflow_id: input.workflow_id,
            state: 'created',
            run_epoch: 1,
            created_at_ms: Date.now(),
            timer: null,
        };
        this.workflowRuns.set(runId, run);
        const settle = (): void => {
            run.timer = null;
            if (run.state !== 'running') {
                return; // cancelled while driving
            }
            // Mock rule mirroring the pinned fail-closed verification: a
            // side-effect step whose run_parameter verification is not bound
            // by the caller's parameters fails the run.
            const failed = mockVerificationUnbound(entry.definition, input.parameters ?? {});
            this.settleRun(run, failed ? 'failed' : 'completed');
        };
        // The service publishes run events from the pinned event stream:
        // RunStarted arrives as 'running' (there is no wire 'created' event).
        run.state = 'running';
        this.publishRunUpdated(run);
        if (this.options.stepDurationMs === 0) {
            settle();
        } else {
            run.timer = setTimeout(settle, this.options.stepDurationMs * 2);
        }
        return { run_id: runId };
    }

    workflowCancel(runId: string): { run_id: string; state: WorkflowRunState } {
        this.assertOpen();
        const run = this.workflowRuns.get(runId);
        if (run === undefined) {
            throw new IpcRequestError('not_found', 'unknown workflow run');
        }
        // The pinned cancel_run is idempotent: terminal runs answer with
        // their state instead of failing.
        if (run.state === 'running' || run.state === 'created') {
            if (run.timer !== null) {
                clearTimeout(run.timer);
                run.timer = null;
            }
            this.settleRun(run, 'cancelled');
        }
        return { run_id: run.run_id, state: run.state };
    }

    private settleRun(run: MockWorkflowRun, state: WorkflowRunState): void {
        run.state = state;
        run.run_epoch += 1;
        this.publishRunUpdated(run);
    }

    private publishRunUpdated(run: MockWorkflowRun): void {
        this.publishFrame({
            v: 1,
            event: 'workflow.run_updated',
            run_id: run.run_id,
            workflow_id: run.workflow_id,
            state: run.state,
            run_epoch: run.run_epoch,
        });
    }

    // -- session face (DEC-021, consumed since DEC-025/M5-06) -----------------
    //
    // Simulates the observable wire behaviour: the primary session is born
    // into the registry, session.open is capacity-bounded, and the
    // conversation journal produces user/outcome entries whose notifications
    // ride the session.* event set at the same points the real service
    // publishes them (submit, step settlement, terminal settlement).

    private registerSession(created_at_ms: number): MockSession {
        const id = mockSessionId(this.nextSessionNumber);
        this.nextSessionNumber += 1;
        const session: MockSession = {
            id,
            state: 'autonomous',
            created_at_ms,
            journal: [],
            chatTurns: [],
            nextChatSequence: 1,
            inFlightChatTurnId: '',
        };
        this.sessions.set(id, session);
        return session;
    }

    /** Resolves the task.submit conversation owner: an explicit binding must
     * exist (`not_found` mirrors DEC-021), absence lands the primary
     * session (the oldest registered one in the mock). */
    private resolveSubmitSession(explicit: string | undefined): MockSession {
        if (explicit !== undefined) {
            const session = this.sessions.get(explicit);
            if (session === undefined) {
                throw new IpcRequestError('not_found', 'unknown session id');
            }
            return session;
        }
        let primary: MockSession | null = null;
        for (const session of this.sessions.values()) {
            if (primary === null || session.created_at_ms < primary.created_at_ms) {
                primary = session;
            }
        }
        if (primary === null) {
            throw new IpcRequestError('invalid_state', 'session registry is empty');
        }
        return primary;
    }

    /** Appends one journal entry and publishes its `session.message`
     * notification (the journal stays the fact source, DEC-012). */
    private appendJournalMessage(
        session: MockSession,
        kind: 'user' | 'outcome',
        text: string,
        taskId: string,
    ): void {
        const sequence = session.journal.length + 1;
        const entry: MockJournalEntry = {
            kind,
            text,
            sequence,
            recorded_at_ms: Date.now(),
        };
        session.journal.push(entry);
        this.publishFrame({
            v: 1,
            event: 'session.message',
            session_id: session.id,
            task_id: taskId,
            kind,
            text,
            sequence,
        });
    }

    sessionList(): SessionSummary[] {
        this.assertOpen();
        const summaries: SessionSummary[] = [];
        for (const session of this.sessions.values()) {
            summaries.push({ id: session.id, state: session.state, created_at_ms: session.created_at_ms });
        }
        return summaries.sort((a, b) => a.created_at_ms - b.created_at_ms);
    }

    sessionOpen(): { session_id: string } {
        this.assertOpen();
        if (this.sessions.size >= SESSION_REGISTRY_CAPACITY) {
            throw new IpcRequestError(
                'unavailable',
                `session capacity exhausted (${SESSION_REGISTRY_CAPACITY})`,
            );
        }
        const session = this.registerSession(Date.now());
        this.publishFrame({ v: 1, event: 'session.updated', session_id: session.id, state: session.state });
        return { session_id: session.id };
    }

    /** The session management face (DEC-026 backlog item 2): wire-faithful
     * mirror — the primary session is refused with the same stable
     * invalid_state the real service answers, an unknown id is not_found,
     * and a successful close removes the registry entry and publishes the
     * session.updated notification with the closed state. */
    sessionClose(sessionId: string): { session_id: string; state: 'closed' } {
        this.assertOpen();
        if (sessionId === this.primarySessionId) {
            throw new IpcRequestError('invalid_state', 'the primary session cannot be closed');
        }
        const session = this.sessions.get(sessionId);
        if (session === undefined) {
            throw new IpcRequestError('not_found', 'unknown session id');
        }
        this.sessions.delete(sessionId);
        this.publishFrame({ v: 1, event: 'session.updated', session_id: sessionId, state: 'closed' });
        return { session_id: sessionId, state: 'closed' };
    }

    /** The dialog face (DEC-027): wire-faithful mirror — a deterministic
     * simulated reply replaces the model call (the mock's documented role),
     * the turn lifecycle publishes pending then ok, and the snapshot face
     * projects the bounded turn log. */
    sessionChat(sessionId: string, text: string): { turn_id: string } {
        this.assertOpen();
        if (!this.options.chatCapability) {
            throw new IpcRequestError('protocol_error', "unknown op 'session.chat'");
        }
        if (this.hostStatus !== 'running') {
            throw new IpcRequestError('unavailable', 'model layer is not configured');
        }
        const session = this.sessions.get(sessionId);
        if (session === undefined) {
            throw new IpcRequestError('not_found', 'unknown session id');
        }
        if (session.inFlightChatTurnId.length > 0) {
            throw new IpcRequestError(
                'invalid_state',
                'a dialog turn is already in flight for this session',
            );
        }
        const turnId = `chat-${String(session.nextChatSequence).padStart(4, '0')}-${mockTurnSuffix()}`;
        const sequence = session.nextChatSequence;
        session.nextChatSequence += 1;
        const turn: MockChatTurn = {
            turn_id: turnId,
            status: 'pending',
            user_text: text,
            sequence,
            recorded_at_ms: Date.now(),
        };
        session.chatTurns.push(turn);
        session.inFlightChatTurnId = turnId;
        this.publishFrame({
            v: 1,
            event: 'session.chat_updated',
            session_id: sessionId,
            turn_id: turnId,
            status: 'pending',
            user_text: text,
            sequence,
        });
        // Deterministic simulated completion (auto flush mode settles via a
        // microtask-like timer so tests and the UI observe the lifecycle).
        const reply = `模拟回复：已收到「${text.length > 24 ? `${text.slice(0, 24)}…` : text}」`;
        setTimeout(() => {
            if (this.closed || session.inFlightChatTurnId !== turnId) {
                return;
            }
            turn.status = 'ok';
            turn.reply_text = reply;
            session.inFlightChatTurnId = '';
            this.publishFrame({
                v: 1,
                event: 'session.chat_updated',
                session_id: sessionId,
                turn_id: turnId,
                status: 'ok',
                user_text: text,
                reply_text: reply,
                sequence,
            });
        }, 15);
        return { turn_id: turnId };
    }

    sessionChatHistory(
        sessionId: string,
        limit?: number,
    ): { session_id: string; turns: MockChatTurn[]; truncated: boolean } {
        this.assertOpen();
        const session = this.sessions.get(sessionId);
        if (session === undefined) {
            throw new IpcRequestError('not_found', 'unknown session id');
        }
        const bounded = Math.max(1, Math.floor(limit ?? 50));
        const newest = session.chatTurns.slice(-bounded);
        return {
            session_id: sessionId,
            turns: newest.map((turn) => ({ ...turn })),
            truncated: session.chatTurns.length > newest.length,
        };
    }

    sessionHistory(
        input: SessionHistoryInput,
    ): { session_id: string; entries: SessionHistoryEntry[]; truncated: boolean } {
        this.assertOpen();
        const session = this.sessions.get(input.session_id);
        if (session === undefined) {
            throw new IpcRequestError('not_found', 'unknown session id');
        }
        const limit = Math.max(1, Math.floor(input.limit ?? HISTORY_DEFAULT_LIMIT));
        const newest = session.journal.slice(-limit);
        return {
            session_id: session.id,
            entries: newest.map((entry) => ({ ...entry })),
            truncated: session.journal.length > newest.length,
        };
    }

    /** M5-07 policy face: in-memory rule set served whole; policy.set
     * replaces the map (the wire face validates the full DEC-010 coverage
     * upstream). Read roots are in-memory state, no file write-back. */
    policyGet(): { rules: Record<string, string>; read_roots: string[] } {
        this.assertOpen();
        if (!this.options.policyCapability) {
            throw new IpcRequestError('protocol_error', "unknown op 'policy.get'");
        }
        const rules: Record<string, string> = {};
        for (const [capability, rule] of this.policyRules) {
            rules[capability] = rule;
        }
        return { rules, read_roots: [...this.policyReadRoots] };
    }

    policySet(rules: Record<string, string>, readRoots?: string[]): {
        rules: Record<string, string>;
        read_roots: string[];
    } {
        this.assertOpen();
        if (!this.options.policyCapability) {
            throw new IpcRequestError('protocol_error', "unknown op 'policy.set'");
        }
        this.policyRules.clear();
        for (const [capability, rule] of Object.entries(rules)) {
            this.policyRules.set(capability, rule);
        }
        if (readRoots !== undefined) {
            this.policyReadRoots = [...readRoots];
        }
        return this.policyGet();
    }

    // -- permission approvals (DEC-020 wire behavior) ------------------------

    /** The mock's pending-confirmation snapshot: entries enter through
     * `demoPermissionRequest` (mock-only demo hook — the real service raises
     * them from Confirm-rule hits) and leave through permission.respond or
     * the bounded timeout (fail closed). */
    permissionList(): PendingPermission[] {
        this.assertOpen();
        const pending: PendingPermission[] = [];
        for (const request of this.pendingApprovals.values()) {
            pending.push({
                request_id: request.request_id,
                capability: request.capability,
                resource: request.resource,
                task_id: request.task_id,
                timeout_ms: Math.max(0, Math.round(request.expires_at - Date.now())),
            });
        }
        return pending;
    }

    /** DEC-020 respond semantics: first response wins; an unknown, already
     * decided or expired id surfaces the stable not_found. An approved
     * request settles the waiting step; denied settles it failed. */
    permissionRespond(requestId: string, approved: boolean): { request_id: string } {
        this.assertOpen();
        const request = this.pendingApprovals.get(requestId);
        if (request === undefined || Date.now() > request.expires_at) {
            this.pendingApprovals.delete(requestId);
            throw new IpcRequestError(
                'not_found',
                'unknown or already decided permission request id',
            );
        }
        this.pendingApprovals.delete(requestId);
        if (request.timer !== null) {
            clearTimeout(request.timer);
            request.timer = null;
        }
        // DEC-020: the judgment outcome surfaces through task.updated / step
        // trace on the real service; the mock demo carries only the request
        // lifecycle itself (pending list in, response out).
        void approved;
        return { request_id: requestId };
    }

    /** Mock-only demo hook (never produced by the real service): raises one
     * scripted permission request that pauses the current task step until
     * responded or expired (fail closed). */
    demoPermissionRequest(capability: string, resource: string): string {
        this.assertOpen();
        const request_id = `perm-${String(this.nextPermissionNumber).padStart(4, '0')}`;
        this.nextPermissionNumber += 1;
        const expires_at = Date.now() + 60_000;
        const entry = {
            request_id,
            capability,
            resource,
            task_id: 'demo',
            expires_at,
            timer: null as ReturnType<typeof setTimeout> | null,
        };
        this.pendingApprovals.set(request_id, entry);
        entry.timer = setTimeout(() => {
            // DEC-020 fail closed: the entry just disappears from the
            // pending snapshot (an approval after expiry is not_found).
            this.pendingApprovals.delete(request_id);
        }, 60_000);
        this.publishFrame({
            v: 1,
            event: 'permission.request',
            request_id,
            capability,
            resource,
            task_id: 'demo',
            timeout_ms: 60_000,
        });
        return request_id;
    }

    // -- event surface ------------------------------------------------------

    subscribe(listener: EventListener): void {
        this.assertOpen();
        if (!this.options.eventsCapability) {
            throw new IpcRequestError('unsupported', 'peer did not advertise the events capability');
        }
        const subscription: Subscription = {
            listener,
            queue: new BoundedEventQueue(this.options.eventQueueCapacity),
            nextSeq: 1,
            droppedSinceOverflow: 0,
            flushScheduled: false,
        };
        this.subscriptions.add(subscription);
        // Enqueue-after-subscribe ordering: the subscription starts at the
        // current host status so a fresh client resyncs its view at once.
        this.enqueue(subscription, { v: 1, event: 'host.status', status: this.hostStatus });
    }

    unsubscribe(listener: EventListener): void {
        for (const subscription of this.subscriptions) {
            if (subscription.listener === listener) {
                this.subscriptions.delete(subscription);
            }
        }
    }

    /** Drains pending queues when flushMode is 'manual'; a no-op otherwise. */
    flush(): void {
        for (const subscription of [...this.subscriptions]) {
            this.flushSubscription(subscription);
        }
    }

    /** Publishes one frame to every subscriber. Also the deterministic
     * injection point for overflow tests. */
    publishFrame(frame: EventFrame): void {
        for (const subscription of [...this.subscriptions]) {
            this.enqueue(subscription, frame);
        }
    }

    private enqueue(subscription: Subscription, frame: EventFrame): void {
        const event = { ...frame, seq: subscription.nextSeq } as ServerEvent;
        subscription.nextSeq += 1;
        // Dropped events still consumed a seq: the client observes the gap
        // and resyncs (DEC-012 decision 4), while the exact loss total is
        // reported by the events.overflow marker at flush time.
        const dropped = subscription.queue.push(event);
        if (dropped > 0) {
            subscription.droppedSinceOverflow += dropped;
        }
        if (this.options.flushMode === 'auto' && !subscription.flushScheduled) {
            subscription.flushScheduled = true;
            queueMicrotask(() => {
                subscription.flushScheduled = false;
                this.flushSubscription(subscription);
            });
        }
    }

    private flushSubscription(subscription: Subscription): void {
        for (const event of subscription.queue.drain()) {
            subscription.listener(event);
        }
        // The marker is delivered directly (never enqueued) so it cannot
        // evict queued events and its count stays exact.
        if (subscription.droppedSinceOverflow > 0) {
            const dropped = subscription.droppedSinceOverflow;
            subscription.droppedSinceOverflow = 0;
            subscription.listener({
                v: 1,
                seq: subscription.nextSeq,
                event: 'events.overflow',
                dropped,
            });
            subscription.nextSeq += 1;
        }
    }

    // -- host + driver internals -------------------------------------------

    private setHostStatus(status: HostStatus): void {
        this.hostStatus = status;
        this.publishFrame({ v: 1, event: 'host.status', status });
    }

    private publishTaskUpdated(task: MockTask): void {
        this.publishFrame({
            v: 1,
            event: 'task.updated',
            task_id: task.id,
            goal: task.goal,
            progress: task.progress,
            has_success: task.hasSuccess,
            success: task.success,
        });
    }

    private advance(task: MockTask): void {
        if (task.cancelRequested) {
            this.unwindCancelled(task);
            return;
        }
        const running = task.steps.find((step) => step.status === 'running');
        if (running !== undefined) {
            this.completeStep(task, running);
            return;
        }
        const pending = task.steps.find((step) => step.status === 'pending');
        if (pending === undefined) {
            this.settle(task, 'Completed', true, true);
            return;
        }
        pending.status = 'running';
        pending.operationId = `op-${String(this.nextOperationNumber).padStart(4, '0')}`;
        this.nextOperationNumber += 1;
        this.publishTaskUpdated(task);
        if (this.options.stepDurationMs === 0) {
            this.advance(task);
        } else {
            task.timer = setTimeout(() => {
                task.timer = null;
                this.advance(task);
            }, this.options.stepDurationMs);
        }
    }

    private completeStep(task: MockTask, step: MockStep): void {
        const failure = failureReason(step);
        if (failure !== null) {
            step.status = 'failed';
            step.exitCode = 1;
            step.error = failure;
            this.publishTaskUpdated(task);
            this.publishTurnSettled(task, step, true);
            // Fail-fast batch-skip: every remaining step publishes one
            // skipped turn (the C++ skip_with_turns discipline).
            this.skipRemaining(task);
            this.publishSkippedTurns(task);
            this.settle(task, 'Failed', false, false);
            return;
        }
        step.status = 'ok';
        if (step.kind === 'process.execute') {
            step.exitCode = 0;
            step.result = `mock output of '${step.argument}'`;
        } else {
            step.result = `mock content of '${step.argument}'`;
        }
        this.publishTaskUpdated(task);
        this.publishTurnSettled(task, step, true);
        this.advance(task);
    }

    /** DEC-021: one settled-step turn plus — when the step reached the
     * action and produced a result — its output chunk. Steps that never
     * executed (skip / cancel unwind) publish a turn only. Callers only
     * pass settled steps; the assert keeps the wire vocabulary honest. */
    private publishTurnSettled(task: MockTask, step: MockStep, reachedAction: boolean): void {
        if (task.sessionId.length === 0) {
            return;
        }
        if (step.status === 'pending' || step.status === 'running') {
            throw new Error('session.turn publishes settled steps only');
        }
        const stepNumber = task.steps.indexOf(step) + 1;
        this.publishFrame({
            v: 1,
            event: 'session.turn',
            session_id: task.sessionId,
            task_id: task.id,
            step: stepNumber,
            kind: step.kind,
            status: step.status,
        });
        if (!reachedAction) {
            return;
        }
        this.publishFrame({
            v: 1,
            event: 'session.output',
            session_id: task.sessionId,
            task_id: task.id,
            step: stepNumber,
            chunk: step.result,
            truncated: step.resultTruncated,
        });
    }

    /** Batch-skip discipline: every skipped step publishes one skipped turn
     * so the timeline sees why each remaining step produced nothing. */
    private publishSkippedTurns(task: MockTask): void {
        for (const step of task.steps) {
            if (step.status === 'skipped') {
                this.publishTurnSettled(task, step, false);
            }
        }
    }

    private unwindCancelled(task: MockTask): void {
        const running = task.steps.find((step) => step.status === 'running');
        if (running !== undefined) {
            running.status = 'cancelled';
            this.publishTurnSettled(task, running, false);
        }
        this.skipRemaining(task);
        this.publishSkippedTurns(task);
        this.publishTaskUpdated(task);
        this.settle(task, 'Cancelled', false, false);
    }

    private skipRemaining(task: MockTask): void {
        for (const step of task.steps) {
            if (step.status === 'pending') {
                step.status = 'skipped';
            }
        }
    }

    private settle(task: MockTask, progress: TaskProgress, hasSuccess: boolean, success: boolean): void {
        task.progress = progress;
        task.hasSuccess = hasSuccess;
        task.success = success;
        this.publishTaskUpdated(task);
        // DEC-021 conversation settlement: the outcome joins the journal and
        // its notification rides the same path (the sentence shape mirrors
        // the pinned conversation view's projection).
        const session = this.sessions.get(task.sessionId);
        if (session !== undefined) {
            this.appendJournalMessage(session, 'outcome', outcomeSentence(progress, task.steps.length), task.id);
        }
    }
}

/** Mock-backed `MirageTransport`: same surface the real transports will
 * implement, so views and stores never special-case the mock. */
export class MockTransport implements MirageTransport {
    readonly label = 'Mock';

    private listener: EventListener | null = null;

    constructor(private readonly service: MockMirageService) {}

    /** All methods reject (never throw synchronously) so callers can rely
     * on promise handling uniformly, including after close(). */
    private call<T>(fn: () => T): Promise<T> {
        try {
            return Promise.resolve(fn());
        } catch (error) {
            return Promise.reject(error);
        }
    }

    get eventsSupported(): boolean {
        return this.service.hello().events === true;
    }

    get workflowsSupported(): boolean {
        return this.service.hello().workflows === true;
    }

    get sessionsSupported(): boolean {
        return this.service.hello().sessions === true;
    }

    get observationSupported(): boolean {
        return this.service.hello().observation === true;
    }

    get chatSupported(): boolean {
        return this.service.hello().chat === true;
    }

    get permissionsSupported(): boolean {
        return this.service.hello().permissions === true;
    }

    get policySupported(): boolean {
        return this.service.hello().policy === true;
    }

    hello(): Promise<ServiceIdentity> {
        return this.call(() => this.service.hello());
    }

    submitTask(request: SubmitTaskInput): Promise<{ task_id: string }> {
        return this.call(() => this.service.submit(request));
    }

    listTasks(): Promise<TaskSummary[]> {
        return this.call(() => this.service.list());
    }

    inspectTask(taskId: string): Promise<InspectTask> {
        return this.call(() => this.service.inspect(taskId));
    }

    cancelTask(taskId: string): Promise<{ task_id: string; progress: TaskProgress }> {
        return this.call(() => this.service.cancel(taskId));
    }

    shutdown(): Promise<void> {
        return this.call(() => {
            this.service.shutdown();
        });
    }

    listWorkflows(): Promise<WorkflowSummary[]> {
        return this.call(() => this.service.workflowList());
    }

    saveWorkflow(definition: WorkflowDefinition): Promise<{ workflow_id: string; digest: string }> {
        return this.call(() => this.service.workflowSave(definition));
    }

    publishWorkflow(
        definition: WorkflowDefinition,
    ): Promise<{ workflow_id: string; digest: string; dry_run_id: string; idempotent: boolean }> {
        return this.call(() => this.service.workflowPublish(definition));
    }

    deleteWorkflow(workflowId: string): Promise<{ workflow_id: string }> {
        return this.call(() => this.service.workflowDelete(workflowId));
    }

    workflowAtomCatalog(): Promise<ExposedTool[]> {
        return this.call(() => this.service.workflowAtomCatalog());
    }

    listWorkflowRuns(): Promise<WorkflowRunSummary[]> {
        return this.call(() => this.service.workflowRunList());
    }

    startWorkflowRun(input: WorkflowStartInput): Promise<{ run_id: string }> {
        return this.call(() => this.service.workflowRun(input));
    }

    cancelWorkflowRun(runId: string): Promise<{ run_id: string; state: WorkflowRunState }> {
        return this.call(() => this.service.workflowCancel(runId));
    }

    getWorkflow(workflowId: string): Promise<WorkflowDefinitionView> {
        return this.call(() => this.service.workflowGet(workflowId));
    }

    desktopObserve(input: DesktopObserveInput = {}): Promise<ObservationView> {
        return this.call(() => this.service.desktopObserve(input));
    }

    listSessions(): Promise<SessionSummary[]> {
        return this.call(() => this.service.sessionList());
    }

    openSession(): Promise<{ session_id: string }> {
        return this.call(() => this.service.sessionOpen());
    }

    closeSession(sessionId: string): Promise<{ session_id: string; state: SessionState }> {
        return this.call(() => this.service.sessionClose(sessionId));
    }

    sessionChat(sessionId: string, text: string): Promise<{ turn_id: string }> {
        return this.call(() => this.service.sessionChat(sessionId, text));
    }

    sessionChatHistory(
        sessionId: string,
        limit?: number,
    ): Promise<{ session_id: string; turns: MockChatTurn[]; truncated: boolean }> {
        return this.call(() => this.service.sessionChatHistory(sessionId, limit));
    }

    permissionList(): Promise<PendingPermission[]> {
        return this.call(() => this.service.permissionList());
    }

    permissionRespond(requestId: string, approved: boolean): Promise<{ request_id: string }> {
        return this.call(() => this.service.permissionRespond(requestId, approved));
    }

    policyGet(): Promise<PolicyView> {
        return this.call(() => this.service.policyGet());
    }

    policySet(
        rules: Record<string, string>,
        readRoots?: string[],
    ): Promise<PolicyView> {
        return this.call(() => this.service.policySet(rules, readRoots));
    }

    sessionHistory(input: SessionHistoryInput): Promise<{
        session_id: string;
        entries: SessionHistoryEntry[];
        truncated: boolean;
    }> {
        return this.call(() => this.service.sessionHistory(input));
    }

    subscribe(listener: EventListener): Promise<void> {
        return this.call(() => {
            this.listener = listener;
            this.service.subscribe(listener);
        });
    }

    unsubscribe(): Promise<void> {
        return this.call(() => {
            if (this.listener !== null) {
                this.service.unsubscribe(this.listener);
                this.listener = null;
            }
        });
    }

    close(): Promise<void> {
        return this.call(() => {
            this.service.close();
        });
    }
}

export function createMockTransport(options: MockServiceOptions = {}): {
    transport: MockTransport;
    service: MockMirageService;
} {
    const service = new MockMirageService(options);
    return { transport: new MockTransport(service), service };
}
