/// Wire codec for protocol v1, mirroring `runtime/ipc/src/protocol.cpp`
/// member-for-member (stable error strings included) plus the DEC-012 draft
/// event envelope. Drift against the C++ codec is a golden-vector test
/// failure (M1.5-01), so keep every branch and reason text aligned.

import type {
    EventName,
    HostStatus,
    InspectTask,
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
        case 'session.history':
            object.op = 'session.history';
            object.session_id = body.session_id;
            if (body.limit !== undefined) {
                object.limit = body.limit;
            }
            break;
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
