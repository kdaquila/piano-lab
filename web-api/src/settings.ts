// Settings in the web API: generic, by name, described by the audio service's own schema (see
// docs/web-api.md). The web server checks only their shape; the audio service checks names and
// ranges.

import { Type } from 'typebox';

/** The longest setting name, in characters. Today's longest is about 20. */
export const maxSettingNameLength = 64;

/** The longest text in a setting value, in characters (device names are the longest texts). */
export const maxSettingTextLength = 256;

/** The most texts in a list value (e.g. MIDI inputs). */
export const maxSettingListLength = 64;

/** The most settings in one message: every setting, or the ones to change. */
export const maxSettings = 256;

/** A setting's name: dotted words, e.g. `sine.gain` or `audio.bufferSize`. */
export const SettingName = Type.String({
    minLength: 1,
    maxLength: maxSettingNameLength,
    pattern: '^[A-Za-z][A-Za-z0-9]*(\\.[A-Za-z][A-Za-z0-9]*)*$',
});

/** One setting's value: a number, a text, or a list of texts (e.g. MIDI input names). */
export const SettingValue = Type.Union([
    Type.Number(),
    Type.String({ maxLength: maxSettingTextLength }),
    Type.Array(Type.String({ maxLength: maxSettingTextLength }), {
        maxItems: maxSettingListLength,
    }),
]);
export type SettingValue = Type.Static<typeof SettingValue>;

/** Settings by name, e.g. `{"sine.gain": 0.5}`: every setting, or the ones to change. */
// The name pattern only picks which properties the value schema applies to, so other names are
// refused separately (additionalProperties), and their length is bounded with propertyNames.
export const SettingValues = Type.Record(SettingName, SettingValue, {
    maxProperties: maxSettings,
    additionalProperties: false,
    propertyNames: { maxLength: maxSettingNameLength },
});
export type SettingValues = Type.Static<typeof SettingValues>;

/** How one setting is described in the settings schema. */
export const SettingInfo = Type.Object({
    /** e.g. `sine.gain` */
    name: SettingName,
    /** What kind of value it takes. */
    kind: Type.Union([Type.Literal('number'), Type.Literal('text'), Type.Literal('textList')]),
    /** Numbers only: the smallest valid value. */
    min: Type.Optional(Type.Number()),
    /** Numbers only: the largest valid value. */
    max: Type.Optional(Type.Number()),
    /** e.g. `s` or `Hz`; empty when it has none. */
    unit: Type.String(),
    /** One line for people. */
    description: Type.String(),
    /** The built-in value. */
    defaultValue: SettingValue,
    /** Numbers only: whole numbers only. */
    integer: Type.Boolean(),
    /** Changing it reopens the audio or MIDI devices, with a short dropout. */
    restartsDevices: Type.Boolean(),
});
export type SettingInfo = Type.Static<typeof SettingInfo>;
