// The web API end to end: a real web server on a free localhost port, with the stand-in audio
// service (fake-audio-service.ts) behind it. Covers the routes, the error shape, the browser
// protections and the WebSocket.

import { request as httpRequest } from 'node:http';
import { join } from 'node:path';
import { createServer, type AddressInfo } from 'node:net';
import type { FastifyInstance } from 'fastify';
import { afterAll, beforeAll, describe, expect, it, vi } from 'vitest';
import { WebSocket } from 'ws';
import type { Event } from '@piano-lab/web-api';
import { AudioService, AudioServiceError } from '../src/audio-service.ts';
import { LiveHub } from '../src/live.ts';
import { describeStatus, runCli } from '../src/commands.ts';
import { allowedHost, allowedOrigin, buildServer, type Audio } from '../src/server.ts';

const log = { info: () => {}, warn: () => {}, error: () => {} };
let app: FastifyInstance;
let audio: AudioService;
let port = 0;
let stops = 0;

beforeAll(async () => {
    const hub = new LiveHub();
    audio = new AudioService({
        command: {
            file: process.execPath,
            args: [join(import.meta.dirname, 'fake-audio-service.ts'), 'normal'],
        },
        log,
        onEvent: (event) => hub.fromService(event),
        onStateChange: (state) => hub.setState(state),
        onReady: () =>
            void audio.request('settings.get', {}).then((values) => hub.setSettings(values)),
    });
    app = await buildServer({ audio, hub, log, version: '9.9.9', onStop: () => (stops += 1) });
    await app.listen({ host: '127.0.0.1', port: 0 });
    port = (app.server.address() as AddressInfo).port;
    await audio.start();
});

afterAll(async () => {
    await audio.stop();
    await app.close();
});

/** A reply: its status and its body, parsed as JSON. */
interface Reply {
    status: number;
    body: unknown;
}

/** Sends a request to the server with exactly these headers, and reads the reply. */
function call(
    method: string,
    path: string,
    { headers = {}, body }: { headers?: Record<string, string>; body?: string } = {},
): Promise<Reply> {
    return new Promise((resolve, reject) => {
        const request = httpRequest(
            {
                host: '127.0.0.1',
                port,
                method,
                path,
                headers: {
                    host: `localhost:${port}`,
                    ...(body !== undefined ? { 'content-type': 'application/json' } : {}),
                    ...headers,
                },
            },
            (response) => {
                let text = '';
                response.setEncoding('utf8').on('data', (chunk: string) => (text += chunk));
                response.on('end', () =>
                    resolve({
                        status: response.statusCode ?? 0,
                        body: text === '' ? null : JSON.parse(text),
                    }),
                );
            },
        );
        request.on('error', reject);
        request.end(body);
    });
}

/** Opens a WebSocket to the server, collecting the events it receives. */
async function connect(
    headers: Record<string, string> = {},
): Promise<{ socket: WebSocket; events: Event[] }> {
    const socket = new WebSocket(`ws://127.0.0.1:${port}/api/v0/live`, {
        headers: { host: `localhost:${port}`, ...headers },
    });
    const events: Event[] = [];
    socket.on('message', (data) => events.push(JSON.parse(String(data)) as Event));
    await new Promise<void>((resolve, reject) => {
        socket.once('open', () => resolve());
        socket.once('error', reject);
    });
    return { socket, events };
}

/** Resolves once `condition` holds, checking every 10 ms; fails the test after `ms`. */
async function until(condition: () => boolean, ms = 2_000): Promise<void> {
    const end = Date.now() + ms;
    while (!condition()) {
        if (Date.now() > end) throw new Error('timed out waiting');
        await new Promise((resolve) => setTimeout(resolve, 10));
    }
}

describe('the browser protections', () => {
    it('take only requests addressed to this computer', () => {
        for (const host of ['localhost', 'localhost:4440', '127.0.0.1:4440', '[::1]:4440']) {
            expect(allowedHost(host), host).toBe(true);
        }
        for (const host of [
            undefined,
            '',
            'evil.example',
            'localhost.evil.example',
            '127.0.0.2',
            'user@localhost',
            '0.0.0.0',
        ]) {
            expect(allowedHost(host), String(host)).toBe(false);
        }
    });

    it('take only piano-lab’s own pages as origins, or none', () => {
        expect(allowedOrigin(undefined, 4440)).toBe(true);
        for (const origin of [
            'http://localhost:4440',
            'http://127.0.0.1:4440',
            'http://[::1]:4440',
        ]) {
            expect(allowedOrigin(origin, 4440), origin).toBe(true);
        }
        for (const origin of [
            'null',
            'http://evil.example',
            'http://localhost:3000',
            'https://localhost:4440',
            'http://localhost:4440/',
            'http://localhost.evil.example:4440',
        ]) {
            expect(allowedOrigin(origin, 4440), origin).toBe(false);
        }
    });

    it('refuse other hosts (DNS rebinding) before anything runs', async () => {
        const reply = await call('GET', '/api/v0/status', {
            headers: { host: `evil.example:${port}` },
        });
        expect(reply).toEqual({
            status: 403,
            body: {
                error: { code: 'forbidden', message: 'requests must be addressed to localhost' },
            },
        });
    });

    it('refuse POSTs from other websites, but let scripts and reads through', async () => {
        const evil = { origin: 'http://evil.example' };
        const refused = await call('POST', '/api/v0/settings', {
            headers: evil,
            body: '{"sine.gain":0.3}',
        });
        expect(refused.status).toBe(403);
        expect((await call('GET', '/api/v0/settings', { headers: evil })).status).toBe(200);
        const own = { origin: `http://localhost:${port}` };
        expect(
            (await call('POST', '/api/v0/settings', { headers: own, body: '{"sine.gain":0.3}' }))
                .status,
        ).toBe(200);
    });

    it('refuse bodies that aren’t JSON, and oversized ones', async () => {
        const form = await call('POST', '/api/v0/settings', {
            headers: { 'content-type': 'text/plain' },
            body: '{"sine.gain":0.3}',
        });
        expect(form.status).toBe(415);
        expect(form.body).toMatchObject({ error: { code: 'unsupported-media-type' } });
        const big = await call('POST', '/api/v0/settings', {
            body: JSON.stringify({ 'a.b': 'x'.repeat(70_000) }),
        });
        expect(big.status).toBe(413);
        expect(big.body).toMatchObject({ error: { code: 'too-large' } });
    });

    it('refuse WebSocket handshakes from other websites', async () => {
        await expect(connect({ origin: 'http://evil.example' })).rejects.toThrow('403');
    });
});

describe('the requests', () => {
    it('report the status', async () => {
        const reply = await call('GET', '/api/v0/status');
        expect(reply.status).toBe(200);
        expect(reply.body).toMatchObject({
            version: '9.9.9',
            audioService: 'running',
            audio: { audioOutput: 'Speakers', recording: false },
        });
    });

    it('change settings, and refuse invalid or out-of-range ones before or by the piano', async () => {
        const changed = await call('POST', '/api/v0/settings', { body: '{"tuning.a4Hz":442}' });
        expect(changed.status).toBe(200);
        expect(changed.body).toMatchObject({ 'tuning.a4Hz': 442 });

        const outOfRange = await call('POST', '/api/v0/settings', { body: '{"sine.gain":7}' });
        expect(outOfRange).toEqual({
            status: 422,
            body: { error: { code: 'refused', message: 'sine.gain: 7 is out of range (0 to 1)' } },
        });

        for (const body of ['{"bad name":1}', '{"sine.gain":true}', '[1]', '{"sine.gain":', '']) {
            const reply = await call('POST', '/api/v0/settings', { body });
            expect(reply.status, body).toBe(400);
            expect(reply.body, body).toMatchObject({ error: { code: 'invalid-request' } });
        }
    });

    it('answer unknown paths and methods with the error shape', async () => {
        expect(await call('GET', '/api/v0/nothing')).toEqual({
            status: 404,
            body: { error: { code: 'not-found', message: 'no GET /api/v0/nothing' } },
        });
        expect((await call('DELETE', '/api/v0/settings')).status).toBe(404);
    });

    it('refuse a body on requests that take none', async () => {
        for (const path of ['/api/v0/stop', '/api/v0/recording/start', '/api/v0/recording/stop']) {
            const reply = await call('POST', path, { body: '{"file":"take1.wav"}' });
            expect(reply.status, path).toBe(400);
            expect(reply.body, path).toMatchObject({ error: { code: 'invalid-request' } });
        }
        expect(stops).toBe(0);
    });

    it('stop piano-lab once the reply is sent, with no body or an empty one', async () => {
        expect((await call('POST', '/api/v0/stop')).status).toBe(202);
        await until(() => stops === 1);
        expect((await call('POST', '/api/v0/stop', { body: '{}' })).status).toBe(202);
        await until(() => stops === 2);
    });

    it('report an audio service that doesn’t answer in the status, rather than failing', async () => {
        const stuck: Audio = {
            state: 'running',
            request: () =>
                Promise.reject(new AudioServiceError('timeout', "the audio service didn't answer")),
            notify: () => 'unavailable',
        };
        const other = await buildServer({
            audio: stuck,
            hub: new LiveHub(),
            log,
            version: '9.9.9',
            onStop: () => {},
        });
        const reply = await other.inject({ method: 'GET', url: '/api/v0/status' });
        await other.close();
        expect(reply.statusCode).toBe(200);
        expect(reply.json()).toEqual({ version: '9.9.9', audioService: 'running', audio: null });
    });
});

describe('the WebSocket', () => {
    it('tells a new client the current state', async () => {
        const { socket, events } = await connect();
        await until(() => events.length >= 3);
        expect(events[0]).toEqual({ type: 'service', state: 'running' });
        expect(events[1]).toMatchObject({
            type: 'settings',
            values: { 'sine.gain': expect.any(Number) },
        });
        expect(events[2]).toEqual({ type: 'keys', keys: [] });
        socket.close();
    });

    it('plays notes, and sends changes to every client', async () => {
        const first = await connect();
        const second = await connect();
        first.socket.send(JSON.stringify({ type: 'note.on', key: 60, velocity: 0.7 }));
        await until(() => second.events.some((e) => e.type === 'keys' && e.keys.includes(60)));

        first.socket.send(JSON.stringify({ type: 'settings.set', values: { 'sine.gain': 0.4 } }));
        await until(() =>
            second.events.some((e) => e.type === 'settings' && e.values['sine.gain'] === 0.4),
        );
        // The sender hears about its own change the same way.
        await expect
            .poll(() =>
                first.events.some((e) => e.type === 'settings' && e.values['sine.gain'] === 0.4),
            )
            .toBe(true);
        first.socket.close();
        second.socket.close();
    });

    it('answers bad or refused commands with an error to that client only', async () => {
        const sender = await connect();
        const other = await connect();
        for (const text of [
            'nope',
            '{"type":"note.on","key":200,"velocity":1}',
            '{"type":"mystery"}',
        ]) {
            sender.socket.send(text);
        }
        sender.socket.send(JSON.stringify({ type: 'settings.set', values: { 'sine.gain': 7 } }));
        await until(() => sender.events.filter((e) => e.type === 'error').length === 4);
        expect(sender.events.findLast((e) => e.type === 'error')).toEqual({
            type: 'error',
            message: 'sine.gain: 7 is out of range (0 to 1)',
        });
        expect(other.events.some((e) => e.type === 'error')).toBe(false);
        sender.socket.close();
        other.socket.close();
    });
});

describe('the piano-lab command', () => {
    /** Runs `piano-lab <args>` against the test server, with what it printed. */
    async function piano(...args: string[]): Promise<{ code: number; out: string; err: string }> {
        const out = vi.spyOn(console, 'log').mockImplementation(() => {});
        const err = vi.spyOn(console, 'error').mockImplementation(() => {});
        try {
            const code = await runCli(args, port);
            const text = (spy: typeof out) =>
                spy.mock.calls.map((printed) => printed.join(' ')).join('\n');
            return { code, out: text(out), err: text(err) };
        } finally {
            out.mockRestore();
            err.mockRestore();
        }
    }

    it('shows the status', async () => {
        expect(await piano('status')).toEqual({
            code: 0,
            out: [
                'piano-lab 9.9.9: the audio service is running',
                'audio: Speakers (Windows Audio), 48000 Hz, 480-sample blocks',
                'MIDI: no inputs open',
                'recording: no',
                'glitches: 0 late blocks',
            ].join('\n'),
            err: '',
        });
        expect(describeStatus({ version: '1.0.0', audioService: 'running', audio: null })).toBe(
            "piano-lab 1.0.0: the audio service is running\naudio: the audio service didn't answer; it may be stuck",
        );
        expect(describeStatus({ version: '1.0.0', audioService: 'restarting', audio: null })).toBe(
            'piano-lab 1.0.0: the audio service is restarting',
        );
    });

    it('stops piano-lab', async () => {
        const before = stops;
        expect(await piano('stop')).toMatchObject({ code: 0, out: 'piano-lab is stopping' });
        await until(() => stops === before + 1);
    });

    it('says when piano-lab isn’t running', async () => {
        // A port that was free a moment ago: nothing answers there.
        const probe = createServer();
        await new Promise<void>((resolve) => probe.listen(0, '127.0.0.1', resolve));
        const free = (probe.address() as AddressInfo).port;
        await new Promise((resolve) => probe.close(resolve));

        const err = vi.spyOn(console, 'error').mockImplementation(() => {});
        const out = vi.spyOn(console, 'log').mockImplementation(() => {});
        try {
            expect(await runCli(['status'], free)).toBe(1);
            expect(err).toHaveBeenCalledWith("piano-lab isn't running");
            expect(await runCli(['stop'], free)).toBe(0);
            expect(out).toHaveBeenCalledWith("piano-lab isn't running");
        } finally {
            err.mockRestore();
            out.mockRestore();
        }
    });

    it('refuses unknown commands and stray arguments', async () => {
        expect(await piano('bogus')).toMatchObject({
            code: 1,
            err: expect.stringContaining("unknown command 'bogus'"),
        });
        expect(await piano('status', 'now')).toMatchObject({
            code: 1,
            err: expect.stringContaining("'status' takes no arguments"),
        });
        expect(await piano('help')).toMatchObject({
            code: 0,
            out: expect.stringContaining('usage: piano-lab'),
        });
    });
});
