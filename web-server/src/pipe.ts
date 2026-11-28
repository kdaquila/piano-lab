// The service pipe, from the web server's side: the private link to the audio service, over its
// stdin (commands) and stdout (replies and events). Messages are JSON-RPC 2.0, one per line; the
// audio service's side is audio-service/pipe (messages.hpp), and this file mirrors it.
//
// The audio service is our own program, but its messages are still checked against these schemas
// as they arrive, so a mismatch between the two sides shows up as a clear problem, not as a
// wrong value somewhere later.

import { Type } from 'typebox';
import { Compile } from 'typebox/compile';
import {
    AudioStatus,
    Devices,
    LogLevel,
    RecordingStarted,
    RecordingStopped,
    SettingInfo,
    SettingValues,
} from '@piano-lab/web-api';

/** The longest line read from the audio service, in characters. Longer lines are dropped whole. */
export const maxLineLength = 1024 * 1024;

/** JSON-RPC 2.0's error codes, as the audio service uses them in error replies. */
export const errorCode = {
    /** The line isn't JSON. */
    parseError: -32700,
    /** The line is JSON, but not a JSON-RPC 2.0 request. */
    invalidRequest: -32600,
    /** No such method. */
    methodNotFound: -32601,
    /** The method exists, but its parameters are wrong. */
    invalidParams: -32602,
    /** The request was read, but couldn't be carried out; the message says why. */
    failed: -32000,
} as const;

// --- Replies -----------------------------------------------------------------------------------

/** No parameters. */
const NoParams = Type.Object({}, { additionalProperties: false });

/**
 * Every method that gets a reply: its parameters, and its reply's result. `settings.set`'s
 * parameters are the settings to change, and its result is every setting after the change.
 */
export const requests = {
    status: { params: NoParams, result: AudioStatus },
    'devices.list': { params: NoParams, result: Devices },
    'settings.get': { params: NoParams, result: SettingValues },
    'settings.schema': { params: NoParams, result: Type.Array(SettingInfo) },
    'settings.set': { params: SettingValues, result: SettingValues },
    'recording.start': { params: NoParams, result: RecordingStarted },
    'recording.stop': { params: NoParams, result: RecordingStopped },
} as const;

/** A method that gets a reply. */
export type RequestMethod = keyof typeof requests;

/** The parameters `method` takes. */
export type RequestParams<M extends RequestMethod> = Type.Static<(typeof requests)[M]['params']>;

/** The result `method`'s reply carries. */
export type RequestResult<M extends RequestMethod> = Type.Static<(typeof requests)[M]['result']>;

/** A checker for each method's result. */
const resultCheckers = Object.fromEntries(
    Object.entries(requests).map(([method, { result }]) => [method, Compile(result)]),
) as { [M in RequestMethod]: ReturnType<typeof Compile<(typeof requests)[M]['result']>> };

// --- Notifications (no reply) ------------------------------------------------------------------

/** Every method sent without waiting for a reply: on-screen keys. */
export const notifications = {
    /** A key pressed on screen. */
    'note.on': Type.Object({
        /** MIDI note number. */
        key: Type.Integer(),
        /** 0 to 1. */
        velocity: Type.Number(),
    }),
    /** A key released on screen. */
    'note.off': Type.Object({
        /** MIDI note number. */
        key: Type.Integer(),
    }),
} as const;

/** A method sent without waiting for a reply. */
export type NotificationMethod = keyof typeof notifications;

/** The parameters notification `method` takes. */
export type NotificationParams<M extends NotificationMethod> = Type.Static<
    (typeof notifications)[M]
>;

// --- Events --------------------------------------------------------------------------------------

/** What the audio service tells the web server without being asked, by event name. */
export const events = {
    /** Ready for commands. Sent once; only `log` events come before it. */
    ready: Type.Object({ version: Type.String() }),
    /** A line for the log. */
    log: Type.Object({
        level: LogLevel,
        message: Type.String(),
    }),
    /** The keys held down changed: MIDI note numbers, lowest first. */
    keys: Type.Object({ keys: Type.Array(Type.Integer()) }),
    /** The output level: the loudest sample since the last one, 0 to 1 (more when clipping). */
    level: Type.Object({ peak: Type.Number() }),
    /** Settings changed. Holds every setting. */
    settings: Type.Object({ values: SettingValues }),
} as const;

/** An event's name. */
export type EventName = keyof typeof events;

/** An event: its name, and what it carries. */
export type ServiceEvent = {
    [N in EventName]: { event: N; params: Type.Static<(typeof events)[N]> };
}[EventName];

// --- Reading -------------------------------------------------------------------------------------

/** A JSON-RPC error, from an error reply. */
export interface RpcError {
    /** One of `errorCode`, or another JSON-RPC code. */
    code: number;
    /** What went wrong, for people. */
    message: string;
}

/** A line read from the audio service. */
export type ServiceMessage =
    /** A reply to request `id`: its result, not yet checked (see `checkResult`). */
    | { kind: 'result'; id: number; result: unknown }
    /** An error reply. `id` is null when the audio service couldn't read the request's id. */
    | { kind: 'error'; id: number | null; error: RpcError }
    /** An event, already checked. */
    | { kind: 'event'; event: ServiceEvent }
    /** A line that isn't any of these: why, for the log. */
    | { kind: 'invalid'; problem: string };

/** The shape of every line from the audio service. */
const Envelope = Type.Object({
    jsonrpc: Type.Literal('2.0'),
    id: Type.Optional(Type.Union([Type.Integer(), Type.Null()])),
    method: Type.Optional(Type.String()),
    params: Type.Optional(Type.Unknown()),
    result: Type.Optional(Type.Unknown()),
    error: Type.Optional(Type.Object({ code: Type.Integer(), message: Type.String() })),
});
const envelopeChecker = Compile(Envelope);

/** A checker for each event's parameters. */
const eventCheckers = Object.fromEntries(
    Object.entries(events).map(([name, schema]) => [name, Compile(schema)]),
) as Record<EventName, ReturnType<typeof Compile>>;

/** The first problem `checker` finds in `value`, for a message. */
function firstProblem(checker: ReturnType<typeof Compile>, value: unknown): string {
    const [error] = checker.Errors(value);
    return error ? `${error.instancePath || '(the message)'} ${error.message}` : 'invalid';
}

/** True when `name` is an event name. */
function isEventName(name: string): name is EventName {
    return Object.hasOwn(events, name);
}

/** Reads one line from the audio service (without its line ending). Never throws. */
export function readMessage(line: string): ServiceMessage {
    let value: unknown;
    try {
        value = JSON.parse(line);
    } catch {
        return { kind: 'invalid', problem: `not JSON: ${line.slice(0, 200)}` };
    }
    if (!envelopeChecker.Check(value)) {
        return {
            kind: 'invalid',
            problem: `not a JSON-RPC 2.0 message: ${firstProblem(envelopeChecker, value)}`,
        };
    }
    if (value.method !== undefined) {
        const name = value.method;
        if (!isEventName(name)) {
            return { kind: 'invalid', problem: `unknown event '${name.slice(0, 100)}'` };
        }
        const checker = eventCheckers[name];
        if (!checker.Check(value.params)) {
            return {
                kind: 'invalid',
                problem: `event '${name}': ${firstProblem(checker, value.params)}`,
            };
        }
        return { kind: 'event', event: { event: name, params: value.params } as ServiceEvent };
    }
    if (value.error !== undefined) {
        return { kind: 'error', id: value.id ?? null, error: value.error };
    }
    if (typeof value.id === 'number' && 'result' in value) {
        return { kind: 'result', id: value.id, result: value.result };
    }
    return { kind: 'invalid', problem: 'a reply with neither a result nor an error' };
}

/**
 * Checks a reply's result against what `method` returns. Returns it typed, or why it doesn't
 * match.
 */
export function checkResult<M extends RequestMethod>(
    method: M,
    result: unknown,
): { ok: true; value: RequestResult<M> } | { ok: false; problem: string } {
    const checker: ReturnType<typeof Compile> = resultCheckers[method];
    if (checker.Check(result)) {
        return { ok: true, value: result as RequestResult<M> };
    }
    return { ok: false, problem: `${method}'s result: ${firstProblem(checker, result)}` };
}

// --- Writing -------------------------------------------------------------------------------------

/** The line (with its line ending) asking for `method`, to be answered with the same `id`. */
export function requestLine<M extends RequestMethod>(
    id: number,
    method: M,
    params: RequestParams<M>,
): string {
    return `${JSON.stringify({ jsonrpc: '2.0', id, method, params })}\n`;
}

/** The line (with its line ending) for a notification: no reply comes. */
export function notificationLine<M extends NotificationMethod>(
    method: M,
    params: NotificationParams<M>,
): string {
    return `${JSON.stringify({ jsonrpc: '2.0', method, params })}\n`;
}

// --- Splitting the output into lines -------------------------------------------------------------

/** Lines taken from a chunk of output, and how many overlong lines were dropped. */
export interface SplitLines {
    lines: string[];
    dropped: number;
}

/**
 * Splits text arriving in chunks into lines. Strips `\r`, skips empty lines, and drops any line
 * longer than `maxLength` characters whole. Mirrors the audio service's `LineSplitter`.
 */
export class LineSplitter {
    readonly #maxLength: number;
    #partial = '';
    /** True while dropping the rest of an overlong line, up to its line ending. */
    #dropping = false;

    constructor(maxLength: number = maxLineLength) {
        this.#maxLength = maxLength;
    }

    /** Takes the next chunk, and returns the lines it completes. */
    push(chunk: string): SplitLines {
        const result: SplitLines = { lines: [], dropped: 0 };
        let start = 0;
        for (;;) {
            const end = chunk.indexOf('\n', start);
            const piece = chunk.slice(start, end === -1 ? undefined : end);
            if (!this.#dropping) {
                this.#partial += piece;
                if (this.#partial.length > this.#maxLength) {
                    this.#partial = '';
                    this.#dropping = true;
                    result.dropped += 1;
                }
            }
            if (end === -1) {
                return result;
            }
            if (!this.#dropping) {
                const line = this.#partial.endsWith('\r')
                    ? this.#partial.slice(0, -1)
                    : this.#partial;
                if (line !== '') {
                    result.lines.push(line);
                }
            }
            this.#partial = '';
            this.#dropping = false;
            start = end + 1;
        }
    }
}
