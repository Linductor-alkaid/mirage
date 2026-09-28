/// Wire codec for protocol v1, mirroring `runtime/ipc/src/protocol.cpp`
/// member-for-member (stable error strings included) plus the DEC-012 draft
/// event envelope. Drift against the C++ codec is a golden-vector test
/// failure (M1.5-01), so keep every branch and reason text aligned.

import type {
    EventName,
    ExposedTool,
    HostStatus,
    InspectTask,
    ObservationGeometry,
    ObservationNode,
    ObservationRegion,
    ObservationRegionSource,
    ObservationView,
    PendingPermission,
    RequestBody,
    ResponseEnvelop,
    ServerEvent,
    SessionMessageKind,
    SessionState,
    StepKind,
    StepStatus,
    TaskProgress,
    TaskStep,
    TurnStatus,
    WorkflowPolicyName,
    WorkflowRunState,
    WorkflowValidation,
} from './types.js';
import { PROTOCOL_VERSION } from './types.js';

const STEP_KINDS: readonly StepKind[] = ['filesystem.read', 'process.execute'];
const STEP_STATUSES: readonly StepStatus[] = [
    'pending',
    'running',
    'ok',
    'failed',
    'skipped',
    'cancelled',
];
const HOST_STATUSES: readonly HostStatus[] = [
    'stopped',
    'starting',
    'running',
    'stopping',
    'failed',
];
const PROGRESS_NAMES: readonly TaskProgress[] = [
    'Idle',
    'Active',
    'Paused',
    'Cancelling',
    'Completed',
    'Failed',
    'Cancelled',
    'Unknown',
];
/** Closed session state projection (DEC-021), stable lowercase wire form. */
const SESSION_STATES: readonly SessionState[] = [
    'opening',
    'autonomous',
    'takeover_pending',
    'human_controlled',
    'resuming',
    'closing',
    'closed',
    'failed',
];
/** Closed conversation-entry vocabulary (DEC-021). */
const SESSION_MESSAGE_KINDS: readonly SessionMessageKind[] = ['user', 'outcome'];
/** Closed settled-step status vocabulary (DEC-021) carried by session.turn. */
const TURN_STATUSES: readonly TurnStatus[] = ['ok', 'failed', 'cancelled', 'skipped'];
/** Closed workflow run state projection (DEC-023), stable lowercase form. */
const WORKFLOW_RUN_STATES: readonly WorkflowRunState[] = [
    'created',
    'running',
    'paused',
    'waiting_user',
    'waiting_agent',
    'completed',
    'failed',
    'cancelled',
];
/** Closed execution policy vocabulary (DEC-023). */
const WORKFLOW_POLICIES: readonly WorkflowPolicyName[] = [
    'strict',
    'recoverable',
    'agent_assisted',
    'interactive',
    'dry_run',
];
/** Closed workflow validation vocabulary (DEC-023). */
const WORKFLOW_VALIDATIONS: readonly WorkflowValidation[] = [
    'not_validated',
    'dry_run_passed',
    'validated',
    'rejected',
];
/** Closed visual-region provenance vocabulary (DEC-026). */
const OBSERVATION_REGION_SOURCES: readonly ObservationRegionSource[] = [
    'ocr',
    'detector',
    'template',
    'geometry',
];
/** Closed Capability vocabulary (DEC-010 / DEC-020) carried by
 * permission.request events and permission.list entries. */
const CAPABILITY_NAMES: readonly string[] = [
    'filesystem.read',
    'filesystem.write',
    'process.execute',
    'window.activate',
    'screen.capture',
    'input.inject',
    'clipboard.read',
    'clipboard.write',
    'application.launch',
    'application.terminate',
    'notification.post',
];

function isRecord(value: unknown): value is Record<string, unknown> {
    return typeof value === 'object' && value !== null && !Array.isArray(value);
}

/** Strict integer member: matches mira::JsonValue::as_integer (no bools/floats). */
function asInteger(value: unknown): number | null {
    return typeof value === 'number' && Number.isInteger(value) ? value : null;
}

function asString(value: unknown): string | null {
    return typeof value === 'string' ? value : null;
}

function asBoolean(value: unknown): boolean | null {
    return typeof value === 'boolean' ? value : null;
}

// ---------------------------------------------------------------------------
// Requests
// ---------------------------------------------------------------------------

export function encodeRequest(id: number, body: RequestBody): string {
    const object: Record<string, unknown> = { v: PROTOCOL_VERSION, id };
    switch (body.op) {
        case 'hello':
            object.op = 'hello';
            break;
        case 'task.submit': {
            object.op = 'task.submit';
            object.goal = body.goal;
            object.steps = body.steps.map((step) => ({ op: step.op, arg: step.arg }));
            if (body.step_timeout_ms !== undefined) {
                object.step_timeout_ms = body.step_timeout_ms;
            }
            if (body.session_id !== undefined) {
                object.session_id = body.session_id;
            }
            break;
        }
        case 'task.list':
            object.op = 'task.list';
            break;
        case 'task.inspect':
            object.op = 'task.inspect';
            object.task_id = body.task_id;
            break;
        case 'task.cancel':
            object.op = 'task.cancel';
            object.task_id = body.task_id;
            break;
        case 'service.shutdown':
            object.op = 'service.shutdown';
            break;
        case 'events.subscribe':
            object.op = 'events.subscribe';
            break;
        case 'events.unsubscribe':
            object.op = 'events.unsubscribe';
            break;
        case 'permission.respond':
            object.op = 'permission.respond';
            object.request_id = body.request_id;
            object.approved = body.approved;
            break;
        case 'permission.list':
            object.op = 'permission.list';
            break;
        case 'session.list':
            object.op = 'session.list';
            break;
        case 'session.open':
            object.op = 'session.open';
            break;
        case 'session.close':
            object.op = 'session.close';
            object.session_id = body.session_id;
            break;
        case 'session.history':
            object.op = 'session.history';
            object.session_id = body.session_id;
            if (body.limit !== undefined) {
                object.limit = body.limit;
            }
            break;
        case 'workflow.list':
            object.op = 'workflow.list';
            break;
        case 'workflow.save':
            object.op = 'workflow.save';
            object.definition = body.definition;
            break;
        case 'workflow.publish':
            object.op = 'workflow.publish';
            object.definition = body.definition;
            break;
        case 'workflow.delete':
            object.op = 'workflow.delete';
            object.workflow_id = body.workflow_id;
            break;
        case 'workflow.atom.catalog':
            object.op = 'workflow.atom.catalog';
            break;
        case 'workflow.runs':
            object.op = 'workflow.runs';
            break;
        case 'workflow.run': {
            object.op = 'workflow.run';
            object.workflow_id = body.workflow_id;
            if (body.digest !== undefined) {
                object.digest = body.digest;
            }
            if (body.parameters !== undefined) {
                object.parameters = body.parameters;
            }
            if (body.policy !== undefined) {
                object.policy = body.policy;
            }
            break;
        }
        case 'workflow.cancel':
            object.op = 'workflow.cancel';
            object.run_id = body.run_id;
            break;
        case 'workflow.get':
            object.op = 'workflow.get';
            object.workflow_id = body.workflow_id;
            break;
        case 'desktop.observe': {
            object.op = 'desktop.observe';
            // Both flags always write: the defaults (semantic on, visual off)
            // are part of the request's pinned canonical form.
            object.semantic = body.semantic ?? true;
            object.visual = body.visual ?? false;
            break;
        }
    }
    return JSON.stringify(object);
}

export type RequestDecode =
    | { ok: true; id: number; body: RequestBody }
    | { ok: false; error: string };

function decodeStep(value: unknown, error: { text: string }): TaskStep | null {
    if (!isRecord(value)) {
        error.text = 'task.submit steps must be objects';
        return null;
    }
    const op = asString(value.op);
    if (op === null) {
        error.text = "task.submit step is missing 'op'";
        return null;
    }
    if (!STEP_KINDS.includes(op as StepKind)) {
        error.text = `task.submit step has unsupported op '${op}'`;
        return null;
    }
    const arg = asString(value.arg);
    if (arg === null || arg.length === 0) {
        error.text = "task.submit step requires a non-empty 'arg'";
        return null;
    }
    return { op: op as StepKind, arg };
}

export function decodeRequest(payload: string): RequestDecode {
    let parsed: unknown;
    try {
        parsed = JSON.parse(payload);
    } catch {
        return { ok: false, error: 'payload is not valid JSON' };
    }
    if (!isRecord(parsed)) {
        return { ok: false, error: 'request payload must be a JSON object' };
    }
    const version = asInteger(parsed.v);
    if (version === null || version !== PROTOCOL_VERSION) {
        return { ok: false, error: 'unsupported protocol version' };
    }
    const id = asInteger(parsed.id);
    if (id === null || id < 0) {
        return { ok: false, error: "request is missing a non-negative 'id'" };
    }
    const op = asString(parsed.op);
    if (op === null) {
        return { ok: false, error: "request is missing 'op'" };
    }
    const stepError = { text: '' };
    switch (op) {
        case 'hello':
            return { ok: true, id, body: { op: 'hello' } };
        case 'task.list':
            return { ok: true, id, body: { op: 'task.list' } };
        case 'service.shutdown':
            return { ok: true, id, body: { op: 'service.shutdown' } };
        case 'events.subscribe':
            return { ok: true, id, body: { op: 'events.subscribe' } };
        case 'events.unsubscribe':
            return { ok: true, id, body: { op: 'events.unsubscribe' } };
        case 'permission.respond': {
            const requestId = asString(parsed.request_id);
            if (requestId === null || requestId.length === 0) {
                return { ok: false, error: "permission.respond requires a non-empty 'request_id'" };
            }
            const approved = asBoolean(parsed.approved);
            if (approved === null) {
                return { ok: false, error: "permission.respond requires an 'approved' boolean" };
            }
            return {
                ok: true,
                id,
                body: { op: 'permission.respond', request_id: requestId, approved },
            };
        }
        case 'permission.list':
            return { ok: true, id, body: { op: 'permission.list' } };
        case 'task.submit': {
            const goal = asString(parsed.goal);
            if (goal === null) {
                return { ok: false, error: "task.submit requires a 'goal' string" };
            }
            const steps: TaskStep[] = [];
            if (parsed.steps !== undefined) {
                if (!Array.isArray(parsed.steps)) {
                    return { ok: false, error: "task.submit 'steps' must be an array" };
                }
                for (const entry of parsed.steps) {
                    const step = decodeStep(entry, stepError);
                    if (step === null) {
                        return { ok: false, error: stepError.text };
                    }
                    steps.push(step);
                }
            }
            let stepTimeoutMs: number | undefined;
            if (parsed.step_timeout_ms !== undefined) {
                const timeout = asInteger(parsed.step_timeout_ms);
                if (timeout === null || timeout <= 0) {
                    return { ok: false, error: "task.submit 'step_timeout_ms' must be positive" };
                }
                stepTimeoutMs = timeout;
            }
            const body: RequestBody = { op: 'task.submit', goal, steps };
            if (stepTimeoutMs !== undefined) {
                (body as { step_timeout_ms?: number }).step_timeout_ms = stepTimeoutMs;
            }
            if (parsed.session_id !== undefined) {
                const sessionId = asString(parsed.session_id);
                if (sessionId === null || sessionId.length === 0) {
                    return { ok: false, error: "task.submit 'session_id' must be non-empty" };
                }
                (body as { session_id?: string }).session_id = sessionId;
            }
            return { ok: true, id, body };
        }
        case 'task.inspect': {
            const taskId = asString(parsed.task_id);
            if (taskId === null || taskId.length === 0) {
                return { ok: false, error: "task.inspect requires a non-empty 'task_id'" };
            }
            return { ok: true, id, body: { op: 'task.inspect', task_id: taskId } };
        }
        case 'task.cancel': {
            const taskId = asString(parsed.task_id);
            if (taskId === null || taskId.length === 0) {
                return { ok: false, error: "task.cancel requires a non-empty 'task_id'" };
            }
            return { ok: true, id, body: { op: 'task.cancel', task_id: taskId } };
        }
        case 'session.list':
            return { ok: true, id, body: { op: 'session.list' } };
        case 'session.open':
            return { ok: true, id, body: { op: 'session.open' } };
        case 'session.close': {
            const sessionId = asString(parsed.session_id);
            if (sessionId === null || sessionId.length === 0) {
                return { ok: false, error: "session.close requires a non-empty 'session_id'" };
            }
            return { ok: true, id, body: { op: 'session.close', session_id: sessionId } };
        }
        case 'workflow.list':
            return { ok: true, id, body: { op: 'workflow.list' } };
        case 'workflow.atom.catalog':
            return { ok: true, id, body: { op: 'workflow.atom.catalog' } };
        case 'workflow.runs':
            return { ok: true, id, body: { op: 'workflow.runs' } };
        case 'workflow.save':
        case 'workflow.publish': {
            const definition = parsed.definition;
            if (definition === undefined || !isRecord(definition)) {
                return {
                    ok: false,
                    error: `workflow.${op === 'workflow.save' ? 'save' : 'publish'} requires a 'definition' object`,
                };
            }
            return {
                ok: true,
                id,
                body:
                    op === 'workflow.save'
                        ? { op: 'workflow.save', definition }
                        : { op: 'workflow.publish', definition },
            };
        }
        case 'workflow.delete': {
            const workflowId = asString(parsed.workflow_id);
            if (workflowId === null || workflowId.length === 0) {
                return { ok: false, error: "workflow.delete requires a non-empty 'workflow_id'" };
            }
            return { ok: true, id, body: { op: 'workflow.delete', workflow_id: workflowId } };
        }
        case 'workflow.run': {
            const workflowId = asString(parsed.workflow_id);
            if (workflowId === null || workflowId.length === 0) {
                return { ok: false, error: "workflow.run requires a non-empty 'workflow_id'" };
            }
            let digest: string | undefined;
            if (parsed.digest !== undefined) {
                const digestValue = asString(parsed.digest);
                if (digestValue === null || digestValue.length === 0) {
                    return { ok: false, error: "workflow.run 'digest' must be non-empty" };
                }
                digest = digestValue;
            }
            let parameters: Record<string, unknown> | undefined;
            if (parsed.parameters !== undefined) {
                if (!isRecord(parsed.parameters)) {
                    return { ok: false, error: "workflow.run 'parameters' must be an object" };
                }
                parameters = parsed.parameters;
            }
            let policy: WorkflowPolicyName | undefined;
            if (parsed.policy !== undefined) {
                const policyValue = asString(parsed.policy);
                if (policyValue === null || !WORKFLOW_POLICIES.includes(policyValue as WorkflowPolicyName)) {
                    return { ok: false, error: "workflow.run 'policy' is not a known policy name" };
                }
                policy = policyValue as WorkflowPolicyName;
            }
            const body: RequestBody = { op: 'workflow.run', workflow_id: workflowId };
            if (digest !== undefined) {
                (body as { digest?: string }).digest = digest;
            }
            if (parameters !== undefined) {
                (body as { parameters?: Record<string, unknown> }).parameters = parameters;
            }
            if (policy !== undefined) {
                (body as { policy?: WorkflowPolicyName }).policy = policy;
            }
            return { ok: true, id, body };
        }
        case 'workflow.cancel': {
            const runId = asString(parsed.run_id);
            if (runId === null || runId.length === 0) {
                return { ok: false, error: "workflow.cancel requires a non-empty 'run_id'" };
            }
            return { ok: true, id, body: { op: 'workflow.cancel', run_id: runId } };
        }
        case 'workflow.get': {
            const workflowId = asString(parsed.workflow_id);
            if (workflowId === null || workflowId.length === 0) {
                return { ok: false, error: "workflow.get requires a non-empty 'workflow_id'" };
            }
            return { ok: true, id, body: { op: 'workflow.get', workflow_id: workflowId } };
        }
        case 'desktop.observe': {
            // Both flags are optional on the wire (absent keeps the default:
            // semantic on, visual off) and must be booleans when present.
            let semantic = true;
            if (parsed.semantic !== undefined) {
                const flag = asBoolean(parsed.semantic);
                if (flag === null) {
                    return { ok: false, error: "desktop.observe 'semantic' must be a boolean" };
                }
                semantic = flag;
            }
            let visual = false;
            if (parsed.visual !== undefined) {
                const flag = asBoolean(parsed.visual);
                if (flag === null) {
                    return { ok: false, error: "desktop.observe 'visual' must be a boolean" };
                }
                visual = flag;
            }
            return { ok: true, id, body: { op: 'desktop.observe', semantic, visual } };
        }
        case 'session.history': {
            const sessionId = asString(parsed.session_id);
            if (sessionId === null || sessionId.length === 0) {
                return { ok: false, error: "session.history requires a non-empty 'session_id'" };
            }
            let limit: number | undefined;
            if (parsed.limit !== undefined) {
                const limitValue = asInteger(parsed.limit);
                if (limitValue === null || limitValue <= 0) {
                    return { ok: false, error: "session.history 'limit' must be positive" };
                }
                limit = limitValue;
            }
            return {
                ok: true,
                id,
                body: limit === undefined
                    ? { op: 'session.history', session_id: sessionId }
                    : { op: 'session.history', session_id: sessionId, limit },
            };
        }
        default:
            return { ok: false, error: `unknown op '${op}'` };
    }
}

// ---------------------------------------------------------------------------
// Responses
// ---------------------------------------------------------------------------

function encodeInspect(task: InspectTask): Record<string, unknown> {
    const object: Record<string, unknown> = {
        id: task.id,
        goal: task.goal,
        progress: task.progress,
    };
    if (task.has_success) {
        object.success = task.success === true;
    }
    object.steps = task.steps.map((step) => ({
        index: step.index,
        kind: step.kind,
        status: step.status,
        operation_id: step.operation_id,
        permission: step.permission,
        ok: step.ok,
        exit_code: step.exit_code,
        result: step.result,
        result_truncated: step.result_truncated,
        error: step.error,
    }));
    return object;
}

export function encodeResponse(response: ResponseEnvelop): string {
    const object: Record<string, unknown> = { v: PROTOCOL_VERSION, id: response.id };
    if (response.ok) {
        object.ok = true;
        const payload = response.payload;
        switch (payload.kind) {
            case 'identity': {
                const value = payload.value;
                object.service = value.service;
                object.mirage_version = value.mirage_version;
                object.mira_core_version = value.mira_core_version;
                object.host_status = value.host_status;
                object.protocol = value.protocol;
                if (value.events !== undefined) {
                    object.events = value.events;
                }
                if (value.permissions !== undefined) {
                    object.permissions = value.permissions;
                }
                if (value.sessions !== undefined) {
                    object.sessions = value.sessions;
                }
                if (value.workflows !== undefined) {
                    object.workflows = value.workflows;
                }
                if (value.observation !== undefined) {
                    object.observation = value.observation;
                }
                break;
            }
            case 'submitted': {
                object.task_id = payload.value.task_id;
                if (payload.value.session_id !== undefined) {
                    object.session_id = payload.value.session_id;
                }
                break;
            }
            case 'list':
                object.tasks = payload.value.tasks.map((task) => ({
                    id: task.id,
                    goal: task.goal,
                    progress: task.progress,
                }));
                break;
            case 'inspect':
                object.task = encodeInspect(payload.value);
                break;
            case 'cancelled':
                object.task_cancelled = {
                    task_id: payload.value.task_id,
                    progress: payload.value.progress,
                };
                break;
            case 'shutdown-accepted':
                // No payload members beyond the ok envelope.
                break;
            case 'permission-responded':
                object.request_id = payload.value.request_id;
                break;
            case 'permission-list':
                object.pending = payload.value.pending.map((entry) => ({
                    request_id: entry.request_id,
                    capability: entry.capability,
                    resource: entry.resource,
                    task_id: entry.task_id,
                    timeout_ms: entry.timeout_ms,
                }));
                break;
            case 'session-list':
                object.sessions = payload.value.sessions.map((session) => ({
                    id: session.id,
                    state: session.state,
                    created_at_ms: session.created_at_ms,
                }));
                break;
            case 'session-opened':
                object.session_id = payload.value.session_id;
                break;
            case 'session-closed':
                object.session_id = payload.value.session_id;
                object.state = payload.value.state;
                break;
            case 'session-history':
                object.session_id = payload.value.session_id;
                object.entries = payload.value.entries.map((entry) => ({
                    kind: entry.kind,
                    text: entry.text,
                    sequence: entry.sequence,
                    recorded_at_ms: entry.recorded_at_ms,
                }));
                object.truncated = payload.value.truncated;
                break;
            case 'workflow-list':
                object.workflows = payload.value.workflows.map((workflow) => ({
                    workflow_id: workflow.workflow_id,
                    name: workflow.name,
                    head_digest: workflow.head_digest,
                    validation: workflow.validation,
                    runnable: workflow.runnable,
                    updated_at_ms: workflow.updated_at_ms,
                }));
                break;
            case 'workflow-saved':
                object.workflow_id = payload.value.workflow_id;
                object.digest = payload.value.digest;
                break;
            case 'workflow-published':
                object.workflow_id = payload.value.workflow_id;
                object.digest = payload.value.digest;
                object.dry_run_id = payload.value.dry_run_id;
                object.idempotent = payload.value.idempotent;
                break;
            case 'workflow-deleted':
                object.workflow_id = payload.value.workflow_id;
                break;
            case 'workflow-atom-catalog':
                object.tools = payload.value.tools.map((tool) => ({
                    wire_name: tool.wire_name,
                    version: tool.version,
                    description: tool.description,
                    has_side_effects: tool.has_side_effects,
                    parameters_schema: tool.parameters_schema,
                }));
                break;
            case 'workflow-run-list':
                object.runs = payload.value.runs.map((run) => ({
                    run_id: run.run_id,
                    workflow_id: run.workflow_id,
                    state: run.state,
                    run_epoch: run.run_epoch,
                    created_at_ms: run.created_at_ms,
                }));
                break;
            case 'workflow-run-started':
                object.run_id = payload.value.run_id;
                break;
            case 'workflow-run-cancelled':
                object.run_id = payload.value.run_id;
                object.state = payload.value.state;
                break;
            case 'workflow-get':
                object.workflow_id = payload.value.workflow_id;
                object.digest = payload.value.digest;
                object.definition = payload.value.definition;
                break;
            case 'observation-view': {
                const value = payload.value;
                object.active_application = value.active_application;
                object.active_window = value.active_window;
                object.window_geometry = value.window_geometry;
                object.window_focused = value.window_focused;
                object.focused_element = value.focused_element;
                object.pointer_x = value.pointer_x;
                object.pointer_y = value.pointer_y;
                object.environment_state = value.environment_state;
                if (value.semantic !== undefined) {
                    object.semantic = {
                        application: value.semantic.application,
                        window_title: value.semantic.window_title,
                        nodes: value.semantic.nodes,
                        truncated: value.semantic.truncated,
                    };
                }
                if (value.visual_snapshot_ref !== undefined) {
                    object.visual_snapshot_ref = value.visual_snapshot_ref;
                }
                if (value.visual_regions !== undefined) {
                    object.visual_regions = value.visual_regions;
                }
                break;
            }
        }
    } else {
        object.ok = false;
        object.error = { code: response.error.code, message: response.error.message };
    }
    return JSON.stringify(object);
}

export type ResponseDecode =
    | { ok: true; response: ResponseEnvelop }
    | { ok: false; error: string };

function decodeStepView(value: unknown): Record<string, unknown> | null {
    return isRecord(value) ? value : null;
}

// --- observation projection decode helpers (DEC-026) ------------------------

const FRAME_MEMBERS_ERROR =
    "desktop.observe response requires the frame members 'active_window', " +
    "'window_geometry', 'window_focused', 'focused_element', 'pointer_x', " +
    "'pointer_y' and 'environment_state'";

function decodeObservationGeometry(
    value: unknown,
    error: { text: string },
): ObservationGeometry | null {
    if (!isRecord(value)) {
        error.text = 'observation geometry must be an object';
        return null;
    }
    const x = asInteger(value.x);
    const y = asInteger(value.y);
    const width = asInteger(value.width);
    const height = asInteger(value.height);
    if (x === null || y === null || width === null || height === null) {
        error.text = "observation geometry requires 'x', 'y', 'width' and 'height'";
        return null;
    }
    return { x, y, width, height };
}

function decodeObservationNode(value: unknown, error: { text: string }): ObservationNode | null {
    if (!isRecord(value)) {
        error.text = 'observation semantic nodes must be objects';
        return null;
    }
    const ref = asString(value.ref);
    const role = asString(value.role);
    const name = asString(value.name);
    const description = asString(value.description);
    const parent = asInteger(value.parent);
    const focused = asBoolean(value.focused);
    const enabled = asBoolean(value.enabled);
    if (
        ref === null ||
        ref.length === 0 ||
        role === null ||
        name === null ||
        description === null ||
        parent === null ||
        parent < -1 ||
        focused === null ||
        enabled === null
    ) {
        error.text =
            "observation semantic nodes require 'ref', 'role', 'name', 'description', a " +
            "'parent' index (>= -1), 'focused' and 'enabled'";
        return null;
    }
    const geometry = decodeObservationGeometry(value.geometry, error);
    if (geometry === null) {
        return null;
    }
    return { ref, role, name, description, parent, geometry, focused, enabled };
}

function decodeObservationRegion(
    value: unknown,
    error: { text: string },
): ObservationRegion | null {
    if (!isRecord(value)) {
        error.text = 'observation visual regions must be objects';
        return null;
    }
    const ref = asString(value.ref);
    const source = asString(value.source);
    const text = asString(value.text);
    const templateId = asString(value.template_id);
    if (
        ref === null ||
        ref.length === 0 ||
        source === null ||
        text === null ||
        templateId === null
    ) {
        error.text =
            "observation visual regions require 'ref', 'source', 'geometry', 'text' and " +
            "'template_id'";
        return null;
    }
    if (!OBSERVATION_REGION_SOURCES.includes(source as ObservationRegionSource)) {
        error.text = "observation visual region 'source' is not a known region source";
        return null;
    }
    const geometry = decodeObservationGeometry(value.geometry, error);
    if (geometry === null) {
        return null;
    }
    return { ref, source: source as ObservationRegionSource, geometry, text, template_id: templateId };
}

function decodeObservationView(
    parsed: Record<string, unknown>,
    error: { text: string },
): ObservationView | null {
    const activeApplication = asString(parsed.active_application);
    const activeWindow = asString(parsed.active_window);
    const focusedElement = asString(parsed.focused_element);
    const environmentState = asString(parsed.environment_state);
    const pointerX = asInteger(parsed.pointer_x);
    const pointerY = asInteger(parsed.pointer_y);
    const windowFocused = asBoolean(parsed.window_focused);
    if (
        activeApplication === null ||
        activeWindow === null ||
        focusedElement === null ||
        environmentState === null ||
        pointerX === null ||
        pointerY === null ||
        windowFocused === null
    ) {
        error.text = FRAME_MEMBERS_ERROR;
        return null;
    }
    const windowGeometry = decodeObservationGeometry(parsed.window_geometry, error);
    if (windowGeometry === null) {
        error.text = FRAME_MEMBERS_ERROR;
        return null;
    }
    const view: ObservationView = {
        active_application: activeApplication,
        active_window: activeWindow,
        window_geometry: windowGeometry,
        window_focused: windowFocused,
        focused_element: focusedElement,
        pointer_x: pointerX,
        pointer_y: pointerY,
        environment_state: environmentState,
    };
    if (parsed.semantic !== undefined) {
        const semantic = parsed.semantic;
        if (!isRecord(semantic)) {
            error.text = "desktop.observe 'semantic' must be an object";
            return null;
        }
        const application = asString(semantic.application);
        const windowTitle = asString(semantic.window_title);
        const truncated = asBoolean(semantic.truncated);
        if (application === null || windowTitle === null || truncated === null || !Array.isArray(semantic.nodes)) {
            error.text =
                "desktop.observe 'semantic' requires 'application', 'window_title', a 'nodes' " +
                'array and \'truncated\'';
            return null;
        }
        const nodes: ObservationNode[] = [];
        for (const entry of semantic.nodes) {
            const node = decodeObservationNode(entry, error);
            if (node === null) {
                return null;
            }
            nodes.push(node);
        }
        view.semantic = { application, window_title: windowTitle, nodes, truncated };
    }
    const hasVisualRef = parsed.visual_snapshot_ref !== undefined;
    const hasVisualRegions = parsed.visual_regions !== undefined;
    // The visual pair is co-present or co-absent; a half-carried visual
    // component is a contract violation, never a partial projection.
    if (hasVisualRef !== hasVisualRegions) {
        error.text =
            "desktop.observe 'visual_snapshot_ref' and 'visual_regions' are co-present";
        return null;
    }
    if (hasVisualRef && hasVisualRegions) {
        const visualSnapshotRef = asString(parsed.visual_snapshot_ref);
        if (visualSnapshotRef === null || visualSnapshotRef.length === 0 || !Array.isArray(parsed.visual_regions)) {
            error.text =
                "desktop.observe requires a non-empty 'visual_snapshot_ref' and a " +
                "'visual_regions' array";
            return null;
        }
        const regions: ObservationRegion[] = [];
        for (const entry of parsed.visual_regions) {
            const region = decodeObservationRegion(entry, error);
            if (region === null) {
                return null;
            }
            regions.push(region);
        }
        view.visual_snapshot_ref = visualSnapshotRef;
        view.visual_regions = regions;
    }
    return view;
}

function decodeInspect(value: unknown, error: { text: string }): InspectTask | null {
    if (!isRecord(value)) {
        error.text = "task.inspect 'task' must be an object";
        return null;
    }
    const id = asString(value.id);
    const goal = asString(value.goal);
    const progress = asString(value.progress);
    if (id === null || goal === null || progress === null) {
        error.text = 'task.inspect requires id, goal and progress';
        return null;
    }
    const task: InspectTask = {
        id,
        goal,
        progress: progress as TaskProgress,
        has_success: false,
        steps: [],
    };
    if (value.success !== undefined) {
        const success = asBoolean(value.success);
        if (success === null) {
            error.text = "task.inspect 'success' must be a boolean";
            return null;
        }
        task.has_success = true;
        task.success = success;
    }
    if (value.steps !== undefined) {
        if (!Array.isArray(value.steps)) {
            error.text = "task.inspect 'steps' must be an array";
            return null;
        }
        for (const entry of value.steps) {
            const step = decodeStepView(entry);
            if (step === null) {
                error.text = 'task inspect steps must be objects';
                return null;
            }
            task.steps.push({
                index: asInteger(step.index) ?? 0,
                kind: (asString(step.kind) ?? '') as InspectTask['steps'][number]['kind'],
                status: (asString(step.status) ?? '') as InspectTask['steps'][number]['status'],
                operation_id: asString(step.operation_id) ?? '',
                permission: asString(step.permission) ?? '',
                ok: asBoolean(step.ok) ?? false,
                exit_code: asInteger(step.exit_code) ?? -1,
                result: asString(step.result) ?? '',
                result_truncated: asBoolean(step.result_truncated) ?? false,
                error: asString(step.error) ?? '',
            });
        }
    }
    return task;
}

export function decodeResponse(payload: string): ResponseDecode {
    let parsed: unknown;
    try {
        parsed = JSON.parse(payload);
    } catch {
        return { ok: false, error: 'payload is not valid JSON' };
    }
    if (!isRecord(parsed)) {
        return { ok: false, error: 'response payload must be a JSON object' };
    }
    const version = asInteger(parsed.v);
    if (version === null || version !== PROTOCOL_VERSION) {
        return { ok: false, error: 'unsupported protocol version' };
    }
    const id = asInteger(parsed.id);
    if (id === null || id < 0) {
        return { ok: false, error: "response is missing a non-negative 'id'" };
    }
    const ok = asBoolean(parsed.ok);
    if (ok === null) {
        return { ok: false, error: "response is missing 'ok'" };
    }
    if (!ok) {
        const error = parsed.error;
        if (!isRecord(error)) {
            return { ok: false, error: "failed response is missing an 'error' object" };
        }
        const code = asString(error.code);
        const message = asString(error.message);
        if (code === null || message === null) {
            return { ok: false, error: "response error requires 'code' and 'message'" };
        }
        return {
            ok: true,
            response: { ok: false, id, error: { code: code as never, message } },
        };
    }
    const errorHolder = { text: '' };
    if (parsed.service !== undefined) {
        const service = asString(parsed.service);
        const mirageVersion = asString(parsed.mirage_version);
        const miraCoreVersion = asString(parsed.mira_core_version);
        const hostStatus = asString(parsed.host_status);
        const protocol = asInteger(parsed.protocol);
        if (
            service === null ||
            mirageVersion === null ||
            miraCoreVersion === null ||
            hostStatus === null ||
            protocol === null
        ) {
            return { ok: false, error: 'hello response is missing identity members' };
        }
        const identity = {
            service,
            mirage_version: mirageVersion,
            mira_core_version: miraCoreVersion,
            host_status: hostStatus as HostStatus,
            protocol,
        };
        // DEC-012 capability member: encoded whenever present, absent = false.
        if (parsed.events !== undefined) {
            const events = asBoolean(parsed.events);
            if (events === null) {
                return { ok: false, error: "hello response 'events' must be a boolean" };
            }
            (identity as { events?: boolean }).events = events;
        }
        // DEC-020 capability member: same discipline as `events`.
        if (parsed.permissions !== undefined) {
            const permissions = asBoolean(parsed.permissions);
            if (permissions === null) {
                return { ok: false, error: "hello response 'permissions' must be a boolean" };
            }
            (identity as { permissions?: boolean }).permissions = permissions;
        }
        // DEC-021 session-face capability member: same discipline as `events`.
        if (parsed.sessions !== undefined) {
            const sessions = asBoolean(parsed.sessions);
            if (sessions === null) {
                return { ok: false, error: "hello response 'sessions' must be a boolean" };
            }
            (identity as { sessions?: boolean }).sessions = sessions;
        }
        // DEC-023 workflow-face capability member: same discipline.
        if (parsed.workflows !== undefined) {
            const workflows = asBoolean(parsed.workflows);
            if (workflows === null) {
                return { ok: false, error: "hello response 'workflows' must be a boolean" };
            }
            (identity as { workflows?: boolean }).workflows = workflows;
        }
        // DEC-026 observation-face capability member: same discipline.
        if (parsed.observation !== undefined) {
            const observation = asBoolean(parsed.observation);
            if (observation === null) {
                return { ok: false, error: "hello response 'observation' must be a boolean" };
            }
            (identity as { observation?: boolean }).observation = observation;
        }
        return {
            ok: true,
            response: { ok: true, id, payload: { kind: 'identity', value: identity } },
        };
    }
    if (parsed.task_id !== undefined) {
        const taskId = asString(parsed.task_id);
        if (taskId === null || taskId.length === 0) {
            return { ok: false, error: "task.submit response requires a non-empty 'task_id'" };
        }
        const submitted: { task_id: string; session_id?: string } = { task_id: taskId };
        if (parsed.session_id !== undefined) {
            const sessionId = asString(parsed.session_id);
            if (sessionId === null || sessionId.length === 0) {
                return { ok: false, error: "task.submit response 'session_id' must be non-empty" };
            }
            submitted.session_id = sessionId;
        }
        return {
            ok: true,
            response: { ok: true, id, payload: { kind: 'submitted', value: submitted } },
        };
    }
    if (parsed.tasks !== undefined) {
        if (!Array.isArray(parsed.tasks)) {
            return { ok: false, error: "task.list 'tasks' must be an array" };
        }
        const tasks = [];
        for (const entry of parsed.tasks) {
            if (!isRecord(entry)) {
                return { ok: false, error: 'task.list entries must be objects' };
            }
            const taskId = asString(entry.id);
            const goal = asString(entry.goal);
            const progress = asString(entry.progress);
            if (taskId === null || goal === null || progress === null) {
                return { ok: false, error: 'task.list entries require id, goal and progress' };
            }
            tasks.push({ id: taskId, goal, progress: progress as TaskProgress });
        }
        return {
            ok: true,
            response: { ok: true, id, payload: { kind: 'list', value: { tasks } } },
        };
    }
    if (parsed.task !== undefined) {
        const task = decodeInspect(parsed.task, errorHolder);
        if (task === null) {
            return { ok: false, error: errorHolder.text };
        }
        return {
            ok: true,
            response: { ok: true, id, payload: { kind: 'inspect', value: task } },
        };
    }
    if (parsed.task_cancelled !== undefined) {
        const cancelled = parsed.task_cancelled;
        if (!isRecord(cancelled)) {
            return { ok: false, error: "task.cancel 'task_cancelled' must be an object" };
        }
        const taskId = asString(cancelled.task_id);
        const progress = asString(cancelled.progress);
        if (taskId === null || taskId.length === 0 || progress === null) {
            return { ok: false, error: "task.cancel requires 'task_id' and 'progress'" };
        }
        return {
            ok: true,
            response: {
                ok: true,
                id,
                payload: { kind: 'cancelled', value: { task_id: taskId, progress: progress as TaskProgress } },
            },
        };
    }
    if (parsed.request_id !== undefined) {
        const requestId = asString(parsed.request_id);
        if (requestId === null || requestId.length === 0) {
            return {
                ok: false,
                error: "permission.respond response requires a non-empty 'request_id'",
            };
        }
        return {
            ok: true,
            response: {
                ok: true,
                id,
                payload: { kind: 'permission-responded', value: { request_id: requestId } },
            },
        };
    }
    if (parsed.pending !== undefined) {
        if (!Array.isArray(parsed.pending)) {
            return { ok: false, error: "permission.list 'pending' must be an array" };
        }
        const pending: PendingPermission[] = [];
        for (const entry of parsed.pending) {
            if (!isRecord(entry)) {
                return { ok: false, error: 'permission.list entries must be objects' };
            }
            const requestId = asString(entry.request_id);
            const capability = asString(entry.capability);
            const resource = asString(entry.resource);
            const taskId = asString(entry.task_id);
            const timeout = asInteger(entry.timeout_ms);
            if (
                requestId === null ||
                requestId.length === 0 ||
                capability === null ||
                resource === null ||
                taskId === null ||
                taskId.length === 0 ||
                timeout === null
            ) {
                return {
                    ok: false,
                    error:
                        "permission.list entries require 'request_id', 'capability', 'resource', 'task_id' and 'timeout_ms'",
                };
            }
            if (timeout <= 0) {
                return {
                    ok: false,
                    error: "permission.list entry 'timeout_ms' must be positive",
                };
            }
            if (!CAPABILITY_NAMES.includes(capability)) {
                return {
                    ok: false,
                    error: "permission.list entry 'capability' is not a known capability",
                };
            }
            pending.push({ request_id: requestId, capability, resource, task_id: taskId, timeout_ms: timeout });
        }
        return {
            ok: true,
            response: { ok: true, id, payload: { kind: 'permission-list', value: { pending } } },
        };
    }
    if (parsed.entries !== undefined) {
        // session.history discriminates on "entries"; it also carries
        // "session_id", so this branch must precede the session-opened one.
        if (!Array.isArray(parsed.entries)) {
            return { ok: false, error: "session.history 'entries' must be an array" };
        }
        const sessionId = asString(parsed.session_id);
        if (sessionId === null || sessionId.length === 0) {
            return { ok: false, error: "session.history requires a non-empty 'session_id'" };
        }
        const truncated = asBoolean(parsed.truncated);
        if (truncated === null) {
            return { ok: false, error: "session.history requires a 'truncated' boolean" };
        }
        const entries = [];
        for (const entry of parsed.entries) {
            if (!isRecord(entry)) {
                return { ok: false, error: 'session.history entries must be objects' };
            }
            const kind = asString(entry.kind);
            const text = asString(entry.text);
            const sequence = asInteger(entry.sequence);
            const recordedAtMs = asInteger(entry.recorded_at_ms);
            if (
                kind === null ||
                kind.length === 0 ||
                text === null ||
                text.length === 0 ||
                sequence === null ||
                sequence < 1 ||
                recordedAtMs === null ||
                recordedAtMs < 0
            ) {
                return {
                    ok: false,
                    error:
                        "session.history entries require 'kind', 'text', a positive 'sequence' and a non-negative 'recorded_at_ms'",
                };
            }
            if (!SESSION_MESSAGE_KINDS.includes(kind as SessionMessageKind)) {
                return {
                    ok: false,
                    error: "session.history entry 'kind' is not a known message kind",
                };
            }
            entries.push({
                kind: kind as SessionMessageKind,
                text,
                sequence,
                recorded_at_ms: recordedAtMs,
            });
        }
        return {
            ok: true,
            response: {
                ok: true,
                id,
                payload: { kind: 'session-history', value: { session_id: sessionId, entries, truncated } },
            },
        };
    }
    if (parsed.session_id !== undefined) {
        const sessionId = asString(parsed.session_id);
        if (sessionId === null || sessionId.length === 0) {
            return { ok: false, error: "session.open response requires a non-empty 'session_id'" };
        }
        if (parsed.state !== undefined) {
            // The closed reply adds "state" to the same envelope shape
            // (mirrors the workflow.cancel / workflow.run discrimination).
            const state = asString(parsed.state);
            if (state === null || !SESSION_STATES.includes(state as SessionState)) {
                return {
                    ok: false,
                    error:
                        "session.close response requires a 'state' string from the session state vocabulary",
                };
            }
            return {
                ok: true,
                response: {
                    ok: true,
                    id,
                    payload: { kind: 'session-closed', value: { session_id: sessionId, state: state as SessionState } },
                },
            };
        }
        return {
            ok: true,
            response: { ok: true, id, payload: { kind: 'session-opened', value: { session_id: sessionId } } },
        };
    }
    if (parsed.sessions !== undefined) {
        if (!Array.isArray(parsed.sessions)) {
            return { ok: false, error: "session.list 'sessions' must be an array" };
        }
        const sessions = [];
        for (const entry of parsed.sessions) {
            if (!isRecord(entry)) {
                return { ok: false, error: 'session.list entries must be objects' };
            }
            const id = asString(entry.id);
            const state = asString(entry.state);
            const createdAtMs = asInteger(entry.created_at_ms);
            if (id === null || id.length === 0 || state === null || createdAtMs === null || createdAtMs < 0) {
                return {
                    ok: false,
                    error: "session.list entries require 'id', 'state' and 'created_at_ms'",
                };
            }
            if (!SESSION_STATES.includes(state as SessionState)) {
                return {
                    ok: false,
                    error: "session.list entry 'state' is not a known session state",
                };
            }
            sessions.push({ id, state: state as SessionState, created_at_ms: createdAtMs });
        }
        return {
            ok: true,
            response: { ok: true, id, payload: { kind: 'session-list', value: { sessions } } },
        };
    }
    if (parsed.workflows !== undefined) {
        if (!Array.isArray(parsed.workflows)) {
            return { ok: false, error: "workflow.list 'workflows' must be an array" };
        }
        const workflows = [];
        for (const entry of parsed.workflows) {
            if (!isRecord(entry)) {
                return { ok: false, error: 'workflow.list entries must be objects' };
            }
            const workflowId = asString(entry.workflow_id);
            const name = asString(entry.name);
            const headDigest = asString(entry.head_digest);
            const validation = asString(entry.validation);
            const runnable = asBoolean(entry.runnable);
            const updatedAtMs = asInteger(entry.updated_at_ms);
            if (
                workflowId === null ||
                workflowId.length === 0 ||
                name === null ||
                name.length === 0 ||
                headDigest === null ||
                headDigest.length === 0 ||
                validation === null ||
                runnable === null ||
                updatedAtMs === null ||
                updatedAtMs < 0
            ) {
                return {
                    ok: false,
                    error:
                        "workflow.list entries require 'workflow_id', 'name', 'head_digest', 'validation', 'runnable' and 'updated_at_ms'",
                };
            }
            if (!WORKFLOW_VALIDATIONS.includes(validation as WorkflowValidation)) {
                return {
                    ok: false,
                    error: "workflow.list entry 'validation' is not a known validation result",
                };
            }
            workflows.push({
                workflow_id: workflowId,
                name,
                head_digest: headDigest,
                validation: validation as WorkflowValidation,
                runnable,
                updated_at_ms: updatedAtMs,
            });
        }
        return {
            ok: true,
            response: { ok: true, id, payload: { kind: 'workflow-list', value: { workflows } } },
        };
    }
    if (parsed.runs !== undefined) {
        if (!Array.isArray(parsed.runs)) {
            return { ok: false, error: "workflow.runs 'runs' must be an array" };
        }
        const runs = [];
        for (const entry of parsed.runs) {
            if (!isRecord(entry)) {
                return { ok: false, error: 'workflow.runs entries must be objects' };
            }
            const runId = asString(entry.run_id);
            const workflowId = asString(entry.workflow_id);
            const state = asString(entry.state);
            const runEpoch = asInteger(entry.run_epoch);
            const createdAtMs = asInteger(entry.created_at_ms);
            if (
                runId === null ||
                runId.length === 0 ||
                workflowId === null ||
                workflowId.length === 0 ||
                state === null ||
                runEpoch === null ||
                runEpoch < 0 ||
                createdAtMs === null ||
                createdAtMs < 0
            ) {
                return {
                    ok: false,
                    error:
                        "workflow.runs entries require 'run_id', 'workflow_id', 'state', 'run_epoch' and 'created_at_ms'",
                };
            }
            if (!WORKFLOW_RUN_STATES.includes(state as WorkflowRunState)) {
                return { ok: false, error: "workflow.runs entry 'state' is not a known run state" };
            }
            runs.push({
                run_id: runId,
                workflow_id: workflowId,
                state: state as WorkflowRunState,
                run_epoch: runEpoch,
                created_at_ms: createdAtMs,
            });
        }
        return {
            ok: true,
            response: { ok: true, id, payload: { kind: 'workflow-run-list', value: { runs } } },
        };
    }
    if (parsed.tools !== undefined) {
        if (!Array.isArray(parsed.tools)) {
            return { ok: false, error: "workflow.atom.catalog 'tools' must be an array" };
        }
        const tools: ExposedTool[] = [];
        for (const entry of parsed.tools) {
            if (!isRecord(entry)) {
                return { ok: false, error: 'workflow.atom.catalog entries must be objects' };
            }
            const wireName = asString(entry.wire_name);
            const version = asString(entry.version);
            const description = asString(entry.description);
            const hasSideEffects = asBoolean(entry.has_side_effects);
            const schema = entry.parameters_schema;
            if (
                wireName === null ||
                wireName.length === 0 ||
                version === null ||
                version.length === 0 ||
                description === null ||
                description.length === 0 ||
                hasSideEffects === null ||
                schema === undefined ||
                !isRecord(schema)
            ) {
                return {
                    ok: false,
                    error:
                        "workflow.atom.catalog entries require 'wire_name', 'version', 'description', 'has_side_effects' and a 'parameters_schema' object",
                };
            }
            tools.push({
                wire_name: wireName,
                version,
                description,
                has_side_effects: hasSideEffects,
                parameters_schema: schema,
            });
        }
        return {
            ok: true,
            response: { ok: true, id, payload: { kind: 'workflow-atom-catalog', value: { tools } } },
        };
    }
    if (parsed.definition !== undefined) {
        // WorkflowDefinitionView discriminates on "definition"; it also
        // carries workflow_id + digest, so it must precede those branches.
        if (!isRecord(parsed.definition)) {
            return { ok: false, error: "workflow.get 'definition' must be an object" };
        }
        const workflowId = asString(parsed.workflow_id);
        const digest = asString(parsed.digest);
        if (workflowId === null || workflowId.length === 0 || digest === null || digest.length === 0) {
            return {
                ok: false,
                error:
                    "workflow.get response requires 'workflow_id', 'digest' and a 'definition' object",
            };
        }
        return {
            ok: true,
            response: {
                ok: true,
                id,
                payload: {
                    kind: 'workflow-get',
                    value: { workflow_id: workflowId, digest, definition: parsed.definition },
                },
            },
        };
    }
    if (parsed.active_application !== undefined) {
        // ObservationView discriminates on "active_application" — no other
        // payload carries it.
        const view = decodeObservationView(parsed, errorHolder);
        if (view === null) {
            return { ok: false, error: errorHolder.text };
        }
        return {
            ok: true,
            response: { ok: true, id, payload: { kind: 'observation-view', value: view } },
        };
    }
    if (parsed.dry_run_id !== undefined) {
        // WorkflowPublished also carries workflow_id + digest; it must be
        // decoded before the saved shape (schema doc 6.5).
        const workflowId = asString(parsed.workflow_id);
        const digest = asString(parsed.digest);
        const dryRunId = asString(parsed.dry_run_id);
        const idempotent = asBoolean(parsed.idempotent);
        if (
            workflowId === null ||
            workflowId.length === 0 ||
            digest === null ||
            digest.length === 0 ||
            dryRunId === null ||
            dryRunId.length === 0 ||
            idempotent === null
        ) {
            return {
                ok: false,
                error:
                    "workflow.publish response requires 'workflow_id', 'digest', 'dry_run_id' and 'idempotent'",
            };
        }
        return {
            ok: true,
            response: {
                ok: true,
                id,
                payload: {
                    kind: 'workflow-published',
                    value: { workflow_id: workflowId, digest, dry_run_id: dryRunId, idempotent },
                },
            },
        };
    }
    if (parsed.digest !== undefined) {
        const workflowId = asString(parsed.workflow_id);
        const digest = asString(parsed.digest);
        if (workflowId === null || workflowId.length === 0 || digest === null || digest.length === 0) {
            return { ok: false, error: "workflow.save response requires 'workflow_id' and 'digest'" };
        }
        return {
            ok: true,
            response: {
                ok: true,
                id,
                payload: { kind: 'workflow-saved', value: { workflow_id: workflowId, digest } },
            },
        };
    }
    if (parsed.workflow_id !== undefined) {
        const workflowId = asString(parsed.workflow_id);
        if (workflowId === null || workflowId.length === 0) {
            return { ok: false, error: "workflow.delete response requires a non-empty 'workflow_id'" };
        }
        return {
            ok: true,
            response: {
                ok: true,
                id,
                payload: { kind: 'workflow-deleted', value: { workflow_id: workflowId } },
            },
        };
    }
    if (parsed.run_id !== undefined) {
        const runId = asString(parsed.run_id);
        if (runId === null || runId.length === 0) {
            return { ok: false, error: "workflow.run response requires a non-empty 'run_id'" };
        }
        if (parsed.state !== undefined) {
            // The cancelled reply adds "state" to the same envelope shape.
            const state = asString(parsed.state);
            if (state === null) {
                return { ok: false, error: "workflow.cancel response requires a 'state' string" };
            }
            return {
                ok: true,
                response: {
                    ok: true,
                    id,
                    payload: { kind: 'workflow-run-cancelled', value: { run_id: runId, state: state as WorkflowRunState } },
                },
            };
        }
        return {
            ok: true,
            response: {
                ok: true,
                id,
                payload: { kind: 'workflow-run-started', value: { run_id: runId } },
            },
        };
    }
    // An ok response carrying none of the known payload discriminators is the
    // acknowledgement shape (service.shutdown).
    return {
        ok: true,
        response: { ok: true, id, payload: { kind: 'shutdown-accepted' } },
    };
}

// ---------------------------------------------------------------------------
// Events (DEC-012 draft)
// ---------------------------------------------------------------------------

export type EventDecode =
    | { ok: true; event: ServerEvent }
    | { ok: false; error: string };

export function decodeEvent(payload: string): EventDecode {
    let parsed: unknown;
    try {
        parsed = JSON.parse(payload);
    } catch {
        return { ok: false, error: 'payload is not valid JSON' };
    }
    if (!isRecord(parsed)) {
        return { ok: false, error: 'event payload must be a JSON object' };
    }
    const version = asInteger(parsed.v);
    if (version === null || version !== PROTOCOL_VERSION) {
        return { ok: false, error: 'unsupported protocol version' };
    }
    const seq = asInteger(parsed.seq);
    if (seq === null || seq < 1) {
        return { ok: false, error: "event is missing a positive 'seq'" };
    }
    const name = asString(parsed.event);
    if (name === null) {
        return { ok: false, error: "event is missing 'event'" };
    }
    switch (name as EventName) {
        case 'task.updated': {
            const taskId = asString(parsed.task_id);
            const goal = asString(parsed.goal);
            const progress = asString(parsed.progress);
            const hasSuccess = asBoolean(parsed.has_success);
            const success = asBoolean(parsed.success);
            if (taskId === null || goal === null || progress === null || hasSuccess === null || success === null) {
                return {
                    ok: false,
                    error:
                        "task.updated requires 'task_id', 'goal', 'progress', 'has_success' and 'success'",
                };
            }
            if (!PROGRESS_NAMES.includes(progress as TaskProgress)) {
                return { ok: false, error: "task.updated 'progress' is not a known progress name" };
            }
            return {
                ok: true,
                event: {
                    v: 1,
                    seq,
                    event: 'task.updated',
                    task_id: taskId,
                    goal,
                    progress: progress as TaskProgress,
                    has_success: hasSuccess,
                    success,
                },
            };
        }
        case 'host.status': {
            const status = asString(parsed.status);
            if (status === null) {
                return { ok: false, error: "host.status requires 'status'" };
            }
            if (!HOST_STATUSES.includes(status as HostStatus)) {
                return { ok: false, error: "host.status 'status' is not a known host status" };
            }
            return { ok: true, event: { v: 1, seq, event: 'host.status', status: status as HostStatus } };
        }
        case 'events.overflow': {
            const dropped = asInteger(parsed.dropped);
            if (dropped === null || dropped < 0) {
                return { ok: false, error: "events.overflow requires a non-negative 'dropped'" };
            }
            return { ok: true, event: { v: 1, seq, event: 'events.overflow', dropped } };
        }
        case 'permission.request': {
            const requestId = asString(parsed.request_id);
            const capability = asString(parsed.capability);
            const resource = asString(parsed.resource);
            const taskId = asString(parsed.task_id);
            const timeout = asInteger(parsed.timeout_ms);
            if (
                requestId === null ||
                requestId.length === 0 ||
                capability === null ||
                resource === null ||
                taskId === null ||
                taskId.length === 0 ||
                timeout === null
            ) {
                return {
                    ok: false,
                    error:
                        "permission.request requires 'request_id', 'capability', 'resource', 'task_id' and 'timeout_ms'",
                };
            }
            if (!CAPABILITY_NAMES.includes(capability)) {
                return {
                    ok: false,
                    error: "permission.request 'capability' is not a known capability",
                };
            }
            if (timeout <= 0) {
                return { ok: false, error: "permission.request 'timeout_ms' must be positive" };
            }
            return {
                ok: true,
                event: {
                    v: 1,
                    seq,
                    event: 'permission.request',
                    request_id: requestId,
                    capability,
                    resource,
                    task_id: taskId,
                    timeout_ms: timeout,
                },
            };
        }
        case 'session.updated': {
            const sessionId = asString(parsed.session_id);
            const state = asString(parsed.state);
            if (sessionId === null || sessionId.length === 0 || state === null) {
                return { ok: false, error: "session.updated requires 'session_id' and 'state'" };
            }
            if (!SESSION_STATES.includes(state as SessionState)) {
                return { ok: false, error: "session.updated 'state' is not a known session state" };
            }
            return {
                ok: true,
                event: { v: 1, seq, event: 'session.updated', session_id: sessionId, state: state as SessionState },
            };
        }
        case 'session.message': {
            const sessionId = asString(parsed.session_id);
            const taskId = asString(parsed.task_id);
            const kind = asString(parsed.kind);
            const text = asString(parsed.text);
            const sequence = asInteger(parsed.sequence);
            if (
                sessionId === null ||
                sessionId.length === 0 ||
                taskId === null ||
                taskId.length === 0 ||
                kind === null ||
                text === null ||
                text.length === 0 ||
                sequence === null ||
                sequence < 1
            ) {
                return {
                    ok: false,
                    error:
                        "session.message requires 'session_id', 'task_id', 'kind', non-empty 'text' and a positive 'sequence'",
                };
            }
            if (!SESSION_MESSAGE_KINDS.includes(kind as SessionMessageKind)) {
                return { ok: false, error: "session.message 'kind' is not a known message kind" };
            }
            return {
                ok: true,
                event: {
                    v: 1,
                    seq,
                    event: 'session.message',
                    session_id: sessionId,
                    task_id: taskId,
                    kind: kind as SessionMessageKind,
                    text,
                    sequence,
                },
            };
        }
        case 'session.turn': {
            const sessionId = asString(parsed.session_id);
            const taskId = asString(parsed.task_id);
            const step = asInteger(parsed.step);
            const kind = asString(parsed.kind);
            const status = asString(parsed.status);
            if (
                sessionId === null ||
                sessionId.length === 0 ||
                taskId === null ||
                taskId.length === 0 ||
                step === null ||
                step < 1 ||
                kind === null ||
                status === null
            ) {
                return {
                    ok: false,
                    error:
                        "session.turn requires 'session_id', 'task_id', a positive 'step', 'kind' and 'status'",
                };
            }
            if (!STEP_KINDS.includes(kind as StepKind)) {
                return { ok: false, error: "session.turn 'kind' is not a known step kind" };
            }
            if (!TURN_STATUSES.includes(status as TurnStatus)) {
                return {
                    ok: false,
                    error: "session.turn 'status' is not a known settled-step status",
                };
            }
            return {
                ok: true,
                event: {
                    v: 1,
                    seq,
                    event: 'session.turn',
                    session_id: sessionId,
                    task_id: taskId,
                    step,
                    kind: kind as StepKind,
                    status: status as TurnStatus,
                },
            };
        }
        case 'session.output': {
            const sessionId = asString(parsed.session_id);
            const taskId = asString(parsed.task_id);
            const step = asInteger(parsed.step);
            const chunk = asString(parsed.chunk);
            const truncated = asBoolean(parsed.truncated);
            if (
                sessionId === null ||
                sessionId.length === 0 ||
                taskId === null ||
                taskId.length === 0 ||
                step === null ||
                step < 1 ||
                chunk === null ||
                truncated === null
            ) {
                return {
                    ok: false,
                    error:
                        "session.output requires 'session_id', 'task_id', a positive 'step', 'chunk' and 'truncated'",
                };
            }
            return {
                ok: true,
                event: {
                    v: 1,
                    seq,
                    event: 'session.output',
                    session_id: sessionId,
                    task_id: taskId,
                    step,
                    chunk,
                    truncated,
                },
            };
        }
        case 'workflow.run_updated': {
            const runId = asString(parsed.run_id);
            const workflowId = asString(parsed.workflow_id);
            const state = asString(parsed.state);
            const runEpoch = asInteger(parsed.run_epoch);
            if (
                runId === null ||
                runId.length === 0 ||
                workflowId === null ||
                workflowId.length === 0 ||
                state === null ||
                runEpoch === null ||
                runEpoch < 0
            ) {
                return {
                    ok: false,
                    error:
                        "workflow.run_updated requires 'run_id', 'workflow_id', 'state', a non-negative 'run_epoch'",
                };
            }
            if (!WORKFLOW_RUN_STATES.includes(state as WorkflowRunState)) {
                return { ok: false, error: "workflow.run_updated 'state' is not a known run state" };
            }
            let summary: string | undefined;
            if (parsed.summary !== undefined) {
                const summaryValue = asString(parsed.summary);
                if (summaryValue === null) {
                    return { ok: false, error: "workflow.run_updated 'summary' must be a string" };
                }
                summary = summaryValue;
            }
            const event: ServerEvent = {
                v: 1,
                seq,
                event: 'workflow.run_updated',
                run_id: runId,
                workflow_id: workflowId,
                state: state as WorkflowRunState,
                run_epoch: runEpoch,
            };
            if (summary !== undefined) {
                (event as { summary?: string }).summary = summary;
            }
            return { ok: true, event };
        }
        default:
            return { ok: false, error: `unknown event '${name}'` };
    }
}

/** Classifies one decoded wire object by its discriminating member (DEC-012):
 * `op` is a request, `ok` a response, `event` an event. */
export type FrameKind = 'request' | 'response' | 'event' | 'unknown';

export function classifyFrame(payload: string): FrameKind {
    let parsed: unknown;
    try {
        parsed = JSON.parse(payload);
    } catch {
        return 'unknown';
    }
    if (!isRecord(parsed)) {
        return 'unknown';
    }
    if (parsed.op !== undefined) {
        return 'request';
    }
    if (parsed.ok !== undefined) {
        return 'response';
    }
    if (parsed.event !== undefined) {
        return 'event';
    }
    return 'unknown';
}

export {
    STEP_KINDS,
    STEP_STATUSES,
    HOST_STATUSES,
    PROGRESS_NAMES,
    CAPABILITY_NAMES,
    SESSION_STATES,
    SESSION_MESSAGE_KINDS,
    TURN_STATUSES,
};
