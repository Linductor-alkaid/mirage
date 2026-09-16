/// Unit tests for the transport abstraction (transport.ts): response
/// unwrapping into payloads vs IpcRequestError, and the error class surface
/// shared by mock and future real transports.

import { describe, expect, it } from 'vitest';
import { IpcRequestError, TransportClosedError, unwrapResponse } from '../src/transport.js';
import type { ResponseEnvelop } from '../src/types.js';

describe('unwrapResponse', () => {
    it('returns the payload of a successful response', () => {
        const response: ResponseEnvelop = {
            ok: true,
            id: 1,
            payload: { kind: 'submitted', value: { task_id: 'task-0001' } },
        };
        expect(unwrapResponse(response)).toEqual({ kind: 'submitted', value: { task_id: 'task-0001' } });
    });

    it('returns the shutdown-ack payload untouched', () => {
        const response: ResponseEnvelop = { ok: true, id: 2, payload: { kind: 'shutdown-accepted' } };
        expect(unwrapResponse(response)).toEqual({ kind: 'shutdown-accepted' });
    });

    it('throws IpcRequestError carrying code and message for failures', () => {
        const response: ResponseEnvelop = {
            ok: false,
            id: 3,
            error: { code: 'not_found', message: "task 'task-9999' was not found" },
        };
        let caught: unknown;
        try {
            unwrapResponse(response);
        } catch (error) {
            caught = error;
        }
        expect(caught).toBeInstanceOf(IpcRequestError);
        const error = caught as IpcRequestError;
        expect(error.code).toBe('not_found');
        expect(error.message).toBe("task 'task-9999' was not found");
        expect(error.name).toBe('IpcRequestError');
    });
});

describe('error classes', () => {
    it('IpcRequestError is an Error with the given code', () => {
        const error = new IpcRequestError('invalid_state', 'mira host is not running');
        expect(error).toBeInstanceOf(Error);
        expect(error.code).toBe('invalid_state');
        expect(error.message).toBe('mira host is not running');
        expect(error.name).toBe('IpcRequestError');
    });

    it('TransportClosedError is an Error with a stable name', () => {
        const error = new TransportClosedError('mock service is closed');
        expect(error).toBeInstanceOf(Error);
        expect(error.message).toBe('mock service is closed');
        expect(error.name).toBe('TransportClosedError');
    });
});
