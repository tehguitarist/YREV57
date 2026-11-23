# 0001 · Run the engine at the hardware's native rate

- Status: Proposed
- Date: 2026-11-17

## Context
The REV7 runs at 31.25 kHz (4 MHz ÷ 128) and the REV5 at 44.1 kHz (5.6448 MHz ÷ 128). Band-limiting, aliasing and the spacing of delay taps in whole samples are part of each unit's sound (the service manuals). US 4,803,731 holds tap delays as whole sample periods.

## Decision
Run the reverb engine at the era's native rate and resample at the edges (host → native → host). Offer a host-rate "eco" mode that emulates only the bandwidth and noise.

## Consequences
- Faithful tap quantisation and filter behaviour; tap tables can be stored in samples.
- Needs two high-quality resamplers per instance and adds latency (report it to the host).
- The 31.25 kHz rate is not an integer ratio of common host rates; use a polyphase or windowed-sinc SRC, and test aliasing near 15.6 kHz.

## Update (2026-11-18): implemented, and how latency is handled
`src/dsp/Converter.*` runs the engine at 31.25 kHz (era 7) or 44.1 kHz (era 5) behind a streaming Kaiser-windowed sinc bridge (24 zero crossings, pass band 0.97 of the lower Nyquist since the twenty-seventh pass, was 16 / 0.95; in era 5 its up kernel also plays the D/A's sample-and-hold droop). Below 88.2 kHz host rates the converter's analogue stages run at twice the host rate (95-tap half-band in and out) so the bilinear transform keeps the measured 12.2 kHz cliff in place. The wet-path delay is about 2–3 ms.

We **do not report it to the host**. Reporting it would make the host pull the whole output earlier, so the dry signal, which is not delayed, would be early; delaying the dry to match would add latency to an insert for no audible gain. Instead the converter takes its own delay off the pre-delay and the 1st-reflection delay, so reflections arrive at the panel times (tested to ±1 ms), and the wet path is never earlier than the hardware's own converter delay. Pre-delays shorter than the bridge delay bottom out at the bridge delay. The host-rate "eco" mode is not built yet.
