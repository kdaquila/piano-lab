// The service pipe from the web server's side. The lines read here are as the audio service
// writes them (captured from `piano-lab-audio run`, with device names made generic), so these
// tests also check that the two sides agree.

import { describe, expect, it } from 'vitest';
import {
    LineSplitter,
    checkResult,
    errorCode,
    notificationLine,
    readMessage,
    requestLine,
} from '../src/pipe.ts';

describe('readMessage', () => {
    it('reads events', () => {
        expect(
            readMessage('{"jsonrpc":"2.0","method":"ready","params":{"version":"0.1.0"}}'),
        ).toEqual({
            kind: 'event',
            event: { event: 'ready', params: { version: '0.1.0' } },
        });
        expect(
            readMessage(
                '{"jsonrpc":"2.0","method":"log","params":{"level":"warning","message":"no MIDI inputs found"}}',
            ),
        ).toEqual({
            kind: 'event',
            event: { event: 'log', params: { level: 'warning', message: 'no MIDI inputs found' } },
        });
        expect(readMessage('{"jsonrpc":"2.0","method":"keys","params":{"keys":[60,64]}}')).toEqual({
            kind: 'event',
            event: { event: 'keys', params: { keys: [60, 64] } },
        });
        expect(readMessage('{"jsonrpc":"2.0","method":"level","params":{"peak":0.5}}')).toEqual({
            kind: 'event',
            event: { event: 'level', params: { peak: 0.5 } },
        });
        expect(
            readMessage(
                '{"jsonrpc":"2.0","method":"settings","params":{"values":{"sine.gain":0.5,"midi.inputs":[]}}}',
            ),
        ).toEqual({
            kind: 'event',
            event: {
                event: 'settings',
                params: { values: { 'sine.gain': 0.5, 'midi.inputs': [] } },
            },
        });
    });

    it('reads replies, leaving the result to be checked by method', () => {
        expect(readMessage('{"jsonrpc":"2.0","id":7,"result":null}')).toEqual({
            kind: 'result',
            id: 7,
            result: null,
        });
        expect(
            readMessage(
                '{"jsonrpc":"2.0","id":5,"error":{"code":-32000,"message":"nothing is being recorded"}}',
            ),
        ).toEqual({
            kind: 'error',
            id: 5,
            error: { code: errorCode.failed, message: 'nothing is being recorded' },
        });
        expect(
            readMessage(
                '{"jsonrpc":"2.0","id":null,"error":{"code":-32700,"message":"not JSON: 1:1: expected_brace"}}',
            ),
        ).toMatchObject({ kind: 'error', id: null, error: { code: errorCode.parseError } });
    });

    it('reports anything else as invalid, without throwing', () => {
        const invalid = [
            '',
            'not json',
            '{"truncated":',
            '[]',
            'null',
            '{"jsonrpc":"1.0","method":"ready","params":{"version":"0.1.0"}}',
            '{"jsonrpc":"2.0","method":"mystery","params":{}}',
            '{"jsonrpc":"2.0","method":"keys","params":{"keys":["C4"]}}',
            '{"jsonrpc":"2.0","method":"log","params":{"level":"loud","message":"x"}}',
            '{"jsonrpc":"2.0","method":"settings","params":{"values":{"bad name!":1}}}',
            '{"jsonrpc":"2.0","id":1}',
            '{"jsonrpc":"2.0","id":"1","result":null}',
            '{"jsonrpc":"2.0","method":"__proto__","params":{}}',
        ];
        for (const line of invalid) {
            expect(readMessage(line), line).toMatchObject({ kind: 'invalid' });
        }
    });
});

describe('checkResult', () => {
    it('accepts what the audio service replies', () => {
        const status = {
            audioType: 'Windows Audio',
            audioOutput: 'Speakers',
            sampleRate: 48000,
            bufferSize: 480,
            midiInputs: [],
            recording: false,
        };
        expect(checkResult('status', status)).toEqual({ ok: true, value: status });

        const devices = {
            audio: [
                { type: 'Windows Audio', outputs: ['Speakers'] },
                { type: 'ASIO', outputs: ['Focusrite USB ASIO'] },
            ],
            midi: [],
        };
        expect(checkResult('devices.list', devices).ok).toBe(true);

        const schema = [
            {
                name: 'audio.type',
                kind: 'text',
                unit: '',
                description: 'Kind of audio device',
                defaultValue: '',
                integer: false,
                restartsDevices: true,
            },
            {
                name: 'audio.bufferSize',
                kind: 'number',
                min: 0,
                max: 8192,
                unit: 'samples',
                description: "Buffer size to ask the device for; 0 for the device's own",
                defaultValue: 0,
                integer: true,
                restartsDevices: true,
            },
            {
                name: 'midi.inputs',
                kind: 'textList',
                unit: '',
                description: 'MIDI inputs to open',
                defaultValue: [],
                integer: false,
                restartsDevices: true,
            },
        ];
        expect(checkResult('settings.schema', schema).ok).toBe(true);

        const values = JSON.parse(
            '{"audio.bufferSize":0,"audio.output":"","midi.inputs":[],"sine.silenceThreshold":1E-5}',
        ) as unknown;
        expect(checkResult('settings.get', values).ok).toBe(true);
        expect(checkResult('recording.stop', { file: 'a.wav', seconds: 1.5 }).ok).toBe(true);
    });

    it('says what doesn’t match', () => {
        const checked = checkResult('status', { audioType: 'Windows Audio' });
        expect(checked.ok).toBe(false);
        expect(checked).toMatchObject({ problem: expect.stringContaining('status') });
        expect(checkResult('recording.start', null).ok).toBe(false);
    });
});

describe('writing', () => {
    it('writes one JSON-RPC 2.0 line per message', () => {
        expect(requestLine(1, 'status', {})).toBe(
            '{"jsonrpc":"2.0","id":1,"method":"status","params":{}}\n',
        );
        expect(requestLine(2, 'settings.set', { 'sine.gain': 0.5 })).toBe(
            '{"jsonrpc":"2.0","id":2,"method":"settings.set","params":{"sine.gain":0.5}}\n',
        );
        expect(notificationLine('note.on', { key: 60, velocity: 0.8 })).toBe(
            '{"jsonrpc":"2.0","method":"note.on","params":{"key":60,"velocity":0.8}}\n',
        );
    });
});

describe('LineSplitter', () => {
    it('joins lines split across chunks and strips line endings', () => {
        const splitter = new LineSplitter();
        expect(splitter.push('{"a":')).toEqual({ lines: [], dropped: 0 });
        expect(splitter.push('1}\r\n{"b":2}\n\n{"c"')).toEqual({
            lines: ['{"a":1}', '{"b":2}'],
            dropped: 0,
        });
        expect(splitter.push(':3}\n')).toEqual({ lines: ['{"c":3}'], dropped: 0 });
    });

    it('drops overlong lines whole and carries on', () => {
        const splitter = new LineSplitter(8);
        expect(splitter.push('short\n123456789')).toEqual({ lines: ['short'], dropped: 1 });
        expect(splitter.push('still the long line')).toEqual({ lines: [], dropped: 0 });
        expect(splitter.push('\nnext\n')).toEqual({ lines: ['next'], dropped: 0 });
        expect(splitter.push('12345678\n')).toEqual({ lines: ['12345678'], dropped: 0 });
    });
});
