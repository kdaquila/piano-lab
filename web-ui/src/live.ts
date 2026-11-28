// The web UI's link to piano-lab: the live WebSocket for events and commands, and the HTTP
// requests for what the WebSocket doesn't carry (the settings' descriptions and the devices).
// Every message from piano-lab is checked against the web API's own schemas before it's used.
// When the link drops, it's opened again, with a pause that grows while piano-lab stays away.

import {
    Command,
    Devices,
    ErrorReply,
    Event,
    SettingInfo,
    paths,
    type SettingValues,
} from '@piano-lab/web-api';
import { Type, type TSchema } from 'typebox';
import { Value } from 'typebox/value';
import type { PianoStoreApi } from './store.ts';

/** The pause before the first reconnection attempt, in milliseconds; it doubles each time. */
export const firstRetryMs = 250;
/** The longest pause between reconnection attempts, in milliseconds. */
export const maxRetryMs = 4_000;

/** What the link needs from the browser (tests pass stand-ins). */
export interface LiveOptions {
    store: PianoStoreApi;
    /** Where piano-lab is: `http://host:port`. */
    origin: string;
    /** Makes a WebSocket (the browser's `WebSocket` by default). */
    openSocket?: (url: string) => WebSocket;
    /** Makes an HTTP request (the browser's `fetch` by default). */
    fetch?: typeof fetch;
}

const SettingInfos = Type.Array(SettingInfo);

/** Why `value` doesn't match `schema`, for the log. */
function problem(schema: TSchema, value: unknown): string {
    const [first] = Value.Errors(schema, value);
    return first ? `${first.instancePath || '(the message)'} ${first.message}` : 'invalid';
}

/** The link to piano-lab. `start` opens it, and keeps it open until `stop`. */
export class Live {
    readonly #store: PianoStoreApi;
    readonly #origin: string;
    readonly #openSocket: (url: string) => WebSocket;
    readonly #fetch: typeof fetch;
    #socket: WebSocket | undefined;
    #retryMs = firstRetryMs;
    #retryTimer: ReturnType<typeof setTimeout> | undefined;
    #stopped = true;

    constructor(options: LiveOptions) {
        this.#store = options.store;
        this.#origin = options.origin;
        this.#openSocket = options.openSocket ?? ((url) => new WebSocket(url));
        this.#fetch = options.fetch ?? ((...args) => fetch(...args));
    }

    /** Opens the link, and reopens it whenever it drops. */
    start(): void {
        this.#stopped = false;
        this.#connect();
    }

    /** Closes the link for good. */
    stop(): void {
        this.#stopped = true;
        clearTimeout(this.#retryTimer);
        this.#socket?.close();
        this.#socket = undefined;
    }

    /** Sends a command. Returns false, sending nothing, while the link is down. */
    send(command: Command): boolean {
        const socket = this.#socket;
        if (socket === undefined || socket.readyState !== WebSocket.OPEN) return false;
        socket.send(JSON.stringify(command));
        return true;
    }

    /** Changes settings live: every client, this one included, hears the result. */
    setSettings(values: SettingValues): boolean {
        return this.send({ type: 'settings.set', values });
    }

    /** Reads what the WebSocket doesn't send: the settings' descriptions and the devices. */
    async refresh(): Promise<void> {
        const store = this.#store.getState();
        const [schema, devices] = await Promise.all([
            this.#get(paths.settingsSchema, SettingInfos),
            this.#get(paths.devices, Devices),
        ]);
        if (schema !== undefined) store.setSchema(schema);
        if (devices !== undefined) store.setDevices(devices);
    }

    #connect(): void {
        const store = this.#store.getState();
        store.setConnection('connecting');
        const url = `${this.#origin.replace(/^http/, 'ws')}${paths.live}`;
        const socket = this.#openSocket(url);
        this.#socket = socket;
        socket.addEventListener('open', () => {
            this.#retryMs = firstRetryMs;
            const state = this.#store.getState();
            state.setConnection('open');
            // piano-lab sends its recent log lines first, the ones shown before included.
            state.clearLog();
            void this.refresh();
        });
        socket.addEventListener('message', (message: MessageEvent) => {
            this.#onMessage(message.data);
        });
        socket.addEventListener('close', () => {
            if (this.#socket !== socket) return;
            this.#socket = undefined;
            if (this.#stopped) return;
            this.#store.getState().setConnection('connecting');
            this.#retryTimer = setTimeout(() => this.#connect(), this.#retryMs);
            this.#retryMs = Math.min(this.#retryMs * 2, maxRetryMs);
        });
    }

    /** Handles one message from piano-lab. */
    #onMessage(data: unknown): void {
        const store = this.#store.getState();
        let value: unknown;
        try {
            value = JSON.parse(String(data));
        } catch {
            store.addLog('warning', 'piano-lab sent a message that isn’t JSON');
            return;
        }
        if (!Value.Check(Event, value)) {
            store.addLog(
                'warning',
                `piano-lab sent an unexpected message: ${problem(Event, value)}`,
            );
            return;
        }
        switch (value.type) {
            case 'service':
                store.setService(value.state);
                // A restarted audio service may have other devices open.
                if (value.state === 'running') void this.refresh();
                return;
            case 'settings':
                store.setSettings(value.values);
                return;
            case 'keys':
                store.setHeldKeys(value.keys);
                return;
            case 'level':
                store.setLevel(value.peak);
                return;
            case 'log':
                if (value.level !== 'debug') store.addLog(value.level, value.message, value.at);
                return;
            case 'error':
                store.addLog('error', value.message);
                return;
        }
    }

    /** GETs `path`, checked against `schema`; logs and returns undefined when it fails. */
    async #get<S extends TSchema>(path: string, schema: S): Promise<Type.Static<S> | undefined> {
        const store = this.#store.getState();
        try {
            const response = await this.#fetch(`${this.#origin}${path}`);
            const body: unknown = await response.json();
            if (response.ok && Value.Check(schema, body)) return body as Type.Static<S>;
            if (Value.Check(ErrorReply, body)) {
                // Not running yet: it's read again once the audio service is.
                if (body.error.code !== 'unavailable') store.addLog('warning', body.error.message);
                return undefined;
            }
            store.addLog(
                'warning',
                `piano-lab answered ${path} unexpectedly: ${problem(schema, body)}`,
            );
        } catch {
            // The link is down; it's read again when it's back.
        }
        return undefined;
    }
}
