// The WebSocket side of the web API (`/api/v0/live`): live traffic, with no request and reply.
// Clients send commands; piano-lab sends events to every client, so a change made in one place
// shows up everywhere. Every message is one JSON object with a `type`. See docs/web-api.md.

import { Type } from 'typebox';
import { Key, LogLevel } from './audio.ts';
import { ServiceState } from './http.ts';
import { SettingValues } from './settings.ts';

// --- Commands: client to piano-lab ---------------------------------------------------------------

/** A key pressed (e.g. on screen). */
export const NoteOnCommand = Type.Object(
    {
        type: Type.Literal('note.on'),
        key: Key,
        /** How hard, 0 to 1. */
        velocity: Type.Number({ minimum: 0, maximum: 1 }),
    },
    { additionalProperties: false },
);

/** A key released. */
export const NoteOffCommand = Type.Object(
    { type: Type.Literal('note.off'), key: Key },
    { additionalProperties: false },
);

/**
 * Change some settings, live. All or nothing: if piano-lab refuses any of them, nothing changes
 * and only this client gets an `error` event. Otherwise every client gets a `settings` event.
 */
export const SetSettingsCommand = Type.Object(
    { type: Type.Literal('settings.set'), values: SettingValues },
    { additionalProperties: false },
);

/** Any command a client may send. */
export const Command = Type.Union([NoteOnCommand, NoteOffCommand, SetSettingsCommand]);
export type Command = Type.Static<typeof Command>;

// --- Events: piano-lab to clients ----------------------------------------------------------------

/** The keys held down changed (from any source), or a client just connected. */
export const KeysEvent = Type.Object({
    type: Type.Literal('keys'),
    /** MIDI note numbers, lowest first. */
    keys: Type.Array(Type.Integer()),
});

/** The output level, about 30 times a second while anything sounds, then one 0. */
export const LevelEvent = Type.Object({
    type: Type.Literal('level'),
    /** The loudest sample since the last one, 0 to 1 (more when clipping). */
    peak: Type.Number(),
});

/** A line from piano-lab's log (from `info` up). */
export const LogEvent = Type.Object({
    type: Type.Literal('log'),
    level: LogLevel,
    message: Type.String(),
});

/** Every setting's value: after any change, from any client, or when a client connects. */
export const SettingsEvent = Type.Object({
    type: Type.Literal('settings'),
    values: SettingValues,
});

/** What the audio service is doing changed, or a client just connected. */
export const ServiceEvent = Type.Object({
    type: Type.Literal('service'),
    state: ServiceState,
});

/** Sent to one client only: a command it sent was invalid or refused. */
export const ErrorEvent = Type.Object({
    type: Type.Literal('error'),
    /** What went wrong, for people. */
    message: Type.String(),
});

/** Any event piano-lab sends. */
export const Event = Type.Union([
    KeysEvent,
    LevelEvent,
    LogEvent,
    SettingsEvent,
    ServiceEvent,
    ErrorEvent,
]);
export type Event = Type.Static<typeof Event>;
