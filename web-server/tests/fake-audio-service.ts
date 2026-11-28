// A stand-in for `piano-lab-audio run`, for the web server's tests: speaks the service pipe like
// the real one, with no devices. Its first argument picks how it behaves:
//   normal       ready at once; answers requests; stops when its input ends
//   crash        ready, then stops with exit code 3 and a line on stderr
//   bad-reply    ready; answers `status` with something that isn't a status
//   silent       never gets ready and never answers
//   stubborn     ready; ignores the end of its input (has to be killed)

import { createInterface } from 'node:readline';

const mode = process.argv[2] ?? 'normal';
const write = (message: object) =>
    process.stdout.write(`${JSON.stringify({ jsonrpc: '2.0', ...message })}\n`);
const event = (method: string, params: object) => write({ method, params });

const status = {
    audioType: 'Windows Audio',
    audioOutput: 'Speakers',
    sampleRate: 48000,
    bufferSize: 480,
    midiInputs: [],
    recording: false,
    lateBlocks: 0,
};
let settings: Record<string, unknown> = { 'sine.gain': 0.25, 'tuning.a4Hz': 440 };

if (mode !== 'silent') {
    event('log', { level: 'info', message: 'starting' });
    event('ready', { version: '0.0.0-test' });
}
if (mode === 'crash') {
    process.stderr.write('something broke\n');
    process.exit(3);
}
if (mode === 'stubborn') {
    setInterval(() => {}, 1000);
}

const lines = createInterface({ input: process.stdin });
lines.on('line', (line) => {
    if (mode === 'silent') return;
    const request = JSON.parse(line) as {
        id?: number;
        method: string;
        params?: Record<string, unknown>;
    };
    const { id, method, params = {} } = request;
    const reply = (result: unknown) => write({ id, result });
    const fail = (code: number, message: string) =>
        write({ id: id ?? null, error: { code, message } });
    switch (method) {
        case 'status':
            return reply(mode === 'bad-reply' ? { nope: true } : status);
        case 'settings.get':
            return reply(settings);
        case 'settings.set':
            if (typeof params['sine.gain'] === 'number' && params['sine.gain'] > 1) {
                return fail(-32000, 'sine.gain: 7 is out of range (0 to 1)');
            }
            settings = { ...settings, ...params };
            reply(settings);
            return event('settings', { values: settings });
        case 'note.on':
            return event('keys', { keys: [params['key']] });
        case 'note.off':
            return event('keys', { keys: [] });
        default:
            return fail(-32601, `unknown method '${method}'`);
    }
});
lines.on('close', () => {
    if (mode === 'stubborn') return;
    event('log', { level: 'info', message: 'stopped' });
    process.exit(0);
});
