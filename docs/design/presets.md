# Factory presets

Code: `src/plugin/FactoryPresets.h` (generated), `src/plugin/Presets.cpp`. Generator: `tools/gen_factory_presets.py`. Tests: `tests/PresetTests.cpp`.

## Banks

| Bank | Presets | Content |
| --- | --- | --- |
| FACTORY, COMBI | 12 + 3 | Our own generic presets (the prototype's `PRESETS` list; own values; HIGH anchored on the hardware programs since 2026-11-24, [below](#factory-and-combi-high)) |
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
- **Reverb programs** print time, HIGH, LOW, diffusion, initial delay, 1st reflections, density, LPF, space modulation and the secondary reverb (REV5); those map directly. The ER pattern: the reverb programs play their own early taps, read from the hardware captures of the factory programs (the REV5 prints no ER mode for them; the REV7 programs three per channel, REV7 OM p. 14). REV7 (pass 40): Large Hall Rev Hall, Small Hall, Handclaps and Reverb Flange Rev Room, Vocal Plate and Strings Rev Plate, and Percussion Plate, Snare, Spring and Echo Room their own sets (`er_mode` 15–18). REV5 (pass 41, tap 1 re-read in pass 44): 01 and 23 Rev Hall, 02 and 10 Rev Room, 03 Rev Plate, 04 Rev Perc Plate, 16 Rev Spring, 17 Rev Echo Room, 18 Rev Strings, 22 Rev Snare, 91–97 Rev Combi.
- **The secondary reverb (REV5)** is printed as x1.0, 0.0 ms, 100 % on every reverb program and is stored as printed. In our engine `rev2_level` 100 % is a quarter of the primary's level (pass 32, fitted on the REV5 captures), so the second tail sits well under the first. REV7 programs set the secondary level to 0 (the unit has none; ours).
- **Early-reflection programs** (REV7 E/R 1 and 2, REV5 5, 6, 19-21, 24, 25, 30, 98's reflection half): mode, liveness, room size, diffusion, initial delay, 1st reflection, LPF, density, space modulation and ER feedback map directly. REV7 prints mode 1-6 (our index is mode minus 1); REV5 prints names, including PIANO for Rehearsal Room. Organ, Brass and Guitar are hidden (Q-01); the Piano key has been shown since 2026-11-25 and Rehearsal Room loads Piano (printed; the pass 52 pattern). `INSTRUMENT_STAND_IN` in the generator keeps the stand-ins for the other three. `manual_programs_mapping.csv` records the printed PIANO in its note and marks the stand-in `ours`; every other printed mode is marked `printed`. They have no late tail: `er_rev_balance` 0 and `rt` 0.3 s (ours). The REV7 prints no reflection count, so 18 (its stated maximum) is ours. The REV7 programs set the hidden `er_type` from the ROM chart's type column (E/R1 or E/R2; twenty-fifth pass): E/R2 on Early Reflection 2, Electric Bass B, Gate Reverb, Reverse Gate, Piano, Guitar and Live Reference. The program notes call Gate Reverb E/R1; the chart is followed (Q-28). The REV5 programs do the same (twenty-eighth pass): E/R2 on Early Ref.2, Electric Bass B, Reverse Gate and Live Reference (REV5 OM sec. 10, printed pp. 61, 75, 79, 85). Exception (pass 57): *Kick* (both units, printed E/R1) loads E/R2, because its hardware captures decay past the taps like the E/R2 programs (Q-28; `derived` in the mapping).
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

## FACTORY and COMBI: HIGH

Pass 37 (2026-11-24) moved `hi_ratio` to where the hardware reads its printed HIGH (the top of each era's band), so our own presets' guessed values (0.5–1.0, chosen under the 8 kHz-octave reading) played far brighter than any hardware program. The maintainer's decision: anchor each on the printed HIGH of the nearest hardware program (REV7 and REV5 print the same values for the same programs; the reverb programs' median is 0.3). Ours (the choice of anchor):

| Preset | HIGH (was) | Anchor |
| --- | --- | --- |
| Large Hall, Hall + Delay | 0.3 (0.7) | Large Hall 0.3; the combination programs 0.3 |
| Small Hall | 0.4 (0.6) | Small Hall 0.4 |
| Vocal Plate | 0.3 (0.8) | Vocal Plate 0.3 |
| Bright Plate | 0.5 (1.0) | Percussion Plate 0.5, the brightest plate |
| Spring Tank | 0.2 (0.3) | Spring 0.2 |
| Drum Room | 0.4 (0.6) | Small Hall 0.4 (*CLA Drum Room* refitted to 0.4 too) |
| Gated Snare | 0.3 (0.8) | REV5 Reverb & Gate 0.3 |
| Reverse Swell | 0.3 (0.7) | the reverb median (Reverse Gate is an E/R program, no HIGH) |
| Slap Echo | 0.3 (0.6) | Echo Room 0.3 |
| Chorus Wash, Symphonic Hall, Pan Verb, Pitch Shimmer | 0.3 (0.5–0.7) | the combination programs (Chorus, Symphonic, Pan, Pitch Change & Reverb) 0.3 |
| Flange Room | 0.4 (0.6) | Reverb Flange 0.4 |

LOW was already in the hardware's 0.8–1.2 range and stays. Not heard.

## CLA Drum Room

The hidden SECRET bank's one preset, reached by shift-clicking the unlit era key. It is era 7 voiced by ear and by fitting towards the sound of a Yamaha REV1 drum room. **The REV1 is neither of our two eras**: this is our era 7 voiced to sit near that sound, not a model of the REV1. Values are ours:

| Control | Value |
| --- | --- |
| Era, ER mode | 7, Small Hall |
| RT, HIGH, LOW | 1.4 s, x0.4, x1.5 |
| Pre-delay, 1st reflection | 0.1 ms, level 0 |
| Room size, liveness, reflections, diffusion | 2.0, 6, 16, 10 |
| E/R-reverb balance | 62.5 % |
| Time variance | era 7's drift (ADR 0007), a voicing choice |
| Analogue EQ | on, low 100 Hz −4 dB, high 16 kHz −5 dB |
| Voice crossovers (hidden) | low 350 Hz, high 6500 Hz |
| Early fill (hidden) | `voice_er_fill` 0.4, `voice_er_fill_size` 2.0 |
| Early cloud (hidden) | `voice_er_cloud` 1, hold 20 ms, rise −3 dB, fade 3 dB per 10 ms; reflection low shelf −10 dB at 100 Hz |
| Voice tilts (hidden) | reflections +9 dB (8 kHz shelf), late −10 dB (5 kHz shelf) |
| Sub decay, diffuser gain (hidden) | `voice_sub_ratio` ×1.3, `voice_diff_gain` ×1.0 |
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

**Fitted on the captures (ours, `derived` in the mapping CSV).** REV7 (pass 45, `VOICE7`, on the hardware captures of the programs themselves; replaces pass 24's diffuser sizes): diffuser size / reflection gain, late onset 0: Large Hall ×1.25 / 0 dB, Vocal Plate ×0.6 / −8, Percussion Plate ×0.6 / +8, Reverb Flange ×0.6 / +4, Spring ×0.6 / −8, Echo Room ×0.6 / −8, Strings ×0.6 / −12, Snare ×1.0 / +8, Handclaps ×0.8 / +4; Small Hall keeps its family voice (it fitted). REV5 (pass 44, `VOICE5`, on the hardware captures of the programs themselves; replaces passes 26 and 32): diffuser size / reflection gain per family, late onset 0 on all: Large Hall ×1.0 / +2 dB (with 23 and 91–97 by family, inference), Small Hall ×0.7 / +1, Vocal Plate ×0.6 / −1 (loops ×1.0), Percussion Plate ×0.7 / +1 (loops ×1.0), Strings ×0.8 / −1, Spring ×0.7 / +1, Echo Room ×0.6 / 0, Snare ×1.0 / −1. Fitted on the 500 Hz–4 kHz energy per window re the first 5 ms, C50 and C80. The REV7 programs keep onset 0. E/R programs (pass 49, `VOICE_ER7` / `VOICE_ER5`, the reflection field against the 1st reflection on the same captures): REV7 Electric Bass A and Kick −3.5 dB; REV5 Electric Bass A +2.5, Electric Bass B −2.5, Kick −3.5 (pass 57, refitted with its E/R2 tail); the others 0. Per program over the family voice (pass 60, `VOICE5_PROG_ER`): REV5 Reverb Flange +6.5 dB.

**Early fill (pass 79, `FILL7`, on the REV7 captures of the programs themselves; derived):** level / size: Small Hall 0.2 / 2, Vocal Plate 1.0 / 0.75, Reverb Flange 0.1 / 1, Echo Room 0.75 / 1, Strings 0.75 / 0.75, Snare 0.1 / 2, Handclaps 0.2 / 2; Large Hall, Percussion Plate and Spring off (no gain, or worse features).

**Per-program refits (passes 81–89, derived; `DIFF_GAIN`, `SUB_RATIO` and `PROG_VOICE` in the generator, each kept only where no metric of that program got worse, except the pass-89 trade):** REV7 *Percussion Plate* diffuser gain ×1.15 with a 500 Hz low crossover, *Strings* ×1.15; REV7 *Snare* sub decay ×1.15 and late onset 4 ms (pass 87), *Handclaps* ×0.85; REV7 *Small Hall* sub shelf 56 Hz ×2 (pass 83); REV7 *Large Hall* diffuser size ×1.4 and gain ×1.15 (pass 85) with its sub shelf at 130 Hz ×1.1 (pass 89; 150 Hz ×1.3 in passes 85–88, which held 300–800 ms but left the bass past 800 ms too long). REV5 *Percussion Plate* diffusers ×0.78 with gain ×1.25; REV5 *Large Hall* reflections +3 dB with diffuser gain ×1.15, *Snare* late tilt +2.5 dB, reflections +0.5 dB, crossovers 400 Hz / 11 kHz (pass 82); REV5 *Small Hall* sub shelf 100 Hz ×0.9 (pass 83). REV5 98 *ER + Rev & G* plays the combinations' tap set beside its chart (`voice_rev_taps` 9, pass 67) on the printed S-Hall chart (`voice_er_printed`, pass 86). Each REV7 program's tap read: `TAP_READ7` (pass 61; [parameters.md](parameters.md) `voice_tap_read`).

Reflection-only, gate, bass, instrument and FX programs keep the defaults: their late network is not heard (`er_rev_balance` 100 %), and their printed settings already define them. REV5 98 (ER + Rev) keeps the defaults too.
