// What the audio service reports, as the web API passes it on: what's open, the devices there
// are, and recordings.

import { Type } from 'typebox';

/** One kind of audio device and its outputs. */
export const AudioDeviceList = Type.Object({
    /** e.g. "Windows Audio" or "ASIO" */
    type: Type.String(),
    /** As the system names them. */
    outputs: Type.Array(Type.String()),
});
export type AudioDeviceList = Type.Static<typeof AudioDeviceList>;

/** The audio outputs and MIDI inputs there are. */
export const Devices = Type.Object({
    audio: Type.Array(AudioDeviceList),
    /** MIDI inputs, as the system names them. */
    midi: Type.Array(Type.String()),
});
export type Devices = Type.Static<typeof Devices>;

/** What the audio service has open. */
export const AudioStatus = Type.Object({
    /** The open device's type; empty when none is open. */
    audioType: Type.String(),
    /** The open output; empty when none is open. */
    audioOutput: Type.String(),
    /** In hertz; 0 when no device is open. */
    sampleRate: Type.Number(),
    /** In samples; 0 when no device is open. */
    bufferSize: Type.Integer(),
    /** The MIDI inputs that are open. */
    midiInputs: Type.Array(Type.String()),
    /** True while recording the output. */
    recording: Type.Boolean(),
});
export type AudioStatus = Type.Static<typeof AudioStatus>;

/** A recording that has started. */
export const RecordingStarted = Type.Object({
    /** Where it's being written. */
    file: Type.String(),
});
export type RecordingStarted = Type.Static<typeof RecordingStarted>;

/** A recording that has stopped. */
export const RecordingStopped = Type.Object({
    /** The finished file. */
    file: Type.String(),
    /** Its length, in seconds. */
    seconds: Type.Number(),
});
export type RecordingStopped = Type.Static<typeof RecordingStopped>;

/** The lowest MIDI note number. */
export const lowestMidiNote = 0;

/** The highest MIDI note number. */
export const highestMidiNote = 127;

/** A key, as a MIDI note number: 60 is middle C, 21 to 108 are the piano's 88 keys. */
export const Key = Type.Integer({ minimum: lowestMidiNote, maximum: highestMidiNote });

/** A line for the log's level. */
export const LogLevel = Type.Union([
    Type.Literal('debug'),
    Type.Literal('info'),
    Type.Literal('warning'),
    Type.Literal('error'),
]);
export type LogLevel = Type.Static<typeof LogLevel>;
