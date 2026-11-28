// The web UI end to end, in Edge, against a real piano-lab (serve.ts): chords by multi-touch,
// notes by mouse, and a control that changes the sound. What piano-lab itself heard is checked
// on a WebSocket of the test's own, so a key lighting up on screen isn't taken as proof.

import { expect, test, type Page } from '@playwright/test';
import { endToEndPort } from './serve-port.ts';

const api = `http://localhost:${endToEndPort}/api/v0`;

/** Listens to piano-lab's live events, collecting the keys it reports held. */
async function listen(): Promise<{ held: number[][]; close: () => void }> {
    const socket = new WebSocket(`ws://localhost:${endToEndPort}/api/v0/live`);
    const held: number[][] = [];
    socket.addEventListener('message', (message) => {
        const event = JSON.parse(String(message.data)) as { type: string; keys?: number[] };
        if (event.type === 'keys' && event.keys !== undefined) held.push(event.keys);
    });
    await new Promise((resolve) => socket.addEventListener('open', resolve, { once: true }));
    return { held, close: () => socket.close() };
}

/** The centre of key `note` on the page, a little above its bottom edge. */
async function keyPoint(page: Page, note: number): Promise<{ x: number; y: number }> {
    const box = await page.locator(`[data-key="${note}"]`).boundingBox();
    if (box === null) throw new Error(`key ${note} isn't on screen`);
    return { x: box.x + box.width / 2, y: box.y + box.height * 0.85 };
}

/** Opens the web UI and waits until the piano is ready. */
async function open(page: Page): Promise<string[]> {
    const problems: string[] = [];
    page.on('console', (message) => {
        if (message.type() === 'error') problems.push(message.text());
    });
    page.on('pageerror', (error) => problems.push(error.message));
    await page.goto('/');
    await expect(page.getByRole('status')).toHaveText('Ready', { timeout: 30_000 });
    return problems;
}

test.beforeEach(async ({ request }) => {
    // piano-lab answers as soon as it listens; the audio service is ready a moment later.
    await expect
        .poll(
            async () =>
                ((await (await request.get(`${api}/status`)).json()) as { audioService: string })
                    .audioService,
            { timeout: 30_000 },
        )
        .toBe('running');
    // Quiet, so the tests don't startle anyone.
    const reply = await request.post(`${api}/settings`, { data: { 'sine.gain': 0.03 } });
    expect(reply.ok()).toBe(true);
});

test('a chord played by touch, every finger at once, reaches the piano', async ({ page }) => {
    const problems = await open(page);
    const piano = await listen();
    const points = await Promise.all([60, 64, 67].map((note) => keyPoint(page, note)));

    // Three fingers down together, as a touch screen reports them.
    const cdp = await page.context().newCDPSession(page);
    await cdp.send('Input.dispatchTouchEvent', {
        type: 'touchStart',
        touchPoints: points.map((point, id) => ({ x: point.x, y: point.y, id })),
    });
    await expect.poll(() => piano.held.at(-1)).toEqual([60, 64, 67]);
    for (const note of [60, 64, 67]) {
        await expect(page.locator(`[data-key="${note}"]`)).toHaveAttribute('data-lit', 'true');
    }

    await cdp.send('Input.dispatchTouchEvent', { type: 'touchEnd', touchPoints: [] });
    await expect.poll(() => piano.held.at(-1)).toEqual([]);
    await expect(page.locator('[data-key="64"]')).toHaveAttribute('data-lit', 'false');
    piano.close();
    expect(problems).toEqual([]);
});

test('a note played with the mouse, sliding to the next key', async ({ page }) => {
    const problems = await open(page);
    const piano = await listen();
    const a4 = await keyPoint(page, 69);
    const b4 = await keyPoint(page, 71);

    await page.mouse.move(a4.x, a4.y);
    await page.mouse.down();
    await expect.poll(() => piano.held.at(-1)).toEqual([69]);
    await page.mouse.move(b4.x, b4.y, { steps: 5 });
    await expect.poll(() => piano.held.at(-1)).toEqual([71]);
    await page.mouse.up();
    await expect.poll(() => piano.held.at(-1)).toEqual([]);
    piano.close();
    expect(problems).toEqual([]);
});

test('the volume control changes the piano’s volume', async ({ page, request }) => {
    const problems = await open(page);
    const volume = page.getByRole('slider', { name: 'Volume' });
    await expect(volume).toHaveAttribute('aria-valuetext', '3%');
    await volume.focus();
    await page.keyboard.press('PageUp');
    await expect(volume).toHaveAttribute('aria-valuetext', '8%');
    await expect
        .poll(
            async () =>
                ((await (await request.get(`${api}/settings`)).json()) as Record<string, number>)[
                    'sine.gain'
                ],
        )
        .toBeCloseTo(0.08, 3);
    expect(problems).toEqual([]);
});

test('the overview strip moves the keyboard', async ({ page }) => {
    await open(page);
    await expect(page.getByText('C3 – C5')).toBeVisible();
    const strip = page.getByRole('slider', { name: 'Keyboard position' });
    const box = await strip.boundingBox();
    if (box === null) throw new Error('no overview strip');
    // A tap near the right end shows the top of the keyboard.
    await page.touchscreen.tap(box.x + box.width * 0.97, box.y + box.height / 2);
    await expect(page.locator('[data-key="108"]')).toBeInViewport();
});
