// Running the audio service: the web server starts `piano-lab-audio run` as its child process,
// talks to it over the service pipe (pipe.ts), restarts it if it stops unasked, and stops it on
// exit. Only this file touches the child process.

import { spawn, type ChildProcessWithoutNullStreams } from 'node:child_process';
import { existsSync } from 'node:fs';
import type { ServiceState } from '@piano-lab/web-api';
import { join } from 'node:path';
import {
    LineSplitter,
    checkResult,
    notificationLine,
    readMessage,
    requestLine,
    type NotificationMethod,
    type NotificationParams,
    type RequestMethod,
    type RequestParams,
    type RequestResult,
    type ServiceEvent,
} from './pipe.ts';

/** How long things may take, in milliseconds. */
export interface Timings {
    /** A request waiting for its reply. */
    replyTimeoutMs: number;
    /** The audio service reporting it's ready, after each start; if it doesn't, it's restarted. */
    readyTimeoutMs: number;
    /** Stopping once its input ends (it saves its settings first), before it's ended by force. */
    stopTimeoutMs: number;
    /** The pause before the first restart; it doubles with each quick stop. */
    firstRestartDelayMs: number;
    /** The longest pause before a restart. */
    maxRestartDelayMs: number;
    /** After running this long, a stop counts as new: the pause starts over. */
    steadyRunMs: number;
}

/** The timings the web server uses. */
export const defaultTimings: Timings = {
    replyTimeoutMs: 10_000,
    readyTimeoutMs: 30_000,
    stopTimeoutMs: 5_000,
    firstRestartDelayMs: 500,
    maxRestartDelayMs: 10_000,
    steadyRunMs: 30_000,
};

/** How many of the audio service's last stderr lines are kept, to log when it stops. */
const stderrLinesKept = 20;

/**
 * The most bytes that may wait to be written to the audio service. Past that, it isn't keeping
 * up: new requests and notifications are refused rather than queued without limit.
 */
export const maxQueuedBytes = 1024 * 1024;

/** What `notify` did: sent it, or why not. */
export type NotifyResult =
    | 'sent'
    /** The audio service isn't running right now. */
    | 'unavailable'
    /** Too much is already waiting to be written to it (`maxQueuedBytes`). */
    | 'busy';

/** A program to run, with its arguments. */
export interface Command {
    file: string;
    args: readonly string[];
}

/** Where to log: the same shape as the web server's (Fastify's) logger. */
export interface Log {
    info(message: string): void;
    warn(message: string): void;
    error(message: string): void;
}

/** Why a request failed. */
export type FailureKind =
    /** The audio service read the request and answered with an error. */
    | 'rejected'
    /** It isn't running right now (starting, restarting or stopped). */
    | 'unavailable'
    /** No reply came in time. */
    | 'timeout'
    /** The reply didn't match what the method returns. */
    | 'invalid-reply';

/** A request that failed: why, and the audio service's message for `rejected` ones. */
export class AudioServiceError extends Error {
    readonly kind: FailureKind;
    /** The JSON-RPC error code, for `rejected` requests. */
    readonly code: number | undefined;

    constructor(kind: FailureKind, message: string, code?: number) {
        super(message);
        this.name = 'AudioServiceError';
        this.kind = kind;
        this.code = code;
    }
}

/** The repository's root, two folders up from this file (`web-server/src` or `web-server/dist`). */
const repoRoot = join(import.meta.dirname, '..', '..');

/**
 * The audio service's program, `piano-lab-audio`, found from the web server's own location. In
 * the repository that's the dev build; a shipped piano-lab will add its platform package here.
 * Returns why when there's none.
 */
export function audioServiceProgram(): { file: string } | { problem: string } {
    const program = process.platform === 'win32' ? 'piano-lab-audio.exe' : 'piano-lab-audio';
    const devBuild = join(repoRoot, 'build', 'dev', 'audio-service', 'app', program);
    if (existsSync(devBuild)) {
        return { file: devBuild };
    }
    return { problem: `the audio service isn't built (looked for ${devBuild}); run npm run build` };
}

/** The command that runs the audio service live: `piano-lab-audio run`, or why there's none. */
export function audioServiceCommand(): Command | { problem: string } {
    const program = audioServiceProgram();
    return 'problem' in program ? program : { file: program.file, args: ['run'] };
}

/** What the audio service is doing: the web API's `ServiceState`, or stopped for good. */
export type RunState = ServiceState | 'stopped';

/** What `AudioService` needs. */
export interface AudioServiceOptions {
    /** The program to run (see `audioServiceCommand`). */
    command: Command;
    log: Log;
    /** Called with every event but `ready`, in order. */
    onEvent: (event: ServiceEvent) => void;
    /** Called each time the audio service is ready for requests, with its version. */
    onReady?: (version: string) => void;
    /** Called whenever `state` changes. */
    onStateChange?: (state: RunState) => void;
    /** Shorter timings, for tests. */
    timings?: Partial<Timings>;
    /** A smaller `maxQueuedBytes`, for tests. */
    maxQueuedBytes?: number;
}

/** A request waiting for its reply. */
interface Pending {
    method: RequestMethod;
    resolve: (result: unknown) => void;
    reject: (error: AudioServiceError) => void;
    timer: NodeJS.Timeout;
}

/** Waiting for the next `ready`. */
interface ReadyWaiter {
    resolve: () => void;
    reject: (error: AudioServiceError) => void;
}

/** Why the audio service can't take more right now, or undefined when it can. */
type Unready = 'unavailable' | 'busy' | undefined;

/**
 * The running audio service. `start` runs it; from then on it's kept running, restarted after
 * any stop the web server didn't ask for, until `stop`.
 */
export class AudioService {
    readonly #options: AudioServiceOptions;
    readonly #timings: Timings;
    #child: ChildProcessWithoutNullStreams | undefined;
    #state: RunState = 'stopped';
    #stopping = false;
    #nextId = 1;
    readonly #pending = new Map<number, Pending>();
    #readyWaiters: ReadyWaiter[] = [];
    #stderrTail: string[] = [];
    #startedAt = 0;
    #restartDelayMs: number;
    #restartTimer: NodeJS.Timeout | undefined;
    /** Restarts the audio service if it isn't ready in time, after each start. */
    #readyTimer: NodeJS.Timeout | undefined;
    #exited: Promise<void> = Promise.resolve();

    constructor(options: AudioServiceOptions) {
        this.#options = options;
        this.#timings = { ...defaultTimings, ...options.timings };
        this.#restartDelayMs = this.#timings.firstRestartDelayMs;
    }

    /** What it's doing; requests are taken only while `running`. */
    get state(): RunState {
        return this.#state;
    }

    /** Sets `state`, telling `onStateChange` when it changed. */
    #setState(state: RunState): void {
        if (state !== this.#state) {
            this.#state = state;
            this.#options.onStateChange?.(state);
        }
    }

    /**
     * Starts the audio service. Resolves once it's ready for requests; rejects if it isn't in time
     * (`readyTimeoutMs`) or stops first. Either way it's restarted, until `stop`.
     */
    start(): Promise<void> {
        this.#stopping = false;
        this.#setState('starting');
        const ready = this.#nextReady();
        this.#spawn();
        return ready;
    }

    /** Sends a request and resolves with its reply's result, checked against `method`'s. */
    request<M extends RequestMethod>(
        method: M,
        params: RequestParams<M>,
    ): Promise<RequestResult<M>> {
        const child = this.#child;
        const unready = this.#unready();
        if (unready !== undefined || child === undefined) {
            const message =
                unready === 'busy'
                    ? "the audio service isn't keeping up right now; try again"
                    : "the audio service isn't running right now";
            return Promise.reject(new AudioServiceError('unavailable', message));
        }
        const id = this.#nextId++;
        return new Promise<RequestResult<M>>((resolve, reject) => {
            const timer = setTimeout(() => {
                this.#pending.delete(id);
                reject(
                    new AudioServiceError('timeout', `the audio service didn't answer ${method}`),
                );
            }, this.#timings.replyTimeoutMs);
            this.#pending.set(id, {
                method,
                resolve: (result) => resolve(result as RequestResult<M>),
                reject,
                timer,
            });
            child.stdin.write(requestLine(id, method, params));
        });
    }

    /**
     * Sends a notification (no reply comes). Sends nothing, and says why, when the audio service
     * isn't running or isn't keeping up.
     */
    notify<M extends NotificationMethod>(method: M, params: NotificationParams<M>): NotifyResult {
        const unready = this.#unready();
        if (unready !== undefined || this.#child === undefined) {
            return unready ?? 'unavailable';
        }
        this.#child.stdin.write(notificationLine(method, params));
        return 'sent';
    }

    /** Why nothing more can be sent to the audio service right now, if anything. */
    #unready(): Unready {
        if (this.#state !== 'running' || this.#child === undefined) {
            return 'unavailable';
        }
        const limit = this.#options.maxQueuedBytes ?? maxQueuedBytes;
        return this.#child.stdin.writableLength > limit ? 'busy' : undefined;
    }

    /**
     * Stops the audio service: ends its input, so it saves its settings and exits, and kills it
     * if it hasn't within `stopTimeoutMs`. It isn't restarted.
     */
    async stop(): Promise<void> {
        this.#stopping = true;
        clearTimeout(this.#restartTimer);
        clearTimeout(this.#readyTimer);
        const child = this.#child;
        if (child === undefined) {
            this.#setState('stopped');
            return;
        }
        this.#setState('stopping');
        this.#markUnavailable('the audio service is stopping');
        child.stdin.end();
        const timer = setTimeout(() => {
            this.#options.log.warn("the audio service didn't stop in time; ending it");
            child.kill();
        }, this.#timings.stopTimeoutMs);
        await this.#exited;
        clearTimeout(timer);
    }

    /**
     * A promise for the next `ready` event. Rejected if the audio service stops first, or isn't
     * ready in time (see `#spawn`).
     */
    #nextReady(): Promise<void> {
        return new Promise<void>((resolve, reject) => {
            this.#readyWaiters.push({ resolve, reject });
        });
    }

    /** Runs the program, and wires its output and its exit. */
    #spawn(): void {
        const { file, args } = this.#options.command;
        // Detached, in its own process group (and, on Windows, its own hidden console), so Ctrl+C
        // in the terminal reaches only the web server, which then stops it in order (`stop`).
        const child = spawn(file, args, {
            stdio: ['pipe', 'pipe', 'pipe'],
            windowsHide: true,
            detached: true,
        });
        this.#child = child;
        this.#startedAt = Date.now();
        this.#stderrTail = [];
        let resolveExited = () => {};
        this.#exited = new Promise((resolve) => (resolveExited = resolve));
        // A program that hangs while starting (e.g. in a device driver) is ended, and so restarted.
        this.#readyTimer = setTimeout(() => {
            const seconds = this.#timings.readyTimeoutMs / 1000;
            this.#options.log.error(`the audio service wasn't ready after ${seconds} s; ending it`);
            for (const waiter of this.#readyWaiters.splice(0)) {
                waiter.reject(
                    new AudioServiceError('timeout', "the audio service didn't get ready"),
                );
            }
            child.kill();
        }, this.#timings.readyTimeoutMs);

        const stdout = new LineSplitter();
        child.stdout.setEncoding('utf8').on('data', (chunk: string) => {
            const { lines, dropped } = stdout.push(chunk);
            if (dropped > 0) {
                this.#options.log.warn(`the audio service wrote ${dropped} overlong line(s)`);
            }
            for (const line of lines) {
                this.#onLine(line);
            }
        });
        const stderr = new LineSplitter();
        child.stderr.setEncoding('utf8').on('data', (chunk: string) => {
            this.#stderrTail.push(...stderr.push(chunk).lines);
            this.#stderrTail.splice(0, Math.max(0, this.#stderrTail.length - stderrLinesKept));
        });
        // Writing to a program that has just stopped fails; its exit is reported below.
        child.stdin.on('error', () => {});
        child.on('error', (error) => {
            this.#options.log.error(`couldn't run the audio service (${file}): ${error.message}`);
        });
        child.on('close', (code, signal) => {
            this.#child = undefined;
            clearTimeout(this.#readyTimer);
            // Its last stderr line may have had no line ending.
            this.#stderrTail.push(...stderr.flush());
            this.#stderrTail.splice(0, Math.max(0, this.#stderrTail.length - stderrLinesKept));
            this.#onExit(code, signal);
            resolveExited();
        });
    }

    /** Handles one line from the audio service. */
    #onLine(line: string): void {
        const message = readMessage(line);
        const { log } = this.#options;
        switch (message.kind) {
            case 'event':
                if (message.event.event === 'ready') {
                    this.#onReady(message.event.params.version);
                } else {
                    this.#options.onEvent(message.event);
                }
                return;
            case 'result': {
                const pending = this.#take(message.id);
                if (pending === undefined) {
                    log.warn(
                        `the audio service answered request ${message.id}, which isn't waiting`,
                    );
                    return;
                }
                const checked = checkResult(pending.method, message.result);
                if (checked.ok) {
                    pending.resolve(checked.value);
                } else {
                    log.error(`the audio service's reply doesn't match: ${checked.problem}`);
                    pending.reject(new AudioServiceError('invalid-reply', checked.problem));
                }
                return;
            }
            case 'error': {
                const pending = message.id === null ? undefined : this.#take(message.id);
                if (pending === undefined) {
                    log.warn(`the audio service couldn't read a request: ${message.error.message}`);
                    return;
                }
                pending.reject(
                    new AudioServiceError('rejected', message.error.message, message.error.code),
                );
                return;
            }
            case 'invalid':
                log.warn(`the audio service wrote something unexpected: ${message.problem}`);
                return;
        }
    }

    /** Takes request `id` off the waiting list. */
    #take(id: number): Pending | undefined {
        const pending = this.#pending.get(id);
        if (pending !== undefined) {
            this.#pending.delete(id);
            clearTimeout(pending.timer);
        }
        return pending;
    }

    /** The audio service is ready for requests. */
    #onReady(version: string): void {
        clearTimeout(this.#readyTimer);
        this.#setState('running');
        this.#options.log.info(`the audio service ${version} is ready`);
        for (const waiter of this.#readyWaiters.splice(0)) {
            waiter.resolve();
        }
        this.#options.onReady?.(version);
    }

    /** Fails every waiting request and `ready` waiter with `message`. */
    #markUnavailable(message: string): void {
        for (const id of this.#pending.keys()) {
            this.#take(id)?.reject(new AudioServiceError('unavailable', message));
        }
        for (const waiter of this.#readyWaiters.splice(0)) {
            waiter.reject(new AudioServiceError('unavailable', message));
        }
    }

    /** The audio service has stopped: logs why, and restarts it unless it was asked to stop. */
    #onExit(code: number | null, signal: NodeJS.Signals | null): void {
        const how = signal === null ? `with exit code ${code}` : `by signal ${signal}`;
        if (this.#stopping) {
            this.#markUnavailable('the audio service has stopped');
            this.#setState('stopped');
            this.#options.log.info(`the audio service stopped ${how}`);
            return;
        }
        this.#setState('restarting');
        this.#markUnavailable('the audio service stopped; restarting it');
        const ranMs = Date.now() - this.#startedAt;
        if (ranMs >= this.#timings.steadyRunMs) {
            this.#restartDelayMs = this.#timings.firstRestartDelayMs;
        }
        const delay = this.#restartDelayMs;
        this.#restartDelayMs = Math.min(delay * 2, this.#timings.maxRestartDelayMs);
        const tail =
            this.#stderrTail.length > 0 ? `; its last output:\n${this.#stderrTail.join('\n')}` : '';
        this.#options.log.error(
            `the audio service stopped unexpectedly ${how}; restarting it in ${delay / 1000} s${tail}`,
        );
        this.#restartTimer = setTimeout(() => {
            if (!this.#stopping) {
                this.#spawn();
            }
        }, delay);
    }
}
