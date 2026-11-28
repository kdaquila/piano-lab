// The HTTP side of the web API: its paths, what `GET /api/v0/status` returns, and the one error
// shape every request may answer with. See docs/web-api.md.

import { Type } from 'typebox';
import { AudioStatus } from './audio.ts';

/** The API's version, in every path. A breaking change gets a new one. */
export const apiVersion = 'v0';

/** Where the API lives: every path starts with this. */
export const apiBase = `/api/${apiVersion}`;

/** Every path of the API. */
export const paths = {
    status: `${apiBase}/status`,
    stop: `${apiBase}/stop`,
    devices: `${apiBase}/devices`,
    settings: `${apiBase}/settings`,
    settingsSchema: `${apiBase}/settings/schema`,
    recordingStart: `${apiBase}/recording/start`,
    recordingStop: `${apiBase}/recording/stop`,
    /** The WebSocket for live traffic (see live.ts). */
    live: `${apiBase}/live`,
} as const;

/** The port piano-lab listens on (a nod to A440). */
export const defaultPort = 4440;

/** The largest request body, and the largest WebSocket message, in bytes. */
export const maxMessageBytes = 64 * 1024;

/** What the audio service is doing. */
export const ServiceState = Type.Union([
    /** Starting; not ready for requests yet. */
    Type.Literal('starting'),
    /** Ready for requests. */
    Type.Literal('running'),
    /** It stopped unexpectedly and is being started again. */
    Type.Literal('restarting'),
    /** piano-lab is stopping. */
    Type.Literal('stopping'),
]);
export type ServiceState = Type.Static<typeof ServiceState>;

/** `GET /api/v0/status`: what's running. */
export const Status = Type.Object({
    /** piano-lab's version, e.g. "0.1.0". */
    version: Type.String(),
    /** What the audio service is doing. */
    audioService: ServiceState,
    /** What it has open; null unless it's running. */
    audio: Type.Union([AudioStatus, Type.Null()]),
});
export type Status = Type.Static<typeof Status>;

/** Why a request failed, in the error reply. */
export const ErrorCode = Type.Union([
    /** 400: the request doesn't match the API (its message says where). */
    Type.Literal('invalid-request'),
    /** 403: refused by the browser protections (Host or Origin). */
    Type.Literal('forbidden'),
    /** 404: no such path (or not with this method). */
    Type.Literal('not-found'),
    /** 413: the body is too big. */
    Type.Literal('too-large'),
    /** 415: a body that isn't `application/json`. */
    Type.Literal('unsupported-media-type'),
    /** 422: well-formed, but the piano refused it, e.g. a setting out of range. */
    Type.Literal('refused'),
    /** 503: the audio service isn't running right now (starting or restarting). */
    Type.Literal('unavailable'),
    /** 504: the audio service didn't answer in time. */
    Type.Literal('timeout'),
    /** 500: something went wrong inside piano-lab. */
    Type.Literal('internal'),
]);
export type ErrorCode = Type.Static<typeof ErrorCode>;

/** Every failed request's reply. */
export const ErrorReply = Type.Object({
    error: Type.Object({
        code: ErrorCode,
        /** What went wrong, for people. */
        message: Type.String(),
    }),
});
export type ErrorReply = Type.Static<typeof ErrorReply>;

/** The HTTP status each error code is sent with. */
export const errorStatus: Record<ErrorCode, number> = {
    'invalid-request': 400,
    forbidden: 403,
    'not-found': 404,
    'too-large': 413,
    'unsupported-media-type': 415,
    refused: 422,
    unavailable: 503,
    timeout: 504,
    internal: 500,
};
