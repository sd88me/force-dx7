# Force DX7

A 6-operator FM synthesizer for the Akai Force running
[MockbaMod](https://github.com/MockbaTheBorg/MockbaMod), built by vendoring
[MSFA](https://github.com/asb2m10/dexed) — the DX7 synthesis engine used by
the open-source **Dexed** project — directly into a standalone Force voice.

Like Maze Voice, Force DX7 is a real **audio DSP synth**, not a MIDI
generator: it renders actual FM-synthesised samples and mixes them into what
the Force's own app reads from its audio-in capture device, so the voice
comes out on a normal Audio-In track — play it from a MIDI track routed to
Force DX7's virtual input port, and monitor/record it like any other audio
source.

## Features

- Full **6-operator FM synthesis**, all **32 classic DX7 algorithms**, plus
  feedback (0–7) and oscillator sync.
- Per-operator **envelope generators** (4-stage rate/level, not ADSR — see
  the web panel's explanatory copy) and **key scaling** (breakpoint, left/
  right depth, left/right curve).
- Global **pitch envelope**, **LFO** (speed, delay, pitch/amplitude
  modulation depth, key sync, five waveforms), octave transpose and
  fine transpose.
- **6-way output destination** — IN1, IN2, IN1+IN2 (stereo into Audio-In
  1/2), OUT3, OUT4, OUT3+OUT4 (stereo into Out 3/4) — matching
  force-audio-jack's injection points; switching destination never resumes
  from a stale backlog since both rings are always rendered.
- **Bank (`.syx` cartridge) and patch browsing** with real patch names, 32
  built-in factory patches, and hot-reload of banks dropped into `banks/`.
- **Algorithm routing diagram** in the web panel, sourced independently from
  a hardware-analysis chart rather than derived from Dexed's own rendering
  code — see `docs/ALGORITHMS.md` for per-algorithm confidence notes.
- Full web control panel, a Q-Link track template for physical-knob control,
  and an on-device touchscreen page (see below).

## Using Force DX7

Route a MIDI track to `DX7:In (Mockba)` for notes, and monitor/record from
the Force's Audio-In (or Out 3/4) track matching whichever destination is
selected in Output Mix (see Requirements below). Control it from any of:

- **The web control panel** at `http://<force-ip>:8307` — every parameter,
  a 2×3 operator grid (not tabs), 4-on-4 oscillator/envelope sub-layout with
  an envelope-curve visualisation per operator, the algorithm routing
  diagram, bank/patch browsing, and the engine start/stop button.
- **Physical Q-Link knobs** — a MIDI track routed to `DX7:In (Mockba)`
  channel 1, with `addon/Force DX7 Control.xtk` loaded onto it, gives 16
  pre-named knobs for the most commonly tweaked parameters (algorithm,
  feedback, output, octave, LFO and per-operator levels).
- **The touchscreen page** (see below).

## Requirements

- An Akai Force running [MockbaMod](https://github.com/MockbaTheBorg/MockbaMod).
- [`force-audio-jack`](https://github.com/sd88me/force-audio-jack) — a separate,
  shared MockbaMod addon that injects synthesised audio into what the
  Force's app reads from its audio-in capture device. Force DX7 depends on
  it rather than bundling its own copy, so several voice addons can share
  one tap. Enable it once; it is already bundled in the
  [`sd88me/MockbaMod`](https://github.com/sd88me/MockbaMod) fork at
  `SD/AddOns/ForceAudioJack`.
- For the touchscreen page: [force-shadow](https://github.com/sd88me/force-shadow)
  v1.0.0 or later.

## Installation

**From a release (no build needed):** download `ForceDX7-<version>.zip`
from the Releases page (under *Assets*, not the "Source code" archives) and
unzip it onto the SD card root, overwriting the old files in
`AddOns/ForceDX7` (your banks stay put). Put any `.syx` banks in
`AddOns/ForceDX7/banks/`. Then run the `manage.sh ENABLE` commands from
steps 1 and 2 below on the device.

**From a checkout:**

1. **Enable the shared audio tap** (once, even if another voice addon
   already needs it):
   ```
   ssh root@<force-ip> '/media/662522/AddOns/ForceAudioJack/manage.sh ENABLE'
   ```
2. **Deploy this addon** — one command does the scp and enables both the
   engine and the web panel:
   ```bash
   scripts/deploy.sh root@<force-ip>
   ```
   This is equivalent to, and replaces, manually running:
   ```
   ssh root@<force-ip> 'rm -rf /media/662522/AddOns/ForceDX7'
   scripts/package.sh --stage /tmp/dx7   # addon/ + build/dx7_host + web/
   scp -r /tmp/dx7/AddOns/ForceDX7 root@<force-ip>:/media/662522/AddOns/ForceDX7
   ssh root@<force-ip> '/media/662522/AddOns/ForceDX7/manage.sh ENABLE'
   ssh root@<force-ip> '/media/662522/AddOns/ForceDX7/web/manage.sh ENABLE'   # web panel
   ```
   The engine (`addon/manage.sh`) and the web panel (`addon/web/manage.sh`)
   are two **separate** addons — enabling one does not enable the other. The
   web panel has no dependency on the audio tap and can stay always-on even
   while the engine is stopped (every control just answers 503 until the
   engine's control socket exists).
3. **Start the engine** from the nodeServer Modules page (`/moduler`) — see
   the hard rule below for why this should not be enabled at boot.
4. Route a MIDI track to `DX7:In (Mockba)` for notes, and set up an Audio-In
   (or Out 3/4) track to monitor the injected audio, matching whichever
   destination is selected in the Output Mix section.

## Hard rule: start the engine manually, never at boot

`dx7_host` is started and stopped entirely from the nodeServer Modules page
(`/moduler`) — it is **never** launched at boot (`NSMODULE.json`'s Autoload
is deliberately unavailable). `addon/manage.sh ENABLE`/`DISABLE` only manage
whether the addon's files are present; they never touch `LD_PRELOAD` or
restart `acvs` themselves. That's `ForceAudioJack`'s job exclusively — it
arms the shared injection tap at boot with zero voices attached, and
`dx7_host` attaches to it on demand afterwards with no restart required.

Following the same precaution as this addon's sibling voices (Maze Voice,
JV-880): once the engine has been started via the nodeServer toggle, stop it
from the same toggle before restarting `acvs` or rebooting, rather than
restarting with a voice attached. This hasn't had the same dedicated
multi-run investigation done for Force DX7 specifically (see
`DESIGN.md`'s Known limitations for what has and hasn't been tested here),
but the underlying convention — and the reason autoload is disabled — is
identical.

## Touchscreen GUI (shadow mode)

A full editor page for the Force's own touchscreen, rendered by
[`force-shadow`](https://github.com/sd88me/force-shadow): open it with
`SHIFT+SCENE-2`, start/stop the engine from the ENGINE cell in the top bar.
An LCD-style cyan-on-slate theme, with a live bank readout (tap = BANKS tab)
and patch stepper in the top bar of every tab, reading back values from
`dx7_host` over the control socket so a patch load from anywhere updates
every knob.

| Tab | Contents |
|-----|----------|
| GLOBAL | Voice (algorithm, feedback, output, octave, transpose, osc sync), LFO (waveform, key sync, speed/delay/depth), pitch EG with its graph and rate/level knobs |
| OP1–OP6 | Oscillator, key scaling (breakpoint, depths, curves), and a **draggable rate/level envelope graph** (drag a point: x = rate, y = level) |
| BANKS | Paged bank grid with an A–Z jump strip, plus the current bank's 32 patches |

The page is **generated** by `scripts/gen_shadow_page.py` — edit the script
and rerun it rather than hand-editing `shadow_page.conf` directly:

```bash
python3 scripts/gen_shadow_page.py
```

The engine must be running (ENGINE button on the page) for names/values to
appear; with it off the page shows placeholders.

## Building from source

```bash
./scripts/build.sh   # dx7_host, native armhf-under-QEMU Docker build
```

Writes straight into `dist/ForceDX7/`, ready to deploy (or copy into
`addon/` — see `scripts/build.sh`'s packaging step). Set `CROSS_PREFIX` to
skip Docker if you already have a real armhf toolchain. `forceAudioJack.so`
(the shared audio tap) is built from its own separate
[`force-audio-jack`](https://github.com/sd88me/force-audio-jack) repo, not
from here.

## Project layout

```
src/
  dx7_host.cpp             RtMidi in, timer-driven render, writes to the audio-tap's ring
  dsp/
    dx7_plugin.cpp          v2 plugin API wrapper around MSFA
    msfa/                   Google/asb2m10's MSFA DX7 synthesis engine, vendored verbatim
  forceAudioInject.h        shared-memory ring layout, vendored from force-audio-jack (keep byte-for-byte identical)
  rtmidi/                   vendored RtMidi 6 (ALSA backend)
  module.json               parameter/UI metadata consumed by the web and shadow UIs
addon/                      the MockbaMod addon (engine): manage.sh, prebuilt
                            dx7_host, module.json, NSMODULE.json,
                            Force DX7 Control.xtk, shadow_page.conf, web/
web/                        the web control panel, a separate addon
  dx7_ui.html                control panel UI
  server.py                  stdlib-only HTTP bridge to dx7_host's control socket
  manage.sh, run_dx7_web.sh
scripts/                    Dockerfile/build.sh (armhf build), gen_shadow_page.py, deploy.sh
docs/
  ALGORITHMS.md              algorithm routing diagram provenance and per-algorithm confidence notes
nodeserver-integration/     patches for the separate nodeServer addon (home-page link)
```

## Related projects & credits

- [MockbaMod](https://github.com/MockbaTheBorg/MockbaMod) ([mockbatheb.org](http://mockbatheb.org/)) —
  the addon firmware framework this runs on.
- [MSFA](https://github.com/asb2m10/dexed) — the DX7 FM synthesis engine
  this addon vendors verbatim, from the **Dexed** project by Google and
  asb2m10. Force DX7's own port of MSFA onto the v2 plugin API is by
  charlesvestal. Licensed **GPL-3.0** — see LICENSE.

## License

GPL-3.0 — see the [LICENSE](LICENSE) file. Copyright © sd88me; MSFA/Dexed
portions Copyright © their original authors (Google, asb2m10), also
GPL-3.0.
