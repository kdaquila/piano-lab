---
paths:
  - "web-ui/**"
---

# The web UI (React, MUI, Zustand)

- **Touch first.** Big hit targets (44 px at least), nothing that needs hover, Pointer Events for anything played, so every finger is tracked on its own. Check it works with a mouse too.
- **Style with MUI's `sx` prop.** MUI 9 has no layout props on `Stack` and `Box` (`alignItems="center"` is a type error); put them in `sx`. No CSS files, no other styling system.
- **State lives in the Zustand store (`src/store.ts`), flat and normalized:** collections keyed by id (setting name, MIDI note number, log line number), no nested trees. Components pick out only what they show with `usePiano(selector)`, and send through `useSender()`. Tests give `PianoProvider` their own store and a stand-in sender.
- **Everything from piano-lab is checked against the `web-api` schemas** at the boundary (`src/live.ts`), with TypeBox's `Value`, never `Compile`: the page's Content-Security-Policy forbids generated code.
- **Controls come from the settings' own descriptions** (`schema[name]`: range, unit, kind); don't hard-code a setting's range in the UI.
- **Tests:** Vitest + React Testing Library in `web-ui/tests` (jsdom, so no layout: give pointer events a target), and Playwright in `web-ui/e2e`, in Edge, against a real piano-lab (`npm run acceptance`). To look at the real UI while working, `npm start`, or `node web-ui/e2e/serve.ts` for a piano-lab on port 4441 that opens no browser and keeps its settings out of the developer's own.
- `npm run check:ui` is the web UI's part of the gate. The plan's decisions for it are in [docs/plan/milestone-0.md](../../docs/plan/milestone-0.md#07-web-ui).
