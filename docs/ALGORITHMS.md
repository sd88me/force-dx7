# DX7 algorithm routing data — provenance and confidence

The 32-algorithm connectivity table used by `web/dx7_ui.html`'s routing
diagram was transcribed from an independent technical chart (a hardware
reverse-engineering analysis of the actual DX7 chip, not derived from any
software implementation's source code) rather than from Dexed's own
`AlgoDisplay.cpp` — that file is Dexed's specific copyrighted implementation
(its own pixel coordinates and connection-type encoding), not a bare data
table, so it wasn't used as a source here. The underlying engineering facts
(which operator modulates which, on real 1983 DX7 hardware) aren't
copyrightable either way, but this project chose to source them
independently rather than reverse-engineer them out of one specific GPL
codebase's rendering logic.

**Confidence is not uniform across all 32.** Carrier positions (which
operators are audible outputs) and feedback-operator location are read with
high confidence for every algorithm — those are visually unambiguous in the
reference chart (shaded vs. unshaded boxes, a distinct loop icon). The
*exact* branch/merge shape for algorithms with more than one modulator
feeding a single carrier (roughly algorithms 9-15, 19-22, 26-27) is a
best-effort reading of a static image, not independently re-verified against
real hardware or a second source. If you spot a wrong connection once you
can compare the diagram against a real Force screen or a DX7/Dexed side by
side, that's real signal — please flag it and it'll get corrected, rather
than assuming the chart is authoritative.

Simple-stack algorithms (1-8, 16-18, 23-25, 28-32) are the ones to trust
most; anything with a visible branch or merge line is the ones to
spot-check first.
