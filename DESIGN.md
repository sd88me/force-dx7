# Force DX7 — design notes

## Why this exists

MSFA (the DSP core behind the open-source Dexed project) is a real,
sample-rendering FM synthesis engine, not a MIDI generator, so getting it
onto the Force means getting its audio out of a standalone process and onto
an actual Audio-In (or Out 3/4) track. The Force's main app (`/usr/bin/MPC`,
a JUCE binary) opens its ADA2 codec with raw `hw:` device names and holds
both playback and capture exclusively — there is no JACK, no `snd-aloop`,
and `MPC` calls `snd_pcm_writei`/`readi` directly rather than the mmap path.
So there is no host-level audio bus to inject synthesised audio into; the
only seam is the one MockbaMod's own addons already use: **LD_PRELOAD
symbol interposition** against libasound's stable public ABI, inside the
live `MPC` process. Force DX7 does not implement that interposition itself
— it depends on the separate `force-audio-jack` addon, which owns the tap
exclusively so several voice addons can share it.

## Architecture

```
DX7:In (Mockba) (virtual MIDI port, notes + CC)
        │
        ▼
dx7_host  ──renders──▶  dx7_plugin.cpp (v2 plugin API wrapper)  ──▶  MSFA (vendored verbatim)
        │
        ▼  (float32 stereo, shared-memory ring — IN1/IN2/OUT3/OUT4)
forceAudioJack.so  (LD_PRELOAD'd into /usr/bin/MPC)
        │  interposes snd_pcm_readi/writei — mixes the ring's audio into
        │  whatever MPC reads from capture or writes to output
        ▼
Audio-In or Out 3/4 track on the Force
```

`src/dsp/msfa/` is Google/asb2m10's MSFA engine (the DX7 synthesis core
behind Dexed), vendored **verbatim** — zero source edits, the cleanest port
of the sibling voice ports so far. `dx7_plugin.cpp` is the only new code
between it and the host: it wraps MSFA's native API in Schwung's v2 plugin
ABI (`create_instance`, `on_midi`, `set_param`/`get_param`, `render_block`),
translates the flat `module.json` parameter set into MSFA's SysEx-shaped
voice/global data, and owns patch/bank (`.syx`) loading.

`dx7_host.cpp` plays the role a plugin host plays for the DSP core:

| Role | `dx7_host.cpp` |
|---|---|
| load the DSP plugin | links `dx7_plugin.o` + MSFA objects, calls them directly |
| MIDI event delivery | RtMidi input callback → `on_midi` |
| block rendering | timer thread, fixed 128-frame blocks → `render_block` |
| parameter control | a local Unix control socket → `set_param`/`get_param` |
| audio out | float32 into a shared-memory ring, both destination slots always rendered |

`forceAudioJack.so` is the consumer: LD_PRELOAD'd into `MPC`, it mixes
(additive, not replace) whatever's in the selected ring(s) into the real
audio `MPC` reads/writes on the relevant handle. It is a general-purpose
mechanism, not specific to this module, and lives in its own repo,
[`force-audio-jack`](https://github.com/sd88me/force-audio-jack) — this repo
depends on it rather than bundling it. `dx7_host.cpp` keeps a vendored,
byte-for-byte copy of just the shared ring-layout header
(`src/forceAudioInject.h`) since it needs to agree on the wire format.

## Output routing

Output Mix exposes a single 6-way **destination** selector (`mix.dest`) —
IN1, IN2, IN1+IN2, OUT3, OUT4, OUT3+OUT4 — rather than separate on/off and
channel controls. `dx7_host` opens both the in-bus and out-bus rings at
startup and always renders into both continuously; switching `mix.dest`
just flips which ring is marked `enabled` in its header, so a destination
change never resumes playback from a stale backlog built up on the ring
that wasn't previously selected. The web GUI shows full labels (`IN1`,
`IN2`, `OUT3`, `OUT4`, `IN1,2`, `OUT3,4`); the shadow GUI's enum widget uses
compact `I1`/`I2`/`O3`/`O4`/`I1+2`/`O3+4` forms since its options list is
itself comma-delimited.

## Web GUI

`web/dx7_ui.html` lays operators out as a 2×3 grid (not tabs), each with a
4-on-4 oscillator/envelope sub-layout and its own rate/level envelope-curve
visualisation. The algorithm routing diagram's connectivity data was
transcribed from an independent hardware-analysis chart rather than
Dexed's own `AlgoDisplay.cpp` (that file is Dexed's specific copyrighted
rendering implementation, not a bare data table) — see `docs/ALGORITHMS.md`
for the full provenance note and per-algorithm confidence levels. Carrier
positions and feedback-operator location are high-confidence for all 32
algorithms; the exact branch/merge shape for algorithms with more than one
modulator feeding a single carrier (roughly 9–15, 19–22, 26–27) is a
best-effort single-source reading, flagged as the ones to spot-check first
against real hardware.

`web/server.py` is a stdlib-only Python HTTP bridge to `dx7_host`'s Unix
control socket (`SET`/`GET`/`DESCRIBE`/`NOTE`), served on port 8307. The web
panel is its own addon, independent of the engine.

## Touchscreen GUI

`addon/shadow_page.conf` is **generated** by `scripts/gen_shadow_page.py`
from `src/dsp/dx7_plugin.cpp`'s own parameter set — 8 tabs (GLOBAL,
OP1–OP6, BANKS) in an LCD-style cyan-on-slate theme, deliberately distinct
from Maze Voice's cream-on-brown plate. GLOBAL covers voice (algorithm,
feedback, output, octave, transpose, osc sync), LFO, and pitch EG; each
OPn tab covers oscillator, key scaling, and an envelope graph; BANKS is a
paged bank grid with an A–Z jump strip sized to the number of `.syx` files
present, plus the current bank's 32 patches. The GLOBAL pitch-EG graph and
each operator's envelope graph have draggable handles (x = rate, y = level,
fixed time scale) kept in sync with the stacked RATE/LEVEL knobs in both
directions — this needs a matching `force-shadow` build (list/stepper/
readout widgets, themes, hinted font, handle dragging); no page change is
needed on its own. Not present on the shadow page: the Output Mix
*destination* control, since the host wants literal enum text there and the
page's `int_values` mode sends indices for everything.

## Known limitations

**Source and the deployed binary are in sync, using the fixed-128-frame
render loop.** An earlier "gritty and distorted" audio bug (`dx7_plugin.cpp`
implicitly assuming `MOVE_FRAMES_PER_BLOCK`-sized calls that a variable-size
`render_block()` loop violated) was root-caused and fixed by rewriting
`timer_loop()` around fixed 128-frame blocks with fractional frame-debt
carried across wakes and catch-up chunks capped per wake (no unpaced burst
drain — an earlier, unpaced version of this same idea caused a *different*
bug: ring backlog stuck at a steady ~4400 frames indefinitely). This shape,
**with `RATE_CORRECTION` disabled**, was deployed and confirmed clean by ear
on real hardware (no grit, no glitching) — the first time the fix was
validated by listening rather than by backlog metric alone. Do not trust
older notes describing a source/binary mismatch; that was a transient state
during the rebuild and has since been resolved — MIDI port naming and
full-word knob labels, which live in the same source files as the render
loop, came back into sync as part of the same fix.

**`RATE_CORRECTION` (~1021ppm) is correct and should not be re-tuned from a
short sample.** A 20-minute unattended live measurement against the
deployed (pre-fixed-128) build found -0.2ppm residual drift on top of the
already-applied correction — essentially zero. Earlier same-day attempts to
"fix" this from 15–90s spot checks were themselves the source of a false
"drift" impression, caused by mistaking a genuine ~15–20 minute startup
backlog decay transient (measured 4028 → 126 frames, monotonic, self-
healing, not clock drift) for ongoing drift. Note this transient is
specific to this project — force-jv880, measured in the same session with
the same harness, does not show it, staying near a steady ~150 frames from
the start; the cause of that difference hasn't been investigated.
Separately, `RATE_CORRECTION` combined with the fixed-128-block shape (as
opposed to the older variable-length loop it was originally tuned against)
was found to cause unbounded backlog growth (~1160ppm) in one trial, which
is *why* it currently ships disabled alongside the fixed-128-block loop —
don't re-enable both together without re-validating.

**Still open:**
- No proper long-term (multi-minute) soak test of the current, ear-
  confirmed fixed-128-block build's backlog behaviour — only briefly
  observed live before the ear-confirmation. The audio-quality question
  (the original bug) is resolved; the backlog-steady-state question is not
  fully closed.
- The `.xtk` Q-Link template (`addon/Force DX7 Control.xtk`, 16 knobs) has
  not been visually confirmed on a real touchscreen/Q-Link screen.
- `nodeserver-integration/forcedx7.js` (and the equivalents in sibling
  Force projects) redirects to a hard-coded IP rather than deriving the URL
  from `req.headers.host`, so the home-page quick-link breaks whenever the
  Force's DHCP address changes. Untested fix on device, left as a known
  issue.
- The algorithm routing diagram's branch/merge shape for algorithms with
  multiple modulators feeding one carrier (9–15, 19–22, 26–27) is read from
  a single reference chart, not independently re-verified against real
  hardware — see `docs/ALGORITHMS.md`.
- Real audible patch-quality review with actual loaded `.syx` banks, beyond
  spot-checking, hasn't been done in depth.

## Toolchain

`dx7_host` (needs RtMidi + ALSA headers): native armhf-under-QEMU Docker
build (`scripts/Dockerfile`, `scripts/build.sh`), Debian bookworm base.
`forceAudioJack.so` builds from its own separate
[`force-audio-jack`](https://github.com/sd88me/force-audio-jack) repo.

## Related projects

- [`force-audio-jack`](https://github.com/sd88me/force-audio-jack) — the
  shared audio-injection tap this module depends on; see its own README/
  DESIGN for the injection mechanism's own design detail.
- [MSFA](https://github.com/asb2m10/dexed) — the vendored DX7 synthesis
  engine, from the Dexed project by Google and asb2m10.
