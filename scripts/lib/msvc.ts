// The Visual Studio C++ environment (MSVC x64 and the Windows SDK), for CMake, Ninja, clang-tidy.
//
// A plain terminal doesn't have the compiler's environment variables, and Ninja builds need them.
// Visual Studio sets them up with vcvars64.bat, which only works inside cmd. So we run it once in a
// cmd child process, read the environment it leaves behind, and hand that to the programs we start.
// The user's terminal is never changed.

import { existsSync } from 'node:fs';
import { join } from 'node:path';
import { capture, type Env, TaskError } from './run.ts';

let cached: Env | undefined;

/** The environment of an x64 Visual Studio developer prompt: this one if it already is one. */
export async function msvcEnv(): Promise<Env> {
    cached ??= await loadMsvcEnv();
    return cached;
}

/** True for an x64-hosted, x64-targeting developer prompt. One from the Start menu is x86. */
function isX64DevPrompt(env: Env): boolean {
    return (
        env['VSCMD_VER'] !== undefined &&
        env['VSCMD_ARG_TGT_ARCH'] === 'x64' &&
        env['VSCMD_ARG_HOST_ARCH'] === 'x64'
    );
}

/** The newest Visual Studio installation with the x64 C++ tools, found with vswhere. */
async function findVisualStudio(): Promise<string> {
    const programFiles = process.env['ProgramFiles(x86)'] ?? 'C:\\Program Files (x86)';
    const vswhere = join(programFiles, 'Microsoft Visual Studio', 'Installer', 'vswhere.exe');
    if (!existsSync(vswhere)) {
        throw new TaskError(
            'Visual Studio was not found (vswhere.exe is missing). ' +
                'Install Visual Studio 2022 with the C++ workload.',
        );
    }
    const vcTools = 'Microsoft.VisualStudio.Component.VC.Tools.x86.x64';
    const found = await capture(
        vswhere,
        [
            '-latest',
            '-products',
            '*',
            '-requires',
            vcTools,
            '-property',
            'installationPath',
            '-utf8',
        ],
        { env: process.env },
    );
    const vsPath = found.stdout.trim();
    if (found.code !== 0 || vsPath === '') {
        throw new TaskError(
            'No Visual Studio installation with the C++ tools (MSVC x64) was found.',
        );
    }
    return vsPath;
}

async function loadMsvcEnv(): Promise<Env> {
    if (process.platform !== 'win32') {
        throw new TaskError('The C++ build needs Windows and Visual Studio for now.');
    }
    if (isX64DevPrompt(process.env)) {
        return process.env;
    }

    const vcvars = join(await findVisualStudio(), 'VC', 'Auxiliary', 'Build', 'vcvars64.bat');
    if (!existsSync(vcvars)) {
        throw new TaskError(`Visual Studio's vcvars64.bat is missing: ${vcvars}`);
    }

    // After vcvars64.bat, this same Node prints the environment as JSON. That keeps every
    // character intact, which cmd's own `set` only does after changing the console's code page.
    // /s: cmd removes the outer quotes and runs the rest as written.
    const printEnv = `"${process.execPath}" -p JSON.stringify(process.env)`;
    const command = `"call "${vcvars}" >nul && ${printEnv}"`;
    const cmd = process.env['ComSpec'] ?? 'cmd.exe';
    const shell = await capture(cmd, ['/d', '/s', '/c', command], {
        env: process.env,
        verbatim: true,
    });
    const failed = `Setting up the Visual Studio environment failed:\n${shell.stderr}`;
    let env: Env;
    try {
        env = JSON.parse(shell.stdout) as Env;
    } catch {
        throw new TaskError(failed + shell.stdout);
    }
    if (shell.code !== 0 || !isX64DevPrompt(env)) {
        throw new TaskError(failed);
    }
    return env;
}
