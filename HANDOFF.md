# force-dx7 — v1.0

## ⚠️ UPDATE 2026-09-17 (resolved): fixed-128-block re-derived, confirmed clean by ear, rename+labels back in

Follow-up to the "glitchy, reverted" episode this same day (short version:
a variable-length+`RATE_CORRECTION` binary got deployed for an unrelated
naming rename, had never actually been listened to, and turned out to
sound glitchy -- reverted at the time). Root cause was never the rename
itself; it was deploying an unvalidated timer_loop shape.

Fix: rebuilt `timer_loop()` with the fixed-128-frame-block approach (see
the detailed comment above it in `dx7_host.cpp` for the exact design --
fractional frame-debt carried across wakes, capped chunks per wake, no
unpaced burst), **with `RATE_CORRECTION` disabled** -- the measurement
below found that constant genuinely doesn't mix with this chunking shape
(unbounded backlog growth, ~1160ppm, not measurement noise). Deployed live
and **the user listened and confirmed it sounds clean** -- no grit, no
glitching. This is the first time this session's fixed-128 rewrite was
actually validated by ear rather than by backlog metric alone.

MIDI port rename and full-word knob labels are back on the live binary as
a result (both live in the same source files as the timer_loop, so they
ride along automatically once the timer_loop itself is trustworthy).
Source and deployed binary are back in sync.

**Still open**: long-term backlog behavior under this exact build hasn't
had a proper multi-minute soak test (only briefly observed live before the
ear-confirmation). The measurement session below was run against the
now-superseded variable-length binary -- its RATE_CORRECTION-is-correct
finding still stands for that shape, but its backlog/drift numbers don't
describe what's live now. Worth a fresh long soak against this exact
build before considering the backlog question fully closed, though the
audio-quality question (the actual original bug) is resolved.

## UPDATE 2026-09-17 (later same day): RATE_CORRECTION is NOT the problem — measured, not guessed

Ran a real 20-minute unattended measurement on the live device (currently-
deployed production binary, untouched, engine idle the whole time -- no
notes/patch changes) with a liveness check every 30s and the existing 5s
backlog log pulled in full afterward, specifically to stop guessing from
15-90s spot checks (which is what produced the wrong "backlog is growing/
shrinking, RATE_CORRECTION must be miscalibrated" impression earlier today
-- see the section below). Linear-fit result on the post-transient data
(first 90s excluded): **-0.2ppm residual drift** -- essentially zero, on
top of the already-deployed ~1021ppm `RATE_CORRECTION`. That constant is
correct as deployed; do not re-tune it from a short live sample again.

What the same run DID surface, real and reproducible: backlog starts around
**~4000 frames on a fresh engine start and takes a full 15-20 minutes to
decay down** to a healthy ~100-200 frame steady state (measured: 4028 ->
126 over the 20-minute window, monotonically decreasing, never stuck,
never growing). This is a genuine startup-transient characteristic, not
clock drift -- force-jv880 does NOT show this (see its own HANDOFF.md;
same measurement session, its backlog started at ~150 and stayed there
the whole time). Worth understanding *why* dx7 differs from jv880 here if
anyone revisits the rebuild, but it's self-healing and not what "the grit
bug" was ever about -- don't conflate the two.

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

## Done for v1.0 (all resolved 2026-09-17, see git log for detail)
- **Web UI sync bug**: fixed — patch changes now reseed every knob (and
  the algorithm diagram) via `syncPatchName()` calling `seedKnobs()` once
  the swap is confirmed landed, not just the name/index.
- **Knob labels**: full words throughout (`Coarse`, `Fine`, `Vel Sens`,
  `LFO Pitch Mod`, etc.), no more acronyms.
- **Envelope section explanatory copy**: added, clarifying R1-4/L1-4 are
  rates-toward-a-target and target-levels, not ADSR stages.
- **nodeServer integration**: applied — `forcedx7.js` copied in and the
  `ENDPOINTS.js` entry added, live and working.
- **MIDI port / display name rename**: done (`DX7:In (Mockba)`, "DX7" not
  "Force DX7" on the home page).
- **Output Mix section**: added (Voice Out / Channel / Voice Vol), first
  row of controls, exposing the host-level mix.* API that already existed
  but was never surfaced in the UI.
- **The original "gritty and distorted" audio bug**: root-caused AND
  fixed AND confirmed clean by ear this time (not just by backlog metric)
  — see the fixed-128-block `timer_loop()` UPDATE section above.

## Still open
- Long-term backlog behavior under the current fixed-128-block build
  hasn't had a proper multi-minute soak test (see UPDATE section above).
- Real audible patch-quality review with actual loaded `.syx` banks beyond
  spot-checking — only lightly done so far.
- `.xtk` template screen rendering — handled outside this project's own
  session, not verified here.
