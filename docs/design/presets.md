# Factory presets

Code: `src/plugin/FactoryPresets.h` (generated), `src/plugin/Presets.cpp`. Generator: `tools/gen_factory_presets.py`. Tests: `tests/PresetTests.cpp`.

## Banks

| Bank | Presets | Content |
| --- | --- | --- |
| FACTORY, COMBI | 12 + 3 | Our own generic presets (the prototype's `PRESETS` list; own values) |
| SECRET | 1 | Hidden: *CLA Drum Room* (below). No browser tab and Prev / Next skip it; shift-click the era key that is not lit to open the browser on it (the era stays); both era keys light while it is open or its preset is loaded |
| REV7 | 30 | The REV7's 30 ROM programs: printed names and settings |
| REV5 | 39 | The REV5's ROM programs 1-30 and combinations 91-99: printed names and settings |
| USER | any | The user's own (XML files) |

Manual programs keep their printed names (`Large Hall`, `Reverse Gate`, `PC & Rev & G`); the browser's REV7 and REV5 tabs say which unit. Several names repeat across banks (FACTORY, REV7 and REV5 all have a `Large Hall`), so presets are keyed by bank and name: the saved state stores `BANK/name` (a bare name from older state restores the first preset of that name), and names are unique within a bank.

## Policy and sources

Presets may take the program names and settings **printed in the owner's manuals** (ADR 0004 as amended). Nothing comes from firmware, ROM dumps or SysEx data.

| Unit | Source | Data file |
| --- | --- | --- |
| REV7 | Owner's manual, "Memory (ROM) contents and controllable parameters", printed p. 27 (PDF p. 28) | [`data/presets/rev7_factory_presets.csv`](../../data/presets/rev7_factory_presets.csv) |
| REV5 | Owner's manual section 10, "ROM contents and controllable parameters", printed pp. 56-94, one page per program | [`data/presets/rev5_factory_presets.csv`](../../data/presets/rev5_factory_presets.csv) |

The generator reads those two files, maps each program onto our parameters and writes [`data/presets/manual_programs_mapping.csv`](../../data/presets/manual_programs_mapping.csv): one row per preset and parameter, with the manual page, a `basis` and a note. The basis is:

- `printed`: the manual prints this value for this program and it maps one to one;
- `derived`: computed from printed values (a unit conversion, or a split across our controls);
- `ours`: the manual prints nothing for this control of ours, so the value is our choice ("ours" in the generator and the CSV). Never firmware.

Anything not listed in a preset takes the parameter default when the preset loads. The analogue EQ, mix, width, ducker, character and dither are not stored in the hardware programs and stay at their defaults (flat EQ, mix 50 %). Output level, wet input, wet R invert and the EQ view are not stored in any preset.

## Mapping rules

- **Era:** REV7 programs set era 7 and REV5 programs era 5.
- **ER/REV BAL (REV5):** the manual defines 100 % as early reflections only and 0 % as reverb only (OM p. 18, p. 41). Since 2026-11-19 our `er_rev_balance` follows the same convention (default 60 %, which sounds as the old 40 % did), so the printed value is stored as is.
- **Reverb programs** print time, HIGH, LOW, diffusion, initial delay, 1st reflections, density, LPF, space modulation and the secondary reverb (REV5); those map directly. The ER pattern: REV5 reverb and combination programs play their own early taps (Rev Hall; the two plates Rev Plate), read from reference recordings (twenty-second pass, engine.md; the REV5 prints no ER mode for them); REV7 reverb programs play their three early reflections per channel (REV7 OM p. 14; twenty-third pass, engine.md): Large Hall, Small Hall and Vocal Plate their own sets (Rev Hall, Rev Room, Rev Plate, read from captures at their factory initial delays), the rest the nearest by name (Strings Rev Hall; Percussion Plate and Spring Rev Plate; Echo Room, Snare and Handclaps Rev Room; ours).
- **The secondary reverb (REV5)** is printed as x1.0, 0.0 ms, 100 % on every reverb program and is stored as printed. In our engine that adds an equal-level second tail, so a REV5 reverb program is about 3 dB louder than the same settings with the secondary off. REV7 programs set the secondary level to 0 (the unit has none; ours).
- **Early-reflection programs** (REV7 E/R 1 and 2, REV5 5, 6, 19-21, 24, 25, 30, 98's reflection half): mode, liveness, room size, diffusion, initial delay, 1st reflection, LPF, density, space modulation and ER feedback map directly. REV7 prints mode 1-6 (our index is mode minus 1); REV5 prints names, including PIANO for Rehearsal Room. The instrument modes are hidden (Q-01), so Rehearsal Room keeps its printed name but loads Small Hall, the nearest charted room (ours; `INSTRUMENT_STAND_IN` in the generator). `manual_programs_mapping.csv` records the printed PIANO in its note and marks the stand-in `ours`; every other printed mode is marked `printed`. They have no late tail: `er_rev_balance` 0 and `rt` 0.3 s (ours). The REV7 prints no reflection count, so 18 (its stated maximum) is ours. The REV7 programs set the hidden `er_type` from the ROM chart's type column (E/R1 or E/R2; twenty-fifth pass): E/R2 on Early Reflection 2, Electric Bass B, Gate Reverb, Reverse Gate, Piano, Guitar and Live Reference. The program notes call Gate Reverb E/R1; the chart is followed (Q-28). The REV5 programs do the same (twenty-eighth pass): E/R2 on Early Ref.2, Electric Bass B, Reverse Gate and Live Reference (REV5 OM sec. 10, printed pp. 61, 75, 79, 85).
- **Gate (REV5):** program 23 prints a gate (trigger level 65, delay 10 ms, hold 150 ms, release 5 ms); the combinations print the same block with trigger level 0 (off). They map to `gate_level`, `gate_trig_delay`, `gate_hold` and `gate_release`. Every other program prints GATE LVL 0 %.
- **Digital EQ (REV5):** every program prints the EQ off and flat (low peak 315 Hz, mid 1.0 kHz, high peak 4.0 kHz, 0 dB, Q 1.0); that goes to `deq_*` as printed.
- **FX-only programs** (delay, echo, mod, pitch, pan): the FX slot type and its printed controls. The hardware LPF FRQ goes to `fx_lpf`. The delay and echo have no level control on the REV5, so `dly_level` and `echo_level` are 100 % (ours; REV7 delay prints 100 %). REV5 echo HIGH x0.9 becomes `echo_damp` 9 (ratio = value / 10). `rev_mix` is 0 on these programs, so they play the effect alone (with the 1st reflections). Pitch Change A, B and C select `pitch_sel` 0, 1 and 2; B and C put the second voice in `pitch2_*`.
- **Reverb Flange (REV7 10):** the `revflange` mod type, with the printed REV TIME in `mod_rt`. **REV5 10** (since 2026-11-21): a reverb program with a flange on it (OM p. 29): the engine at its printed reverb settings (time, HIGH, LOW, diffusion, density, LPF, ER/REV BAL, secondary) on the Rev Hall taps, the plain `flange` mod type before it at the printed rate, depth and delay, feedback 30 % (ours, the REV7's fixed value).
- **Combinations 91-97:** the FX slot holds the effect and `fx_order` the printed order (91-94 effect then reverb, 95-97 reverb then effect); the reverb part prints time, HIGH, LOW, diffusion, density, LPF and space modulation. These programs print no ER/REV BAL, so ours is the same as Large Hall (46). **98** (ER + Rev) runs our ER and the reverb together with the printed balance. **99** (Plate & Hall) is the primary reverb at the printed plate settings and our secondary reverb as the hall: `rev2_time` 1.3 (2.6 s / 2.0 s), `rev2_delay` 20 ms (REV2 DLY 30 ms less REV1 DLY 10 ms), `rev2_level` 100 % with `voice_rev2_gain` +12 dB (REV1/2 BAL 50 % read as equal levels: inference) and `voice_rev2_hi` 0.3 (2 HIGH).

## Mapping gaps

Hardware controls we do not have, and what the generator does.

| Gap | What we do |
| --- | --- |
| **No reverb off.** Programs with no reverb (delay, echo, mod, pitch, pan) still run the reverb engine | `rt` 0.3 s, `er_rev_balance` 0, `er_count` 1, liveness 0, room size 0.1 (ours): one tiny reflection. A real "reverb off" switch in the engine would be cleaner |
| **REV MIX (91-97):** the reverb level in a combination program (45 %) | Mapped to `rev_mix` (2026-11-22) |
| **PITCH BAL (94, 97):** pitch/reverb balance (100 %) | Not mapped: the manual lists only its name and range (pp. 38-40, OCR), no description; at the printed 100 % it is read as our routing (inference) |
| **MIDI TRG, BASE KEY (pitch):** MIDI-keyed gate and transposition | Dropped (DAW automation covers it); the base key (C3) is not modelled |
| **BALANCE, OUT LVL, OUT PHASE** (100 %, 100 %, off on every program) | Not stored in presets (maintainer's decision, 2026-11-22): mix and output are session controls; BALANCE 100 % would force fully wet on every load, OUT LVL 100 % is our 0 dB default, and there is no phase control |
| **Secondary reverb HIGH, LOW, diffusion (99):** 0.3, 1.2, 5 | 2 HIGH on the hidden `voice_rev2_hi` (2026-11-22); 2 LOW and 2 DIFFUSION equal the plate's, so they share. REV1/2 BAL 50 % as equal levels: `rev2_level` 100 % plus `voice_rev2_gain` +12 dB |
| **ER DLY and REV DLY (98):** 0.1 ms and 20 ms | Read as after the common initial delay (10 ms; inference): the reflections at the initial delay, the late network 19.9 ms later on the hidden `voice_late_onset` (2026-11-22) |
| **Digital EQ ahead of or after the reverb (Q-15)** | Stored as printed (all off), so the question does not arise here |

Every stored value is inside its parameter's range (checked by the tests), so no printed value had to be clamped.

## Chart conflicts and checks

- REV5 gate release prints 5-24,000 ms on program 23 but 3-24,000 ms on 91-99; the value is 5 ms in both, so nothing changes. REV5 Electric Bass A prints ER FB GAIN as 0-99 % where the others print +-99 %; our parameter is ±99 % (widened 2026-11-19, treating the print as a misprint), and the value is 0.
- The first REV7 transcription had errors, now corrected against the page image: phasing rate 1.1 Hz (not 1), symphonic depth 50 % (not 80), Handclaps HIGH x0.1, Piano diffusion 6, Spring diffusion 5, and liveness was missing for the E/R programs.
- Page images re-read for this bank: all 30 REV7 programs (one page), and REV5 programs 1, 5, 7, 10, 19, 23, 28, 29, 91, 98 and 99. The remaining REV5 values rest on the earlier page-image transcription in the CSV (read from the same scans).

## CLA Drum Room

The hidden SECRET bank's one preset, reached by shift-clicking the unlit era key. It is era 7 voiced by ear and by fitting towards the sound of a Yamaha REV1 drum room. **The REV1 is neither of our two eras**: this is our era 7 voiced to sit near that sound, not a model of the REV1. Values are ours:

| Control | Value |
| --- | --- |
| Era, ER mode | 7, Small Hall |
| RT, HIGH, LOW | 1.4 s, x0.37, x1.27 |
| Pre-delay, 1st reflection | 0.1 ms, level 0 |
| Room size, liveness, reflections, diffusion | 3, 5, 18, 8 |
| E/R-reverb balance | 75 % |
| Analogue EQ | on, low 150 Hz -10 dB, high 16 kHz -5 dB |
| Voice tilts (hidden) | reflections +8 dB (8 kHz shelf), late −2 dB (5 kHz shelf) |
| Mix | 22 % |

## Regenerating

```
python3 tools/gen_factory_presets.py
clang-format -i src/plugin/FactoryPresets.h
```

Run from the repo root. The test `manual programs: bank sizes, unique names, every value in range, every preset loads` checks the bank sizes (30 and 39), unique names, ids, ranges and that each preset loads and reads back.

## Hidden voice values (2026-11-19)

The manual programs that have a late tail carry the hidden voice parameters ([parameters.md](parameters.md)), chosen by family as best guesses. All of these are ours (inference), marked `ours` in `manual_programs_mapping.csv` and set in `VOICE` in `tools/gen_factory_presets.py`. The engine was fitted to the captures at the defaults, so the values stay modest; iterate by ear.

| Family (programs) | Loop × | Diffuser × | Low crossover | ER gain |
|---|---|---|---|---|
| Large Hall (REV7 1; REV5 1, 23, 91–97) | 1.1 | 1.1 | 260 Hz | 0 dB |
| Small Hall (REV7 2, REV5 2) | 0.9 | 0.95 | 320 Hz | +1 dB |
| Vocal Plate (REV7 3, REV5 3) | 0.8 | 0.8 | 380 Hz | −2 dB |
| Percussion Plate (REV7 4, REV5 4) | 0.8 | 0.75 | 400 Hz | −1 dB |
| Spring (REV7 16, REV5 16) | 0.85 | 1.2 | 450 Hz | −2 dB |
| Echo Room (REV7 17, REV5 17) | 1.0 | 0.85 | 280 Hz | +1.5 dB |
| Strings (REV7 18, REV5 18) | 1.15 | 1.2 | 240 Hz | −1 dB |
| Snare (REV7 22, REV5 22), Handclaps (REV7 29) | 0.9 | 0.85 | 350 Hz | +1 dB |
| Plate & Hall (REV5 99, plate-led) | 0.85 | 0.85 | 350 Hz | 0 dB |

**Fitted on the captures (ours, `derived` in the mapping CSV).** REV7 (pass 24): Large Hall diffusers ×1.5, Vocal and Percussion Plate ×0.6. REV5 (pass 26, `VOICE5`): Large Hall and Small Hall (with 23 and 91–97) diffusers ×1.0 and `voice_late_onset` 10 ms; Vocal and Percussion Plate loops ×1.0, diffusers ×1.0 and late onset 18 ms. Fitted on the late build-up above 300 Hz from each capture's first tap; Percussion Plate follows the Vocal Plate and the combinations the Large Hall by family (inference). The REV7 programs keep onset 0 (their captures fit best there), and so do the springs, Snare, Strings and Echo Room (springs and a snare capture fit best at 0; no capture for the others).

Reflection-only, gate, bass, instrument and FX programs keep the defaults: their late network is not heard (`er_rev_balance` 100 %), and their printed settings already define them. REV5 98 (ER + Rev) keeps the defaults too.
