#include "Engine.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <functional>

#include "ErTables.h"
#include "LoopHighCut.h"

namespace yrev::dsp
{
    namespace
    {
        // ---- Config (all times in ms so the engine can run at any rate) ----
        // Loop lengths: about 45 to 88 ms, spaced roughly geometrically (x1.1 per step), rounded up to primes.
        // Their sum (~0.52 s) covers Schroeder's mode-density bound to about 3.4 s RT; longer decays lean on the
        // in-loop all-passes for density. Our own values.
        constexpr float kLoopMs[Engine::kNumLoops] = {45.1f, 49.7f, 54.6f, 60.1f, 66.1f, 72.7f, 80.0f, 88.0f};
        // Two all-passes inside each loop (short, then long) multiply the echoes on every pass, so density builds
        // within the first few loop trips instead of growing linearly with time. Kept short (2.5-9.7 ms): an all-pass
        // ripples its group delay with a period of 1/M, and longer ones (the first set ran to 16 ms) left the 125 Hz
        // octave on one or two ripples per loop, so the bass decay depended on where it fell (second tuning pass).
        constexpr float kLoopApMs[2][Engine::kNumLoops] = {{2.8f, 4.4f, 3.2f, 5.2f, 2.5f, 4.0f, 3.5f, 4.7f},
                                                           {7.1f, 8.2f, 6.2f, 9.1f, 7.6f, 5.5f, 8.6f, 9.7f}};
        // Input diffusion: four series all-passes, 7.8-30 ms, so the late network builds up over the first 100-150 ms
        // instead of starting at full level (second tuning pass, for EDT; the first pass used 3.1-12.1 ms).
        constexpr float kDiffuserMs[4] = {7.75f, 11.75f, 19.75f, 30.25f};
        // Each loop's diffuser is scaled by its own factor (loop 0 plays the lengths above), so the eight do not echo
        // in step. Ours.
        constexpr double kDiffuserScale[Engine::kNumLoops] = {1.0, 1.071, 0.937, 1.137, 0.889, 1.193, 0.967, 1.051};
        constexpr double kDiffuserScaleMax = 1.193;
        // Hidden per-preset voice ranges (Parameters.cpp keeps the same limits); the lines are sized for the maxima.
        constexpr double kVoiceLoopMin = 0.7, kVoiceLoopMax = 1.4, kVoiceDiffMin = 0.6, kVoiceDiffMax = 1.5;
        constexpr float kVoiceLoXoverMin = 100.0f, kVoiceLoXoverMax = 800.0f, kVoiceErGainMaxDb = 6.0f;
        constexpr float kVoiceHiXoverMin = 4000.0f, kVoiceHiXoverMax = 14000.0f;
        // Late onset (twenty-sixth pass, ours): the late network hears the pre-delayed input and the reflection feed
        // this much later, so a program's late field can start after its early taps. The REV5 plate captures fall
        // silent between their first taps and about 40 ms; the halls' late field starts about 10 ms after ours.
        constexpr float kVoiceLateOnsetMaxMs = 60.0f;
        constexpr float kVoiceRev2GainMaxDb = 12.0f;
        // Voice tilts (thirty-fifth pass, ours): a high shelf on the audible reflections and one on the late tail, for
        // a preset whose early field and late field want opposite treble (CLA Drum Room: the capture's reflections
        // about 4 dB brighter at 8 kHz than ours, its late field about 2.3 dB darker). The reflections the late network
        // hears are a separate feed, so each tilt moves only its own group. The reflection shelf sits at 8 kHz so it
        // lifts the top octave without the band around 1 kHz (at 5 kHz it read the early bass 2 dB high re 1 kHz); the
        // late one at 5 kHz.
        constexpr float kVoiceTiltMaxDb = 12.0f, kErTiltHz = 8000.0f, kLateTiltHz = 5000.0f, kVoiceTiltQ = 0.707f;
        // Era-5 reflection top octave (twenty-seventh pass): about half the REV5 E/R capture taps carry a comb in the
        // top octave (a notch at 17.5-19.5 kHz), the rest are clean; the converter is fitted on the clean taps and the
        // reflections get the comb's average (clean minus all: -0.8 / -2.8 / -4.9 dB at 16 / 18 / 20 kHz, within 0.2 dB
        // below 14 kHz) as a second-order low-pass at the converter's erTopHz, Q 0.5 (0.65 dB rms over 8-20 kHz at the
        // 44.1 kHz native rate). Ours (inference: the comb is in the hardware's taps, its cause unknown).
        constexpr float kErTopQ = 0.5f, kErTopMaxFrac = 0.48f;
        // Late feed signs (bass smoothness, gap 12, ninth pass in docs/design/engine.md). Every loop hears every
        // reflection, but loop g takes reflection k with the sign of Hadamard row kFeedRow[g] at column
        // (k + kFeedColR x ch) mod 8 (ch 0 left, 1 right). Fed one summed cluster, all eight loops had the same comb in
        // their input: a fixed bass shape in every render (+6 dB from 160 to 200 Hz, a hole at 63-100 Hz; band-to-band
        // 2.3 dB against the captures' 1.2). With orthogonal signs each loop hears a different comb and their powers
        // add, so the bass smooths out while echo density and input energy are unchanged. Rows go to the loops in
        // bit-reversed order: in natural order they line up with the read-out signs (kSignL / kSignR are rows 2 and 1),
        // and the first pass through the loops then skews the late L/R correlation (-0.2 or +0.2 at 500 Hz). Order and
        // column offset chosen on the matched set from five orders and offsets 1 and 3. Loop 0 keeps row 0, the old
        // summed feed. Ours.
        constexpr int kFeedRow[Engine::kNumLoops] = {0, 4, 2, 6, 1, 5, 3, 7};
        constexpr int kFeedColR = 3;
        static_assert (Engine::kNumLoops == 8, "the feed signs are Hadamard rows of order 8");
        // Orthogonal read-out signs (two Hadamard rows): uncorrelated L and R from one network (learnings item 7).
        constexpr float kSignL[Engine::kNumLoops] = {1, 1, -1, -1, 1, 1, -1, -1};
        constexpr float kSignR[Engine::kNumLoops] = {1, -1, 1, -1, 1, -1, 1, -1};
        // Shelf crossovers for lo_ratio and hi_ratio. No source gives the REV7/REV5 crossover points (Q-27); fitted to
        // 19 REV7/REV5 captures (measurement of the captures; inference that the hardware damps this way).
        // Low: 300 Hz, from the octave RT shape (6.0 % rms band-ratio
        // error at 400 Hz, 3.0 % at 300 Hz). High: the captures' treble damping keeps growing through the band (third-
        // octave RT ratios 2-10 kHz REV7, 2-16 kHz REV5), which a gentle shelf high up fits best: Q 0.5 at 8.5 kHz,
        // 5.2 % rms, against 9.9 % for Q 0.71 at 4.5 kHz.
        constexpr float kShelfQ = 0.7071f; // the low shelf's corner is the voice's loXoverHz (default 300 Hz)
        constexpr float kHiShelfQ = 0.5f;  // the high shelf's corner is the voice's hiXoverHz (default 8.5 kHz)
        constexpr float kHiShelfDefaultHz = 8500.0f;
        // hi_ratio is the ratio of the 8 kHz octave to 1 kHz (what tests and captures measure), not the shelf's
        // plateau: the plateau is set so the shelf gives hi_ratio at kHiRefHz (calibrated by measurement at the
        // default corner). The reference stands for where the dark tail weights the octave. With a lower corner the
        // whole octave is damped and the weight slides down, so the reference follows a corner below the default as
        // (corner / 8.5 kHz)^kHiRefFollow; above it the reference stays. Above the default the corner is held below
        // kHiShelfMaxFrac x fs (10 kHz at 31.25 kHz), where the shelf still reaches 7 kHz. Sweep of corners 4-14 kHz,
        // hi 0.2-0.8, at 31.25, 44.1 and 48 kHz: within 8 % rms, 2-5 % for most corners (measured, engine.md).
        constexpr double kHiRefHz = 7000.0, kHiRefFollow = 0.3, kHiShelfMaxFrac = 0.32;
        constexpr double kMidRefHz = 1000.0; // where the loops' broadband decay is held to rt (setLoopFilters)
        constexpr int kMidCalSteps = 4;
        constexpr double kMaxHiCutDb = -50.0; // deepest high-shelf cut per loop pass (setLoopFilters)
        constexpr double kMaxLoopGain = 0.9999;
        constexpr float kDensityGain[4] = {0.3f, 0.45f, 0.6f, 0.7f}; // in-loop all-pass gain for density 1..4
        // Decay law: an in-loop all-pass of length M and gain g counts as M (1 + kApHold g^2 / (1 - g^2)) of delay. It
        // holds energy longer than M (its slow modes dominate the late decay), but less than M / (1 - g^2): fitted so
        // the 1 kHz RT is centred for density 1-4 at 31.25, 44.1 and 48 kHz (measured).
        constexpr double kApHold = 0.3;
        // Below lo ratio 1 the shelf aims at rt x lo^kLoWarp: the in-loop all-passes pull a fast bass decay towards
        // the mid decay (125 Hz measured about lo^0.9 with a plain law), so the target is pre-warped. Above 1 the
        // plain law holds within a few per cent (measured).
        constexpr double kLoWarp = 1.1;
        // diffuser all-pass gain: 0.8 * (diffusion / 10)^0.68, so 5 -> 0.5 (tuned) and 10 -> 0.8 (stays well below 1)
        constexpr float kDiffusionMax = 0.8f, kDiffusionCurve = 0.68f;
        // Each diffuser all-pass's own echo tail decays at least kDiffuserSpeedup times as fast as the fastest band of
        // the reverb (g <= 10^(-3 kDiffuserSpeedup M / rt_min)), so its smear never holds up a short decay. Bites below
        // about 0.6 s at the default diffusion, below about 2 s at diffusion 10. Ours (gap 11, docs/design/engine.md).
        constexpr double kDiffuserSpeedup = 2.0;
        constexpr float kRefRt = 2.0f; // late level is energy-normalised to a 2 s decay; shorter ones get quieter
        // Late and reflection levels and the diffusion/density gains, tuned against the REV7/REV5 captures
        // (EDT 1.05-1.5 x RT, echo density 0.9 by 110-175 ms on the
        // halls). Second pass: EDT about 1.1 x RT and density 0.9 by about 145 ms on the default hall (engine.md).
        constexpr float kLateLevel = 0.65f;
        // Per-channel reflection cluster, energy-normalised. Fitted so the share of energy in the first 80 ms matches
        // the captures (median -4.9 / -4.7 dB of the total, REV7 / REV5, the REV7 captures' non-hardware onset click left
        // out). Seventh pass: 0.26 for both eras. Ninth pass, after the REV5 charts (their clusters spread later) and
        // the per-loop feed signs (kFeedRow): 0.27 / 0.255, then era 7 0.28 after the REV7 size-4.0 charts were re-read
        // (their right channel had been misread 5-19 dB low): -4.94 / -4.71 dB against -4.9 / -4.72, C80 -3.26 /
        // -2.91 against -2.85 / -2.93 dB. The reflection count was no lever (6-16 reflections: -5.7 to -5.2
        // dB). Fifteenth pass: era 5 0.29 after its 9 kHz reflection bandwidth. Seventeenth pass: era 7 0.34 after its
        // 7.5 kHz reflection bandwidth and 5.5 kHz late bandwidth (C80 -3.31 against -3.14 dB, first 80 ms -4.97
        // against -4.89 dB, REV7 onset click left out).
        // Nineteenth pass (loop mixing, factory space mod 0, darker late bandwidths): 0.33 / 0.275 (C80 -3.05 / -2.84
        // against -3.14 / -2.93 dB, first 80 ms -4.80 / -4.66 against -4.89 / -4.72 dB).
        // Twentieth pass (REV5 tables anchored on each channel's first tap): era 5 0.29 (C80 -3.03 against -2.93 dB,
        // first 80 ms -4.78 against -4.72 dB).
        // Twenty-first pass (era-5 bandwidths off, program LPF a one-pole, each matched REV5 job at its fitted LPF): era
        // 5 0.31 (C80 -2.90 against -2.93 dB, first 80 ms -4.70 against -4.72 dB).
        // Twenty-second pass (era-5 reverb programs on their own taps, Rev Hall / Rev Plate; late taps 0.15 / 0.9): era
        // 5 0.24 (C80 -3.06 against -2.93 dB, first 80 ms -4.81 against -4.72 dB; guardrail 0.72 -> 0.64).
        constexpr float kErLevel7 = 0.33f, kErLevel5 = 0.24f;
        // What the late network hears: mostly the reflection clusters (each taken at unit energy), a little of the
        // pre-delayed input. Its energy builds up over the reflection span, as a room's does, rather than all at the
        // pre-delay; input energy stays that of the first pass (0.5 direct + 0.25 x reflections). Ours (inference).
        constexpr float kLateDirect = 0.167f, kLateFromEr = 0.333f;
        // True stereo (ADR 0006, ours): each side's reflections read mid +/- k side, i.e. 0.75 own side + 0.25 other.
        constexpr float kTrueStereoSpread = 0.5f;
        // The reflections feeding the late network fade at kLateFeedFade x 60 dB per rt across the cluster (then the
        // feed is put back to its unfaded energy), so a short decay is not held up by input still arriving over a long
        // reflection span: in a room the reflections themselves die away at the room's rate. At 2.6 s the last tap of
        // a room-size-2 cluster loses about 12 dB, so the bloom stays. Ours (gap 11, docs/design/engine.md).
        constexpr float kLateFeedFade = 2.0f;
        // Each loop is also read part of the way along its delay (total output energy kept), so the late field starts
        // speaking earlier and its echo density builds up gradually instead of arriving all at once a full loop after
        // its input. Per era ([0] era 7, [1] era 5), fitted on the matched captures (refitted jobs, masked; tenth and
        // thirteenth passes, docs/design/engine.md): era 5 a quarter loop at weight 0.4 (echo density 0.9 at 91 ms
        // against 88); era 7, once its bass lift was in (the density measure is broadband), 0.18 of a loop at weight
        // 0.55 (99 ms against 99.5; ripple +0.15 dB). Nineteenth pass (loop mixing, factory space mod 0): era 5's
        // weight 0.4 -> 0.25 (89 ms against 88; EDT/RT 1.53 against 1.54); era 7 unchanged (97 ms). Twenty-second pass
        // (era-5 reverb programs on their own sparse taps): the captures' reverb is 6-9 dB stronger at 50-80 ms
        // relative to the first tap than ours, so era 5 reads its loops earlier and louder, 0.15 of a loop at weight
        // 0.9 (early kurtosis 127 -> 76 against 42, 50-100 ms opening colour 0.17 dB rms; bass ripple excess -0.1 dB).
        // Ours.
        constexpr double kLateTapFraction[2] = {0.18, 0.15}, kLateTapWeight[2] = {0.55, 0.9};
        // Loop mixing (nineteenth pass). The eight loops used to feed only themselves, so the late field's modes were
        // eight regular combs laid over each other, and below about 600 Hz the tail was far more modal than either
        // capture set (per-band ripple with the band's decay removed, 140-280 Hz: 14.2 / 13.4 dB against 11.2 / 6.6,
        // REV7 / REV5; decaying noise about 5.5). Each sample the loops' feedback now passes Householder reflections
        // (x - (2 / b) sum x) over interleaved blocks of kMixBlock loops (block g holds loops g, g + 8/b, ...): an
        // orthogonal matrix, so the decay law (Jot's rule per loop) and stability hold, and nothing is time-variant.
        // Era 7 mixes only above kMixAboveHz: the feedback is split by an all-pass complementary pair (low = (x + Ax)
        // / 2, high = (x - Ax) / 2, A a first-order all-pass), so low - high = Ax and the mixing stays lossless at
        // every frequency, while the bass keeps the REV7 captures' resolved modes. Fitted at the factory space mod 0 on
        // the refitted matched jobs: bass ripple excess 0.0 / -0.4 dB (was +3.7 / +7.9 at mod 0, +2.2 / +6.0 at mod 2);
        // it also restores the mid texture that space mod 2 used to provide. Era 7 blocks of 4 above 400 Hz, era 5
        // blocks of 4 at every frequency. Ours (docs/design/engine.md, nineteenth pass).
        constexpr int kMixBlock[2] = {4, 4};
        constexpr double kMixAboveHz[2] = {400.0, 0.0}; // 0 = mix at every frequency
        static_assert (Engine::kNumLoops % 4 == 0, "mixing blocks of 4 interleave the loops");
        // REV7: initial delay and 1st reflection 0.1-100 ms; REV5: 0.1-1,000 ms (owner's manual p. 14)
        constexpr float kMaxPreDelayMs = 1000.0f, kMaxRef1Ms = 1000.0f;
        constexpr float kMaxRoomSize = 25.0f, kChartRoomSize = 4.0f;
        constexpr float kLivenessDbPer100Ms =
            6.5f; // extra fade per 100 ms of reflection time per step below liveness 5
        // (a third-party SPX90 model, measured 2026-11-22: 6.5 dB per 100 ms per step from the first tap at liveness 0
        // and 2; was 3, a guess; the manuals give only "the rate at which the reflected sounds fade")
        constexpr float kChartFade5 = 0.68f;   // era 5: share of the printed chart's level fade played (below)
        constexpr int kLastFallingPattern = 2; // patterns 0-2 (S-Hall, L-Hall, Random) fall; 3-5 rise or arch
        constexpr float kMinRt = 0.1f, kMaxRt = 99.0f;
        constexpr double kTwoPi = 6.283185307179586;
        constexpr float kHalfPi = 1.5707963f;
        // Switching: reflection, pre-delay and 1st-reflection changes crossfade (equal power) over this time.
        constexpr double kLayoutFadeMs = 30.0;
        // Space modulation (inference, Q-06: the REV5 manual gives the 0-10 control but no rate or depth). Each loop's
        // delay swings sinusoidally by up to +/- kSpaceModMaxMs at 10, at its own slow rate and starting phase, so the
        // modes drift apart and the tail loses its metallic ring without an obvious pitch wobble (4 to 11 cents peak at
        // 10). Rates are not multiples of each other; phases start fixed at reset so renders repeat.
        constexpr double kSpaceModMaxMs = 0.8;
        constexpr double kSpaceModHz[Engine::kNumLoops] = {0.43, 0.61, 0.53, 0.79, 0.67, 0.97, 1.13, 1.29};
        constexpr double kSpaceModSmoothMs = 50.0; // depth glide, so knob moves do not jump the delays
        // Late-tail bass coherence. The orthogonal read-out leaves L and R uncorrelated at every frequency; the
        // REV7/REV5 captures' tails are mildly in phase in the bass (L/R correlation at 125 Hz about +0.1 /
        // +0.25, near 0 above 500 Hz), as a real room's are.
        // Below kTailCohHz the tail's mid is raised by sqrt(1 + rho) and its side lowered by sqrt(1 - rho): correlation
        // rho there, same energy. Ours (inference for why the hardware does it). Twenty-fourth pass: the split is
        // all-pass complementary, so it never pushes a band out of phase past the corner (a low-pass add-back read
        // -0.10 at 500 Hz against the captures' +0.06 / +0.12). Twenty-ninth pass: the first-order split at 300 Hz
        // still passed 59 % of rho at 250 Hz, which read +0.25 late at 250 Hz in era 7 and +0.20 in era 5 against the
        // captures' -0.04 to +0.04 (with the stage off ours reads the captures' value there). Now third order:
        // low = (A1 + A2) / 2, high = (A2 - A1) / 2, A1 a first-order and A2 a second-order all-pass from the analog
        // Butterworth pair (1 - s) / (1 + s) and (s^2 - s + 1) / (s^2 + s + 1); |low|^2 + |high|^2 = 1 and their
        // cross term is 0, so mid and side keep their energy, and |low|^2 = 1 / (1 + (f / fc)^6). Fitted per era on the
        // matched set (scratch sweep of corner 150 / 200 Hz, rho 0.25-0.45 per era, and a broadband rho 0.06-0.1 above
        // the corner): late correlation error at 125 / 250 / 500 Hz (160-1280 ms, rms against the captures) REV7 / REV5
        // 0.19 / 0.15 -> 0.11 / 0.10; 250 Hz late +0.25 / +0.23 -> +0.07 / +0.09. Guardrail REV7 1.04 unchanged, REV5
        // 0.62 -> 0.68 (500 Hz late correlation, which the captures hold at about +0.1 and the gentle split used to
        // supply, and the bass band-to-band level). A broadband rho fixed 500 Hz but brought 250 Hz back; not adopted.
        constexpr double kTailCoherence[2] = {0.25, 0.35}, kTailCohHz[2] = {150.0, 150.0}; // REV7, REV5
        // Thirty-fourth pass: the captures also hold the late tail mildly in phase at 500 Hz-1 kHz (about +0.05 to
        // +0.12, between a near-0 250 Hz and the in-phase bass; ours read -0.05 to +0.03 after pass 29). Each channel
        // gets a second-order band-pass (constant 0 dB peak) copy of the other at kBandCoherence / 2: for uncorrelated
        // L and R the correlation is about rho |H|^2 and the band-pass's real part is |H|^2, never negative, so no band
        // goes out of phase, and the power added is (rho / 2)^2 |H|^2 (under 0.03 dB). The direct path is untouched: a
        // cascade of all-pass splits (the twenty-ninth pass's form, raised below 2 kHz and undone below 500 Hz) fitted
        // the correlation as well but smeared the late field's first spikes (echo density at 80 ms 0.63 -> 0.79 against
        // the captures' 0.42, REV7), and adding the band back to the dry signal leaked into 250 Hz. Fitted on the
        // matched set (late correlation error at 125 Hz-1 kHz, 160-1280 ms, rms against the captures, REV7 / REV5 0.103 /
        // 0.101 -> 0.094 / 0.080; docs/design/engine.md, pass 34).
        constexpr double kBandCoherence[2] = {0.10, 0.15}, kBandCohHz[2] = {900.0, 700.0},
                         kBandCohQ = 1.0; // REV7, REV5

        // ER feedback (REV5 p. 21): FB DLY 0.1-1,400 ms, FB GAIN 0-99 %, FB HIGH x0.1-1.0. A comb on the reflection
        // output, so the whole pattern repeats FB DLY after it starts; FB HIGH is an in-loop one-pole low-pass whose
        // gain at 5 kHz is HIGH, as the FX slot's HIGH (LoopHighCut.h; was a 3 kHz high shelf, inference).
        constexpr float kErFbMaxMs = 1400.0f, kErFbMaxGain = 0.99f;
        // Era-5 reflection diffusion (sixteenth pass, docs/design/engine.md). Both units' E/R programs have a DIFFUSION
        // control, "the complexity of the reflections", 0 the "clearer" (REV5 OM p. 21, REV7 OM p. 28), so the
        // hardware's reflections are not bare impulses at every setting. The REV5 captures' early arrivals
        // are slightly smeared (energy within +-0.15 ms of the strongest six over +-1.5 ms: 0.82; bare taps 0.99) with
        // a sparse floor between them (echo density 0.11-0.25 at 10-40 ms; bare taps 0.05-0.07). One short all-pass per
        // channel on the reflection output, gain kErDiffPerStep x diffusion (0.3 at the default 5), matches both
        // (0.81-0.82; 0.12-0.17) without touching anything later. The REV7 captures fit bare taps (0.90 against 0.82;
        // the all-pass takes early kurtosis 55 -> 33 against 69), so era 7 keeps them. Length and law ours
        // (inference); the reflection output is read kErDiffMs early so the all-pass's main arrival keeps the chart's
        // time.
        // Twenty-second pass: the gain depends on the program type. The REV5 reverb programs' early taps ring at about
        // -14 dB 0.3 ms after each tap (REV5 reverb captures; ours at 0.06 x 5: -13), the E/R programs' at -21
        // to -27 dB (twentieth pass; 0.024 x 5 fits), so the Rev modes keep the reverb fit and the charted modes take
        // the E/R fit. Measurement of the captures; that the hardware diffuses the two program types differently is
        // inference (the REV5 reverb figure, OM printed p. 16, puts DIFFUSION on the reverb).
        constexpr double kErDiffMs = 0.71;
        // REV7 E/R2 programs (twenty-fifth pass). The manual: E/R2 presets "contain more reflections than the E/R1
        // type, and give a brighter, more 'live' quality" (REV7 OM printed p. 17, OCR), and the gated programs are E/R
        // programs (ROM chart p. 27). The one REV7 gated capture is a dense field open for
        // the reflection span, then an exponential close (-0.43 dB/ms), where our E/R taps are sparse and stop dead:
        // the reflections passed through a decaying diffuser give both, the close being the diffuser's own decay. Four
        // series all-passes per channel, each gain set for a kEr2T60Ms decay (g = 10^(-3 M / T60)); the right channel's
        // lengths x kEr2RScale so the two sides do not ring alike. Lengths and law ours (inference); fitted on that
        // capture.
        // Fitted (scratch sweep of T60 60-140 ms, lengths x0.7-1.4, right x0.93-1.15) on the factory Gate Reverb
        // settings (`er2cmp.py`): the close's steepest 70 ms -0.47 dB/ms against the capture's -0.45, the 20-330 ms
        // envelope 7.3 dB rms against 15.1 for bare taps, 596 arrivals within 20 dB against 33 (capture 234). What is
        // left is the open section's slope (the capture rises, the charted Random taps fade), the library's edit.
        // REV5 E/R2 (twenty-eighth pass). The REV5 has the same two E/R types: its ROM chart prints Early Ref.2,
        // Electric Bass B, Reverse Gate and Live Reference as E/R2 and the others as E/R1 (REV5 OM sec. 10, printed
        // pp. 60-85, page images), and only the E/R2 programs carry DENSITY (1-2) and SPACE MOD. Three of the six REV5
        // E/R captures (REV5 *L-Hall*, *Pan-A 2*, *Spring*) decay smoothly past their last reflection, flat
        // across 250 Hz-8 kHz and uncorrelated L/R (T60 about 0.17 and 0.32 s); *Pan-A* and *Pan-A 2* share a mode,
        // one bare and one with the tail, so we read the tail as E/R2 (inference). The same diffuser fits: T60 200 ms
        // (scratch sweep 40-300 ms, lengths x0.7-1.4) takes the 0-500 ms envelope from 24.8 / 30.5 to 7.9 / 11.5 dB rms
        // on *L-Hall* / *Pan-A 2*; both channels at the left lengths keep the mono sum's bass as the bare taps (x1.15
        // cut 125 Hz by 9 dB; the captures' mono bass is level). *Spring* is not this (best 10.2 -> 9.1 at another
        // setting). DENSITY is not modelled: which value each capture used is unknown.
        constexpr float kEr2Ms[4] = {4.0f, 6.2f, 9.6f, 15.2f};
        constexpr float kEr2RScale[2] = {1.15f, 1.0f}; // REV7, REV5
        constexpr double kEr2T60Ms[2] = {80.0, 200.0};
        constexpr float kEr2RScaleMax = 1.15f;
        constexpr float kErDiffPerStep = 0.06f, kErDiffPerStepEr = 0.024f;
        // Secondary reverb (REV5 p. 17): time x0.1-10 of rt, delay 0-500 ms from the start of the primary
        // reverberation, level relative to it. Structure and lengths are ours: four loops, one all-pass each, prime
        // lengths between the primary loops' so the two tails do not share modes. RT2 is held to 0.1-99 s.
        constexpr float kLoop2Ms[Engine::kNumLoops2] = {39.7f, 47.3f, 57.1f, 69.9f};
        constexpr float kLoop2ApMs[Engine::kNumLoops2] = {6.1f, 8.3f, 5.3f, 7.1f};
        constexpr float kSign2L[Engine::kNumLoops2] = {1, 1, -1, -1};
        constexpr float kSign2R[Engine::kNumLoops2] = {1, -1, 1, -1};
        constexpr float kRev2MaxDelayMs = 500.0f;
        constexpr double kRev2ApGain = 0.5;
        // REV2 LVL 100 % plays the secondary at this share of the primary's amplitude (linear in %, inference). The
        // engine's late field was fitted on the REV5 reverb captures with the secondary off, while their factory
        // programs print REV2 x1.0 / 0 ms / 100 % (ROM chart, REV5 OM pp. 56-99): those captures fit best with the
        // secondary at 0-100 % of the primary, median 22 % over twelve (the nine matched REV5 reverbs and three with a
        // program LPF; at the old full share they read 3.9 dB too late-heavy in C80). Thirty-second pass, 2026-11-21.
        constexpr double kRev2LevelShare = 0.25;

        // er_mode choice index -> chart pattern, in the era's chart set (er::kPatterns REV7, er::kPatterns5 REV5).
        // Modes 0-5 are the charted ones. Era 5 Pan A/B (6, 7) play their own charts (er::kPanPatterns5, blended in
        // the layout code below). Era 7 modes 6/7 (the REV7 has no Pan charts) are placeholders. The instrument modes
        // (8-11, era 5 only, no charts, hidden in the UI) are mapped to charted stand-ins by the processor
        // (yrev::erModeStandIn); the entries here only matter if the engine is driven directly (Q-01).
        // Rev Hall, Rev Plate and Rev Room (12-14) play the reverb programs' own early taps: in era 5 the REV5's
        // (er::kRevPatterns5, twenty-second pass; Rev Room plays the sparse Rev Hall set), in era 7 the REV7's three
        // per channel (er::kRevPatterns7, twenty-third pass). The chart entries below are unused for them.
        constexpr int kModeToPattern[15] = {0, 1, 2, 3, 4, 5, 2, 3, 0, 1, 4, 5, 1, 4, 0};
        constexpr int kPanModeA = 6, kPanModeB = 7;                             // er_mode indices of Pan A and Pan B
        constexpr int kRevHallMode = 12, kRevPlateMode = 13, kRevRoomMode = 14; // reverb-program taps
        constexpr float kRevDenseFrom = 3.5f; // density 4 plays Rev Hall B (dense), 1-3 Rev Hall A
        // Era-7 Rev sets scale with the initial delay (2026-11-21, item 6): every tap sits at a fixed multiple of the
        // first. one REV7 capture (first tap 21.1 ms) plays the Vocal Plate set at x1.115 / 1.395 L and x1.150 / 1.349 R of
        // its first tap, a second plate capture (45.8 ms) at x1.110 / 1.396 and x1.146 / 1.350: within 0.5 % on all four taps
        // at less than half the delay; and the Hall and Room sets, read at 30.7 and 20.5 ms, share a third tap at
        // x2.61-2.62 of the first (measurement of the captures; that the hardware defines the taps this way is
        // inference). The tables hold the offsets as measured; they scale by the initial delay over the first-tap time
        // they were read at (Rev Hall, Rev Plate, Rev Room), so a render at those delays is unchanged.
        constexpr float kRevRefMs7[3] = {30.69f, 45.78f, 20.48f};
        static_assert (Engine::kMaxErTaps == er::kMaxTaps);

        int msToSamples (double ms, double fs)
        {
            return (int) std::lround (ms * 0.001 * fs);
        }

        bool isPrime (int n)
        {
            if (n < 2)
                return false;
            for (int d = 2; d * d <= n; ++d)
                if (n % d == 0)
                    return false;
            return true;
        }
        int nextPrime (int n)
        {
            while (!isPrime (n))
                ++n;
            return n;
        }

        // Loop gain for a decay of `rt` seconds over `samples` of delay (Jot / Schroeder: -60 dB per rt).
        double loopGain (double samples, double fs, double rt)
        {
            return std::pow (10.0, -3.0 * samples / (fs * rt));
        }
        // Effective delay of an in-loop all-pass for the decay law (see kApHold).
        double allPassHold (double length, double g)
        {
            return length * (1.0 + kApHold * g * g / (1.0 - g * g));
        }
        // Bass decay target (see kLoWarp).
        double bassRt (double rt, double loRatio)
        {
            const double lo = std::clamp (loRatio, 0.1, 2.4);
            return rt * (lo < 1.0 ? std::pow (lo, kLoWarp) : lo);
        }
        float gainToDb (double g)
        {
            return (float) (20.0 * std::log10 (std::max (g, 1.0e-12)));
        }
        // A loop's broadband gain and its two shelves. The shelves are set for the bass and treble decays relative to
        // the broadband gain; a deep high shelf (HIGH x0.1-0.2 asks -100 dB or more per pass at the plateau) still cuts
        // several dB per pass at 1 kHz, which shortened the main decay to 0.2-0.5 x rt (thirty-third pass, gap 11). So
        // the broadband gain is raised until the two shelves together pass exactly the rt decay at kMidRefHz (a few
        // fixed-point steps; the shelves are recomputed each step, so the bass and treble targets stay exact).
        template <typename Loop>
        void setLoopFilters (Loop& l, double total, double fs, double rt, double rtLo, double rtHi, float loHz,
                             float hiHz)
        {
            const double gMid = loopGain (total, fs, rt);
            // the treble cut per pass is held to kMaxHiCutDb: an 8 kHz decay shorter than about 0.15 s is below what
            // the input diffusers and one loop pass already take, and a deeper shelf only cuts into 1 kHz
            const double gLo = loopGain (total, fs, rtLo);
            const double gHi = std::max (loopGain (total, fs, rtHi), gMid * std::pow (10.0, kMaxHiCutDb / 20.0));
            double g = gMid;
            for (int k = 0; k <= kMidCalSteps; ++k)
            {
                l.gain = g;
                l.loShelf.setLowShelf (fs, loHz, kShelfQ, gainToDb (gLo / g));
                l.hiShelf.setHighShelf (fs, hiHz, kHiShelfQ, gainToDb (gHi / g));
                const double m = l.loShelf.magnitude (fs, kMidRefHz) * l.hiShelf.magnitude (fs, kMidRefHz);
                g = std::min (gMid / std::max (m, 1.0e-6), kMaxLoopGain);
            }
        }
    } // namespace

    void Engine::prepare (double sampleRate, int, double maxSampleRate)
    {
        // allocate for the highest rate this engine will run at, then set the lengths for the current one
        const double cap = std::max (sampleRate > 0.0 ? sampleRate : 48000.0, maxSampleRate);
        const double maxErMs =
            (double) er::kChartFirstTapMs + (double) er::kChartSpanMs * kMaxRoomSize / kChartRoomSize;
        input.prepare (msToSamples (kMaxPreDelayMs + maxErMs + kMaxRef1Ms + kVoiceLateOnsetMaxMs, cap) + 16);
        sideMemory.prepare (input.capacity ());
        for (auto& chain : diffuser)
            for (size_t i = 0; i < chain.size (); ++i)
                chain[i].prepare (
                    nextPrime (msToSamples ((float) (kDiffuserMs[i] * kDiffuserScaleMax * kVoiceDiffMax), cap)) + 4);
        for (int i = 0; i < kNumLoops; ++i)
        {
            auto& l = loops[(size_t) i];
            l.line.prepare (nextPrime (msToSamples ((float) (kLoopMs[i] * kVoiceLoopMax), cap)) +
                            msToSamples (kSpaceModMaxMs, cap) + 4);
            for (size_t a = 0; a < l.ap.size (); ++a)
                l.ap[a].prepare (nextPrime (msToSamples (kLoopApMs[a][i], cap)) + 4);
        }
        for (auto& l : erFbLine)
            l.prepare (msToSamples (kErFbMaxMs, cap) + 4);
        for (auto& a : erDiff)
            a.prepare (nextPrime (msToSamples (kErDiffMs, cap)) + 4);
        for (auto& ch : er2Diff)
            for (size_t k = 0; k < ch.size (); ++k)
                ch[k].prepare (nextPrime (msToSamples (kEr2Ms[k] * kEr2RScaleMax, cap)) + 4);
        rev2Pre.prepare (msToSamples (kRev2MaxDelayMs, cap) + 4);
        for (int i = 0; i < kNumLoops2; ++i)
        {
            loops2[(size_t) i].line.prepare (nextPrime (msToSamples (kLoop2Ms[i], cap)) + 4);
            loops2[(size_t) i].ap.prepare (nextPrime (msToSamples (kLoop2ApMs[i], cap)) + 4);
        }
        maxFs = cap;
        setSampleRate (sampleRate > 0.0 ? sampleRate : 48000.0);
    }

    void Engine::applyEr2 (int set)
    {
        // g = 10^(-3 M / T60) per all-pass; the lines were sized for the longer (REV7) right channel in prepare()
        er2Set = std::clamp (set, 0, 1);
        for (size_t c = 0; c < er2Diff.size (); ++c)
            for (size_t k = 0; k < er2Diff[c].size (); ++k)
            {
                const int len = nextPrime (msToSamples (kEr2Ms[k] * (c == 1 ? kEr2RScale[(size_t) er2Set] : 1.0f), fs));
                er2Diff[c][k].setLength (len);
                er2Diff[c][k].setGain (
                    (float) std::pow (10.0, -3.0 * (1000.0 * len / fs) / kEr2T60Ms[(size_t) er2Set]));
            }
    }

    void Engine::setSampleRate (double sampleRate)
    {
        // no allocation: every line was sized in prepare() for maxFs
        fs = std::clamp (sampleRate, 8000.0, maxFs);
        erTopHzSet = -1.0f; // redesign the reflection top-octave low-pass for the new rate
        tiltFresh = true;
        applyLengths ();
        for (int i = 0; i < kNumLoops; ++i)
        {
            auto& l = loops[(size_t) i];
            for (size_t a = 0; a < l.ap.size (); ++a)
                l.ap[a].setLength (nextPrime (msToSamples (kLoopApMs[a][i], fs)));
            const double w = kTwoPi * kSpaceModHz[i] / fs;
            l.rotC = std::cos (w);
            l.rotS = std::sin (w);
        }
        for (int i = 0; i < kNumLoops2; ++i)
        {
            auto& l = loops2[(size_t) i];
            l.len = nextPrime (msToSamples (kLoop2Ms[i], fs));
            l.ap.setLength (nextPrime (msToSamples (kLoop2ApMs[i], fs)));
            l.ap.setGain (kRev2ApGain);
        }
        erFbHiGain = -1.0f; // recompute the FB HIGH low-pass for the new rate
        erDiffLen = nextPrime (msToSamples (kErDiffMs, fs));
        for (auto& a : erDiff)
            a.setLength (erDiffLen);
        applyEr2 (er2Set < 0 ? 0 : er2Set);
        measureHiRef ();
        cohSet = -1; // coherence split redesigned for the new rate in update()
        fadeLen = std::max (1, msToSamples (kLayoutFadeMs, fs));
        modSmooth = 1.0 - std::exp (-1.0 / (kSpaceModSmoothMs * 0.001 * fs));
        dirty = true;
        reset ();
    }

    void Engine::reset ()
    {
        input.reset ();
        sideMemory.reset ();
        for (auto& chain : diffuser)
            for (auto& d : chain)
                d.reset ();
        for (auto& l : loops)
        {
            l.line.reset ();
            for (auto& a : l.ap)
                a.reset ();
            l.loShelf.reset ();
            l.hiShelf.reset ();
            l.apState = 0.0;
            l.mixX = l.mixY = 0.0;
        }
        for (int i = 0; i < kNumLoops; ++i)
        {
            const double ph = kTwoPi * i / kNumLoops;
            loops[(size_t) i].lfoC = std::cos (ph);
            loops[(size_t) i].lfoS = std::sin (ph);
        }
        modDepth = modTarget;
        fresh = true; // the memory is empty: the next layout applies without a fade
        fadeLeft = 0;
        hasPending = false;
        for (auto& f : lpf)
            f.reset ();
        coh[0] = coh[1] = CohState{};
        bandZ[0][0] = bandZ[0][1] = bandZ[1][0] = bandZ[1][1] = 0.0;
        lateBwState.fill (0.0f);
        erBwState[0] = erBwState[1] = 0.0f;
        for (auto& f : erTop)
            f.reset ();
        for (auto& f : erTilt)
            f.reset ();
        for (auto& f : lateTilt)
            f.reset ();
        for (auto& l : erFbLine)
            l.reset ();
        erFbLp[0] = erFbLp[1] = 0.0f;
        for (auto& a : erDiff)
            a.reset ();
        for (auto& ch : er2Diff)
            for (auto& a : ch)
                a.reset ();
        rev2Pre.reset ();
        for (auto& l : loops2)
        {
            l.line.reset ();
            l.ap.reset ();
            l.loShelf.reset ();
            l.hiShelf.reset ();
        }
    }

    double Engine::tailSeconds (const EngineParams& p)
    {
        const double rt = std::clamp ((double) p.rt, (double) kMinRt, (double) kMaxRt);
        const double erSpan = 0.001 * (double) er::kChartSpanMs *
                              std::clamp ((double) p.roomSize, 0.1, (double) kMaxRoomSize) / kChartRoomSize;
        const double lo = std::max (1.0, (double) p.loRatio);
        double tail = rt * lo;
        if (p.rev2Level > 0.0f)
            tail = std::max (tail, std::clamp (p.rev2DelayMs, 0.0f, kRev2MaxDelayMs) * 0.001 +
                                       std::clamp (rt * p.rev2Time, (double) kMinRt, (double) kMaxRt) * lo);
        double erTail = 0.0;
        if (p.erFbGain != 0.0f)
        {
            // repeats of the reflection pattern until they fall by 60 dB
            const double g = std::min ((double) kErFbMaxGain, std::abs (p.erFbGain) * 0.01);
            erTail = std::clamp ((double) p.erFbDelayMs, 0.1, (double) kErFbMaxMs) * 0.001 * (-3.0 / std::log10 (g));
        }
        const double onset = 0.001 * std::clamp ((double) p.lateOnsetMs, 0.0, (double) kVoiceLateOnsetMaxMs);
        return (double) p.preDelayMs * 0.001 + erSpan + std::max (tail + onset, erTail);
    }

    void Engine::measureHiRef ()
    {
        // the corner, held below kHiShelfMaxFrac x fs above the default (where it would run into Nyquist)
        hiShelfHz = (float) std::min ((double) hiXoverHz, std::max ((double) kHiShelfDefaultHz, kHiShelfMaxFrac * fs));
        // the 8 kHz octave's reference frequency: fixed at and above the default corner, lower below it
        const double ref = hiShelfHz < kHiShelfDefaultHz
                               ? kHiRefHz * std::pow ((double) hiShelfHz / kHiShelfDefaultHz, kHiRefFollow)
                               : kHiRefHz;
        // fraction of the high shelf's plateau (in dB) reached at the reference, for the hi_ratio mapping
        BasicBiquad<double> probe;
        probe.setHighShelf (fs, hiShelfHz, kHiShelfQ, -6.0f);
        hiRefFrac = std::clamp (20.0 * std::log10 (probe.magnitude (fs, std::min (ref, 0.45 * fs))) / -6.0, 0.05, 1.0);
    }

    double Engine::hiPlateau (double hiRatio) const
    {
        // decay rate at kHiRefHz = mid rate + hiRefFrac x (plateau rate - mid rate); solve for the plateau ratio
        const double h = std::clamp (hiRatio, 0.1, 1.0);
        return std::max (0.04, 1.0 / (1.0 + (1.0 / h - 1.0) / hiRefFrac));
    }

    void Engine::applyLengths ()
    {
        // no allocation: the lines were sized in prepare() for maxFs and the voice maxima
        for (int j = 0; j < kNumLoops; ++j)
            for (size_t i = 0; i < 4; ++i)
                diffuser[(size_t) j][i].setLength (
                    nextPrime (msToSamples ((float) (kDiffuserMs[i] * kDiffuserScale[j] * diffuserScale), fs)));
        for (int i = 0; i < kNumLoops; ++i)
        {
            loopLen[(size_t) i] = nextPrime (msToSamples ((float) (kLoopMs[i] * loopScale), fs));
            for (size_t e = 0; e < 2; ++e)
                tapLen[e][(size_t) i] = std::max (1, (int) std::lround (kLateTapFraction[e] * loopLen[(size_t) i]));
        }
    }

    void Engine::update (const EngineParams& p)
    {
        tapEra = p.erTableSet == 1 ? 1 : 0; // the era's late taps and loop mixing (cheap; no allocation)
        {
            const double xf = kMixAboveHz[tapEra];
            const double t = xf > 0.0 ? std::tan (kTwoPi * 0.5 * std::min (xf, 0.45 * fs) / fs) : 0.0;
            mixApC = xf > 0.0 ? (t - 1.0) / (t + 1.0) : 0.0; // first-order all-pass at the crossover; 0 = no split
        }
        if (cohSet != tapEra)
        {
            // third-order all-pass complementary split (bilinear, prewarped at the corner)
            cohSet = tapEra;
            const double k = std::tan (0.5 * kTwoPi * std::min (kTailCohHz[tapEra], 0.45 * fs) / fs), ik = 1.0 / k;
            cohC1 = (k - 1.0) / (k + 1.0);
            const double d0 = ik * ik + ik + 1.0;
            cohB1 = (2.0 - 2.0 * ik * ik) / d0;
            cohB2 = (ik * ik - ik + 1.0) / d0;
            cohGain[0] = std::sqrt (1.0 + kTailCoherence[tapEra]);
            cohGain[1] = std::sqrt (1.0 - kTailCoherence[tapEra]);
            // band coherence: RBJ band-pass (constant 0 dB peak gain); b1 = 0, b2 = -b0
            const double w = kTwoPi * std::min (kBandCohHz[tapEra], 0.45 * fs) / fs,
                         al = std::sin (w) / (2.0 * kBandCohQ);
            bandB0 = al / (1.0 + al);
            bandA1 = -2.0 * std::cos (w) / (1.0 + al);
            bandA2 = (1.0 - al) / (1.0 + al);
            bandEps = 0.5 * kBandCoherence[tapEra];
        }
        tapWeight = kLateTapWeight[tapEra];
        tapNorm = 1.0 / std::sqrt (1.0 + tapWeight * tapWeight);
        // bitwise compare: a plain struct of floats and ints, no padding (and no -Wfloat-equal)
        static_assert (sizeof (EngineParams) == 42 * 4);
        if (!dirty && std::memcmp (&p, &last, sizeof (EngineParams)) == 0)
            return;
        last = p;
        dirty = false;

        // hidden per-preset voice: new lengths jump the loops, which is fine because they arrive with a preset load
        // (a fresh engine behind the whole-engine crossfade); the loop gains below follow the new lengths
        const double ls = std::clamp ((double) p.loopScale, kVoiceLoopMin, kVoiceLoopMax);
        const double ds = std::clamp ((double) p.diffuserScale, kVoiceDiffMin, kVoiceDiffMax);
        if (std::abs (ls - loopScale) > 1.0e-9 || std::abs (ds - diffuserScale) > 1.0e-9)
        {
            loopScale = ls;
            diffuserScale = ds;
            applyLengths ();
        }
        loXoverHz = std::clamp (p.loXoverHz, kVoiceLoXoverMin, kVoiceLoXoverMax);
        const float hx = std::clamp (p.hiXoverHz, kVoiceHiXoverMin, kVoiceHiXoverMax);
        if (std::abs (hx - hiXoverHz) > 1.0e-3f)
        {
            hiXoverHz = hx; // the hi_ratio mapping follows the corner (prepare() measures it for a new rate)
            measureHiRef ();
        }

        const double rt = std::clamp ((double) p.rt, (double) kMinRt, (double) kMaxRt);
        const double rtLo = bassRt (rt, (double) p.loRatio);
        const double rtHi = rt * hiPlateau ((double) p.hiRatio);

        // ---- delays and reflections ----
        const int cap = input.capacity ();
        Layout next;
        auto& preDelay = next.preDelay;
        preDelay = std::clamp (msToSamples (std::clamp (p.preDelayMs, 0.1f, kMaxPreDelayMs), fs), 1, cap);
        next.ref1Delay = std::clamp (msToSamples (std::clamp (p.ref1DelayMs, 0.1f, kMaxRef1Ms), fs), 0, cap);
        next.ref1Gain = std::clamp (p.ref1Level, 0.0f, 100.0f) * 0.01f;
        next.ref1DelayL = std::clamp (msToSamples (std::clamp (p.ref1LDelayMs, 0.1f, kMaxRef1Ms), fs), 0, cap);
        next.ref1GainL = std::clamp (p.ref1LLevel, 0.0f, 100.0f) * 0.01f;
        next.ref1DelayR = std::clamp (msToSamples (std::clamp (p.ref1RDelayMs, 0.1f, kMaxRef1Ms), fs), 0, cap);
        next.ref1GainR = std::clamp (p.ref1RLevel, 0.0f, 100.0f) * 0.01f;
        next.sideGain = p.trueStereo ? kTrueStereoSpread : 0.0f;
        const int onset = msToSamples (std::clamp (p.lateOnsetMs, 0.0f, kVoiceLateOnsetMaxMs), fs);
        next.lateDirect = std::clamp (preDelay + onset, 1, cap);

        const int patIndex = kModeToPattern[std::clamp (p.erMode, 0, 14)];
        // Rev modes: the reverb programs' own taps, from the initial delay, with no room size, liveness or reflection
        // count (the reverb programs have none; REV5 OM pp. 16-18, REV7 OM p. 13). Era 5: density picks the sparse or
        // dense Rev Hall set (inference: the dense captures carry the sparse set's taps plus about 20 more, and the
        // factory reverb programs print density 4); Rev Room plays the sparse set (stand-in, ours). Era 7: three per
        // channel ("3 early reflections are programmed for each channel", REV7 OM p. 14, OCR), the first at the
        // initial delay on both channels (REV7 captures, twenty-third pass).
        const bool rev = p.erMode >= kRevHallMode && p.erMode <= kRevRoomMode;
        const int revIndex = p.erMode - kRevHallMode;
        static_assert (kRevPlateMode - kRevHallMode == 1 && kRevRoomMode - kRevHallMode == 2); // revIndex order
        const auto& pat =
            rev ? (p.erTableSet == 1 ? er::kRevPatterns5[revIndex == 1   ? 2
                                                         : revIndex == 2 ? 0
                                                                         : (p.density >= kRevDenseFrom ? 1 : 0)]
                                     : er::kRevPatterns7[revIndex])
            : p.erTableSet == 1 ? er::kPatterns5[patIndex]
                                : er::kPatterns[patIndex];
        const float scale =
            rev ? (p.erTableSet == 1
                       ? 1.0f
                       : std::clamp (p.initialDelayMs > 0.0f ? p.initialDelayMs : p.preDelayMs, 0.1f, kMaxPreDelayMs) /
                             kRevRefMs7[std::clamp (revIndex, 0, 2)])
                : std::clamp (p.roomSize, 0.1f, kMaxRoomSize) / kChartRoomSize;
        const float live = rev ? 5.0f : std::clamp (p.liveness, 0.0f, 10.0f);
        // Era-5 Pan A/B: liveness is the pan direction, not the fade (REV5 OM p. 21), so the fade is left flat.
        const bool pan = p.erTableSet == 1 && (p.erMode == kPanModeA || p.erMode == kPanModeB);
        // Liveness: 5 plays the chart as drawn; below 5 later reflections fade faster (kLivenessDbPer100Ms, measured on
        // the SPX90 model); above 5 the chart's own fade flattens towards equal levels (inference from both manuals: the
        // reflections fade "more slowly" up to an "intensely reflective" 10; the SPX90 model only adds 6 dB there, which
        // the manuals contradict).
        // Era 5: the hardware at liveness 5 plays its printed fade at kChartFade5 of the chart's depth (the REV5 S-Hall
        // and Random captures, taps matched to the chart: 0.84 / 0.64 / 0.63 / 0.61 per channel; 2026-11-21, E/R
        // leftovers; inference), and liveness acts on that. Only the falling charts (S-Hall, L-Hall, Random, and the
        // stand-ins that play them): no capture covers the rising ones (Reverse, Plate, Spring), whose shape is the
        // mode, so they stay as drawn (maintainer's decision, 2026-11-21).
        const bool fallingChart = patIndex <= kLastFallingPattern;
        const float chartFade = (p.erTableSet == 1 && !rev && !pan && fallingChart) ? kChartFade5 : 1.0f;
        const float flatten = chartFade * ((!pan && live > 5.0f) ? 1.0f - 0.16f * (live - 5.0f) : 1.0f);
        const float extraPer100 = (!pan && live < 5.0f) ? -kLivenessDbPer100Ms * (5.0f - live) : 0.0f;
        const float feedDbPerMs = -60.0f * kLateFeedFade / (float) (rt * 1000.0);
        // era 5: the reflection output passes the diffusion all-pass, so it is read that much early (kErDiffMs); the
        // late feed keeps the charted times (shifting it too combs the late input against the direct feed)
        next.erEarly = p.erTableSet == 1 ? erDiffLen : 0;
        auto fill = [&] (const er::Tap* src, int count, std::array<Tap, kMaxErTaps>& dst) -> int
        {
            const int n = rev ? std::min (count, (int) dst.size ())
                              : std::clamp (p.erCount, 1, std::min (count, (int) dst.size ()));
            double energy = 0.0, feedEnergy = 0.0;
            for (int k = 0; k < n; ++k)
            {
                const float ms = src[k].ms * scale;
                auto& t = dst[(size_t) k];
                t.delay = std::clamp (preDelay + msToSamples (ms, fs), 0, cap);
                t.feedDelay = std::clamp (t.delay + onset, 0, cap);
                const float db = src[k].db * flatten + extraPer100 * ms * 0.01f;
                t.gain = std::pow (10.0f, db / 20.0f);
                t.feed = t.gain * std::pow (10.0f, feedDbPerMs * ms / 20.0f);
                energy += (double) t.gain * t.gain;
                feedEnergy += (double) t.feed * t.feed;
            }
            // reflections at the era's level (REV5 charts: kErLevel5); the late feed at unit energy x kLateFromEr
            const float erLevel = p.erTableSet == 1 ? kErLevel5 : kErLevel7;
            const float norm = erLevel / (float) std::sqrt (std::max (energy, 1.0e-9));
            const float feedNorm = kLateFromEr / (float) std::sqrt (std::max (feedEnergy, 1.0e-30));
            for (int k = 0; k < n; ++k)
            {
                dst[(size_t) k].gain *= norm;
                dst[(size_t) k].feed *= feedNorm;
            }
            return n;
        };
        // Tap-by-tap blend of two charts: time linearly with weight tw, level in amplitude with weight w (0 = a,
        // 1 = b). Where the counts differ the longer chart is sampled evenly along its sequence (design choice,
        // inference). Stack arrays keep the audio thread allocation-free.
        std::array<er::Tap, er::kMaxTaps> bl, br;
        auto blend = [] (float w, float tw, const er::Tap* a, int na, const er::Tap* b, int nb,
                         std::array<er::Tap, er::kMaxTaps>& out)
        {
            const int n = std::min (na, nb);
            for (int k = 0; k < n; ++k)
            {
                const auto ia = (size_t) (n > 1 ? std::lround ((double) k * (na - 1) / (n - 1)) : 0);
                const auto ib = (size_t) (n > 1 ? std::lround ((double) k * (nb - 1) / (n - 1)) : 0);
                out[(size_t) k].ms = a[ia].ms + tw * (b[ib].ms - a[ia].ms);
                const float amp =
                    (1.0f - w) * std::pow (10.0f, a[ia].db / 20.0f) + w * std::pow (10.0f, b[ib].db / 20.0f);
                out[(size_t) k].db = 20.0f * std::log10 (std::max (amp, 1.0e-6f));
            }
            return n;
        };
        // Era-7 modes whose size-1.0 chart is a pattern of its own (er::kSmallPatterns7: Random, Spring): room size
        // <= 1 plays that chart, >= 4 the size-4.0 chart, and between them the two blend in log room size (design
        // choice, inference; the hardware's behaviour between the two charted sizes is not documented).
        const er::Pattern* small = nullptr;
        if (p.erTableSet != 1)
            for (int i = 0; i < er::kNumSmallPatterns7; ++i)
                if (er::kSmallPatternMode7[i] == patIndex)
                    small = &er::kSmallPatterns7[i];
        if (pan)
        {
            // Pan charts at liveness 0 and 10 (er::kPanPatterns5: A/0, A/10, B/0, B/10). Levels: liveness w = live / 10
            // blends them (design choice, inference), so 0 and 10 play the charts as drawn, 5 is the mean of the two
            // (about equal levels on both sides, no pan) and values between shift the pan smoothly. Times are not
            // blended: each channel plays its own chart's times from the liveness side, up to 5 the liveness-0 chart's
            // (the REV5 Pan-A captures at liveness 5, both channels on the liveness-0 times, 0.16-0.24 ms median error;
            // blending put them 2-2.6 ms off), above 5 the liveness-10 chart's, which mirrors the two tap sets across
            // the channels (inference; the switch is crossfaded with the layout).
            const int base = p.erMode == kPanModeB ? 2 : 0;
            const auto& c0 = er::kPanPatterns5[base];
            const auto& c10 = er::kPanPatterns5[base + 1];
            const float w = live * 0.1f;
            const float tw = live > 5.0f ? 1.0f : 0.0f;
            const int nl = blend (w, tw, c0.left, c0.countL, c10.left, c10.countL, bl);
            const int nr = blend (w, tw, c0.right, c0.countR, c10.right, c10.countR, br);
            next.numTapsL = fill (bl.data (), nl, next.tapsL);
            next.numTapsR = fill (br.data (), nr, next.tapsR);
        }
        else if (small != nullptr && p.roomSize < kChartRoomSize)
        {
            const float w = std::clamp (std::log (std::max (p.roomSize, 0.1f)) / std::log (kChartRoomSize), 0.0f, 1.0f);
            if (w <= 0.0f)
            {
                next.numTapsL = fill (small->left, small->countL, next.tapsL);
                next.numTapsR = fill (small->right, small->countR, next.tapsR);
            }
            else
            {
                const int nl = blend (w, w, small->left, small->countL, pat.left, pat.countL, bl);
                const int nr = blend (w, w, small->right, small->countR, pat.right, pat.countR, br);
                next.numTapsL = fill (bl.data (), nl, next.tapsL);
                next.numTapsR = fill (br.data (), nr, next.tapsR);
            }
        }
        else
        {
            next.numTapsL = fill (pat.left, pat.countL, next.tapsL);
            next.numTapsR = fill (pat.right, pat.countR, next.tapsR);
        }

        // ER/REV BAL as on the REV5: 100 % early reflections only, 0 % reverb only, about 50 % equal (OM p. 18).
        // bal below is the late share: 1 = late only.
        const float bal = 1.0f - std::clamp (p.erRevBalance, 0.0f, 100.0f) * 0.01f;
        next.erGain = std::min (1.0f, 2.0f * (1.0f - bal)) *
                      std::pow (10.0f, std::clamp (p.erGainDb, -kVoiceErGainMaxDb, kVoiceErGainMaxDb) / 20.0f);
        static_assert (sizeof (Layout) == (2 * kMaxErTaps * 4 + 13) * 4);
        if (fresh)
        {
            cur = next;
            fresh = false;
        }
        else if (fadeLeft > 0)
        {
            pending = next;
            hasPending = true;
        }
        else if (std::memcmp (&next, &cur, sizeof (Layout)) != 0)
            startFade (next);
        lateGain = std::min (1.0f, 2.0f * bal) * kLateLevel / std::sqrt ((float) kNumLoops);

        // ---- diffusion ----
        const double rtFastest = std::min ({rt, rtLo, rtHi}); // the diffuser must not outlast any band's decay
        const float dg = kDiffusionMax * std::pow (std::clamp (p.diffusion, 0.0f, 10.0f) / 10.0f, kDiffusionCurve);
        for (auto& chain : diffuser)
            for (auto& d : chain)
            {
                const double cap =
                    std::pow (10.0, -3.0 * kDiffuserSpeedup * (double) d.getLength () / (fs * rtFastest));
                d.setGain ((float) std::min ((double) dg, cap));
            }
        const int dens = std::clamp ((int) std::lround (p.density), 1, 4) - 1;

        // ---- loops: Jot's rule on the effective loop delay ----
        double meanLen = 0.0;
        for (int i = 0; i < kNumLoops; ++i)
        {
            auto& l = loops[(size_t) i];
            double total = (double) loopLen[(size_t) i];
            const double g = (double) kDensityGain[dens];
            for (auto& a : l.ap)
            {
                a.setGain (g);
                total += allPassHold ((double) a.getLength (), g);
            }
            meanLen += total / kNumLoops;
            setLoopFilters (l, total, fs, rt, rtLo, rtHi, loXoverHz, hiShelfHz);
        }
        // energy of a loop is 1 / (1 - g^2): normalise to the reference decay so long RTs do not swell
        const double g = loopGain (meanLen, fs, rt), gRef = loopGain (meanLen, fs, kRefRt);
        loopInGain = (float) std::min (1.0, std::sqrt ((1.0 - g * g) / (1.0 - gRef * gRef)));

        // ---- ER feedback ----
        erFbG = std::clamp (p.erFbGain * 0.01f, -kErFbMaxGain, kErFbMaxGain);
        erFbDelay =
            std::clamp (msToSamples (std::clamp (p.erFbDelayMs, 0.1f, kErFbMaxMs), fs), 1, erFbLine[0].capacity ());
        if (const float hi = std::clamp (p.erFbHi, 0.1f, 1.0f); std::not_equal_to<float>{}(hi, erFbHiGain))
        {
            erFbHiGain = hi;
            erFbCoef = (float) loopHighCoef (hi, fs);
        }

        // ---- era-5 reflection diffusion (kErDiffPerStep); era 7 plays bare taps ----
        erDiffOn = p.erTableSet == 1;
        // E/R2: the charted modes only (the Rev modes are the reverb programs' own reflections); each era its own
        // decay and channel scale
        er2On = p.erType == 1 && !rev;
        if (er2On && er2Set != std::clamp (p.erTableSet, 0, 1))
            applyEr2 (std::clamp (p.erTableSet, 0, 1));
        for (auto& a : erDiff)
            a.setGain ((rev ? kErDiffPerStep : kErDiffPerStepEr) * std::clamp (p.diffusion, 0.0f, 10.0f));

        // ---- secondary reverb: Jot's rule as for the primary loops, at rt x rev2_time ----
        rev2On = p.rev2Level > 0.0f;
        rev2Delay =
            std::clamp (msToSamples (std::clamp (p.rev2DelayMs, 0.0f, kRev2MaxDelayMs), fs), 0, rev2Pre.capacity ());
        {
            const double rt2 =
                std::clamp (rt * std::clamp ((double) p.rev2Time, 0.1, 10.0), (double) kMinRt, (double) kMaxRt);
            const double rt2Lo = bassRt (rt2, (double) p.loRatio);
            const double hi2 =
                p.rev2HiRatio > 0.0f ? std::clamp ((double) p.rev2HiRatio, 0.1, 1.0) : (double) p.hiRatio;
            const double rt2Hi = rt2 * hiPlateau (hi2);
            double mean2 = 0.0;
            for (auto& l : loops2)
            {
                const double total = (double) l.len + allPassHold ((double) l.ap.getLength (), kRev2ApGain);
                mean2 += total / kNumLoops2;
                setLoopFilters (l, total, fs, rt2, rt2Lo, rt2Hi, loXoverHz, hiShelfHz);
            }
            // same energy normalisation as the primary network, so rev2_level is relative to it
            const double g2 = loopGain (mean2, fs, rt2), gRef = loopGain (mean2, fs, kRefRt);
            rev2InGain = std::min (1.0, std::sqrt ((1.0 - g2 * g2) / (1.0 - gRef * gRef)));
            rev2OutGain = (double) lateGain * std::sqrt ((double) kNumLoops / kNumLoops2) *
                          std::clamp ((double) p.rev2Level, 0.0, 100.0) * 0.01 * kRev2LevelShare *
                          std::pow (10.0, std::clamp ((double) p.rev2GainDb, 0.0, (double) kVoiceRev2GainMaxDb) / 20.0);
        }

        // ---- late-network input bandwidth (era colour, set by the converter) ----
        lateBwCoef = p.lateBandwidthHz > 0.0f && p.lateBandwidthHz < 0.45 * fs
                         ? (float) (1.0 - std::exp (-kTwoPi * (double) p.lateBandwidthHz / fs))
                         : 1.0f;

        // ---- reflection-output bandwidth (era openings; set by the converter) ----
        erBwCoef = p.erBandwidthHz > 0.0f && p.erBandwidthHz < 0.45 * fs
                       ? (float) (1.0 - std::exp (-kTwoPi * (double) p.erBandwidthHz / fs))
                       : 1.0f;
        // ---- reflection-output top octave (era 5's tap comb on average; set by the converter) ----
        {
            // voice tilts: redesigned when a value or the rate changes (they arrive with a preset load)
            const float et = std::clamp (p.erTiltDb, -kVoiceTiltMaxDb, kVoiceTiltMaxDb);
            const float lt = std::clamp (p.lateTiltDb, -kVoiceTiltMaxDb, kVoiceTiltMaxDb);
            const float erHz = std::min (kErTiltHz, (float) (0.4 * fs)),
                        lateHz = std::min (kLateTiltHz, (float) (0.4 * fs));
            if (tiltFresh || std::abs (et - erTiltSet) > 1.0e-3f)
                for (auto& f : erTilt)
                    f.setHighShelf (fs, erHz, kVoiceTiltQ, et);
            if (tiltFresh || std::abs (lt - lateTiltSet) > 1.0e-3f)
                for (auto& f : lateTilt)
                    f.setHighShelf (fs, lateHz, kVoiceTiltQ, lt);
            erTiltSet = et;
            lateTiltSet = lt;
            erTiltOn = std::abs (et) > 1.0e-3f;
            lateTiltOn = std::abs (lt) > 1.0e-3f;
            tiltFresh = false;
        }
        erTopOn = p.erTopHz > 0.0f && p.erTopHz < kErTopMaxFrac * fs;
        if (erTopOn && std::abs (p.erTopHz - erTopHzSet) > 1.0f)
            for (auto& f : erTop)
                f.setLowPass (fs, p.erTopHz, kErTopQ);
        erTopHzSet = erTopOn ? p.erTopHz : -1.0f;

        // ---- space modulation ----
        modTarget = (double) std::clamp (p.spaceMod, 0.0f, 10.0f) * 0.1 * kSpaceModMaxMs * 0.001 * fs;

        // ---- wet low-pass (REV5 LPF FRQ) ----
        // First order (bilinear one-pole): fitted per capture on 19 REV5 captures, a one-pole beats the earlier
        // second-order low-pass in every program family (colour error 2.7 / 4.2 / 1.6 dB rms against 3.3 / 5.9 / 2.3 on
        // reverbs, springs, echo; twenty-first pass). The order is a fit to third-party captures, not a hardware spec.
        lpfOn = p.wetLpfKHz < 16.5f;
        if (lpfOn)
        {
            const double k = std::tan (0.5 * kTwoPi * (double) std::clamp (p.wetLpfKHz, 1.0f, 16.0f) * 1000.0 / fs);
            for (auto& f : lpf)
                f.setCoefficients (k / (1.0 + k), k / (1.0 + k), 0.0, (k - 1.0) / (k + 1.0), 0.0);
        }
    }

    void Engine::process (const float* inL, const float* inR, float* outL, float* outR, int n, const EngineParams& p)
    {
        update (p);
        for (int i = 0; i < n; ++i)
            processSample (inL[i], inR[i], outL[i], outR[i]);
    }

    void Engine::startFade (const Layout& next) noexcept
    {
        prev = cur;
        cur = next;
        fadeLeft = fadeLen;
    }

    void Engine::readLayout (const Layout& lay, float& outL, float& outR, std::array<float, kNumLoops>& lateIn,
                             float& refL, float& refR) const noexcept
    {
        float erL = 0.0f, erR = 0.0f;
        std::array<float, kNumLoops> col{}; // the feed summed per Hadamard column (kFeedColR)
        // true stereo: the left taps lean to the left input, the right ones to the right (ADR 0006); otherwise the
        // side memory is not read, so Mono and Stereo stay exactly as they were
        const float k = lay.sideGain;
        auto readL = [this, k] (int d) noexcept
        { return k != 0.0f ? input.tap (d) + k * sideMemory.tap (d) : input.tap (d); };
        auto readR = [this, k] (int d) noexcept
        { return k != 0.0f ? input.tap (d) - k * sideMemory.tap (d) : input.tap (d); };
        const int early = lay.erEarly; // era 5: the reflection output is read early (kErDiffMs), the feed is not
        for (int i = 0; i < lay.numTapsL; ++i)
        {
            const auto& t = lay.tapsL[(size_t) i];
            const float x = readL (t.delay);
            erL += t.gain * (early != 0 ? readL (std::max (0, t.delay - early)) : x);
            col[(size_t) (i & 7)] += t.feed * (t.feedDelay != t.delay ? readL (t.feedDelay) : x);
        }
        for (int i = 0; i < lay.numTapsR; ++i)
        {
            const auto& t = lay.tapsR[(size_t) i];
            const float x = readR (t.delay);
            erR += t.gain * (early != 0 ? readR (std::max (0, t.delay - early)) : x);
            col[(size_t) ((i + kFeedColR) & 7)] += t.feed * (t.feedDelay != t.delay ? readR (t.feedDelay) : x);
        }
        const float ref1 = lay.ref1Gain * input.tap (lay.ref1Delay);
        outL = lay.erGain * erL;
        outR = lay.erGain * erR;
        // centre 1st reflection to both sides; the REV5's left and right ones to their own side (p. 14)
        refL = ref1 + lay.ref1GainL * readL (lay.ref1DelayL);
        refR = ref1 + lay.ref1GainR * readR (lay.ref1DelayR);
        // the late network hears a little of the pre-delayed input and mostly the reflections (kLateFromEr, faded)
        // loop g's feed = sum over columns of H8[g][col] x col: an in-place fast Walsh-Hadamard transform
        for (int h = 1; h < kNumLoops; h <<= 1)
            for (int i = 0; i < kNumLoops; i += h << 1)
                for (int j = i; j < i + h; ++j)
                {
                    const float a = col[(size_t) j], b = col[(size_t) (j + h)];
                    col[(size_t) j] = a + b;
                    col[(size_t) (j + h)] = a - b;
                }
        const float direct = kLateDirect * input.tap (lay.lateDirect);
        for (int g = 0; g < kNumLoops; ++g)
            lateIn[(size_t) g] = direct + col[(size_t) kFeedRow[g]];
    }

    void Engine::mixLoops (std::array<double, kNumLoops>& fb) noexcept
    {
        // Householder reflections over interleaved blocks, optionally above a crossover (kMixBlock, kMixAboveHz)
        const int b = kMixBlock[tapEra];
        if (b < 2)
            return;
        std::array<double, kNumLoops> low{};
        const bool split = mixApC != 0.0;
        if (split)
            for (size_t j = 0; j < (size_t) kNumLoops; ++j)
            {
                auto& l = loops[j];
                const double x = fb[j], ax = mixApC * x + l.mixX - mixApC * l.mixY;
                l.mixX = x;
                l.mixY = ax;
                low[j] = 0.5 * (x + ax);
                fb[j] = 0.5 * (x - ax);
            }
        const int groups = kNumLoops / b;
        for (int g = 0; g < groups; ++g)
        {
            double sum = 0.0;
            for (int j = g; j < kNumLoops; j += groups)
                sum += fb[(size_t) j];
            const double k = 2.0 / b * sum;
            for (int j = g; j < kNumLoops; j += groups)
                fb[(size_t) j] -= k;
        }
        if (split)
            for (size_t j = 0; j < (size_t) kNumLoops; ++j)
                fb[j] += low[j];
    }

    void Engine::processSample (float inL, float inR, float& outL, float& outR) noexcept
    {
        // mid and side memories; the side is read only in true stereo (ADR 0006)
        input.write (0.5f * (inL + inR));
        sideMemory.write (0.5f * (inL - inR));

        float wl, wr, r1, r1r;
        std::array<float, kNumLoops> d;
        readLayout (cur, wl, wr, d, r1, r1r);
        if (fadeLeft > 0)
        {
            // equal power: the old and new reads are different parts of the signal, so they add as uncorrelated
            float ol, orr, or1, or1r;
            std::array<float, kNumLoops> od;
            readLayout (prev, ol, orr, od, or1, or1r);
            const float t = kHalfPi * (float) (fadeLen - fadeLeft) / (float) fadeLen;
            const float gNew = std::sin (t), gOld = std::cos (t);
            wl = gNew * wl + gOld * ol;
            wr = gNew * wr + gOld * orr;
            for (size_t g = 0; g < d.size (); ++g)
                d[g] = gNew * d[g] + gOld * od[g];
            r1 = gNew * r1 + gOld * or1;
            r1r = gNew * r1r + gOld * or1r;
            if (--fadeLeft == 0 && hasPending)
            {
                hasPending = false;
                if (std::memcmp (&pending, &cur, sizeof (Layout)) != 0)
                    startFade (pending);
            }
        }

        // era-5 reflection diffusion: the reflections (not the 1st reflections, nor the late feed) pass one short
        // all-pass per channel; at diffusion 0 it is a plain delay of kErDiffMs, which the early read takes back
        if (erDiffOn)
        {
            wl = erDiff[0].process (wl);
            wr = erDiff[1].process (wr);
        }
        // E/R2 diffuser: the reflections only, as above
        if (er2On)
            for (size_t k = 0; k < er2Diff[0].size (); ++k)
            {
                wl = er2Diff[0][k].process (wl);
                wr = er2Diff[1][k].process (wr);
            }
        // ER feedback: the reflections (not the 1st reflection) repeat FB DLY after they start, darker each time
        if (erFbG != 0.0f)
        {
            float* ch[2] = {&wl, &wr};
            for (int c = 0; c < 2; ++c)
            {
                const float back = erFbLine[c].readBeforeWrite (erFbDelay);
                erFbLp[c] += erFbCoef * (back - erFbLp[c]);
                erFbLp[c] = std::abs (erFbLp[c]) < 1.0e-20f ? 0.0f : erFbLp[c];
                const float y = *ch[c] + erFbG * erFbLp[c];
                erFbLine[c].write (y);
                *ch[c] = y;
            }
        }
        wl += r1;
        wr += r1r;
        ref1Out[0] = r1;
        ref1Out[1] = r1r;
        if (erBwCoef < 1.0f)
        {
            // the era's bandwidth on the reflections too (fifteenth pass); the late feed above is already taken
            erBwState[0] += erBwCoef * (wl - erBwState[0]);
            erBwState[1] += erBwCoef * (wr - erBwState[1]);
            erBwState[0] = std::abs (erBwState[0]) < 1.0e-20f ? 0.0f : erBwState[0];
            erBwState[1] = std::abs (erBwState[1]) < 1.0e-20f ? 0.0f : erBwState[1];
            wl = erBwState[0];
            wr = erBwState[1];
        }
        if (erTopOn)
        {
            wl = erTop[0].process (wl);
            wr = erTop[1].process (wr);
        }
        if (erTiltOn)
        {
            wl = erTilt[0].process (wl);
            wr = erTilt[1].process (wr);
        }

        // the era's input bandwidth: a one-pole low-pass on everything the late networks hear
        std::array<double, kNumLoops> loopIn;
        for (size_t g = 0; g < d.size (); ++g)
        {
            float& s = lateBwState[g];
            s += lateBwCoef * (d[g] - s);
            s = std::abs (s) < 1.0e-20f ? 0.0f : s;
            float v = s;
            for (auto& ap : diffuser[g])
                v = ap.process (v);
            d[g] = v;
            loopIn[g] = (double) (v * loopInGain);
        }

        // secondary reverb: hears the primary network's input after rev2_delay
        rev2Pre.write ((double) d[0]);   // loop 0 hears the plain summed feed (Hadamard row 0)
        double tailL = 0.0, tailR = 0.0; // the diffuse tails (secondary and primary), before the bass coherence
        if (rev2On)
        {
            const double in2 = rev2Pre.tap (rev2Delay) * rev2InGain;
            double l2 = 0.0, r2 = 0.0;
            for (int j = 0; j < kNumLoops2; ++j)
            {
                auto& l = loops2[(size_t) j];
                const double y = l.line.readBeforeWrite (l.len);
                l2 += kSign2L[j] * y;
                r2 += kSign2R[j] * y;
                l.line.write (in2 + l.ap.process (l.hiShelf.process (l.loShelf.process (y * l.gain))));
            }
            tailL = rev2OutGain * l2;
            tailR = rev2OutGain * r2;
        }

        modDepth += modSmooth * (modTarget - modDepth);
        if (modTarget <= 0.0 && modDepth < 1.0e-6)
            modDepth = 0.0;
        const bool modOn = modDepth > 0.0;

        double lateL = 0.0, lateR = 0.0;
        std::array<double, kNumLoops> fb{};
        for (int j = 0; j < kNumLoops; ++j)
        {
            auto& l = loops[(size_t) j];
            double y;
            if (modOn)
            {
                // advance the LFO phasor; the second-order correction keeps its radius at 1
                const double c = l.lfoC * l.rotC - l.lfoS * l.rotS;
                const double s = l.lfoS * l.rotC + l.lfoC * l.rotS;
                const double k = 1.5 - 0.5 * (c * c + s * s);
                l.lfoC = c * k;
                l.lfoS = s * k;
                // fractional read through a first-order all-pass (unity gain at every frequency, so the decay law
                // holds); the fraction stays in [0.5, 1.5) where the all-pass approximates a delay well
                const double dl = (double) loopLen[(size_t) j] + modDepth * l.lfoS;
                const int whole = (int) std::floor (dl - 0.5);
                const double frac = dl - (double) whole;
                const double a = (1.0 - frac) / (1.0 + frac);
                const double x0 = l.line.readBeforeWrite (whole), x1 = l.line.readBeforeWrite (whole + 1);
                y = a * x0 + x1 - a * l.apState;
                l.apState = y;
            }
            else
            {
                y = l.line.readBeforeWrite (loopLen[(size_t) j]);
                l.apState = y;
            }
            const double out = tapNorm * (y + tapWeight * l.line.readBeforeWrite (tapLen[tapEra][(size_t) j]));
            lateL += kSignL[j] * out;
            lateR += kSignR[j] * out;
            fb[(size_t) j] = l.ap[1].process (l.ap[0].process (l.hiShelf.process (l.loShelf.process (y * l.gain))));
        }
        mixLoops (fb);
        for (int j = 0; j < kNumLoops; ++j)
            loops[(size_t) j].line.write (loopIn[(size_t) j] + fb[(size_t) j]);

        tailL += (double) lateGain * lateL;
        tailR += (double) lateGain * lateR;
        {
            // bass coherence (kTailCoherence): mid up, side down below kTailCohHz, energy kept
            double ms[2] = {0.5 * (tailL + tailR), 0.5 * (tailL - tailR)};
            for (int c = 0; c < 2; ++c)
            {
                auto& st = coh[c];
                const double x = ms[c];
                const double a1 = cohC1 * x + st.x1 - cohC1 * st.y1;
                st.x1 = x;
                st.y1 = a1;
                const double a2 = cohB2 * x + cohB1 * st.u1 + st.u2 - cohB1 * st.v1 - cohB2 * st.v2;
                st.u2 = st.u1;
                st.u1 = x;
                st.v2 = st.v1;
                st.v1 = a2;
                ms[c] = 0.5 * (a2 - a1) + cohGain[c] * 0.5 * (a1 + a2); // high + gain x low
            }
            const double mid = ms[0], side = ms[1];
            tailL = mid + side;
            tailR = mid - side;
            // band coherence (kBandCoherence): each channel takes a band-pass copy of the other
            const double in[2] = {tailL, tailR};
            double bp[2];
            for (int c = 0; c < 2; ++c)
            {
                auto& z = bandZ[c];
                const double y = bandB0 * in[c] + z[0];
                z[0] = -bandA1 * y + z[1];
                z[1] = -bandB0 * in[c] - bandA2 * y;
                bp[c] = y;
            }
            tailL += bandEps * bp[1];
            tailR += bandEps * bp[0];
        }
        if (lateTiltOn)
        {
            tailL = lateTilt[0].process (tailL);
            tailR = lateTilt[1].process (tailR);
        }
        wl += (float) tailL;
        wr += (float) tailR;
        if (lpfOn)
        {
            wl = lpf[0].process (wl);
            wr = lpf[1].process (wr);
        }
        outL = wl;
        outR = wr;
    }
} // namespace yrev::dsp
