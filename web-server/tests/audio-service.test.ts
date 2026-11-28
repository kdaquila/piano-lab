// Running the audio service, with a stand-in (fake-audio-service.ts) in its place: requests and
// replies, events, failures, restarts and stopping.

import { join } from 'node:path';
import { afterEach, describe, expect, it } from 'vitest';
import { AudioService, AudioServiceError, type Log, type Timings } from '../src/audio-service.ts';
import type { ServiceEvent } from '../src/pipe.ts';

/** Short timings, so the tests are quick. */
const quick: Partial<Timings> = {
    replyTimeoutMs: 1_000,
    readyTimeoutMs: 2_000,
    stopTimeoutMs: 300,
    firstRestartDelayMs: 50,
    maxRestartDelayMs: 200,
};

/** A log that keeps what it's given. */
function memoryLog(): Log & { lines: string[] } {
    const lines: string[] = [];
    return {
        lines,
        info: (message) => lines.push(`info: ${message}`),
        warn: (message) => lines.push(`warn: ${message}`),
        error: (message) => lines.push(`error: ${message}`),
    };
}

/** Every service a test started, stopped after it. */
const started: AudioService[] = [];
afterEach(async () => {
    await Promise.all(started.splice(0).map((service) => service.stop()));
});

/** An `AudioService` running the stand-in in `mode`, with what it logs and every event. */
function fake(mode: string, overrides: Partial<Timings> = {}, maxQueuedBytes?: number) {
    const log = memoryLog();
    const events: ServiceEvent[] = [];
    const versions: string[] = [];
    const service = new AudioService({
        command: {
            file: process.execPath,
            args: [join(import.meta.dirname, 'fake-audio-service.ts'), mode],
        },
        log,
        onEvent: (event) => events.push(event),
        onReady: (version) => versions.push(version),
        timings: { ...quick, ...overrides },
        ...(maxQueuedBytes === undefined ? {} : { maxQueuedBytes }),
    });
    started.push(service);
    return { service, log, events, versions };
}

/** Resolves once `condition` holds, checking every 10 ms; fails the test after `ms`. */
async function until(condition: () => boolean, ms = 2_000): Promise<void> {
    const end = Date.now() + ms;
    while (!condition()) {
        if (Date.now() > end) throw new Error('timed out waiting');
        await new Promise((resolve) => setTimeout(resolve, 10));
    }
}

describe('AudioService', () => {
    it('starts, answers requests, and passes events on', async () => {
        const { service, events, versions } = fake('normal');
        await service.start();
        expect(service.state).toBe('running');
        expect(versions).toEqual(['0.0.0-test']);
        expect(events[0]).toEqual({ event: 'log', params: { level: 'info', message: 'starting' } });

        const status = await service.request('status', {});
        expect(status.audioOutput).toBe('Speakers');
        const values = await service.request('settings.set', { 'sine.gain': 0.5 });
        expect(values['sine.gain']).toBe(0.5);
        await until(() => events.some((e) => e.event === 'settings'));

        expect(service.notify('note.on', { key: 60, velocity: 1 })).toBe('sent');
        await until(() => events.some((e) => e.event === 'keys'));
        expect(events.find((e) => e.event === 'keys')).toEqual({
            event: 'keys',
            params: { keys: [60] },
        });
    });

    it('rejects a request the audio service refuses, with its message', async () => {
        const { service } = fake('normal');
        await service.start();
        const refused = service.request('settings.set', { 'sine.gain': 7 });
        await expect(refused).rejects.toBeInstanceOf(AudioServiceError);
        await expect(refused).rejects.toMatchObject({
            kind: 'rejected',
            code: -32000,
            message: 'sine.gain: 7 is out of range (0 to 1)',
        });
    });

    it('rejects a reply that doesn’t match, and logs it', async () => {
        const { service, log } = fake('bad-reply');
        await service.start();
        await expect(service.request('status', {})).rejects.toMatchObject({
            kind: 'invalid-reply',
        });
        expect(
            log.lines.some((line) => line.startsWith('error: ') && line.includes('status')),
        ).toBe(true);
    });

    it('ends and restarts an audio service that never gets ready', async () => {
        const { service, log } = fake('silent', { readyTimeoutMs: 200 });
        await expect(service.start()).rejects.toMatchObject({ kind: 'timeout' });
        await expect(service.request('status', {})).rejects.toMatchObject({ kind: 'unavailable' });
        expect(service.notify('note.off', { key: 60 })).toBe('unavailable');
        // Ended, restarted, and ended again: restarts are timed too.
        const endings = () => log.lines.filter((line) => line.includes("wasn't ready after 0.2 s"));
        await until(() => endings().length >= 2);
        expect(log.lines.some((line) => line.includes('stopped unexpectedly'))).toBe(true);
    });

    it('refuses more once too much is waiting for the audio service', async () => {
        const { service: small } = fake('deaf', {}, 4096);
        await small.start();
        let result = small.notify('note.on', { key: 60, velocity: 1 });
        for (let sent = 0; result === 'sent' && sent < 1_000_000; sent++) {
            result = small.notify('note.on', { key: 60, velocity: 1 });
        }
        expect(result).toBe('busy');
        await expect(small.request('status', {})).rejects.toMatchObject({
            kind: 'unavailable',
            message: expect.stringContaining('keeping up'),
        });
    });

    it('restarts after a crash, logging why with its stderr', async () => {
        const { service, log, versions } = fake('crash');
        await service.start();
        await until(() => versions.length >= 3);
        const crashes = log.lines.filter((line) => line.includes('stopped unexpectedly'));
        expect(crashes.length).toBeGreaterThanOrEqual(2);
        expect(crashes[0]).toContain('exit code 3');
        expect(crashes[0]).toContain('something broke\nat the very end');
        // The pause doubles with each quick stop: 0.05 s, then 0.1 s.
        expect(crashes[0]).toContain('in 0.05 s');
        expect(crashes[1]).toContain('in 0.1 s');
    });

    it('stops when asked, without restarting, and fails requests after', async () => {
        const { service, log, events } = fake('normal');
        await service.start();
        await service.stop();
        expect(service.state).toBe('stopped');
        expect(events.at(-1)).toEqual({
            event: 'log',
            params: { level: 'info', message: 'stopped' },
        });
        expect(log.lines.at(-1)).toBe('info: the audio service stopped with exit code 0');
        await expect(service.request('status', {})).rejects.toMatchObject({ kind: 'unavailable' });
    });

    it('ends an audio service that doesn’t stop in time', async () => {
        const { service, log } = fake('stubborn');
        await service.start();
        await service.stop();
        expect(log.lines).toContain("warn: the audio service didn't stop in time; ending it");
    });

    it('logs a program that can’t be run, and keeps trying', async () => {
        const log = memoryLog();
        const service = new AudioService({
            command: { file: join(import.meta.dirname, 'no-such-program.exe'), args: [] },
            log,
            onEvent: () => {},
            timings: { ...quick, readyTimeoutMs: 300 },
        });
        started.push(service);
        await expect(service.start()).rejects.toBeInstanceOf(AudioServiceError);
        expect(log.lines.some((line) => line.includes("couldn't run the audio service"))).toBe(
            true,
        );
    });
});
