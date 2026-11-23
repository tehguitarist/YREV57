# Roadmap

**Status: beta, version 0.9.1.** Every feature is in. Left before 1.0: the last live checks, the first release with installers, and anything the beta turns up.

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
- [x] CPU pass: about 1.3–2 % of one M1 Pro core at 48 / 96 kHz
- [x] Static listening pass

## Before 1.0

- [ ] Live checks: the GATE LVL input gate on a noisy source; a listen to the latest liveness and combination-program changes
- [ ] First release: signed installers for macOS, Windows and Linux ([docs/release.md](docs/release.md))
- [ ] Trademark disclaimer on the product page (when it exists)
- [ ] Fixes from beta feedback

## Later

- [ ] The four instrument early-reflection modes (piano, organ, brass, guitar), hidden until there is data for them
- [ ] Rev sets for the REV7 programs that borrow their nearest neighbour's
- [ ] REV5 E/R2 DENSITY 1 (printed on the E/R2 programs, not modelled)
