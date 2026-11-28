// piano-lab's web server, started by `piano-lab start`: listens on localhost, runs the audio
// service, and connects the two. Stops on Ctrl+C or `POST /api/v0/stop`, stopping the audio
// service first so it saves its settings.

import { readFileSync } from 'node:fs';
import { join } from 'node:path';
import { defaultPort, paths } from '@piano-lab/web-api';
import { AudioService, audioServiceCommand } from './audio-service.ts';
import { LiveHub } from './live.ts';
import { consoleLog } from './log.ts';
import { buildServer } from './server.ts';

/** piano-lab's version: the web server package's. */
function version(): string {
    const file = join(import.meta.dirname, '..', 'package.json');
    const manifest = JSON.parse(readFileSync(file, 'utf8')) as { version: string };
    return manifest.version;
}

/** Runs the web server until it's stopped. Returns the exit code. */
export async function runWebServer(port: number = defaultPort): Promise<number> {
    const log = consoleLog();
    const command = audioServiceCommand();
    if ('problem' in command) {
        log.error(command.problem);
        return 1;
    }

    const hub = new LiveHub();
    const audio: AudioService = new AudioService({
        command,
        log,
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
        log,
        version: version(),
        onStop: () => void stop(),
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
    log.info(`piano-lab ${version()} is listening on http://localhost:${port}`);

    for (const signal of ['SIGINT', 'SIGTERM', 'SIGBREAK'] as const) {
        process.on(signal, () => void stop());
    }
    audio
        .start()
        .catch((error: unknown) => log.error(`the audio service didn't start: ${String(error)}`));

    await new Promise<void>((resolve) => app.server.once('close', resolve));
    await stopping;
    return 0;
}
