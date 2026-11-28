// piano-lab's web server, started by `piano-lab start`: listens on localhost, runs the audio
// service, connects the two, and opens the web UI in the browser. Stops on Ctrl+C or
// `POST /api/v0/stop`, stopping the audio service first so it saves its settings.

import { readFileSync } from 'node:fs';
import { join } from 'node:path';
import { defaultPort, paths } from '@piano-lab/web-api';
import { AudioService, audioServiceCommand, type Command, type Log } from './audio-service.ts';
import { LiveHub } from './live.ts';
import { consoleLog } from './log.ts';
import { buildServer } from './server.ts';
import { openBrowser, webUiFolder } from './web-ui.ts';

/** piano-lab's version: the web server package's. */
function version(): string {
    const file = join(import.meta.dirname, '..', 'package.json');
    const manifest = JSON.parse(readFileSync(file, 'utf8')) as { version: string };
    return manifest.version;
}

/** How `runWebServer` runs; the defaults are piano-lab's own. */
export interface RunOptions {
    /** The port to listen on. */
    port?: number;
    /** Whether to open the web UI in the browser once listening. */
    browser?: boolean;
    /** The audio service to run (`audioServiceCommand()`'s by default). */
    command?: Command;
}

/** Runs the web server until it's stopped. Returns the exit code. */
export async function runWebServer(options: RunOptions = {}): Promise<number> {
    const { port = defaultPort, browser = true } = options;
    const log = consoleLog();
    const command = options.command ?? audioServiceCommand();
    if ('problem' in command) {
        log.error(command.problem);
        return 1;
    }

    const hub = new LiveHub();
    // The web server's own warnings and errors (e.g. the audio service stopping unexpectedly)
    // reach the web UI's log too, as the audio service's do.
    const consoleOnly = log;
    const shared: Log = {
        info: consoleOnly.info,
        warn: (message) => {
            consoleOnly.warn(message);
            hub.log('warning', message);
        },
        error: (message) => {
            consoleOnly.error(message);
            hub.log('error', message);
        },
    };
    const webUi = webUiFolder();
    if ('problem' in webUi) {
        shared.warn(webUi.problem);
    }

    const audio: AudioService = new AudioService({
        command,
        log: shared,
        onEvent: (event) => {
            if (event.event === 'log') {
                log.write(event.params.level, event.params.message);
            }
            hub.fromService(event);
        },
        onStateChange: (state) => hub.setState(state),
        onReady: () => {
            audio.request('settings.get', {}).then(
                (values) => hub.setSettings(values),
                (error: unknown) => log.warn(`couldn't read the settings: ${String(error)}`),
            );
        },
    });

    let stopping: Promise<void> | undefined;
    const stop = () => {
        stopping ??= (async () => {
            log.info('piano-lab is stopping');
            await audio.stop();
            await app.close();
            log.info('piano-lab has stopped');
        })();
        return stopping;
    };

    const app = await buildServer({
        audio,
        hub,
        log: shared,
        version: version(),
        onStop: () => void stop(),
        webUi,
    });
    try {
        await app.listen({ host: 'localhost', port });
    } catch (error) {
        const inUse = (error as NodeJS.ErrnoException).code === 'EADDRINUSE';
        log.error(
            inUse
                ? `port ${port} is already in use: is piano-lab already running? (${paths.status})`
                : `couldn't listen on port ${port}: ${String(error)}`,
        );
        return 1;
    }
    const url = `http://localhost:${port}`;
    log.info(`piano-lab ${version()} is listening on ${url}`);
    if (browser && 'folder' in webUi) {
        openBrowser(url, (problem) => log.warn(problem));
    }

    for (const signal of ['SIGINT', 'SIGTERM', 'SIGBREAK'] as const) {
        process.on(signal, () => void stop());
    }
    // A start that fails is logged by the audio service, and retried; one cut short by stopping
    // isn't a problem at all.
    audio.start().catch(() => {});

    await new Promise<void>((resolve) => app.server.once('close', resolve));
    await stopping;
    return 0;
}
