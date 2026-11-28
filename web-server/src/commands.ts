// The `piano-lab` command's commands (cli.ts runs them): starts piano-lab, and talks to a running
// one through the web API.
//   piano-lab start          run piano-lab (the web server and the audio service) until stopped
//   piano-lab stop           stop a running piano-lab
//   piano-lab status         show what a running piano-lab has open
//   piano-lab render ...     render a music file to WAV (handed to the audio service's program)

import { spawn } from 'node:child_process';
import { Compile } from 'typebox/compile';
import { ErrorReply, Status, defaultPort, paths } from '@piano-lab/web-api';
import { audioServiceProgram } from './audio-service.ts';
import { runWebServer } from './main.ts';

/** The command's help text. */
export const usage = `usage: piano-lab <command> [options]

piano-lab, a physically modelled piano.

commands:
  start           run piano-lab until Ctrl+C or 'piano-lab stop': plays the piano from your MIDI
                  keyboard and serves its web API on http://localhost:${defaultPort}
  stop            stop the piano-lab that's running
  status          show what the running piano-lab has open
  render <input> [-o <output.wav>] [--sample-rate <hz>] [--settings <file>]
                  render a MIDI or MusicXML file to a WAV file, with no audio device
                  ('piano-lab render --help' for more)
  help            show this
`;

/** Where a running piano-lab answers, on `port`. */
function baseUrl(port: number): string {
    return `http://localhost:${port}`;
}

const statusChecker = Compile(Status);
const errorChecker = Compile(ErrorReply);

/** Why a request to a running piano-lab failed, for people. */
function failure(error: unknown): string {
    const cause = error instanceof Error && error.cause instanceof Error ? error.cause : error;
    const code = (cause as NodeJS.ErrnoException | undefined)?.code;
    return code === 'ECONNREFUSED'
        ? "piano-lab isn't running"
        : `couldn't reach piano-lab: ${cause instanceof Error ? cause.message : String(cause)}`;
}

/** The message of an error reply, or the HTTP status when it isn't one. */
async function errorMessage(response: Response): Promise<string> {
    const body: unknown = await response.json().catch(() => undefined);
    return errorChecker.Check(body) ? body.error.message : `HTTP ${response.status}`;
}

/** Describes a status for people, one fact per line. */
export function describeStatus(status: Status): string {
    const lines = [`piano-lab ${status.version}: the audio service is ${status.audioService}`];
    const audio = status.audio;
    if (audio === null && status.audioService === 'running') {
        lines.push("audio: the audio service didn't answer; it may be stuck");
    }
    if (audio !== null) {
        lines.push(
            audio.audioOutput === ''
                ? 'audio: no output open'
                : `audio: ${audio.audioOutput} (${audio.audioType}), ${audio.sampleRate} Hz, ${audio.bufferSize}-sample blocks`,
            `MIDI: ${audio.midiInputs.length === 0 ? 'no inputs open' : audio.midiInputs.join(', ')}`,
            `recording: ${audio.recording ? 'yes' : 'no'}`,
            `glitches: ${audio.lateBlocks} late block${audio.lateBlocks === 1 ? '' : 's'}${audio.driverXruns === undefined ? '' : `, ${audio.driverXruns} counted by the driver`}`,
        );
    }
    return lines.join('\n');
}

/** `piano-lab status`: 0 when piano-lab is running, 1 when it isn't. */
async function showStatus(port: number): Promise<number> {
    let response: Response;
    try {
        response = await fetch(`${baseUrl(port)}${paths.status}`);
    } catch (error) {
        console.error(failure(error));
        return 1;
    }
    const body: unknown = await response.json().catch(() => undefined);
    if (errorChecker.Check(body)) {
        console.error(`piano-lab couldn't say: ${body.error.message}`);
        return 1;
    }
    if (!response.ok || !statusChecker.Check(body)) {
        console.error(
            `piano-lab answered oddly (HTTP ${response.status}); is something else on port ${port}?`,
        );
        return 1;
    }
    console.log(describeStatus(body));
    return 0;
}

/** `piano-lab stop`: asks the running piano-lab to stop. 0 when it's stopping or wasn't running. */
async function stop(port: number): Promise<number> {
    let response: Response;
    try {
        response = await fetch(`${baseUrl(port)}${paths.stop}`, { method: 'POST' });
    } catch (error) {
        const message = failure(error);
        console.log(message);
        return message === "piano-lab isn't running" ? 0 : 1;
    }
    if (!response.ok) {
        console.error(`piano-lab didn't stop: ${await errorMessage(response)}`);
        return 1;
    }
    console.log('piano-lab is stopping');
    return 0;
}

/** `piano-lab render ...`: runs the audio service's own `render`, and returns its exit code. */
function render(args: readonly string[]): Promise<number> {
    const program = audioServiceProgram();
    if ('problem' in program) {
        console.error(`error: ${program.problem}`);
        return Promise.resolve(1);
    }
    return new Promise((resolve) => {
        const child = spawn(program.file, ['render', ...args], { stdio: 'inherit' });
        child.on('error', (error) => {
            console.error(`error: couldn't run ${program.file}: ${error.message}`);
            resolve(1);
        });
        child.on('close', (code) => resolve(code ?? 1));
    });
}

/**
 * Runs the command line (without the program's name) and returns the exit code. `port` is where
 * piano-lab listens (tests use another).
 */
export async function runCli(args: readonly string[], port: number = defaultPort): Promise<number> {
    const [command, ...rest] = args;
    const noArguments = (run: () => Promise<number>) => {
        if (rest.length > 0) {
            console.error(`error: '${command}' takes no arguments\n\n${usage}`);
            return Promise.resolve(1);
        }
        return run();
    };
    switch (command) {
        case 'start':
            return noArguments(() => runWebServer(port));
        case 'stop':
            return noArguments(() => stop(port));
        case 'status':
            return noArguments(() => showStatus(port));
        case 'render':
            return render(rest);
        case 'help':
        case '-h':
        case '--help':
            console.log(usage);
            return 0;
        case undefined:
            console.error(usage);
            return 1;
        default:
            console.error(`error: unknown command '${command}'\n\n${usage}`);
            return 1;
    }
}
