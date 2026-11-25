# Plugin architecture (proposed)

> Status: **Draft.** Everything here is design judgement drawn from the research, not a sourced finding. Decisions that are settled enough to record are in [`decisions/`](decisions/).

Build the plugin as a **modelled converter-and-EQ chain wrapped around an original delay-and-all-pass reverb engine**, tuned to the published and measured behaviour, with a switch between a 1985 and a 1987 "era".

## 1. Signal flow

```mermaid
flowchart LR
  IN[Stereo in] --> SPLIT{ }
  SPLIT -->|dry, stereo| MIX
  SPLIT --> MONO[Mono sum\n'authentic' default\nor true-stereo option]
  MONO --> AEQ[Wet-only 3-band\nanalogue EQ model]
  AEQ --> CONV_IN[Converter in:\nAA LPF, pre-emphasis,\nSRC to native rate,\ndither (Era 5), 16-bit quantiser,\nDC offset]
  CONV_IN --> ENG[Engine @ native rate:\nER multitap → diffusion →\nlate network (delays, APFs, LPFs)\n+ modulation + secondary reverb]
  ENG --> DEQ[Digital per-program EQ\n(Era 5)]
  DEQ --> CONV_OUT[Converter out:\n16-bit, SRC to host rate,\nde-emphasis,\nrecon LPF]
  CONV_OUT --> MIX[Mix 1:1 direct/wet,\nbypass, out-phase R]
  MIX --> OUT[Stereo out]
```

1. Stereo in, then mono sum for the wet path only; dry stays stereo.
2. Wet-only 3-band EQ **before** the converter model (as both machines do), so boosts feed the noise and clipping behaviour.
3. Converter model: low-pass at 12 kHz (Era 7) or 20 kHz (Era 5), pre-emphasis, sample-rate conversion to the native rate, 16-bit quantisation (plus dither in the Era 5), de-emphasis. (An aperture-droop stage was planned and modelled until 2026-11-24; reference recordings of both units showed no droop, so it is off.)
4. Reverb engine at the native rate: early reflections, diffusion, and a late network built from delays, all-pass filters and low-pass filters.
5. Mix stage with a 1:1 direct/wet relationship, bypass and out-phase.

## 2. Era switch (the main character control)

| Setting | Era 7 (REV7-like, 1985) | Era 5 (REV5-like, 1987) |
| --- | --- | --- |
| Native rate | 31.25 kHz | 44.1 kHz |
| Wet bandwidth | 12 kHz (−3 dB 12.2 kHz, ≥ 50 dB down 15.5 kHz) | 20 kHz (≥ 10 dB down 22 kHz) |
| Word length | 16-bit linear, 24-bit internal | 16-bit, 24-bit internal |
| Dither | none | on (level to measure), switchable |
| Emphasis order | LPF → emphasis | emphasis → LPF |
| Dynamic range target | about 78 dB reverb, 84 dB delay | same (confirmed) |
| Reverb time | 0.3–10 s | 0.3–99 s + secondary reverb |
| LFO range | 0.1–20 Hz | 0.1–40 Hz |
| EQ | analogue only | analogue + per-program digital |
| ER count | 18 per channel | up to 34, with ER feedback |

Running the engine at the native rate and resampling at the edges is the faithful route ([ADR 0001](decisions/0001-native-rate-engine.md)). A cheaper alternative runs at host rate and emulates only bandwidth and noise; offer it as a CPU-saving mode.

## 3. Modes, in priority order

1. Hall, plate and room reverbs (REV type).
2. Early-reflection patterns, including reverse and gated.
3. Gate and reverse gate after the reverb (REV5 combination style).
4. Echo and delay, with High Damp in the feedback loop.
5. Symphonic, chorus, flange and reverb-flange (three-voice 0°/120°/240° structure).
6. Pitch shift and pan (REV5 only) last, if at all.

## 4. Engine starting point

- **Early reflections:** grouped multitap read from one delay line with per-tap gains, a room-size coefficient scaling all tap times (US 4,803,731), initialised from [`data/er-taps/rev7/rev7_er_taps_size4.csv`](../../data/er-taps/rev7/rev7_er_taps_size4.csv), plus the mono centre 1st-reflection tap.
- **Late network, first model:** three parallel combs → three series all-passes with a band-pass in the loop (US 4,570,523 / 4,731,835), then iterate towards a better-diffusing structure if measurements demand it ([ADR 0003](decisions/0003-own-late-network.md)).
- **Frequency-dependent decay:** Hi ratio 0.1–1.0 and Lo ratio 0.1–2.4 against a 1 kHz reference RT.
- **Secondary reverb:** a second network fed from the first, time ×0.1–10, delay 0–500 ms, level 0–100 %.
- **Modulation:** 8-bit-quantised LFO into delay reads as a starting point; "space modulation" on the tail in the Era 5.

## 5. Character options

- **Bit depth and noise:** quantisation and residual-noise model with on/off and depth. Make noise interact with reverb time, since reviewers noticed it on long REV7 decays.
- **Pre/de-emphasis** (aperture droop dropped, see item 3 above): a fixed filter pair around the quantiser (zero ≈ 3.1 kHz, pole ≈ 10.3 kHz; computed net residual ≤ 0.17 dB). Audition with and without.
- **True stereo:** the originals sum to mono before the effect (MONO uses the left input only, STEREO sums L+R). Default is the authentic L+R sum; true stereo is a third, plugin-only option on the same legend ([ADR 0002](decisions/0002-mono-wet-sum.md)).
- **Program EQ:** the REV7's EQ is not stored with programs; storing it is an obvious modern improvement.
- **DC offset:** a small fixed offset on the wet path, audible when the R wet phase is inverted.

## 6. Presets and hidden values

Both units hide parameters. Decide which of ours to hide so presets keep their identity, and give each preset its own hidden values. Recreate the "preset per patch" workflow with program recall, and add host automation, which neither unit offered in real time.

## 7. Getting it right

- **Measure if possible.** IRs at several RTs, Hi/Lo ratios and each ER mode give tap spacing, decay-versus-frequency and echo-density growth: the quickest route to the "invisible" behaviour.
- **Listen as well as measure.** The reported "grunge" may live in truncation and low-level limit cycles an IR will not show.
- Keep it "inspired by" ([ADR 0004](decisions/0004-inspired-by.md), [legal.md](../legal.md)).

FX slot (delay, echo, modulation, pitch, pan): see [fx-slot.md](fx-slot.md).
