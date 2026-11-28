// What the web UI knows about the piano, in one Zustand store. Kept flat and normalized: every
// collection is keyed by its id (a setting's name, a key's MIDI note number, a log line's number),
// with no nested trees, so each component picks out exactly what it shows.

import type {
    Devices,
    LogLevel,
    ServiceState,
    SettingInfo,
    SettingValue,
    SettingValues,
} from '@piano-lab/web-api';
import { create } from 'zustand';

/** The link to piano-lab: connecting (or reconnecting), or open. */
export type Connection = 'connecting' | 'open';

/** One line of the log panel. */
export interface LogEntry {
    id: number;
    /** When it arrived, in milliseconds since 1970. */
    at: number;
    level: Exclude<LogLevel, 'debug'>;
    message: string;
}

/** The most log lines kept; older ones are dropped. */
export const maxLogEntries = 500;

export interface PianoState {
    connection: Connection;
    /** What the audio service is doing, as piano-lab last said; undefined until it has. */
    service: ServiceState | undefined;
    /** Every setting's current value, by name. */
    settings: Record<string, SettingValue>;
    /** Every setting's description, by name. */
    schema: Record<string, SettingInfo>;
    /** The audio outputs and MIDI inputs piano-lab can open, once read. */
    devices: Devices | undefined;
    /** The keys held down on the piano (from any source), by MIDI note number. */
    heldKeys: Record<number, true>;
    /** The output level: the loudest recent sample, 0 to 1 (more when clipping). */
    level: number;
    /** The log lines, oldest first, by id. */
    logIds: number[];
    logById: Record<number, LogEntry>;
    nextLogId: number;
}

/** How the store changes: one function per kind of news. */
export interface PianoActions {
    setConnection(connection: Connection): void;
    setService(service: ServiceState): void;
    setSettings(values: SettingValues): void;
    setSchema(infos: readonly SettingInfo[]): void;
    setDevices(devices: Devices): void;
    setHeldKeys(keys: readonly number[]): void;
    setLevel(peak: number): void;
    addLog(level: LogEntry['level'], message: string, at?: number): void;
    clearLog(): void;
}

export type PianoStore = PianoState & PianoActions;

/** The state before piano-lab has said anything. */
export const initialState: PianoState = {
    connection: 'connecting',
    service: undefined,
    settings: {},
    schema: {},
    devices: undefined,
    heldKeys: {},
    level: 0,
    logIds: [],
    logById: {},
    nextLogId: 1,
};

/** Makes a store (the app has one, `usePiano`; tests make their own). */
export function createPianoStore() {
    return create<PianoStore>()((set) => ({
        ...initialState,
        setConnection: (connection) =>
            set(connection === 'open' ? { connection } : { connection, heldKeys: {}, level: 0 }),
        setService: (service) => set({ service }),
        setSettings: (values) => set({ settings: { ...values } }),
        setSchema: (infos) => set({ schema: Object.fromEntries(infos.map((i) => [i.name, i])) }),
        setDevices: (devices) => set({ devices }),
        setHeldKeys: (keys) =>
            set({ heldKeys: Object.fromEntries(keys.map((key) => [key, true as const])) }),
        setLevel: (level) => set({ level }),
        addLog: (level, message, at = Date.now()) =>
            set((state) => {
                const id = state.nextLogId;
                const kept = state.logIds.slice(-(maxLogEntries - 1));
                const logById: Record<number, LogEntry> = {};
                for (const old of kept) {
                    const entry = state.logById[old];
                    if (entry !== undefined) logById[old] = entry;
                }
                logById[id] = { id, at, level, message };
                return { logIds: [...kept, id], logById, nextLogId: id + 1 };
            }),
        clearLog: () => set({ logIds: [], logById: {} }),
    }));
}

export type PianoStoreApi = ReturnType<typeof createPianoStore>;
