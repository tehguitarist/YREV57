# 0007 · Era-7 time variance: a slow, outside-the-loop gain variation

- Status: Accepted (2026-11-25)
- Date: 2026-11-25

## Context
Repeated measurements of the same REV7 reverb program do not repeat exactly: from about 50 ms on, two runs decorrelate to roughly half, evenly across the band and without growing with time, while the early taps stay identical and the late energy and spectrum stay the same. So the variation is slow (seconds), sits outside the recirculating loops (it does not grow per trip), carries about half the late energy, and is not delay modulation. The REV5 repeats exactly, so this concerns era 7 only.

Our `space_mod` has the wrong shape for it: it moves the early taps, which the REV7 holds, and its effect grows with time and frequency. It stays at 0, the factory value, in both eras.

## Decision
Give era 7's reverb programs a slow time variation of our own design (inference throughout; ADR 0003 applies, nothing is copied from firmware):
- **Where:** on the late field's output read, outside the recirculating loops. The taps (first 50 ms) and the loops stay static.
- **What:** the late output is the sum of a fixed part and a part whose read gains drift per channel and loop (slow random walks of a few seconds), about half the late energy, band-flat above 120 Hz so the late bass and the spectrum do not move between runs.
- **Control:** an era value set by the converter, not a preset parameter; no parameter ID is added and `space_mod` is unchanged.
- **Determinism:** seeded from the noise seed, so with *Noise seed* Frozen renders repeat sample for sample; per instance otherwise, like hiss and dither.
- ***CLA Drum Room*** (era 7) gets the same mechanism at the same depth, as a voicing choice.

## Consequences
- Renders of the same era-7 reverb program differ slightly between runs, as the hardware does; exact comparisons in the tests use the frozen seed.
- Impulse responses taken from the plug-in do not repeat exactly; measurement tools must average or fix the seed.
- Open: the real mechanism is unknown. Our band levels vary a little more between runs than the hardware's, which suggests its variation has more independent parts than our eight loops.

## Implementation
[engine.md](../engine.md), item 5. The right read vector is kept orthogonal to the left (else L and R correlate and the sum gains level), the static read passes a shelf so the late energy is flat at every frequency, and the drift is high-passed at 120 Hz.
