# force-dx7 — v0.9 handoff

## ⚠️ UPDATE 2026-09-17: the "just re-apply paced 128-block" plan below didn't work — read before trying again

This project had **no git history at all** (unlike its sibling ports) despite
this file's repeated "see git history" notes for the pre-revert fix -- that
code was not actually recoverable. Fixed by `git init`-ing this repo
(baseline commit captures the state described below); do this check on any
project before trusting a "see git history" note in its own docs.

A fresh, faithful re-implementation of the plan below (fixed 128-frame
`render_block()` calls, fractional "frame debt" carried across wakes,
capped at `MAX_CHUNKS_PER_WAKE` per wake instead of draining unpaced) was
built and **live-tested on the real device** (192.168.1.187) rather than
just reasoned about. Two concrete, reproducible problems came out of that,
neither predicted by the plan below:

1. **Combined with the existing `RATE_CORRECTION` constant, ring backlog
   grows without bound** instead of settling -- measured climbing from
   ~5760 to ~7040 frames over 15 real seconds (~51 frames/sec, i.e.
   ~1160ppm), suspiciously close in magnitude to `RATE_CORRECTION`'s own
   ~1021ppm. Never actually growing infinitely dangerous short-term (ring
   is 65536 frames, so overflow is ~20 min out), but clearly wrong, and
   this exact combination (fixed-block chunking + `RATE_CORRECTION`) had
   never been live-tested before -- the deployed "still sounds good" binary
   predates `RATE_CORRECTION` entirely (source/binary mismatch, see below),
   and `RATE_CORRECTION` was only ever tested against the variable-length
   loop on force-maze/force-jv880, not this fixed-block shape.
2. **Even with `RATE_CORRECTION` disabled** (diagnostic-only, to isolate
   #1), backlog spikes to ~4400-4500 frames within the first 5 seconds of
   a fresh start and then sits **completely flat** for a 65-second soak --
   no growth, but no decay either. That's the same "stuck ~4400 frames
   indefinitely" signature this file already attributed to the *original*
   aborted 128-block attempt's unpaced draining -- meaning the pacing fix
   (chunk cap + carried debt) does NOT actually reproduce the
   "settles near ~100-200 frames" behavior this plan expected, at least
   not within 65s.
   - A side-by-side control matters here: the untouched variable-length
     source (with `RATE_CORRECTION` on) was *also* live-tested for
     comparison and its backlog was visibly **decaying** within the same
     15-second window (3994 → 3574 → 3298 frames) -- so on this specific
     device, the existing variable-length shape recovers from a startup
     burst and the new fixed-block shape does not. That's a real
     difference caused by the block-size/pacing change, not device noise.

**Net effect:** the WIP fixed-128-block implementation is committed to git
(see the commit after the baseline one) for reference, but it is **not
deployed** -- the live device has been restored to the pre-existing,
user-confirmed-good binary (`dx7_host.bak-pretest` on-device, unrelated to
any source currently in this repo -- the mismatch described below is still
unresolved). Do not just redeploy the WIP build; it regresses the backlog
behavior in ways worse than what's live now.

**What actually needs to happen before trying again:**
- The original "gritty and distorted" audio symptom was never re-confirmed
  as present or absent in this session -- nobody listened. That needs a
  real ears-on-device check with the *current* deployed binary before
  spending more effort on a rebuild aimed at fixing it.
- Backlog/drift behavior on this device apparently needs longer than
  60-90s to characterize (the variable-length control was still visibly
  decaying when observation stopped) -- a proper fix here needs the same
  kind of long, carefully-logged measurement session already parked for
  force-jv880's clock-drift crackle, not another quick live trial. Doing
  both projects' clock-drift measurement in one longer session, since
  they share the same `RATE_CORRECTION` constant and host-loop pattern,
  is probably the efficient move rather than repeating this per-project.
- If a fixed-block approach is retried, the "cap chunks per wake, carry
  debt forward" design (this session's implementation) is a real, tested
  data point that it isn't sufficient by itself -- the next attempt needs
  a hypothesis for why backlog isn't decaying (e.g. does the fixed chunk
  size interact with `MAX_CHUNKS_PER_WAKE` to create a rate ceiling that's
  too close to the steady-state production rate to ever run below it and
  let the consumer catch up?) rather than repeating the same shape.

## ⚠️ CRITICAL: source/binary mismatch — read before any rebuild

The **currently deployed** `dx7_host` binary (on the live Force) is the
**fixed-128-frame-block** version that fixes the original "gritty and
distorted" audio bug (dx7_plugin.cpp's MOVE_FRAMES_PER_BLOCK=128 assumption
being violated by variable-size render_block calls). User confirmed this
build "still sounds good."

The **source file** `src/dx7_host.cpp` was then **reverted** back to the
plain variable-frame-count timer loop (because an early, unpaced version of
the 128-block fix caused a *different*, worse bug — see below), and
`RATE_CORRECTION` (clock-drift compensation, see below) was added on top of
that reverted state.

**Net result: source ≠ deployed binary.** If you rebuild from source as it
sits now, you will REGRESS the grit fix. Before the next rebuild:

1. Re-apply fixed-128-frame-block calling to `timer_loop()` (git history /
   this file's earlier state has the exact code), but **fix the pacing
   bug**: the first attempt drained catch-up frames in an unpaced tight
   loop (up to 32× 128-frame chunks back-to-back with zero sleep between
   them), which pushed the ForceAudioIn ring backlog to a stuck ~4400
   frames (~100ms latency) immediately on every fresh start, confirmed
   reproducible. Need real pacing between catch-up chunks (e.g. cap how
   many chunks drain per wake, or add a tiny sleep between them) so a
   startup burst can't inject a huge amount of ring backlog instantly.
2. Combine with `RATE_CORRECTION` (already in the file) for the clock-drift
   fix too.
3. Rebuild, deploy, and re-verify: (a) no grit on a single note at any
   volume — the original symptom, (b) ring backlog settles near a healthy
   ~100-200 frames like force-jv880/force-maze do, not stuck high, (c) no
   new burst/backlog issue on a fresh restart specifically (that's exactly
   how the pacing bug was caught last time).

## What's done and verified
- Full engine: dx7_plugin.cpp + MSFA vendored verbatim, zero source edits
  needed (only host shim changes) — cleanest port of the three so far.
- Host shim, ForceAudioIn integration (mix-slot 2), RtMidi, control socket.
- Web UI: 2×3 operator grid (not tabs), 4-on-4 oscillator/envelope
  sub-layout, original envelope-curve visualization per operator, algorithm
  routing diagram (data from an independent hardware-analysis source, NOT
  Dexed's code — see `docs/ALGORITHMS.md` for confidence levels per
  algorithm), bank (.syx cartridge) + patch browsing with real names,
  engine start/stop button.
- `.xtk` Q-Link template generated (16 knobs) — NOT yet checked on a real
  screen.
- Gritty-audio bug: root-caused and fixed (see critical note above for
  current source/binary state).
- Clock-drift wobble: `RATE_CORRECTION` fix applied (same constant proven
  in force-maze) — reduces but does not eliminate residual crackle; this is
  an accepted, known limitation shared with force-jv880 (see below).

## Outstanding work for v1.0 (not yet done)
- **Web UI sync bug**: knobs and the algorithm diagram don't refresh when
  switching patches via the dropdown — only the currently-turned knob
  updates. Need a "reseed everything from chain_params after a patch
  change" call, similar to force-jv880's `syncPresetName()` pattern but for
  *all* params, not just the name. (force-jv880 has the identical bug —
  worth fixing both from one shared understanding of the root cause.)
- **Knob labels**: currently abbreviated (`Op1 Crs`, `Op1 Fin`, `LFO PMD`
  etc.) — user wants full names instead of acronyms.
- **Envelope section needs explanatory copy**: R1-4/L1-4 (DX7's own
  envelope model) is confusing to anyone expecting standard ADSR — add a
  short explanation of what each stage actually does.
- **nodeServer integration not yet applied**: `nodeserver-integration/`
  has the redirect module + README instructions ready, but nobody has
  actually copied `forcedx7.js` into a live nodeServer install or added
  the `ENDPOINTS.js` entry yet. Also add a naming-convention update while
  doing this (see below).
- Backlog/latency (~100ms) on the currently-deployed binary — real, not
  yet fixed, folded into the critical rebuild item above.
- Real audible patch-quality review with actual loaded `.syx` banks beyond
  spot-checking — only lightly done so far.
- `.xtk` template never confirmed rendering correctly on the physical
  touchscreen (same open item as every port in this project).

## New naming-convention requests (cross-project, see also force-jv880)
- Rename this addon's virtual MIDI port from `Mockba DX7:In` to
  `DX7 In (Mockba)` (pattern: synth name first, "(Mockba)" suffix, not
  prefix) — change in `dx7_host.cpp`'s `client` default / RtMidi port name.
- On nodeServer, wherever this addon's name is displayed (NSMODULE.json's
  `NAME`, the home-page ENDPOINTS.js entry's `NAME`), drop the leading
  "Force" — should read "DX7", not "Force DX7". Scope of this rename
  request across the OTHER addons (acid/maze/maze-seq) wasn't explicitly
  confirmed — check with the user before renaming those too.
