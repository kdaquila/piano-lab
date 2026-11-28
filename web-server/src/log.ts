// The web server's log: one line per message on the console, with the time and level, for the
// web server's own messages and the audio service's alike.

import type { LogLevel } from '@piano-lab/web-api';
import type { Log } from './audio-service.ts';

/** The console's text for `level`, padded so messages line up. */
const levelText: Record<LogLevel, string> = {
    debug: 'debug  ',
    info: 'info   ',
    warning: 'warning',
    error: 'error  ',
};

/** One log line: local time to the millisecond, the level, and the message. */
export function logLine(level: LogLevel, message: string, at: Date = new Date()): string {
    const two = (n: number) => String(n).padStart(2, '0');
    const time = `${two(at.getHours())}:${two(at.getMinutes())}:${two(at.getSeconds())}.${String(at.getMilliseconds()).padStart(3, '0')}`;
    return `${time} ${levelText[level]} ${message}`;
}

/** A log that writes to the console: `info` to stdout, warnings and errors to stderr. */
export function consoleLog(): Log & { write(level: LogLevel, message: string): void } {
    const write = (level: LogLevel, message: string) => {
        const line = logLine(level, message);
        if (level === 'warning' || level === 'error') {
            console.error(line);
        } else {
            console.log(line);
        }
    };
    return {
        write,
        info: (message) => write('info', message),
        warn: (message) => write('warning', message),
        error: (message) => write('error', message),
    };
}
