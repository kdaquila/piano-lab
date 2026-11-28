// The web server's HTTP side: the web API's routes (paths in web-api's http.ts), the WebSocket for
// live traffic, and the browser protections in front of all of them. Fastify checks every
// request against its schema before our handlers run.

import fastifyWebsocket from '@fastify/websocket';
import { TypeBoxValidatorCompiler, type TypeBoxTypeProvider } from '@fastify/type-provider-typebox';
import Fastify, { type FastifyError, type FastifyInstance, type FastifyRequest } from 'fastify';
import { Type } from 'typebox';
import { Compile } from 'typebox/compile';
import {
    Command,
    SettingValues,
    errorStatus,
    maxMessageBytes,
    paths,
    type ErrorCode,
    type ErrorReply,
    type Status,
} from '@piano-lab/web-api';
import {
    AudioServiceError,
    type AudioService,
    type FailureKind,
    type Log,
    type NotifyResult,
} from './audio-service.ts';
import { publicState, send, type Client, type LiveHub } from './live.ts';

/** What the server needs of the audio service (`AudioService` fits). */
export type Audio = Pick<AudioService, 'state' | 'request' | 'notify'>;

/** What `buildServer` needs. */
export interface ServerOptions {
    audio: Audio;
    hub: LiveHub;
    log: Log;
    /** piano-lab's version, for `GET /api/v0/status`. */
    version: string;
    /** Called once `POST /api/v0/stop` has been answered: piano-lab should stop. */
    onStop: () => void;
}

/** The host names a request may be addressed to: this computer only (see `allowedHost`). */
const localHostNames = new Set(['localhost', '127.0.0.1', '[::1]']);

/**
 * True when a `Host` header names this computer. Anything else may be DNS rebinding: a website
 * whose name the browser was tricked into resolving to this computer.
 */
export function allowedHost(host: string | undefined): boolean {
    if (host === undefined) {
        return false;
    }
    try {
        const url = new URL(`http://${host}`);
        return localHostNames.has(url.hostname) && url.pathname === '/' && url.username === '';
    } catch {
        return false;
    }
}

/**
 * True when an `Origin` header is one of piano-lab's own pages on `port`, or there's none (a
 * script or `curl`, not a browser page). Browsers send it on every cross-site `POST` and
 * WebSocket handshake, so other websites are refused.
 */
export function allowedOrigin(origin: string | undefined, port: number): boolean {
    if (origin === undefined) {
        return true;
    }
    try {
        const url = new URL(origin);
        return (
            url.protocol === 'http:' &&
            localHostNames.has(url.hostname) &&
            url.port === String(port) &&
            url.origin === origin
        );
    } catch {
        return false;
    }
}

/** The error reply for `code`. */
function errorReply(code: ErrorCode, message: string): ErrorReply {
    return { error: { code, message } };
}

/** The web API's error code for a failed request to the audio service. */
const failureCodes: Record<FailureKind, ErrorCode> = {
    rejected: 'refused',
    unavailable: 'unavailable',
    timeout: 'timeout',
    'invalid-reply': 'internal',
};

/** The web API's error code for one of Fastify's own errors. */
function fastifyErrorCode(error: FastifyError): ErrorCode {
    if (error.validation !== undefined) {
        return 'invalid-request';
    }
    switch (error.statusCode) {
        case 400:
            return 'invalid-request';
        case 404:
            return 'not-found';
        case 413:
            return 'too-large';
        case 415:
            return 'unsupported-media-type';
        default:
            return 'internal';
    }
}

/** The port a request arrived on. */
function localPort(request: FastifyRequest): number {
    return request.socket.localPort ?? 0;
}

/** A checker for WebSocket commands. */
const commandChecker = Compile(Command);

/** The body of a request that takes nothing: none (which Fastify checks as null), or `{}`. */
const NoBody = Type.Union([Type.Null(), Type.Object({}, { additionalProperties: false })]);

/** Why a notification wasn't sent, for the client that sent it. */
const notifyProblems: Record<Exclude<NotifyResult, 'sent'>, string> = {
    unavailable: "the audio service isn't running right now",
    busy: "the audio service isn't keeping up right now; the note was dropped",
};

/** Why notifying `result` failed, or undefined when it was sent. */
function notifyProblem(result: NotifyResult): string | undefined {
    return result === 'sent' ? undefined : notifyProblems[result];
}

/** Builds the web server (not yet listening). */
export async function buildServer(options: ServerOptions): Promise<FastifyInstance> {
    const { audio, hub, log } = options;
    const app = Fastify({
        logger: false,
        bodyLimit: maxMessageBytes,
        // Every route answers the same way with or without a trailing slash, and nothing else.
        routerOptions: { ignoreTrailingSlash: true },
    }).withTypeProvider<TypeBoxTypeProvider>();
    app.setValidatorCompiler(TypeBoxValidatorCompiler);
    // Bodies are JSON or nothing: other types (forms, plain text) are refused with 415.
    app.removeContentTypeParser('text/plain');

    // The browser protections, before anything else runs. (vision.md: security first.)
    app.addHook('onRequest', async (request, reply) => {
        if (!allowedHost(request.headers.host)) {
            return reply
                .code(403)
                .send(errorReply('forbidden', 'requests must be addressed to localhost'));
        }
        const changes = request.method !== 'GET' && request.method !== 'HEAD';
        const websocket = request.headers.upgrade !== undefined;
        if ((changes || websocket) && !allowedOrigin(request.headers.origin, localPort(request))) {
            if (websocket) {
                // A refused handshake never becomes a connection: close its socket once answered.
                const socket = request.raw.socket;
                reply.raw.once('finish', () => socket.end());
            }
            return reply
                .code(403)
                .header('connection', 'close')
                .send(errorReply('forbidden', 'requests from other websites are refused'));
        }
        return undefined;
    });

    app.setErrorHandler((error: FastifyError, _request, reply) => {
        if (error instanceof AudioServiceError) {
            const code = failureCodes[error.kind];
            return reply.code(errorStatus[code]).send(errorReply(code, error.message));
        }
        const code = fastifyErrorCode(error);
        if (code === 'internal') {
            log.error(`a request failed: ${error.stack ?? error.message}`);
            return reply.code(500).send(errorReply(code, 'something went wrong inside piano-lab'));
        }
        return reply.code(errorStatus[code]).send(errorReply(code, error.message));
    });
    app.setNotFoundHandler((request, reply) =>
        reply.code(404).send(errorReply('not-found', `no ${request.method} ${request.url}`)),
    );

    // --- Requests --------------------------------------------------------------------------------

    // The status says what's wrong rather than failing: an audio service that stops or doesn't
    // answer is reported by its state, with no `audio`.
    app.get(paths.status, async (): Promise<Status> => {
        const audioStatus =
            audio.state === 'running'
                ? await audio.request('status', {}).catch((error: unknown) => {
                      if (error instanceof AudioServiceError) return null;
                      throw error;
                  })
                : null;
        return {
            version: options.version,
            audioService: publicState(audio.state),
            audio: audioStatus,
        };
    });

    app.post(paths.stop, { schema: { body: NoBody } }, async (_request, reply) => {
        reply.raw.once('finish', options.onStop);
        return reply.code(202).send({});
    });

    app.get(paths.devices, () => audio.request('devices.list', {}));
    app.get(paths.settings, () => audio.request('settings.get', {}));
    app.get(paths.settingsSchema, () => audio.request('settings.schema', {}));
    app.post(paths.settings, { schema: { body: SettingValues } }, (request) =>
        audio.request('settings.set', request.body),
    );
    app.post(paths.recordingStart, { schema: { body: NoBody } }, () =>
        audio.request('recording.start', {}),
    );
    app.post(paths.recordingStop, { schema: { body: NoBody } }, () =>
        audio.request('recording.stop', {}),
    );

    // --- Live traffic ----------------------------------------------------------------------------

    await app.register(fastifyWebsocket, { options: { maxPayload: maxMessageBytes } });
    app.get(paths.live, { websocket: true }, (socket) => {
        const client: Client = socket;
        hub.add(client);
        socket.on('close', () => hub.remove(client));
        socket.on('error', () => hub.remove(client));
        socket.on('message', (data, isBinary) => {
            const problem = isBinary ? 'messages must be JSON text' : runCommand(String(data));
            if (problem !== undefined) {
                send(client, { type: 'error', message: problem });
            }
        });

        /** Carries out one command; returns why it couldn't be, if it couldn't at once. */
        function runCommand(text: string): string | undefined {
            let value: unknown;
            try {
                value = JSON.parse(text);
            } catch {
                return 'not JSON';
            }
            if (!commandChecker.Check(value)) {
                const [first] = commandChecker.Errors(value);
                return `not a command: ${first ? `${first.instancePath} ${first.message}` : 'invalid'}`;
            }
            switch (value.type) {
                case 'note.on':
                    return notifyProblem(
                        audio.notify('note.on', { key: value.key, velocity: value.velocity }),
                    );
                case 'note.off':
                    return notifyProblem(audio.notify('note.off', { key: value.key }));
                case 'settings.set':
                    // Success reaches every client as a `settings` event, from the audio service.
                    audio.request('settings.set', value.values).catch((error: unknown) => {
                        const message = error instanceof Error ? error.message : String(error);
                        send(client, { type: 'error', message });
                    });
                    return undefined;
            }
        }
    });

    return app;
}
