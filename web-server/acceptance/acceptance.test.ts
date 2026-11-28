// Step 0.6's acceptance checks, against the real audio service (`npm run acceptance`). They need
// a sound device and play quiet tones out loud, so they aren't part of `npm run check`.
//
// The audio service is copied to a temporary folder and run from there, so its settings, logs
// and recordings (written next to the program) stay out of the developer's own.

import {
    copyFileSync,
    mkdtempSync,
    readFileSync,
    rmSync,
    writeFileSync,
    existsSync,
} from 'node:fs';
import { networkInterfaces, tmpdir } from 'node:os';
import { join } from 'node:path';
import { connect as netConnect, type AddressInfo } from 'node:net';
import type { FastifyInstance } from 'fastify';
import { afterAll, beforeAll, describe, expect, it } from 'vitest';
import { WebSocket } from 'ws';
import type { AudioStatus, Event, SettingValues, Status } from '@piano-lab/web-api';
import { AudioService, audioServiceProgram } from '../src/audio-service.ts';
import { LiveHub } from '../src/live.ts';
import { buildServer } from '../src/server.ts';

let folder = '';
let app: FastifyInstance;
let audio: AudioService;
let port = 0;
const logLines: string[] = [];

beforeAll(async () => {
    const program = audioServiceProgram();
    if ('problem' in program) throw new Error(program.problem);
    folder = mkdtempSync(join(tmpdir(), 'piano-lab-acceptance-'));
    const copy = join(folder, program.file.split(/[\\/]/).at(-1) ?? 'piano-lab-audio');
    copyFileSync(program.file, copy);

    const log = {
        info: (m: string) => logLines.push(`info: ${m}`),
        warn: (m: string) => logLines.push(`warn: ${m}`),
        error: (m: string) => logLines.push(`error: ${m}`),
    };
    const hub = new LiveHub();
    audio = new AudioService({
        command: { file: copy, args: ['run'] },
        log,
        onEvent: (event) => hub.fromService(event),
        onStateChange: (state) => hub.setState(state),
        onReady: () =>
            void audio.request('settings.get', {}).then((values) => hub.setSettings(values)),
    });
    app = await buildServer({ audio, hub, log, version: 'acceptance', onStop: () => {} });
    await app.listen({ host: 'localhost', port: 0 });
    port = (app.server.address() as AddressInfo).port;
    await audio.start();
}, 60_000);

afterAll(async () => {
    await audio?.stop();
    await app?.close();
    if (folder !== '') rmSync(folder, { recursive: true, force: true });
});

/** Sends a JSON request to the web API and returns the parsed reply. */
async function api<T>(method: 'GET' | 'POST', path: string, body?: unknown): Promise<T> {
    const response = await fetch(`http://localhost:${port}${path}`, {
        method,
        ...(body === undefined
            ? {}
            : { headers: { 'content-type': 'application/json' }, body: JSON.stringify(body) }),
    });
    const reply = (await response.json()) as T;
    if (!response.ok) throw new Error(`${method} ${path}: ${JSON.stringify(reply)}`);
    return reply;
}

/** Opens a WebSocket client, collecting every event. */
async function client(): Promise<{ socket: WebSocket; events: Event[] }> {
    const socket = new WebSocket(`ws://localhost:${port}/api/v0/live`);
    const events: Event[] = [];
    socket.on('message', (data) => events.push(JSON.parse(String(data)) as Event));
    await new Promise<void>((resolve, reject) => {
        socket.once('open', () => resolve());
        socket.once('error', reject);
    });
    return { socket, events };
}

/** Waits `ms` milliseconds. */
const pause = (ms: number) => new Promise((resolve) => setTimeout(resolve, ms));

/** Resolves once `condition` holds, checking every 20 ms; fails after `ms`. */
async function until(condition: () => boolean, ms = 5_000): Promise<void> {
    const end = Date.now() + ms;
    while (!condition()) {
        if (Date.now() > end) throw new Error('timed out waiting');
        await pause(20);
    }
}

/** The loudest level reported while A4 is held for `ms`, at the current settings. */
async function levelOfA4(live: { socket: WebSocket; events: Event[] }, ms = 400): Promise<number> {
    const from = live.events.length;
    live.socket.send(JSON.stringify({ type: 'note.on', key: 69, velocity: 1 }));
    await pause(ms);
    live.socket.send(JSON.stringify({ type: 'note.off', key: 69 }));
    const peaks = live.events
        .slice(from)
        .flatMap((event) => (event.type === 'level' ? [event.peak] : []));
    // Let the note fade before the next one.
    await pause(1_500);
    return Math.max(0, ...peaks);
}

describe('step 0.6 acceptance', () => {
    it('a scripted client changes the volume and sees the level change', async () => {
        const live = await client();
        live.socket.send(JSON.stringify({ type: 'settings.set', values: { 'sine.gain': 0.05 } }));
        await until(() =>
            live.events.some((e) => e.type === 'settings' && e.values['sine.gain'] === 0.05),
        );
        const quiet = await levelOfA4(live);

        live.socket.send(JSON.stringify({ type: 'settings.set', values: { 'sine.gain': 0.2 } }));
        await until(() =>
            live.events.some((e) => e.type === 'settings' && e.values['sine.gain'] === 0.2),
        );
        const louder = await levelOfA4(live);

        expect(quiet).toBeGreaterThan(0.01);
        // Four times the gain: about four times the peak.
        expect(louder / quiet).toBeGreaterThan(3);
        expect(louder / quiet).toBeLessThan(5);
        live.socket.close();
    }, 30_000);

    it('a flood of control messages causes no audio glitches', async () => {
        const before = (await api<Status>('GET', '/api/v0/status')).audio as AudioStatus;
        const live = await client();
        await api('POST', '/api/v0/settings', { 'sine.gain': 0.02 });
        const started = Date.now();
        let sent = 0;
        for (let round = 0; round < 50; round++) {
            for (let key = 21; key <= 108; key++) {
                live.socket.send(JSON.stringify({ type: 'note.on', key, velocity: 0.5 }));
                live.socket.send(JSON.stringify({ type: 'note.off', key }));
                sent += 2;
            }
            const a4Hz = round % 2 === 0 ? 441 : 440;
            live.socket.send(
                JSON.stringify({ type: 'settings.set', values: { 'tuning.a4Hz': a4Hz } }),
            );
            sent += 1;
            await pause(10);
        }
        await pause(1_000);
        const after = (await api<Status>('GET', '/api/v0/status')).audio as AudioStatus;
        live.socket.close();
        console.log(`flood: ${sent} messages in ${Date.now() - started} ms`);

        expect(after.lateBlocks - before.lateBlocks).toBe(0);
        // The driver's own count, where it keeps one (0 when it doesn't).
        expect((after.driverXruns ?? 0) - (before.driverXruns ?? 0)).toBe(0);
        expect(logLines.filter((line) => line.startsWith('error'))).toEqual([]);
    }, 60_000);

    it('settings changed through the API reach settings.toml, keeping a hand-edited file as .bak', async () => {
        const file = join(folder, 'settings.toml');
        await api<SettingValues>('POST', '/api/v0/settings', { 'tuning.a4Hz': 443 });
        await until(
            () => existsSync(file) && readFileSync(file, 'utf8').includes('\na4Hz = 443\n'),
        );

        // Someone edits the file by hand while piano-lab runs...
        writeFileSync(file, `# my own note\n${readFileSync(file, 'utf8')}`);
        // ...then the next change is saved over it, and their version kept.
        await api<SettingValues>('POST', '/api/v0/settings', { 'tuning.a4Hz': 440 });
        await until(() => existsSync(`${file}.bak`));
        expect(readFileSync(`${file}.bak`, 'utf8')).toContain('# my own note');
        await until(() => !readFileSync(file, 'utf8').includes('# my own note'));
    }, 30_000);

    it('nothing listens on any address but localhost', async () => {
        const outside = Object.values(networkInterfaces())
            .flat()
            .filter((address) => address !== undefined && !address.internal)
            .map((address) => address!.address);
        for (const address of outside) {
            const reached = await new Promise<boolean>((resolve) => {
                const socket = netConnect({ host: address, port, timeout: 1_000 });
                socket.once('connect', () => {
                    socket.destroy();
                    resolve(true);
                });
                socket.once('error', () => resolve(false));
                socket.once('timeout', () => {
                    socket.destroy();
                    resolve(false);
                });
            });
            expect(reached, address).toBe(false);
        }
    }, 30_000);
});
