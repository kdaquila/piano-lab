// The link to piano-lab, with a stand-in WebSocket and fetch: events reach the store (checked
// first), commands go out, and a dropped link is reopened.

import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest';
import { Live, firstRetryMs } from '../src/live.ts';
import { createPianoStore } from '../src/store.ts';

/** A WebSocket the test drives. */
class FakeSocket extends EventTarget {
    readyState: number = WebSocket.CONNECTING;
    readonly sent: unknown[] = [];
    readonly url: string;

    constructor(url: string) {
        super();
        this.url = url;
    }

    send(data: string): void {
        this.sent.push(JSON.parse(data));
    }

    close(): void {
        this.drop();
    }

    open(): void {
        this.readyState = WebSocket.OPEN;
        this.dispatchEvent(new Event('open'));
    }

    receive(message: unknown): void {
        const data = typeof message === 'string' ? message : JSON.stringify(message);
        this.dispatchEvent(new MessageEvent('message', { data }));
    }

    drop(): void {
        this.readyState = WebSocket.CLOSED;
        this.dispatchEvent(new Event('close'));
    }
}

/** A fetch that answers the schema and devices requests. */
function fakeFetch(): typeof fetch {
    return vi.fn<typeof fetch>(async (url) => {
        const path = String(url);
        const body = path.endsWith('/settings/schema')
            ? []
            : {
                  audio: [{ type: 'Windows Audio', outputs: ['Speakers'] }],
                  midi: ['Digital Piano'],
              };
        return new Response(JSON.stringify(body), { status: 200 });
    });
}

let sockets: FakeSocket[] = [];
let store = createPianoStore();
let live: Live;

beforeEach(() => {
    sockets = [];
    store = createPianoStore();
    live = new Live({
        store,
        origin: 'http://localhost:4440',
        openSocket: (url) => {
            const socket = new FakeSocket(url);
            sockets.push(socket);
            return socket as unknown as WebSocket;
        },
        fetch: fakeFetch(),
    });
    live.start();
});

afterEach(() => {
    live.stop();
    vi.useRealTimers();
});

describe('the link to piano-lab', () => {
    it('opens the live WebSocket and reads the devices', async () => {
        expect(sockets[0]?.url).toBe('ws://localhost:4440/api/v0/live');
        expect(store.getState().connection).toBe('connecting');
        sockets[0]?.open();
        expect(store.getState().connection).toBe('open');
        await vi.waitFor(() => expect(store.getState().devices?.midi).toEqual(['Digital Piano']));
    });

    it('puts events in the store', () => {
        const socket = sockets[0];
        socket?.open();
        socket?.receive({ type: 'service', state: 'running' });
        socket?.receive({ type: 'settings', values: { 'sine.gain': 0.5 } });
        socket?.receive({ type: 'keys', keys: [60, 64] });
        socket?.receive({ type: 'log', level: 'warning', message: 'no MIDI inputs found', at: 5 });
        socket?.receive({ type: 'error', message: 'sine.gain: 7 is out of range' });
        const state = store.getState();
        expect(state.service).toBe('running');
        expect(state.settings).toEqual({ 'sine.gain': 0.5 });
        expect(state.heldKeys).toEqual({ 60: true, 64: true });
        expect(state.logIds.map((id) => state.logById[id]?.message)).toEqual([
            'no MIDI inputs found',
            'sine.gain: 7 is out of range',
        ]);
        expect(state.logById[state.logIds[0] ?? 0]?.at).toBe(5);
    });

    it('logs messages that don’t match the web API, and uses nothing from them', () => {
        const socket = sockets[0];
        socket?.open();
        socket?.receive('not json');
        socket?.receive({ type: 'keys', keys: 'all of them' });
        const state = store.getState();
        expect(state.heldKeys).toEqual({});
        expect(state.logIds).toHaveLength(2);
        expect(state.logById[state.logIds[1] ?? 0]?.message).toContain('/keys');
    });

    it('sends commands while open, and nothing while not', () => {
        const socket = sockets[0];
        expect(live.send({ type: 'note.on', key: 60, velocity: 1 })).toBe(false);
        socket?.open();
        expect(live.send({ type: 'note.on', key: 60, velocity: 1 })).toBe(true);
        expect(live.setSettings({ 'sine.gain': 0.3 })).toBe(true);
        expect(socket?.sent).toEqual([
            { type: 'note.on', key: 60, velocity: 1 },
            { type: 'settings.set', values: { 'sine.gain': 0.3 } },
        ]);
    });

    it('reopens a dropped link, waiting longer each time it fails', () => {
        vi.useFakeTimers();
        sockets[0]?.open();
        sockets[0]?.receive({ type: 'keys', keys: [60] });
        sockets[0]?.drop();
        expect(store.getState().connection).toBe('connecting');
        expect(store.getState().heldKeys).toEqual({});

        vi.advanceTimersByTime(firstRetryMs);
        expect(sockets).toHaveLength(2);
        sockets[1]?.drop();
        vi.advanceTimersByTime(firstRetryMs);
        expect(sockets).toHaveLength(2);
        vi.advanceTimersByTime(firstRetryMs);
        expect(sockets).toHaveLength(3);

        sockets[2]?.open();
        expect(store.getState().connection).toBe('open');
    });
});
