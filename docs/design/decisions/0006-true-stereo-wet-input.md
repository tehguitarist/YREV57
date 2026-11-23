# 0006 · True-stereo wet input: side-weighted reflections

- Status: Accepted
- Date: 2026-11-19

## Context
ADR 0002 makes the mono-summed wet path the default and leaves a third `wet_input` state, **True stereo**, as a plugin-only option. Neither unit had a true-stereo input: the REV7 sums L and R through two 20 kΩ resistors, and the REV5 uses its stereo/mono switch into one buffer. There is therefore no hardware behaviour to follow. Until now the engine summed both inputs whatever the setting (`Engine::processSample`), so True stereo sounded the same as Stereo.

Constraints from ADR 0002: the mode must not change the authentic modes' sound, and it should not need a second engine (CPU). The late network is our own (ADR 0003), and its stereo comes from orthogonal read-out signs, not from the input.

## Decision
Everything below is our own design (inference throughout).
- The engine keeps two input memories: the mid `(L+R)/2` it always had, and a side `(L−R)/2` beside it. The side memory is written in every mode, so switching mode never reads stale memory.
- In True stereo, each channel's early reflections read `mid ± k·side` at their tap time (left taps +, right taps −) with `k = 0.5`. That is 0.75 of the own side plus 0.25 of the other side. The REV5's left and right 1st reflections do the same. The centre 1st reflection, the pre-delayed direct feed and the secondary reverb stay on the mid.
- The late network hears the reflection feed as before, so it picks up the L/R difference through the taps. Its output stays decorrelated by the read-out signs. No extra loops.
- `k` sits in the engine's reflection layout, so a change of `wet_input` crossfades over 30 ms with the layout (engine.md item 9). In Mono and Stereo `k = 0` and the side memory is never read, so those modes are bit-for-bit what they were.
- The FX slot stays mono-in as on the hardware (it sums its input itself). The converter's A/D and D/A were already per channel.

## Consequences
- A centred source sounds the same in Stereo and True stereo, since the side is zero. Panned sources now keep their side in the early field.
- CPU: one extra delay-line read per reflection tap, in True stereo only, and one extra write per sample in all modes.
- `k` is a constant (`kTrueStereoSpread` in `Engine.cpp`), not a control. Promote it to a parameter only if listening asks for it.
- The late tail does not follow the source position. If that is wanted, a later pass could weight each loop's feed by the side, but that is a tuning job of its own.
