# Roadmap

**Status: version 1.0.0.** Every feature is in, and the programs have been refined against reference recordings of the hardware's factory programs. Next: the live checks, a listening pass on the refinements, and anything users turn up.

## Done

- [x] Research from the owner's manuals, service manuals, schematics and patents
- [x] Framework: JUCE, AU / VST3 / Standalone, LV2 on Linux ([ADR 0005](docs/design/decisions/0005-juce-framework.md)); licence AGPLv3
- [x] Reverb engine at the era's native rate inside a converter model ([engine.md](docs/design/engine.md), ADR 0001, ADR 0003)
- [x] REV5 secondary reverb, gate, ER feedback
- [x] FX slot: delay, echo, chorus, symphonic, flange, reverb flange, phasing, tremolo, pitch, pan, before or after the reverb ([fx-slot.md](docs/design/fx-slot.md))
- [x] FX period options (Stepped, Linear, Triangle, Trem. DM) with defaults chosen by ear
- [x] Non-reverb DSP: wet input mode, levels, analogue and digital EQ, width, ducker, mix, wet solo, bypass, mute
- [x] Full parameter set ([parameters.md](docs/design/parameters.md)) and preset banks: our own, plus the REV7 and REV5 manual programs ([presets.md](docs/design/presets.md))
- [x] Whole-engine crossfade on era, preset and state changes; per-era settings memory
- [x] UI from the approved layout ([ui-layout.md](docs/design/ui-layout.md)); preset management, settings overlay
- [x] CPU pass: about 1.3–2.4 % of one M1 Pro core at 48 / 96 kHz
- [x] Static listening pass
- [x] First release with signed installers (0.9.1 beta; [docs/release.md](docs/release.md))
- [x] Refinement against reference recordings of the factory programs: converters, each program's own early taps and voice, era-5 PIANO mode, era-7 time variance ([ADR 0007](docs/design/decisions/0007-era7-time-variance.md)), reported latency with a delayed dry ([ADR 0001](docs/design/decisions/0001-native-rate-engine.md))
- [x] 1.0.0

## Next

- [ ] Live checks: the GATE LVL input gate on a noisy source; a listen to the latest liveness and combination-program changes
- [ ] Listening pass on the 1.0 refinements
- [ ] Trademark disclaimer on the product page (when it exists)
- [ ] Fixes from user feedback

## Later

- [ ] The organ, brass and guitar early-reflection modes, hidden until there is data for them
- [ ] A more modal late bass on the REV7 programs, and a sparse late field for the Echo Room programs (structural; design first)
- [ ] Rev sets for the REV7 programs that borrow their nearest neighbour's
- [ ] REV5 E/R2 DENSITY 1 (printed on the E/R2 programs, not modelled)
