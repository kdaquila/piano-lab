# Hardware

The physical side of piano-lab at home: the cabinet it lives in, and later notes on the PC, audio interface and speakers.

## The cabinet concept
A piece of living-room furniture that looks like an upright piano. Inside it are a PC running piano-lab, an amplifier, an audio interface and speakers, and a shelf holds a MIDI keyboard. Nothing in it should look like a studio.

| Closed | X-ray (front panels see-through) |
|---|---|
| ![The cabinet, closed, in black with sheet music on the shelf](cabinet-closed.jpg) | ![The same view with the fabric and upper panel see-through, showing the gear inside](cabinet-xray.jpg) |

### Decisions so far
- **Shape:** an upright piano, always against a wall, with an open back. The open back lets the speakers and the electronics breathe, and gives access from behind.
- **Upper panel:** slanted back 12 degrees, one seamless panel that lifts off (hidden clips or magnets), as on a real upright.
- **Height:** 1.20 m. The upper section is sized so open sheet music on the shelf clears the lid by about 3 cm.
- **No built-in screen.** A phone or tablet running the web UI rests on the shelf on top of the fallboard, where sheet music would go. It reaches piano-lab over the local network, not `localhost`, so this cabinet needs [network mode](../plan/milestone-0.md#09-network-mode-and-pairing) (plan step 0.9).
- **Electronics:** behind the slanted upper panel, on a shelf just above the fallboard: a Mini-ITX PC (about 7.6 × 25 × 36 cm, lying flat), a Fosi amplifier and a Focusrite Scarlett audio interface. The PC is quiet enough to sit near the player's head.
- **Speakers:** Polk ES15s in the lower corners, facing forward behind acoustically transparent fabric. The knee board is fabric except a narrow wooden panel (about 20 cm) above the pedals, like a grand piano's pedal lyre. Rear-only speakers were set aside: aimed into the wall, the treble would likely be dull and uneven.
- **Keyboard:** a sturdy shelf takes a range of 88-key keyboards. Their own buttons are ignored except power, which is reached under the overhang. The fallboard hides the keyboard's back panel and sits 1.5 cm above the black keys, so the keyboard slides in and out.
- **Style:** curved cheek blocks inspired by a Boston upright, flush with the top of the shelf; straight legs; brass pedals; a black polished finish.

### Not decided yet
- **Component sizes other than the PC are placeholders.** The speakers, amplifier, audio interface and keyboard are rough guesses, not checked against spec sheets. Measure them before designing any wood around them.
- How the speakers are damped and decoupled inside the cabinet.
- One-button power (turning everything on and off in order) is left for a later version.

## The 3D model
[cabinet-model.html](cabinet-model.html) is an interactive three.js model of the concept. Drag to orbit, scroll to zoom, right-drag to pan. Its panel switches:
- **X-ray:** makes the fabric and front panels see-through, to show the gear inside.
- **Show wall:** shows or hides the wall behind the piano.
- **Finish:** walnut or polished black.
- **On the shelf:** nothing, a phone, a tablet or sheet music. The phone and tablet show a mock-up of a piano plugin screen.

To open it, open the file in a browser. If your browser blocks it when opened as a local file, serve the `docs/hardware` folder with any static file server and open the page from there.

It loads **three.js 0.170.0** from the jsDelivr CDN, pinned to that exact version, so it needs an internet connection. It's a design sketch, not part of piano-lab: nothing builds or tests it.
