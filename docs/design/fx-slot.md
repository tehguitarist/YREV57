# FX slot

Code: `src/dsp/FxSlot.h/.cpp` (standalone; the chain wires it in). Tests: `tests/FxSlotTests.cpp`. Parameter names are in [parameters.md](parameters.md). **Source** marks what the owner's manuals state; **inference** marks our own choices. The structures are our own design, not firmware behaviour.

**Place in the chain** ([`Converter.cpp`](../../src/dsp/Converter.cpp)): the slot runs at the era's native rate (31.25 / 44.1 kHz) inside the converter model, between the A/D and the D/A, beside the engine, because on the hardware the effects ran in the same DSP as the reverb. Repeat times lose the bridge delay as the pre-delay does, so they keep panel timing. **Initial delay** offsets the effect from the direct sound (REV5 OM p. 14; REV7 pp. 14-16). **1st reflections** stay at full level beside REV MIX, which scales only the reverb, so effect-only programs keep them.

**Routing** (the REV5 combination block diagrams, OM printed pp. 37-39): the hardware runs the effect through the reverb **and around it**. `fx_order` FX > Rev: wet = the effect's own part + `rev_mix` × reverb (effect); Rev > FX: wet = effect (dry + `rev_mix` × reverb). The own part is the slot's output less the dry it carries (`FxSlot::dryCarried`: 1 for delay and echo, so only their repeats, since the plugin's MIX supplies the dry; 0 for every other type). `rev_mix` is the REV5's REV MIX ("the amount of reverb mixed into the effect signal", p. 39). `fx_wet` (plugin key, not on the hardware) removes the bypass: FX > Rev = reverb (effect), plus delay or echo repeats; Rev > FX = effect (reverb). Both Reverb Flange programs use it. With FX off the wet is the reverb.

Stereo in place. Off is a bit-exact pass-through. Changing `fx_type` or `mod_type` fades the old effect out over 10 ms, clears it and fades the new one in over 10 ms (the REV7 also mutes briefly on a program change, owner's manual). The slot's own `fx_lpf` is a one-pole low-pass on the output (thru at 17 kHz); `mod_lpf` adds a second one for the mod types.

Mono sum (chorus, symphonic, phasing): (L + R) / 2 before the reverb, where the wet input is mono, and (L + R) × 0.707 after it (Rev > FX), where the reverb's channels are uncorrelated. FX > Rev adds the effect's own full-level signal beside the reverb, as the hardware combinations do.

Engineering: Hermite fractional delay reads, 20 ms smoothing on every delay time, level, depth and feedback, per-sample processing so output is identical for any block size, buffers sized in `prepare`, denormals flushed in the delay writes and filters.

| Type | Structure | Basis |
| --- | --- | --- |
| Delay | L and R lines, each fed back to its own input (REV5 p. 25 text and diagram). Era 7: no feedback, one level (REV7 p. 14), 0.1–900 ms. Era 5: 0.1–2,900 ms, Lch/Rch feedback −99 to +99 % (`dly_fb`, `dly_fb_r`), HIGH ×0.1–1.0 (`dly_high`). Out = in + delayed × `dly_level` | Source: structure, ranges. HIGH as a one-pole low-pass in the loop ([`LoopHighCut.h`](../../src/dsp/LoopHighCut.h)), so each repeat is darker than the last: inference |
| Echo | As delay with HIGH DAMP. Era 7: 0.1–450 ms, one feedback 0–99 % for both sides, HIGH 0–10 in whole steps (`echo_damp`; ROM chart "HIGH x0 ~ x10"): the repeat's treble ratio = value / 10. Era 5: 0.1–1,400 ms, Lch/Rch feedback −99 to +99 % (`echo_fb`, `echo_fb_r`), HIGH ×0.1–1.0. Own-channel feedback (REV5 p. 26 diagram) | Source: REV7 p. 14 ("a low-pass filter in the feedback loop"), REV5 p. 26 |
| Chorus A/B | Mono sum into one line; three taps (centre, left, right) at LFO phases 0, 120 and 240 degrees, base 4 ms, delay swing ±4 ms × `dm_depth`, gain 1 − `am_depth` × (0.5 + 0.5 sin). L = 0.707 (centre + left), R = 0.707 (centre + right): no direct signal | Source: three voices ("the input is split into three signals"), phases, base delay, depth scale. A and B differ only by factory presets |
| Symphonic | Three voices (centre, left, right) off one short line, each swept by a fast and a slow LFO; equal voices at 0.707, no AM, no direct signal. Same in both eras | Source: "based on the same modulation system as the Chorus preset", delay modulation "more rapid and deeper" (REV7 OM p. 19). Values: ours, by ear and against reference recordings |
| Tremolo | Amplitude modulation, L at 120 and R at 240 degrees, depth `mod_depth`; with Trem. DM on, also a short swept delay as the chorus | Source: chorus structure with AM emphasised (REV7 OM p. 19), 0.1 to 40 Hz |
| Flange | Per-channel lines with feedback `mod_fb`; L delay = base + swing × sin, R = base − swing × sin; out = 0.707 (in + delayed). Base `mod_delay` 0.1 to 100 ms (reverb flange 30 ms), swing limited so the delay never crosses zero | Source: L lengthens while R shortens, feedback, ranges. Swing limit: inference |
| Reverb flange | Flange with feedback fixed at 30 %, then a 37 ms feedback comb per channel whose gain gives an RT60 of `mod_rt` (0.3 to 10 s) | Source: 30 % fixed, `mod_rt` replaces feedback. Comb: inference. The factory Reverb Flange programs instead use the reverb engine with the plain flange before it; the type stays for hand-picked use |
| Phasing | Fixed L and R delays (right = `mod_delay` 0.1 to 8 ms, era 5 5 ms, REV5 OM p. 31; left 0.9 of it), centre tap swept from 0 to the right delay by `mod_depth`; no feedback | Source: structure. Left/right ratio: inference |
| Pitch (era 5) | Delay-line shifter (two taps crossfaded with a raised-cosine window) per voice, then a delay. A: one voice with `pitch_fb` back into the shifter. B: two voices, both centre. C: `pitch_*` left, `pitch2_*` right, delays 0.1–200 ms. Mono input; the shifted voices only | Source: REV5 pp. 32–36 (voices, placement, feedback on A only). Algorithm and window: inference. MIDI base key not modelled |
| Pan (era 5) | Mono sum, linear gains (L = 1 − position, R = position), 3 ms smoothing. L>R and R>L ramp once per cycle; L <> R sweeps left to right and back (REV5 OM p. 36); `pan_depth` scales the sweep | Source: direction names, rate and depth ranges. Pan law: inference |

LFO: sine for every type by default (the real waveform is unknown); the rate is clamped to 0.1 to 20 Hz (era 7) or 0.1 to 40 Hz (era 5). The phase restarts when the type changes.

Era limits are applied inside the slot as well as by the processor. Pitch and pan run in any era inside the slot; the processor keeps them to era 5.

**Panel:** delay shows Delay L, Delay R, Level, FX LPF, then Feedback L, Feedback R and High (era 5); echo shows Echo L, Echo R, Level, then Feedback, High damp and Feedback R (era 5); pitch shows Shift, Fine, Delay, Feedback, then Shift 2, Fine 2, Delay 2. Knobs the selected type does not use are greyed out.

**LFO sync:** `fx_sync` with a host tempo runs the mod LFO at the nearest note value to `lfo_rate`, and snaps delay and echo times to the nearest note value. `fx_link` makes the right time follow the left for delay and echo. Plugin features, not on the hardware.

`FxSlot::tailSeconds (params)` returns the time for the repeats to fall by 60 dB (capped at 60 s; reverb flange returns `mod_rt`; short mod types return 0.1 s; off and pan return 0).

## FX period options

Settings has an **FX period** row with four toggles, each a global setting (state properties, not parameters). They are best guesses at how a mid-1980s effect chip behaved:

| Toggle | Default | What it does | Reasoning (inference unless cited) |
| --- | --- | --- | --- |
| Stepped | off | Swept delays take 256 positions across the swing, no interpolation across a step | The YM3807 hands the DSP 8-bit MOD words *(schematic read)*; at ±4 ms and 31.25 kHz a step is about one sample, so slow sweeps tick rather than glide |
| Linear | off | Every fractional delay read in the slot is linear instead of cubic Hermite | Two-tap linear interpolation is the cheaper read a 1985 DSP might have used |
| Triangle | off | Modulation LFO is a triangle with the sine's phase | A triangle is a counter run up and down: no sine table needed |
| Trem. DM | on | Tremolo is built like the chorus: each side passes a short delay swept by the same LFO, under the AM | REV7 owner's manual p. 19: tremolo is "produced in virtually the same manner as the Chorus preset, with the amplitude modulation emphasized" |

**GATE LVL.** The REV5 manual describes GATE LVL on every non-gate program as "the program's internal noise gate", set so "noise is eliminated when no source signal is present" (pp. 22, 27–29, 34, 37). On the output it would cut every echo and reverb tail, so it is modelled on the program input (inference), ahead of the FX slot and the reverb. Every printed GATE LVL is 0 %, so no preset sound changes.
