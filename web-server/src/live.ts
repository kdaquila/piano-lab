// The live side of the web API: every WebSocket client, and what's sent to them. Keeps the latest
// state (settings, keys held, what the audio service is doing) and the recent log lines, so a
// client that connects gets them at once, then every change as it happens.

import type { Event, LogLevel, ServiceState, SettingValues } from '@piano-lab/web-api';
import type { RunState } from './audio-service.ts';
import type { ServiceEvent } from './pipe.ts';

/** A connected client: what the hub needs of a WebSocket (`ws`'s `WebSocket` fits). */
export interface Client {
    /** Bytes queued to send, not yet written to the network. */
    readonly bufferedAmount: number;
    send(data: string): void;
}

/**
 * Bytes a client may have waiting before level events are skipped for it, so a slow client
 * doesn't pile up meter readings. Everything else is always sent.
 */
export const maxBufferedBytes = 256 * 1024;

/** How many recent log lines a client is sent on connecting. */
export const logLinesKept = 100;

/** A log event, as sent to clients. */
type LogLine = Extract<Event, { type: 'log' }>;

/** The web API's name for what the audio service is doing. */
export function publicState(state: RunState): ServiceState {
    return state === 'stopped' ? 'stopping' : state;
}

/** Every WebSocket client, and the state each one is told on connecting. */
export class LiveHub {
    readonly #clients = new Set<Client>();
    #settings: SettingValues | undefined;
    #keys: number[] = [];
    #state: ServiceState = 'starting';
    #logLines: LogLine[] = [];

    /** How many clients are connected. */
    get clientCount(): number {
        return this.#clients.size;
    }

    /** Adds a client, and tells it the current state. */
    add(client: Client): void {
        this.#clients.add(client);
        send(client, { type: 'service', state: this.#state });
        if (this.#settings !== undefined) {
            send(client, { type: 'settings', values: this.#settings });
        }
        send(client, { type: 'keys', keys: this.#keys });
        for (const line of this.#logLines) {
            send(client, line);
        }
    }

    /** Removes a client that has gone. */
    remove(client: Client): void {
        this.#clients.delete(client);
    }

    /** Sends `event` to every client. */
    broadcast(event: Event): void {
        const data = JSON.stringify(event);
        for (const client of this.#clients) {
            if (event.type !== 'level' || client.bufferedAmount <= maxBufferedBytes) {
                client.send(data);
            }
        }
    }

    /** What the audio service is doing changed. */
    setState(state: RunState): void {
        const next = publicState(state);
        if (next !== this.#state) {
            this.#state = next;
            this.broadcast({ type: 'service', state: next });
        }
        if (state === 'restarting' && this.#keys.length > 0) {
            // A stopped audio service holds no keys; it reports them afresh once it's back.
            this.#keys = [];
            this.broadcast({ type: 'keys', keys: [] });
        }
    }

    /** A log line for every client (from `info` up; debug lines aren't sent), kept for new ones. */
    log(level: LogLevel, message: string, at: number = Date.now()): void {
        if (level === 'debug') {
            return;
        }
        const line: LogLine = { type: 'log', level, message, at };
        this.#logLines.push(line);
        this.#logLines.splice(0, Math.max(0, this.#logLines.length - logLinesKept));
        this.broadcast(line);
    }

    /** Every setting's value, read from the audio service (e.g. when it's ready). */
    setSettings(values: SettingValues): void {
        this.#settings = values;
        this.broadcast({ type: 'settings', values });
    }

    /** An event from the audio service: passed on to every client, and remembered. */
    fromService(event: ServiceEvent): void {
        switch (event.event) {
            case 'keys':
                this.#keys = event.params.keys;
                this.broadcast({ type: 'keys', keys: event.params.keys });
                return;
            case 'level':
                this.broadcast({ type: 'level', peak: event.params.peak });
                return;
            case 'log':
                this.log(event.params.level, event.params.message);
                return;
            case 'settings':
                this.setSettings(event.params.values);
                return;
            case 'ready':
                return;
        }
    }
}

/** Sends one event to one client. */
export function send(client: Client, event: Event): void {
    client.send(JSON.stringify(event));
}
