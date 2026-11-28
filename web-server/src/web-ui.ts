// Finding the web UI's files, and opening it in the user's browser.

import { spawn } from 'node:child_process';
import { existsSync } from 'node:fs';
import { join } from 'node:path';

/** The repository's root, two folders up from this file (`web-server/src` or `web-server/dist`). */
const repoRoot = join(import.meta.dirname, '..', '..');

/**
 * The built web UI's folder, found from the web server's own location: in the repository,
 * `web-ui/dist`. Returns why when there's none.
 */
export function webUiFolder(): { folder: string } | { problem: string } {
    const folder = join(repoRoot, 'web-ui', 'dist');
    if (existsSync(join(folder, 'index.html'))) {
        return { folder };
    }
    return { problem: `the web UI isn't built (looked in ${folder}); run npm run build` };
}

/** The program that opens a web address in the default browser, on this system. */
function opener(url: string): { file: string; args: string[] } {
    switch (process.platform) {
        case 'win32':
            // Explorer hands a web address to the default browser, like a double-clicked link.
            return { file: 'explorer.exe', args: [url] };
        case 'darwin':
            return { file: 'open', args: [url] };
        default:
            return { file: 'xdg-open', args: [url] };
    }
}

/** Opens `url` in the default browser. Calls `onProblem` if it can't. */
export function openBrowser(url: string, onProblem: (message: string) => void): void {
    const { file, args } = opener(url);
    const child = spawn(file, args, { detached: true, stdio: 'ignore', windowsHide: true });
    child.on('error', (error) =>
        onProblem(`couldn't open the browser (${error.message}); open ${url} yourself`),
    );
    child.unref();
}
