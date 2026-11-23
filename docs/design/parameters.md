# Parameter specification (draft v0)

Proposed user-facing and hidden parameters, derived from both units' ranges ([`data/parameters/`](../../data/parameters/)). Names are our own. "Source" shows where the range comes from; "R7" = REV7, "R5" = REV5.

## Global

**Era naming:** the two eras are named after the hardware model numbers, **7** (REV7, 1985) and **5** (REV5, 1987). The numbers are model names, not a version count: era 5 is the *later* machine. Era 7 is the default and is stored as index 0; index 1 is era 5. In the UI show "7" and "5" with the years as small captions. Hardware names stay descriptive only ([ADR 0004](decisions/0004-inspired-by.md)).

| ID | Name | Range | Default | Source |
| --- | --- | --- | --- | --- |
| `era` | Era | 7 (REV7, 1985) / 5 (REV5, 1987) | 7 | design |
| `wet_input` | Wet input | Mono (left input only, hardware MONO) / Stereo (L+R summed to one mono wet input, scaled by 0.5 so a centred signal keeps its level, hardware STEREO, **authentic**) / True stereo (plugin only: the early reflections lean to the side the input comes from, ADR 0006) | Stereo | R7, R5, PLG |
| `in_level` | Input level | −∞ to +12 dB (dial 8 = 0 dB) | 0 dB | R7 |
| `mix` | Mix | 0–100 % (1:1 sum at 100 %) | 50 % | R7 MIXING |
| `out_phase_r` | Wet R phase invert | off/on | off | R7 OUT PHASE, R5 |
| `bypass` | Bypass | off/on | off | both |
| `aeq_on` | Analogue EQ on | off/on | off | both |
| `aeq_lo_f` / `aeq_lo_g` | Low freq / gain | 50–700 Hz / ±15 dB | 200 Hz / 0 | both |
| `aeq_mid_f` / `aeq_mid_g` | Mid freq / gain | 350 Hz–5 kHz both eras by circuit (Q-26) / ±15 dB | 1 kHz / 0 | R7 / R5 |
| `aeq_hi_f` / `aeq_hi_g` | High freq / gain | 2–20 kHz / ±15 dB | 8 kHz / 0 | both |
| `character` | Converter character | 0–100 % (0 = clean, 100 = authentic): word length 24 → 16 bits (truncation toward zero), hiss on, era-5 dither on. The era's band limit and emphasis stay at every setting | 100 % | design |
| `width` | Stereo width (wet) | 0–200 % (100 = unchanged, 0 = mono) | 100 % | PLG, DSP from REV-246 |
| `wet_solo` | Wet solo | off/on: output 100 % wet, the mix control is bypassed. A monitoring state: not stored in presets | off | PLG |
| `duck_on` | Ducker on | off/on | off | PLG, DSP from REV-246 |
| `duck_range` | Ducker range | −60–0 dB | −10 dB | PLG |
| `duck_thresh` | Ducker threshold | −80–0 dB | −20 dB | PLG |
| `duck_attack` / `duck_release` | Ducker attack / release | 0–1,000 ms | 10 / 200 ms | PLG |
| `dither` | Dither (Era 5) | off/on | on | R5 SW103 |

## Reverb

| ID | Name | Range | Source |
| --- | --- | --- | --- |
| `rt` | Reverb time @ 1 kHz | 0.3–10 s (Era 7), 0.3–99 s (Era 5) | R7, R5 |
| `hi_ratio` | High ratio | ×0.1–1.0; our engine defines it as the late network's 8 kHz octave RT over 1 kHz RT ([engine.md](engine.md) item 4) | both (Q-03) |
| `lo_ratio` | Low ratio | ×0.1–2.4 | both |
| `pre_delay` | Initial delay | 0.1–100 ms (Era 7), 0.1–1,000 ms (Era 5) | R7, R5 p. 14. Era 7's REV E/R taps scale with it (thirtieth pass, inference from the captures) |
| `diffusion` | Diffusion | 0–10 | both |
| `ref1_delay` / `ref1_level` | 1st reflection, centre | 0.1–100 ms (Era 7), 0.1–1,000 ms (Era 5) / 0–100 % | R7, R5 p. 14 |
| `ref1_l_delay` / `ref1_l_level` / `ref1_r_delay` / `ref1_r_level` | 1st reflections, left and right (Era 5) | 0.1–1,000 ms / 0–100 % | R5 p. 14 |
| `er_rev_balance` | ER/reverb balance (adv.) | 0–100 %: 100 % early reflections only, 0 % reverb only, about 50 % equal (as the REV5, OM p. 18; flipped 2026-11-19, default 60 %) | R5 INT |
| `density` | Density (adv.) | 1–4 | R5 INT |
| `wet_lpf` | Wet low-pass (adv.) | 1–16 kHz in rounded 1/6-octave steps (incl. 3.6, 6.3, 7.0, 8.0) / thru; step list inference. A first-order low-pass (fitted to the REV5 captures, engine.md pass 21) | R5 INT |
| `space_mod` | Space modulation (adv.) | 0–10 | R5 INT |
| `rev2_time` / `rev2_delay` / `rev2_level` | Secondary reverb (adv.) | ×0.1–10 / 0–500 ms / 0–100 % | R5 INT |
| `gate_*` | Gate: threshold, trigger delay, hold, release (the hardware MIDI trigger is dropped: DAW automation covers it) | level / 0.1–1,000 ms / 1–24,000 ms / 3–24,000 ms | R5 combination |

## Early reflections

| ID | Name | Range | Source |
| --- | --- | --- | --- |
| `er_mode` | Mode | Small Hall, Large Hall, Random, Reverse, Plate, Spring, Rev Hall, Rev Plate, Rev Room (+ Pan A/B in Era 5) | both |
| `liveness` | Liveness | 0–10 | both |
| `room_size` | Room size | 0.1–20 (Era 7), 0.1–25 (Era 5) | Q-02 |
| `er_count` | Reflections | 1–18 (Era 7), 1–34 (Era 5) | R7, R5 |
| `er_fb_gain` / `er_fb_delay` / `er_fb_hi` | ER feedback (Era 5) | ±99 % / 0.1–1,400 ms / ×0.1–1.0 | R5 p. 21 |

## Delay / echo / modulation

| ID | Name | Range | Source |
| --- | --- | --- | --- |
| `dly_l` / `dly_r` | Delay L/R | 0.1–900 ms (Era 7), 0.1–2,900 ms (Era 5) | R7, R5 (Q-11 closed) |
| `dly_fb` / `dly_fb_r` / `dly_high` | Delay feedback L/R, HIGH | Era 5 only: −99 to +99 %, ×0.1–1.0 | R5 |
| `echo_l` / `echo_r` / `echo_fb` / `echo_fb_r` / `echo_damp` | Echo | 0.1–450 ms (Era 7), 0.1–1,400 ms (Era 5) / 0–99 % shared (Era 7), ±99 % per side (Era 5) / HIGH 0–10 in whole steps, treble ratio = value / 10 (era 5 shows the REV5's HIGH ratio ×0.1–1.0) | R7, R5 (Q-13) |
| `rev_mix` | REV MIX (added 2026-11-22): the reverb beside the effect's bypass in the hardware routing ([fx-slot.md](fx-slot.md), Routing); 45 % on the combinations, 0 on effect-only programs. INT PARAM drawer | 0–100 %, default 100 | R5 (91–97) |
| `fx_wet` | FX WET (added 2026-11-22): no bypass around the reverb, fully wet. Off by default (the hardware routing). `mod_rt` is unused since the same date (kept for saved states) | off / on | PLG |
| `pitch_*` / `pitch2_*` | Pitch voices | ±12 st, ±100 ct, 0.1–400 ms (C: 200 ms), feedback 0–99 % (A only) | R5 |
| `lfo_rate` | LFO rate | 0.1–20 Hz (Era 7), 0.1–40 Hz (Era 5) | R7, R5 |
| `dm_depth` / `am_depth` | Delay / amplitude mod depth | 0–100 % (100 % = ±4 ms) | R7 |
| `mod_delay` | Base delay | flange 0.1–100 ms, reverb flange 0.1–30 ms (Q-07), phasing 0.1–8 ms (Era 5: 0.1–5 ms; REV5 OM p. 31) | R7, R5 |
| `mod_fb` | Flange feedback | 0–99 % (reverb flange fixed 30 %) | R7 |

## Implemented IDs (panel controls)

These IDs exist in the APVTS layout and are frozen (see `tests/ParameterTests.cpp`). Ranges are the widest of the two eras; the per-era limits are applied by the UI (knob travel) and the processor (`kEraLimits` in `Parameters.h`, which clamps stored values). The engine should still treat them as limits.

- Global additions: `out_level` (−60 to +12 dB, 0), `mute`.
- Reverb: `rt` (0.3–99 s, log), `hi_ratio`, `lo_ratio`, `pre_delay`, `diffusion`, `ref1_delay`, `ref1_level`, `er_rev_balance`, `density`, `wet_lpf` (1–16 kHz in 1/6-octave steps, 17 = thru), `space_mod`, `rev2_time`, `rev2_delay`, `rev2_level`, `gate_level`, `gate_trig_delay`, `gate_hold`, `gate_release`.
- Early reflections: `er_mode` (15 choices: Small Hall, Large Hall, Random, Reverse, Plate, Spring, Pan A, Pan B, Piano, Organ, Brass, Guitar; the last four, indices 8-11, are hidden in the UI until there is data for them (Q-01) and the processor plays a charted stand-in: Small Hall, Large Hall, Plate, Spring; the stored value is left alone; 14 choices since the twenty-second pass: 12 Rev Hall and 13 Rev Plate, the REV5 reverb programs' own early taps read from captures, era 5 only, no room size, liveness or count, Rev Hall dense at density 4; 15 since the twenty-third pass: 14 Rev Room, and the three Rev modes play in both eras: in era 7 the REV7 reverb programs' three early reflections per channel, Rev Hall the Large Hall set, Rev Plate the Vocal Plate set, Rev Room the Small Hall set, read from captures; in era 5 Rev Room plays the sparse Rev Hall set), `liveness`, `room_size`, `er_count`, `er_fb_gain`, `er_fb_delay`, `er_fb_hi`.
- FX slot: `fx_type` (Delay, Echo, Mod, Pitch, Pan, Off), `fx_order`, `fx_link`, `fx_sync` (one each, shared by the FX types that show the key), `fx_lpf` (same steps as `wet_lpf`); delay `dly_l`, `dly_r`, `dly_level`, `dly_fb`; echo `echo_l`, `echo_r`, `echo_fb`, `echo_damp`, `echo_level`; mod `mod_type` (Flange, Rev Flange, Chorus A, Chorus B, Phasing, Tremolo, Symphonic), `lfo_rate`, `mod_depth`, `dm_depth`, `am_depth`, `mod_delay`, `mod_fb`, `mod_rt`, `mod_lpf` (same steps as `wet_lpf`).
- Pitch and pan (era 5): `pitch_sel` (A/B/C), `pitch_shift`, `pitch_fine`, `pitch_delay`, `pitch_fb`, `pan_dir` (L to R, R to L, L <> R (back and forth)), `pan_rate`, `pan_depth`.
- Added from the REV5 manual (pp. 25–36), at the end of the layout: `dly_fb_r` (−99 to 99 %; `dly_fb` is now the left channel), `dly_high` (×0.1–1.0), `echo_fb_r` (−99 to 99 %; `echo_fb` is the left, and both sides in era 7), `pitch2_shift`, `pitch2_fine`, `pitch2_delay` (Pitch B's 2nd voice, Pitch C's right voice). Era-5 ranges widened: `dly_l`/`dly_r` to 2,900 ms, `echo_l`/`echo_r` to 1,400 ms (era 7 keeps 900 / 450 ms through `kEraLimits`).
- Digital EQ (era 5, own design): `eq_view`, `deq_on`, `deq_lo_f`/`_g`/`_q`/`_shelf`, `deq_mid_f`/`_g`/`_q`, `deq_hi_f`/`_g`/`_q`/`_shelf`. The `_q` controls are the REV5's bandwidth: 0.1 narrowest, 5.0 widest (OM p. 15), applied as filter Q = 1 / value (2026-11-22).
- Not stored in presets: bypass, mute, wet solo, input and output level, wet input, wet R invert, EQ view. Everything else is. Plugin settings (quality, noise seed, limiter, era settings, UI scale) live in the plugin state as properties, not parameters.

## Factory preset banks

FACTORY and COMBI are our own generic presets; REV7 (30) and REV5 (39) carry the program names and settings printed in the owner's manuals, mapped onto the parameters above. Sources, the printed / derived / ours rule and the mapping gaps: [presets.md](presets.md).

## Hidden per-preset values

Base delays of the late network, all-pass coefficients, damping corner frequencies, modulation depth of the tail, ER group gains (the symphonic's delays and AM, once listed here, are fixed measured values since 2026-11-22, [fx-slot.md](fx-slot.md)). These give presets identity, as on the hardware.

**Built 2026-11-19 (first set; ours throughout):** four hidden voice parameters, plus `gate_input` (2026-11-20). They aren't on the panel and aren't automatable (`withAutomatable (false)`), but they are saved with the state, presets and the per-era memory like any other preset parameter. Each default is the engine's fixed value, so a preset that leaves them alone sounds as before.

| ID | Range | Default | Acts on |
|---|---|---|---|
| `voice_loop_size` | ×0.7–1.4 | 1.0 | Late-network loop lengths (45–88 ms at 1.0) and their mid-loop taps; the loop gains follow, so `rt` holds |
| `voice_diff_size` | ×0.6–1.5 | 1.0 | Input-diffuser lengths; their gains follow the length rule (held faster than the fastest band) |
| `voice_lo_xover` | 100–800 Hz | 300 | The loops' low-shelf corner, i.e. where `lo_ratio` takes over (primary and secondary networks) |
| `voice_er_gain` | −6 to +6 dB | 0 | The early-reflection group level, on top of `er_rev_balance` (crossfades with the reflection layout) |
| `gate_input` | 0–1 (≥ 0.5 on) | 0 | Added 2026-11-20. Where the gate sits (era 5): on, the REV5 per-program noise gate (GATE LVL, owner's manual pp. 22, 27–29, 34, 37, OCR) on the program input, opening with no trigger delay, so echo and reverb tails ring on; off, the trigger gate of Reverb & Gate and the combinations on the effect output. The REV5 manual programs that print GATE LVL set it on (all print 0 %, so none opens a gate); input placement is inference |
| `er_type` | 0–1 (≥ 0.5 E/R2) | 0 | Added 2026-11-20 (twenty-fifth pass). The REV7 E/R program type: E/R2 passes the reflections through a decaying diffuser (four series all-passes, T60 80 ms; [engine.md](engine.md)), which makes a gated program's dense field and smooth close. Twenty-eighth pass (2026-11-21): era 5 too, at T60 200 ms with both channels at the same lengths (the REV5 E/R captures' tails). The Rev modes ignore it. The REV7 and REV5 manual programs set it from the ROM chart's type column (Q-28) |
| `voice_late_onset` | 0–60 ms | 0 | Added 2026-11-20 (twenty-sixth pass). The late network hears its input (the pre-delayed input and the reflection feed) this much later; the reflections and 1st reflections are untouched, and the reported tail grows by the same amount. Fitted on the REV5 captures: the REV5 halls 10 ms, the plates 18 ms ([engine.md](engine.md), [presets.md](presets.md)); era 7 and the other programs fit best at 0 |
| `voice_er_tilt` | −12 to +12 dB | 0 | Added 2026-11-21 (thirty-fifth pass). A high shelf (8 kHz, Q 0.707) on the audible reflections only (the reflection feed into the late network is separate); for a preset whose early field wants other treble than its late field. Set on *CLA Drum Room* (+8 dB) |
| `voice_late_tilt` | −12 to +12 dB | 0 | Added 2026-11-21 (thirty-fifth pass). A high shelf (5 kHz, Q 0.707) on the late tail only (after the bass and band coherence). Set on *CLA Drum Room* (−2 dB) |
| `voice_rev2_hi` | 0–1.0 | 0 | Added 2026-11-22 (manual audit). The secondary reverb's HIGH ratio; 0 follows `hi_ratio`. Set on REV5 99 *Plate & Hall* (its 2 HIGH ×0.3 against the plate's ×0.5) |
| `voice_rev2_gain` | 0–12 dB | 0 | Added 2026-11-22 (manual audit). A gain on the secondary reverb's level (`rev2_level` 100 % is a quarter of the primary since pass 32). Set on REV5 99 *Plate & Hall* (+12 dB: REV1/2 BAL 50 % read as equal levels, inference) |
| `voice_hi_xover` | 4–14 kHz (log) | 8500 | The loops' high-shelf corner (Q 0.5), i.e. where the `hi_ratio` damping sets in (primary and secondary networks). `hi_ratio` stays the 8 kHz octave's ratio at every corner: the engine re-measures its calibration when the corner moves ([engine.md](engine.md) item 4). At the era-7 native rate (31.25 kHz) corners above 10 kHz play as 10 kHz |

Changes to the lengths jump the loops, which is acceptable because they arrive with a preset load, where the whole-engine crossfade starts a fresh engine. The tail modulation already has a control (`space_mod`), and the symphonic values are measured (2026-11-22, [fx-slot.md](fx-slot.md)). Values for the manual programs: best guesses by family, chosen 2026-11-19 ([presets.md](presets.md)); `voice_hi_xover` (added later the same day) stays at the default on every program until the listening pass.
