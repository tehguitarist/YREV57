#include "Engine.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <functional>
#include <type_traits>

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
        constexpr float kVoiceLoXoverMin = 100.0f, kVoiceLoXoverMax = 800.0f, kVoiceErGainMaxDb = 12.0f;
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
        // 44.1 kHz native rate). Ours (inference: the comb is in the hardware's taps, its cause unknown). Unused since
        // pass 36 (the converter passes erTopHz 0): the REV5 taps show no comb.
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
        // plateau: the plateau is set so the shelf gives hi_ratio at the reference (calibrated by measurement at the
        // default corner). The reference stands for where the dark tail weights the octave. With a lower corner the
        // whole octave is damped and the weight slides down, so the reference follows a corner below the default as
        // (corner / 8.5 kHz)^kHiRefFollow; above it the reference stays. Above the default the corner is held below
        // kHiShelfMaxFrac x fs (10 kHz at 31.25 kHz), where the shelf still reaches 7 kHz. Sweep of corners 4-14 kHz,
        // hi 0.2-0.8, at 31.25, 44.1 and 48 kHz: within 8 % rms, 2-5 % for most corners (measured, engine.md).
        // Pass 37: the reference is now per era (EngineParams::hiRefHz, set by the converter); 7 kHz (the 8 kHz octave)
        // stays the engine's own default. The default corner scales with EngineParams::hiXoverScale.
        constexpr double kHiRefFollow = 0.3, kHiShelfMaxFrac = 0.32;
        constexpr double kMidRefHz = 1000.0; // where the loops' broadband decay is held to rt (setLoopFilters)
        constexpr int kMidCalSteps = 4;
        // Pass 81: a third loop shelf, the decay below kSubHz at kSubRtRatio x the bass decay per era ([0] era 7), x
        // the per-program subRatio. With pass 71's drift energy fixed (the late 63-350 Hz no longer lifted), the REV7
        // reverbs' late 63 Hz read short (300-800 ms median +1.7 dB against the captures; pass 76's note that 63 Hz
        // decays short); x1.05 at 120 Hz worsened no REV7 program's metric over six drift seeds once Percussion Plate,
        // Snare and Handclaps were refitted (scratch grid x1.0-1.15, 80-120 Hz). Era 5 x1.0 (any lift cost the REV5
        // halls' late colour). Ours.
        constexpr double kSubHz = 120.0; // the default corner; per program voice_sub_hz (pass 83)
        constexpr float kSubHzMin = 40.0f, kSubHzMax = 200.0f;
        constexpr double kSubRtRatio[2] = {1.05, 1.0};
        constexpr double kMaxHiCutDb = -50.0; // deepest high-shelf cut per loop pass (setLoopFilters)
        // Early fill (pass 78; our own design, inference): four series all-passes per channel, L and R at different
        // mutually prime lengths so the fill decorrelates the sides; gain kErFillGain per stage. Fitted on CLA Drum
        // Room's REV1 capture, whose first 50 ms is a dense field where our charts leave gaps between taps: a fill
        // that is over by about 40 ms (gain 0.3-0.45) fits, 0.6-0.7 rings into 100-300 ms through the reflection tilt.
        constexpr float kErFillMs[2][4] = {{1.3f, 2.9f, 4.7f, 7.9f}, {1.7f, 3.3f, 5.3f, 8.6f}};
        constexpr float kErFillGain = 0.45f, kErFillMax = 2.0f, kErFillSizeMin = 0.5f, kErFillSizeMax = 3.0f;
        // Early cloud (pass 84; our own design, inference): velvet noise, one tap at a random place in each
        // kCloudCellMs cell, read once for both channels with a random sign per side (fixed seed); heard in place of
        // the chart where a capture's early field is a smooth dense haze (CLA Drum Room's REV1: every 2.5 ms bin of its
        // first 50 ms within about 3 dB) rather than the chart's discrete taps; the early fill after it supplies the
        // density, as an all-pass diffuser after a multitap. Rising from a level in dB at the pre-delay over the hold,
        // then a fade in dB per 10 ms, cut at kCloudFloorDb (below it the fill and the late network carry the field) or
        // kMaxCloudTaps cells. 1 ms cells to -20 dB (about 87 reads at CLA's settings) score as 0.6 ms to -40 dB (400
        // reads, the first version); 2 ms cells lose the 0-50 ms colour.
        constexpr float kCloudCellMs = 1.0f, kCloudFloorDb = -20.0f, kCloudHoldMaxMs = 100.0f;
        constexpr uint32_t kCloudSeed = 0x9E3779B9u + 5u * 7919u; // the sequence chosen on CLA Drum Room (pass 84)
        constexpr float kCloudFadeMin = 0.5f, kCloudFadeMax = 30.0f, kCloudRiseMinDb = -30.0f;
        constexpr float kVoiceErLoMaxDb = 24.0f, kVoiceErLoHzMin = 60.0f, kVoiceErLoHzMax = 2000.0f,
                        kVoiceErLoQ = 0.707f;
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
        // Per era ([0] era 7, [1] era 5), a scale on that gain. Pass 62 (2026-11-25): the REV5 reverb programs
        // (all printed at DIFFUSION 5) build their early field sparser than ours: normalised echo density (20 ms, 300
        // Hz-8 kHz) reaches 0.9 at a median 132 ms against our 74, and reads 0.52-0.79 at 80 ms against our 0.86-0.99.
        // Era 5's diffusers at x0.6 (gain 0.3 at diffusion 5): NED rms over 20-150 ms 0.204 -> 0.107, the pass-44
        // envelope features 1.63 -> 1.82 dB rms (Snare's diffusers refitted). Era 7 matches (median 95 against 114 ms).
        // Measurement of the captures; ours. Pass 63: era 7 x0.6 too, with a stronger part-way loop read
        // (kLateTapWeight): the REV7 late field speaks early but not densely (see there).
        constexpr float kDiffusionEraScale[2] = {0.6f, 0.6f};
        // Pass 81: x the per-program diffGain (hidden voice_diff_gain), capped here
        constexpr float kDiffusionGainMax = 0.9f;
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
        // Pass 63 (2026-11-25): the REV7 reverb programs reach their late level soon after the last tap (Vocal
        // Plate and Strings within 1 ms of tap 3, 10-15 dB over ours at 20-50 ms; Large Hall silent between taps 1
        // and 2, where our diffused feed already spoke), but not densely (echo density 0.9 at a median 100 ms).
        // Era 7's weight 0.55 -> 0.9 with its diffusers x0.6 (kDiffusionEraScale): pass 44's features on the REV7
        // reverb programs 2.50 -> 2.03 dB rms, NED rms 0.150 -> 0.127, colour 100-300 ms 2.56 -> 2.25 dB rms per
        // program (300-800 ms 1.76 -> 1.87). Feeding the loops later (dropping the direct feed or the first taps)
        // scored worse; a fraction of 0.05-0.1 fits the envelopes a little better but darkens the late tail.
        constexpr double kLateTapFraction[2] = {0.18, 0.15}, kLateTapWeight[2] = {0.9, 0.9};
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
        constexpr float kPi = 3.14159265f;
        // Switching: reflection, pre-delay and 1st-reflection changes crossfade (equal power) over this time.
        constexpr double kLayoutFadeMs = 30.0;
        // Space modulation (inference, Q-06: the REV5 manual gives the 0-10 control but no rate or depth). Each loop's
        // delay swings sinusoidally by up to +/- kSpaceModMaxMs at 10, at its own slow rate and starting phase, so the
        // modes drift apart and the tail loses its metallic ring without an obvious pitch wobble (4 to 11 cents peak at
        // 10). Rates are not multiples of each other; phases start fixed at reset so renders repeat.
        constexpr double kSpaceModMaxMs = 0.8;
        // Era-7 time variance (ADR 0007): the drift's gains are recomputed every kVarBlock samples, and each
        // process's period is the set period x 0.7..1.3 (fixed per seed), so the walks do not move in step.
        constexpr int kVarBlock = 32;
        constexpr float kVarRateSpread = 0.6f;
        constexpr float kVarMaxShare = 0.9f;
        // The drift is added as a difference (varied read minus static read) high-passed here (second order), so the
        // late bass stays static: the REV7's two sweeps agree within 0.5 dB at 63-250 Hz late, our unsplit drift moved
        // it 2-4 dB (a few loop modes carry the bass), while the 250 Hz octave still decorrelates (pass 71).
        constexpr double kVarHpHz = 120.0;
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
        // Pass 64 (2026-11-25): the REV5 reverb programs play single taps (the ring 0.3 ms after each is -34
        // dB, the converter's; our all-pass gave a four-spike cluster 0.71 ms apart at -10 dB per spike, and on
        // Percussion Plate's taps 0.4-1.5 ms apart a comb), so the Rev modes take 0 (the ring was the
        // capture chain's, as in pass 36); the charted modes keep theirs.
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
        // REV7 E/R2 (forty-sixth pass): the hardware captures of the six REV7 E/R2 programs show the E/R1 taps passed
        // dry (06's first 10 ms correlate 0.96 with 05's) plus a parallel, not very dense tail, so era 7 now adds the
        // diffuser beside the taps instead of running them through it. Fitted per chart mode on the 0-250 ms
        // envelope:
        // Small Hall and Reverse (06, 24) a short 100 ms tail at lengths x0.7, Plate (20) 200 ms, Spring (25, 28) a
        // tail at the full lengths that starts 20 ms late and outweighs the taps. Large Hall and Random have no
        // capture that the tail moves (30, 23) and take the Small Hall voice (inference). Dry and wet are scaled to
        // unit power, so the E/R level stays the series chain's.
        // REV5 E/R2 (forty-seventh pass): the REV5 captures show the same form (Early Ref.2's first 10 ms
        // -4 dB re the 0-250 ms energy, the series chain's -16), so era 5 runs beside its taps too, per chart mode:
        // Small Hall (06) T60 100 ms at lengths x0.5, Plate (20) 200 ms at x1.0; Reverse (24 Reverse Gate) stays in
        // series, at T60 60 ms and x0.85 (its peak falls 37 dB in 90 ms). Both channels at the same lengths (pass 28).
        // Large Hall (30 Live Reference) moves little with any setting and takes the Small Hall voice (inference).
        // Pass 53: on the hall charts only the first six taps play dry. Both captures show them at the chart's times
        // and levels (30 at size 2.5: 0 / 3.4 / 6.9 / 10.8 / 16.1 / 23.5 ms against 0 / 4.0 / 6.7 / 10.5 / 16.3
        // / 22.5), then a field 15-25 dB down where the chart's later taps fall: so only six pass dry (all taps fed the
        // tail until pass 56, only the rest since), the tail at half weight (scratch grid: dry 0 / 3 / 5 / 6 / 8, wet
        // 0.1-4, T60 100-300 ms, x0.5-1.0). Envelope 06 2.2 -> 2.2 dB rms (inference: two programs; the plate and
        // Reverse voices keep every tap dry). The Large Hall chart's 28 later taps carry far more energy than Small
        // Hall's, and 30's field sits 20-25 dB under its six dry taps, so Large Hall has a voice of its own: the tail
        // at 0.2, T60 200 ms, full lengths (scratch grid x0.5-1.0, T60 100-300, wet 0.05-0.3): 30 8.3 -> 2.4 dB rms.
        // Pass 54: the REV7 capture of its 30 Live Reference (Large Hall, size 2.5) plays three taps dry (0 / 6.3 /
        // 11.5 ms), then a field 18-25 dB down, so era 7's Large Hall takes three dry taps, the tail at 0.2, T60
        // 100 ms (scratch grid: dry 0 / 2 / 3 / 4, x0.7-1.0, T60 100-300, wet 0.2-1.4): 30 6.8 -> 2.7 dB rms. 23 Gate
        // Reverb (Random) plays three taps too, but no split with them in the tail helps it (best 4.7 all dry against
        // 5.2); pass 56 gives Random its own voice with them kept out (voice 9).
        // Era-5 Piano (pass 52): the REV5 capture of 25 Rehearsal Room, an E/R1 program, is a continuous field
        // to 250 ms (T60 about 0.4 s) that 34 taps cannot fill, so Piano always plays its taps with a parallel tail
        // (T60 250 ms, lengths x0.7; scratch grid T60 120-600 ms, x0.5-1.0, wet 0.7-6), whatever the E/R type
        // (inference: one program, one setting).
        // Pass 57: the Plate voices from the hardware captures of Electric Bass B (20, liveness 3) and Kick (21,
        // liveness 1; it plays E/R2 against the chart's E/R1, Q-28), both units, which agree: a tail at short lengths
        // (x0.55) that outweighs the taps (wet 2.8 / 2.0), T60 about 280 / 220 ms at liveness 3 and 100 / 80 ms at 1,
        // so the T60 follows LIVENESS, x1.67 per step (inference: two settings per unit; scratch grid x0.4-0.85, T60
        // 60-360 ms, wet 1.4-4). 0-250 ms envelope REV7 20 2.96 -> 2.63, 21 61 (no tail) -> 1.82 dB rms; REV5 20
        // 4.17 -> 2.29, 21 51 -> 2.53. 20's 1 kHz dip (the size-0.4 taps' comb) stays: the colour is in the taps.
        // Pass 58: REV5 06 Early Ref.2's tail after 100 ms rang as a comb (harmonics of 198 Hz, the 4.8 ms all-pass,
        // 36 dB over the median) where the capture's is broadband: the Small Hall voice at lengths x0.4, T60 150 ms,
        // tail 0.3, right x1.15 (the channels' combs interleave); 0-250 ms envelope 4.15 -> 2.34, 100-200 ms colour 7.5
        // -> 3.9 dB rms. 24 Reverse Gate after its close decayed too fast and too bright: T60 80 ms at x1.0, right
        // x1.15; envelope 10.5 -> 1.3, colour 5.9 -> 3.0 (scratch grids: x0.3-1.0, T60 50-200 ms, tail 0.1-1.4; one
        // program each).
        // Pass 59: the REV7 Spring captures (25 Piano, size 1.0; 28 Guitar, 1.5) are the tail: their first visible taps
        // sit 20-28 dB under the 1st reflection, under a field that rises to 65-75 ms. The tail now outweighs the taps
        // further (weight 8, T60 300 ms, lengths x0.7, 30 ms after the taps): 0-250 ms envelope 25 3.10 -> 1.56,
        // 28 2.70
        // -> 1.50 dB rms (scratch grid x0.55-1.0, T60 150-550, weight 2-16, delay 10-35 ms). The size-4.0 chart scaled
        // at every size (pass 48's Random rule) reads 2.6 / 3.0 at best, so the size-1.0 blend stays (inference).
        // Pass 70: the hardware Plate E/R2 tails recirculate at one period, both units alike: Electric Bass B (size 0.4)
        // 13.1 / 13.2 ms (L / R), Kick (0.3) 8.7 / 8.75 ms, where ours rang at the 8.4 ms all-pass smeared by the
        // short stages. The Plate voices' longest stage is now a loop of its own, 8.67 ms x (size / 0.3)^1.43 (room
        // size chosen over liveness, which differs between the two captures too: size scales the program's delays and
        // liveness already sets the decay; the law fits two points; inference), R x1.01, and the short stages decay
        // faster (shortT60). Scratch grid on the 0-250 ms envelope: REV7 20 / 21 2.76 / 1.78 -> 1.69 / 1.39, REV5
        // 3.33 / 3.71 -> 2.94 / 3.45 dB rms; tail autocorrelation at the period 0.32-0.40 -> 0.45-0.59 (0.56-0.70).
        // Pass 88: 06's tail then still read 9.6 dB over its capture at 200-400 ms and 12-14 dB too bright at 4-10 kHz
        // over 100-300 ms: four all-passes in series decay slower than one, and our tail has no treble loss where the
        // capture's treble dies with its mid band. The halls' voice now has a one-pole low-pass in each stage's
        // feedback (DampedAllPass; 19.75 kHz, 0.06 at 44.1 kHz) and T60 150 -> 130 ms (scratch grid: damping 0.03-0.6,
        // T60 120-200, tail 0.7-1.1; a low-pass on the tail's output instead cost the 0-50 ms colour, 1.9 -> 4-8 dB
        // rms): 06 colour 0-50 / 100-300 ms 1.91 / 7.42 -> 1.75 / 5.99, envelope 1.84 -> 1.09, features 4.21 -> 2.28 dB
        // rms. Pass 86: with the REV5 S-Hall times re-measured on 05 / 06 (L taps 2 and 3 coincide), 06's tail read
        // 9-11 dB under its capture after 20 ms; the halls' tail weight 0.3 -> 0.7 (scratch grid 0.3-1.5, T60 120-250
        // ms, x0.4-0.6): 0-250 ms envelope 4.73 -> 1.84, pass 44's features 6.75 -> 4.21 dB rms, nothing worse.
        constexpr float kEr2PlateLiveStep = 1.67f;
        constexpr double kEr2T60MinMs = 40.0, kEr2T60MaxMs = 800.0;
        struct Er2Voice
        {
            float lenScale;           // x kEr2Ms
            double t60Ms;             // the all-passes' decay
            float wet;                // tail against the taps (before the unit-power scaling); 0 = series
            float preMs;              // the tail's delay after the taps
            float rScale;             // right-channel lengths
            int dryTaps;              // beside the taps: only the first this many play dry (0 = all dry)
            bool restOnly;            // with dryTaps: only the taps after them feed the tail (pass 56)
            float livePerStep = 1.0f; // t60Ms is at liveness 3; x this per liveness step from 3 (1 = fixed; pass 57)
            float loopMs = 0.0f; // > 0: the longest stage this long at room size 0.3, x size^kEr2LoopSizeExp (pass 70)
            float shortT60 = 1.0f; // the three shorter stages' decay, x t60Ms (pass 70)
            float dampHz = 0.0f;   // > 0: a one-pole low-pass this high in each stage's feedback (pass 88)
        };
        constexpr Er2Voice kEr2Voice[] = {
            {0.7f, 100.0, 1.4f, 0.0f, 1.15f, 0, false}, // 0 REV7 Small / Large Hall, Random, Reverse, other charts
            {0.55f, 400.0, 2.8f, 0.0f, 1.01f, 0, false, kEr2PlateLiveStep, 8.67f, 0.25f}, // 1 REV7 Plate (57, 70)
            {0.7f, 300.0, 8.0f, 30.0f, 1.15f, 0, false},                                  // 2 REV7 Spring (pass 59)
            {0.4f, 130.0, 0.7f, 0.0f, 1.15f, 6, true, 1.0f, 0.0f, 1.0f,
             19750.0f}, // 3 REV5 halls, Random, other charts (passes 47, 53, 56, 58, 86, 88)
            {0.55f, 330.0, 1.0f, 0.0f, 1.01f, 0, false, kEr2PlateLiveStep, 8.67f, 0.333f}, // 4 REV5 Plate (57, 70)
            {1.0f, 80.0, 0.0f, 0.0f, 1.15f, 0, false},  // 5 REV5 Reverse (series; pass 58)
            {0.7f, 250.0, 1.4f, 0.0f, 1.0f, 0, false},  // 6 REV5 Piano, E/R1 or E/R2 (pass 52)
            {1.0f, 200.0, 0.2f, 0.0f, 1.0f, 6, true},   // 7 REV5 Large Hall (passes 53, 56)
            {1.0f, 100.0, 0.2f, 0.0f, 1.15f, 3, false}, // 8 REV7 Large Hall (pass 54)
            {0.3f, 100.0, 0.85f, 0.0f, 1.15f, 3, true}, // 9 REV7 Random (pass 56)
        };
        constexpr int kNumEr2Voices = (int) std::size (kEr2Voice);
        constexpr float kEr2Ms[4] = {4.0f, 6.2f, 9.6f, 15.2f};
        constexpr float kEr2RScaleMax = 1.15f, kEr2LenScaleMax = 1.0f;
        constexpr float kEr2LoopSizeRef = 0.3f, kEr2LoopSizeExp = 1.43f, kEr2LoopMinMs = 4.0f, kEr2LoopMaxMs = 25.0f;
        constexpr double kEr2MaxPreMs = 35.0;
        constexpr int kErModeLargeHall = 1, kErModeRandom = 2, kErModeReverse = 3, kErModePlate = 4, kErModeSpring = 5;
        constexpr float kErDiffPerStep = 0.0f, kErDiffPerStepEr = 0.024f; // Rev modes 0 since pass 64 (was 0.06)
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
        // Rev modes (12-20) play the reverb programs' own early taps, read from the hardware captures: in era 5 the
        // REV5's (er::kRevPatterns5, one set per mode, pass 41), in era 7 the REV7's three per channel
        // (er::kRevPatterns7, pass 40; Strings and Combi, which the REV7 lacks, play its Rev Plate and Rev Hall sets).
        // The chart entries below are unused for them.
        constexpr int kModeToPattern[21] = {0, 1, 2, 3, 4, 5, 2, 3, 0, 1, 4, 5, 1, 4, 0, 4, 0, 0, 1, 4, 1};
        constexpr int kPanModeA = 6, kPanModeB = 7; // er_mode indices of Pan A and Pan B
        // Era-5 Piano (pass 52, the REV5 capture of 25 Rehearsal Room): the field sits this far under the
        // charts' level (kErLevel5) against the 1st reflection, and the right channel this much over the left.
        constexpr float kPianoLevelDb = -8.3f, kPianoRightDb = 4.4f;
        constexpr int kPianoMode = 8; // era 5: er::kPianoPattern5 (pass 52), not the stand-in entry above
        constexpr int kRevHallMode = 12, kRevPlateMode = 13, kRevRoomMode = 14; // reverb-program taps
        static_assert (isRevTapMode (kRevHallMode) && isRevTapMode (kRevRoomMode) && !isRevTapMode (kRevHallMode - 1));
        // Era-7 Rev sets: the initial delay only offsets them (pass 40, the REV7 captures: Vocal Plate at 45 ms
        // and Strings at 13 ms share +5.0 / +17.0 ms; Small Hall at 20 ms, Handclaps and Reverb Flange at 0.1 ms share
        // +15.0 / +34.0 ms). Pass 30's scaling, read from captures of unknown programs, is reverted.
        // Era-7 set per Rev mode (12-20): its own seven, then Strings on Rev Plate and Combi on Rev Hall.
        // pass 67: the level of a Rev set beside a chart against the reverb's late share (fitted on the REV5
        // capture of 98 at its printed REV DLY; 1.0 / 1.4 / 2.0: 0-200 ms envelope 2.59 / 2.35 / 2.6 dB rms)
        constexpr float kRevTapsLevel = 1.4f;
        constexpr int kRevSet7For[9] = {0, 1, 2, 3, 4, 5, 6, 1, 0};
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
                             float hiHz, double subRatio = 1.0, double subHz = kSubHz)
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
                double m = l.loShelf.magnitude (fs, kMidRefHz) * l.hiShelf.magnitude (fs, kMidRefHz);
                if constexpr (requires { l.subShelf; })
                {
                    // pass 83: bypassed at 0 dB (most era-5 programs), so it costs nothing there
                    const float subDb = gainToDb (loopGain (total, fs, rtLo * subRatio) / gLo);
                    l.subOn = std::abs (subDb) > 1.0e-4f;
                    l.subShelf.setLowShelf (fs, (float) subHz, kShelfQ, subDb);
                    if (l.subOn)
                        m *= l.subShelf.magnitude (fs, kMidRefHz);
                }
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
        const double cloudSpanMs = (double) kCloudCellMs * kMaxCloudTaps;
        input.prepare (
            msToSamples (kMaxPreDelayMs + std::max (maxErMs, cloudSpanMs) + kMaxRef1Ms + kVoiceLateOnsetMaxMs, cap) +
            16);
        // the early cloud's sequence (pass 84): fixed seeds, so every render repeats; one set of places, a sign per
        // side
        {
            uint32_t r = kCloudSeed;
            auto rnd = [&r] () noexcept
            {
                r = r * 1664525u + 1013904223u;
                return (float) (r >> 8) * (1.0f / 16777216.0f);
            };
            for (size_t k = 0; k < (size_t) kMaxCloudTaps; ++k)
            {
                cloudMs[k] = kCloudCellMs * ((float) k + rnd ());
                cloudSign[0][k] = rnd () < 0.5f ? -1.0f : 1.0f;
                cloudSign[1][k] = rnd () < 0.5f ? -1.0f : 1.0f;
            }
        }
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
                ch[k].prepare (
                    nextPrime (msToSamples (
                        std::max (kEr2Ms[k] * kEr2LenScaleMax, k == 3 ? kEr2LoopMaxMs : 0.0f) * kEr2RScaleMax, cap)) +
                    4);
        for (auto& l : er2Pre)
            l.prepare (msToSamples (kEr2MaxPreMs, cap) + 4);
        for (size_t c = 0; c < erFillAp.size (); ++c)
            for (size_t k = 0; k < erFillAp[c].size (); ++k)
                erFillAp[c][k].prepare (nextPrime (msToSamples (kErFillMs[c][k] * kErFillSizeMax, cap)) + 4);
        rev2Pre.prepare (msToSamples (kRev2MaxDelayMs, cap) + 4);
        for (int i = 0; i < kNumLoops2; ++i)
        {
            loops2[(size_t) i].line.prepare (nextPrime (msToSamples (kLoop2Ms[i], cap)) + 4);
            loops2[(size_t) i].ap.prepare (nextPrime (msToSamples (kLoop2ApMs[i], cap)) + 4);
        }
        maxFs = cap;
        setSampleRate (sampleRate > 0.0 ? sampleRate : 48000.0);
    }

    void Engine::applyEr2 (int set, float live, float size)
    {
        // g = 10^(-3 M / T60) per all-pass; the lines were sized for the longest voice in prepare()
        er2Set = std::clamp (set, 0, kNumEr2Voices - 1);
        er2Live = live;
        er2Size = size;
        const auto& v = kEr2Voice[(size_t) er2Set];
        const double t60 =
            std::clamp (v.t60Ms * std::pow ((double) v.livePerStep, (double) live - 3.0), kEr2T60MinMs, kEr2T60MaxMs);
        const float loopMs =
            v.loopMs > 0.0f
                ? std::clamp (v.loopMs * std::pow (std::max (size, 0.1f) / kEr2LoopSizeRef, kEr2LoopSizeExp),
                              kEr2LoopMinMs, kEr2LoopMaxMs)
                : 0.0f;
        // pass 88: the feedback low-pass, matched-z from its corner (0.06 at era 5's 44.1 kHz)
        const float damp = v.dampHz > 0.0f ? (float) std::exp (-kTwoPi * v.dampHz / fs) : 0.0f;
        double direct = 1.0; // the chain's first output sample per unit input (left), the product of the -g
        for (size_t c = 0; c < er2Diff.size (); ++c)
            for (size_t k = 0; k < er2Diff[c].size (); ++k)
            {
                const bool loop = loopMs > 0.0f && k == 3;
                // the loop stage at its exact length (a prime would move R's period up to 2 %; it rings alone)
                const int len = loop
                                    ? msToSamples (loopMs * (c == 1 ? v.rScale : 1.0f), fs)
                                    : nextPrime (msToSamples (kEr2Ms[k] * v.lenScale * (c == 1 ? v.rScale : 1.0f), fs));
                const double g =
                    std::pow (10.0, -3.0 * (1000.0 * len / fs) / (loopMs > 0.0f && !loop ? t60 * v.shortT60 : t60));
                er2Diff[c][k].setLength (len);
                er2Diff[c][k].setGain ((float) g);
                er2Diff[c][k].setDamping (damp);
                direct *= c == 0 ? -g : 1.0;
            }
        // unit power: the chain keeps the energy, and its direct sample adds coherently to the dry taps when the
        // tail is not delayed
        er2Parallel = v.wet > 0.0f;
        er2DrySplit = er2Parallel && v.dryTaps > 0;
        er2RestOnly = er2DrySplit && v.restOnly;
        const double coh = (v.preMs > 0.0f || er2RestOnly) ? 0.0 : 2.0 * v.wet * direct; // rest only: other taps
        const float norm = (float) (1.0 / std::sqrt (std::max (1.0e-3, 1.0 + (double) v.wet * v.wet + coh)));
        er2DryG = er2Parallel ? norm : 0.0f;
        er2WetG = er2Parallel ? v.wet * norm : 1.0f;
        er2PreLen = std::clamp (msToSamples (v.preMs, fs), 0, er2Pre[0].capacity ());
    }

    void Engine::setSampleRate (double sampleRate)
    {
        // no allocation: every line was sized in prepare() for maxFs
        fs = std::clamp (sampleRate, 8000.0, maxFs);
        erTopHzSet = -1.0f;    // redesign the reflection top-octave low-pass for the new rate
        erFillSizeSet = -1.0f; // and the early fill's lengths
        tiltFresh = true;
        erLoHzSet = -1.0f; // and the reflection low shelf (pass 84)
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
        applyEr2 (er2Set < 0 ? 0 : er2Set, er2Live, er2Size);
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
            l.subShelf.reset ();
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
        erAvgPrev[0] = erAvgPrev[1] = erAvgPrev2[0] = erAvgPrev2[1] = 0.0f;
        lateAvgPrev.fill (0.0f);
        lateAvgPrev2.fill (0.0f);
        for (auto& f : erTop)
            f.reset ();
        for (auto& f : erTilt)
            f.reset ();
        for (auto& f : erLo)
            f.reset ();
        for (auto& f : lateTilt)
            f.reset ();
        for (auto& l : erFbLine)
            l.reset ();
        erFbLp[0] = erFbLp[1] = 0.0f;
        for (auto& a : erDiff)
            a.reset ();
        for (auto& l : er2Pre)
            l.reset ();
        for (auto& ch : er2Diff)
            for (auto& a : ch)
                a.reset ();
        for (auto& ch : erFillAp)
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
        const double def = (double) kHiShelfDefaultHz * hiXoverScale;
        hiShelfHz = (float) std::min ((double) hiXoverHz, std::max (def, kHiShelfMaxFrac * fs));
        // the reference frequency: fixed at and above the default corner, lower below it
        const double ref = hiShelfHz < def ? hiRefHz * std::pow ((double) hiShelfHz / def, kHiRefFollow) : hiRefHz;
        // fraction of the high shelf's plateau (in dB) reached at the reference, for the hi_ratio mapping
        BasicBiquad<double> probe;
        probe.setHighShelf (fs, hiShelfHz, kHiShelfQ, -6.0f);
        hiRefFrac = std::clamp (20.0 * std::log10 (probe.magnitude (fs, std::min (ref, 0.45 * fs))) / -6.0, 0.05, 1.0);
    }

    double Engine::hiPlateau (double hiRatio) const
    {
        // decay rate at the reference = mid rate + hiRefFrac x (plateau rate - mid rate); solve for the plateau ratio
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
        static_assert (sizeof (EngineParams) == 62 * 4);
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
        const float hs = std::clamp (p.hiXoverScale, 0.5f, 2.0f);
        const float hx = std::clamp (p.hiXoverHz, kVoiceHiXoverMin, kVoiceHiXoverMax) * hs;
        const float hr = std::max (1000.0f, p.hiRefHz);
        if (std::abs (hx - hiXoverHz) > 1.0e-3f || std::abs (hr - hiRefHz) > 1.0e-3f ||
            std::abs (hs - hiXoverScale) > 1.0e-6f)
        {
            hiXoverHz = hx; // the hi_ratio mapping follows the corner (prepare() measures it for a new rate)
            hiRefHz = hr;
            hiXoverScale = hs;
            measureHiRef ();
        }

        const double rt = std::clamp ((double) p.rt, (double) kMinRt, (double) kMaxRt);
        const double rtLo = bassRt (rt, (double) p.loRatio);
        // pass 75: the treble decay floors at hiRtFloorSec (era 7; REV7 Handclaps, HIGH 0.1 at RT 0.4 s, decays about
        // 0.4 s at 4-8 kHz in its capture)
        const double rtHi = std::max (rt * hiPlateau ((double) p.hiRatio),
                                      std::min (rt, (double) std::clamp (p.hiRtFloorSec, 0.0f, 2.0f)));

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

        const int patIndex = kModeToPattern[std::clamp (p.erMode, 0, 18)];
        // Rev modes: the reverb programs' own taps, from the initial delay, with no room size, liveness or reflection
        // count (the reverb programs have none; REV5 OM pp. 16-18, REV7 OM p. 13). Era 5: one set per mode, 3-29 taps
        // (REV5 captures, pass 41; density no longer picks a set). Era 7: three per
        // channel ("3 early reflections are programmed for each channel", REV7 OM p. 14, OCR), the first at the
        // initial delay on both channels, offset by it, not scaled (REV7 captures, pass 40); Spring and Echo
        // Room play four.
        const bool rev = isRevTapMode (p.erMode);
        const int revIndex = std::clamp (p.erMode - kRevHallMode, 0, 8);
        static_assert (kRevPlateMode - kRevHallMode == 1 && kRevRoomMode - kRevHallMode == 2); // revIndex order
        static_assert (er::kNumRevPatterns5 == 9 && er::kNumRevPatterns7 == 7 && isRevTapMode (kRevHallMode + 8) &&
                       !isRevTapMode (kRevHallMode + 9));
        // Era-5 Piano: a pattern measured from the REV5 capture of 25 Rehearsal Room (pass 52), scaled by
        // room size and faded by liveness like the charts; not a printed chart, so no kChartFade5.
        const bool piano = p.erTableSet == 1 && p.erMode == kPianoMode;
        const auto& pat =
            rev     ? (p.erTableSet == 1 ? er::kRevPatterns5[revIndex] : er::kRevPatterns7[kRevSet7For[revIndex]])
            : piano ? er::kPianoPattern5
            : p.erTableSet == 1 && patIndex == 0 && p.erPrinted != 0 ? er::kShallPrinted5 // pass 86
            : p.erTableSet == 1                                      ? er::kPatterns5[patIndex]
                                                                     : er::kPatterns[patIndex];
        const float scale = rev ? 1.0f : std::clamp (p.roomSize, 0.1f, kMaxRoomSize) / kChartRoomSize;
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
        const bool fallingChart = patIndex <= kLastFallingPattern && !piano;
        const float chartFade = (p.erTableSet == 1 && !rev && !pan && fallingChart) ? kChartFade5 : 1.0f;
        const float flatten = chartFade * ((!pan && live > 5.0f) ? 1.0f - 0.16f * (live - 5.0f) : 1.0f);
        const float extraPer100 = (!pan && live < 5.0f) ? -kLivenessDbPer100Ms * (5.0f - live) : 0.0f;
        const float feedDbPerMs = -60.0f * kLateFeedFade / (float) (rt * 1000.0);
        // era 5: the reflection output passes the diffusion all-pass, so it is read that much early (kErDiffMs); the
        // late feed keeps the charted times (shifting it too combs the late input against the direct feed)
        next.erEarly = p.erTableSet == 1 ? erDiffLen : 0;
        // asRev: a Rev set (all its taps, unscaled, as charted); beside (pass 67): from the late onset, which the feed
        // then keeps
        auto fill = [&] (const er::Tap* src, int count, std::array<Tap, kMaxErTaps>& dst, bool asRev = false,
                         bool beside = false) -> int
        {
            const bool r = rev || asRev;
            const int n = r ? std::min (count, (int) dst.size ())
                            : std::clamp (p.erCount, 1, std::min (count, (int) dst.size ()));
            double energy = 0.0, feedEnergy = 0.0;
            for (int k = 0; k < n; ++k)
            {
                const float ms = src[k].ms * (r ? 1.0f : scale);
                auto& t = dst[(size_t) k];
                t.delay = std::clamp ((beside ? next.lateDirect : preDelay) + msToSamples (ms, fs), 0, cap);
                t.feedDelay = std::clamp (t.delay + (beside ? 0 : onset), 0, cap);
                const float db = r ? src[k].db : src[k].db * flatten + extraPer100 * ms * 0.01f;
                t.gain = src[k].sign * std::pow (10.0f, db / 20.0f); // inverted taps: the Rev sets (pass 65), REV7
                                                                     // Small Hall and REV5 Plate charts (passes 66, 69)
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
                out[(size_t) k].sign = a[ia].sign; // the size-4.0 chart's polarity (pass 66)
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
            if (piano)
            {
                // the measured field's level against the 1st reflection, per channel (REV5 25: R the louder)
                const float gl = std::pow (10.0f, (kPianoLevelDb - kPianoRightDb * 0.5f) / 20.0f),
                            gr = std::pow (10.0f, (kPianoLevelDb + kPianoRightDb * 0.5f) / 20.0f);
                for (int k = 0; k < next.numTapsL; ++k)
                    next.tapsL[(size_t) k].gain *= gl;
                for (int k = 0; k < next.numTapsR; ++k)
                    next.tapsR[(size_t) k].gain *= gr;
            }
        }

        // ER/REV BAL as on the REV5: 100 % early reflections only, 0 % reverb only, about 50 % equal (OM p. 18).
        // bal below is the late share: 1 = late only.
        const float bal = 1.0f - std::clamp (p.erRevBalance, 0.0f, 100.0f) * 0.01f;
        // pass 67: a reverb program's own taps beside the chart (revTaps), from the late onset (REV DLY), part of the
        // reverb; they feed the late network and the chart does not (REV5 98: the combinations' network, early taps
        // and all, 21.8 ms after the S-Hall reflections; the REV5 capture, pass 55)
        if (p.revTaps > 0 && !rev)
        {
            const int ri = std::clamp (p.revTaps - 1, 0, 8);
            const auto& rp = p.erTableSet == 1 ? er::kRevPatterns5[ri] : er::kRevPatterns7[kRevSet7For[ri]];
            next.numRevL = fill (rp.left, rp.countL, next.revL, true, true);
            next.numRevR = fill (rp.right, rp.countR, next.revR, true, true);
            next.revGain = std::min (1.0f, 2.0f * bal) * kRevTapsLevel;
            for (auto* taps : {&next.tapsL, &next.tapsR})
                for (auto& t : *taps)
                    t.feed = 0.0f;
        }
        next.erGain = std::min (1.0f, 2.0f * (1.0f - bal)) *
                      std::pow (10.0f, std::clamp (p.erGainDb, -kVoiceErGainMaxDb, kVoiceErGainMaxDb) / 20.0f);
        // pass 84: the early cloud from the pre-delay, at the era's reflection level (unit energy x erLevel, as the
        // chart), heard at cloudMix in place of the chart
        next.cloudMix = std::clamp (p.erCloud, 0.0f, 1.0f);
        next.numCloud = 0;
        if (next.cloudMix > 0.0f)
        {
            const float hold = std::clamp (p.erCloudHoldMs, 0.0f, kCloudHoldMaxMs);
            const float fade = std::clamp (p.erCloudFadeDb, kCloudFadeMin, kCloudFadeMax);
            const float rise = std::clamp (p.erCloudRiseDb, kCloudRiseMinDb, 0.0f);
            const float erLevel = p.erTableSet == 1 ? kErLevel5 : kErLevel7;
            int n = 0;
            double energy = 0.0;
            for (size_t k = 0; k < (size_t) kMaxCloudTaps; ++k)
            {
                const float ms = cloudMs[k];
                const float db =
                    ms <= hold ? rise * (1.0f - ms / std::max (hold, 1.0e-3f)) : -fade * (ms - hold) * 0.1f;
                if (db < kCloudFloorDb)
                    break;
                auto& t = next.cloud[(size_t) n++];
                const float g = std::pow (10.0f, db / 20.0f);
                t.delay = std::clamp (preDelay + msToSamples (ms, fs), 0, cap);
                t.gainL = cloudSign[0][k] * g;
                t.gainR = cloudSign[1][k] * g;
                energy += (double) g * g;
            }
            const float norm = erLevel / (float) std::sqrt (std::max (energy, 1.0e-9));
            for (int k = 0; k < n; ++k)
            {
                next.cloud[(size_t) k].gainL *= norm;
                next.cloud[(size_t) k].gainR *= norm;
            }
            next.numCloud = n;
        }
        // E/R2: the charted modes only (the Rev modes are the reverb programs' own reflections); each era its own
        // decay and channel scale. Chosen here because the voice's dry-tap count is part of the layout.
        const bool pianoMode = p.erTableSet == 1 && p.erMode == kPianoMode;
        const bool er2Next = (p.erType == 1 || pianoMode) && !rev;
        const int er2Voice = pianoMode           ? 6
                             : p.erTableSet == 1 ? (p.erMode == kErModePlate       ? 4
                                                    : p.erMode == kErModeReverse   ? 5
                                                    : p.erMode == kErModeLargeHall ? 7
                                                                                   : 3)
                                                 : (p.erMode == kErModePlate       ? 1
                                                    : p.erMode == kErModeSpring    ? 2
                                                    : p.erMode == kErModeLargeHall ? 8
                                                    : p.erMode == kErModeRandom    ? 9
                                                                                   : 0);
        next.er2DryTaps = er2Next ? kEr2Voice[(size_t) er2Voice].dryTaps : 0;
        if (next.er2DryTaps > 0)
        {
            // unit power with only the first taps dry: dry share f of the tap energy plus the tail at w^2 (the two
            // add as uncorrelated) against 1 + w^2 when every tap plays dry (the REV5 05 and 06 captures,
            // E/R1 and E/R2 at the same settings, carry the same energy)
            double all = 0.0, dry = 0.0;
            for (const auto* taps : {&next.tapsL, &next.tapsR})
            {
                const int n = taps == &next.tapsL ? next.numTapsL : next.numTapsR;
                for (int k = 0; k < n; ++k)
                {
                    const double g2 = (double) (*taps)[(size_t) k].gain * (*taps)[(size_t) k].gain;
                    all += g2;
                    dry += k < next.er2DryTaps ? g2 : 0.0;
                }
            }
            // (rest only, pass 56: the tail hears only the taps after the dry ones, so it carries w^2 (1 - f))
            const auto& v = kEr2Voice[(size_t) er2Voice];
            const double f = all > 0.0 ? dry / all : 1.0, w2 = (double) v.wet * v.wet;
            next.erGain *= (float) std::sqrt ((1.0 + w2) / std::max (1.0e-6, f + w2 * (v.restOnly ? 1.0 - f : 1.0)));
        }
        static_assert (sizeof (Layout) == (4 * kMaxErTaps * 4 + 3 * kMaxCloudTaps + 19) * 4);
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
        const float dg = std::min (kDiffusionGainMax,
                                   kDiffusionEraScale[tapEra] * kDiffusionMax * std::clamp (p.diffGain, 0.5f, 2.0f) *
                                       std::pow (std::clamp (p.diffusion, 0.0f, 10.0f) / 10.0f, kDiffusionCurve));
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
            setLoopFilters (l, total, fs, rt, rtLo, rtHi, loXoverHz, hiShelfHz,
                            kSubRtRatio[tapEra] * (double) std::clamp (p.subRatio, 0.5f, 2.0f),
                            (double) std::clamp (p.subHz, kSubHzMin, kSubHzMax));
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
        er2On = er2Next;
        const float er2LiveNext = std::clamp (p.liveness, 0.0f, 10.0f);
        const float er2SizeNext = std::clamp (p.roomSize, 0.1f, kMaxRoomSize);
        if (er2On && (er2Set != er2Voice || std::not_equal_to<float>{}(er2Live, er2LiveNext) ||
                      (kEr2Voice[(size_t) er2Voice].loopMs > 0.0f && std::not_equal_to<float>{}(er2Size, er2SizeNext))))
            applyEr2 (er2Voice, er2LiveNext, er2SizeNext);
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

        erAvgU = std::clamp (p.erTapAverage, 0.0f, 0.5f);
        lateAvgU = std::clamp (p.lateTapAverage, 0.0f, 0.5f);
        erAvgU2 = std::clamp (p.erTapAverage - 0.5f, 0.0f, 0.5f);
        lateAvgU2 = std::clamp (p.lateTapAverage - 0.5f, 0.0f, 0.5f);
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
            // pass 84: the reflection low shelf
            const float lo = std::clamp (p.erLoTiltDb, -kVoiceErLoMaxDb, kVoiceErLoMaxDb);
            const float loHz = std::min (std::clamp (p.erLoHz, kVoiceErLoHzMin, kVoiceErLoHzMax), (float) (0.4 * fs));
            if (std::abs (lo - erLoSet) > 1.0e-3f || std::abs (loHz - erLoHzSet) > 1.0e-3f)
                for (auto& f : erLo)
                    f.setLowShelf (fs, loHz, kVoiceErLoQ, lo);
            erLoSet = lo;
            erLoHzSet = loHz;
            erLoOn = std::abs (lo) > 1.0e-3f;
            erTiltOn = std::abs (et) > 1.0e-3f;
            lateTiltOn = std::abs (lt) > 1.0e-3f;
            tiltFresh = false;
        }
        // pass 78: the early fill
        erFillG = std::clamp (p.erFill, 0.0f, kErFillMax);
        erFillOn = erFillG > 0.0f;
        if (const float sz = std::clamp (p.erFillSize, kErFillSizeMin, kErFillSizeMax);
            erFillOn && std::abs (sz - erFillSizeSet) > 1.0e-4f)
        {
            for (size_t c = 0; c < erFillAp.size (); ++c)
                for (size_t k = 0; k < erFillAp[c].size (); ++k)
                {
                    erFillAp[c][k].setLength (nextPrime (msToSamples (kErFillMs[c][k] * sz, fs)));
                    erFillAp[c][k].setGain (kErFillGain);
                }
            erFillSizeSet = sz;
        }
        erTopOn = p.erTopHz > 0.0f && p.erTopHz < kErTopMaxFrac * fs;
        if (erTopOn && std::abs (p.erTopHz - erTopHzSet) > 1.0f)
            for (auto& f : erTop)
                f.setLowPass (fs, p.erTopHz, kErTopQ);
        erTopHzSet = erTopOn ? p.erTopHz : -1.0f;

        // ---- space modulation ----
        modTarget = (double) std::clamp (p.spaceMod, 0.0f, 10.0f) * 0.1 * kSpaceModMaxMs * 0.001 * fs;

        // ---- era-7 time variance (ADR 0007) ----
        {
            const float share = std::clamp (p.lateVarShare, 0.0f, kVarMaxShare);
            if (share > 0.0f && !varOn)
                varCount = 0; // fresh gains on the next sample
            varOn = share > 0.0f;
            varAlpha = std::sqrt (1.0f - share);
            varBeta = std::sqrt (share);
            varStep = (float) kVarBlock / (float) (std::max (0.1f, p.lateVarSec) * fs);
            const double hpHz = kVarHpHz;
            if (varHpFs != fs + hpHz)
            {
                varHpFs = fs + hpHz;
                // RBJ high-pass, Q 1/sqrt 2
                const double w0 = kTwoPi * hpHz / fs, al = std::sin (w0) / std::sqrt (2.0), cw = std::cos (w0);
                const double a0 = 1.0 + al;
                for (auto& f : varHp)
                    f.setCoefficients ((1.0 + cw) / 2.0 / a0, -(1.0 + cw) / a0, (1.0 + cw) / 2.0 / a0, -2.0 * cw / a0,
                                       (1.0 - al) / a0);
            }
            // Pass 81: the static read passes a shelf S with |S|^2 = 1 - share |HP|^2, and the drift adds only the
            // varying part (gain - alpha), high-passed; the two are uncorrelated, so the late energy is flat at every
            // frequency. Pass 71 added (gain - 1) high-passed to the unfiltered static read, whose coherent part
            // (1 - HP + alpha HP) is not in phase with alpha HP: the late 63-350 Hz rose 0.3-1.1 dB at share 0.5 (the
            // REV7 reverbs' 300-800 ms 250 Hz median -1.1 -> -2.0 dB against the captures). S(s) = (alpha s^2 +
            // sqrt (2 alpha) s + 1) / (s^2 + sqrt 2 s + 1) at kVarHpHz, bilinear with the high-pass's prewarp.
            if (const double key = fs * 4.0 + (double) varAlpha; varOn && key != varStaticKey)
            {
                varStaticKey = key;
                const double k = 1.0 / std::tan (kPi * hpHz / fs), k2 = k * k, al = (double) varAlpha;
                const double n1 = std::sqrt (2.0 * al), d1 = std::sqrt (2.0);
                const double a0 = k2 + d1 * k + 1.0;
                for (auto& f : varStatic)
                    f.setCoefficients ((al * k2 + n1 * k + 1.0) / a0, 2.0 * (1.0 - al * k2) / a0,
                                       (al * k2 - n1 * k + 1.0) / a0, 2.0 * (1.0 - k2) / a0, (k2 - d1 * k + 1.0) / a0);
            }
        }

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

    float Engine::varNoise () noexcept
    {
        varRng ^= varRng << 13;
        varRng ^= varRng >> 17;
        varRng ^= varRng << 5;
        return (float) ((double) varRng / 2147483648.0 - 1.0); // -1..1
    }

    void Engine::seedVariance (uint32_t seed) noexcept
    {
        varRng = seed | 1u;
        for (auto& f : varHp)
            f.reset ();
        for (auto& f : varStatic)
            f.reset ();
        constexpr float kUnitVar = 1.7320508f; // uniform +-sqrt 3: unit variance
        for (size_t c = 0; c < 2; ++c)
            for (size_t j = 0; j < (size_t) kNumLoops; ++j)
            {
                varA[c][j] = kUnitVar * varNoise ();
                varB[c][j] = kUnitVar * varNoise ();
                varPh[c][j] = 0.5f * (varNoise () + 1.0f);
                varRate[c][j] = 1.0f / (1.0f + 0.5f * kVarRateSpread * varNoise ());
            }
        varCount = 0;
    }

    // The read-out gains for the next kVarBlock samples. Per channel the walks have their mean taken out and their rms
    // set to 1, so the varying part is orthogonal to the fixed one and each channel's late energy stays put (the loop
    // outputs are near uncorrelated and equal in level); gain = alpha + beta u with alpha^2 + beta^2 = 1, so two runs
    // with independent walks agree at about alpha^2 = 1 - share. The right read vector (signs x gains) is then made
    // orthogonal to the left one, as the static sign rows are: without that the drift correlates L and R (0.2-0.6 at
    // 100-400 ms) and L + R gains 1-1.5 dB (measured 2026-11-25; inference, ADR 0007).
    void Engine::stepVariance () noexcept
    {
        constexpr float kUnitVar = 1.7320508f;
        std::array<std::array<float, kNumLoops>, 2> w; // read vectors: sign x gain
        for (size_t c = 0; c < 2; ++c)
        {
            std::array<float, kNumLoops> u;
            float mean = 0.0f;
            for (size_t j = 0; j < (size_t) kNumLoops; ++j)
            {
                float& ph = varPh[c][j];
                ph += varStep * varRate[c][j];
                if (ph >= 1.0f)
                {
                    ph -= std::floor (ph);
                    varA[c][j] = varB[c][j];
                    varB[c][j] = kUnitVar * varNoise ();
                }
                const float x = 0.5f - 0.5f * std::cos (kPi * ph);
                u[j] = varA[c][j] + (varB[c][j] - varA[c][j]) * x;
                mean += u[j];
            }
            mean /= (float) kNumLoops;
            float ss = 0.0f;
            for (auto& v : u)
            {
                v -= mean;
                ss += v * v;
            }
            const float norm = ss > 1.0e-6f ? std::sqrt ((float) kNumLoops / ss) : 0.0f;
            const float* sg = c == 0 ? kSignL : kSignR;
            for (size_t j = 0; j < (size_t) kNumLoops; ++j)
                w[c][j] = sg[j] * (varAlpha + varBeta * norm * u[j]);
        }
        float dot = 0.0f, ll = 0.0f;
        for (size_t j = 0; j < (size_t) kNumLoops; ++j)
        {
            dot += w[0][j] * w[1][j];
            ll += w[0][j] * w[0][j];
        }
        const float k = ll > 1.0e-6f ? dot / ll : 0.0f;
        float rr = 0.0f;
        for (size_t j = 0; j < (size_t) kNumLoops; ++j)
        {
            w[1][j] -= k * w[0][j];
            rr += w[1][j] * w[1][j];
        }
        const float rn = rr > 1.0e-6f ? std::sqrt (ll / rr) : 1.0f; // the right channel at the left's energy
        for (size_t j = 0; j < (size_t) kNumLoops; ++j)
        {
            varGain[0][j] = w[0][j] * kSignL[j];
            varGain[1][j] = w[1][j] * rn * kSignR[j];
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
                             float& refL, float& refR, float& dryL, float& dryR, float& revL,
                             float& revR) const noexcept
    {
        float erL = 0.0f, erR = 0.0f, eDryL = 0.0f, eDryR = 0.0f;
        const int nDry = lay.er2DryTaps;
        std::array<float, kNumLoops> col{}; // the feed summed per Hadamard column (kFeedColR)
        // true stereo: the left taps lean to the left input, the right ones to the right (ADR 0006); otherwise the
        // side memory is not read, so Mono and Stereo stay exactly as they were
        const float k = lay.sideGain;
        auto readL = [this, k] (int d) noexcept
        { return k != 0.0f ? input.tap (d) + k * sideMemory.tap (d) : input.tap (d); };
        auto readR = [this, k] (int d) noexcept
        { return k != 0.0f ? input.tap (d) - k * sideMemory.tap (d) : input.tap (d); };
        const int early = lay.erEarly; // era 5: the reflection output is read early (kErDiffMs), the feed is not
        // One channel's chart taps, specialised on the side read and the early read (CPU pass 2026-11-25: the branches
        // had been taken per tap; the sums keep their order, so the output is bit-identical).
        auto chart = [&] (const auto& taps, int num, auto read, int colOff, float& er, float& eDry) noexcept
        {
            auto run = [&] (auto earlyOn) noexcept
            {
                for (int i = 0; i < num; ++i)
                {
                    const auto& t = taps[(size_t) i];
                    const float x = read (t.delay);
                    float o;
                    if constexpr (decltype (earlyOn)::value)
                        o = t.gain * read (std::max (0, t.delay - early));
                    else
                        o = t.gain * x;
                    er += o;
                    if (i < nDry)
                        eDry += o;
                    col[(size_t) ((i + colOff) & 7)] += t.feed * (t.feedDelay != t.delay ? read (t.feedDelay) : x);
                }
            };
            if (early != 0)
                run (std::true_type{});
            else
                run (std::false_type{});
        };
        auto tapL = [this] (int d) noexcept { return input.tap (d); };
        if (k != 0.0f)
        {
            auto sideL = [this, k] (int d) noexcept { return input.tap (d) + k * sideMemory.tap (d); };
            auto sideR = [this, k] (int d) noexcept { return input.tap (d) - k * sideMemory.tap (d); };
            chart (lay.tapsL, lay.numTapsL, sideL, 0, erL, eDryL);
            chart (lay.tapsR, lay.numTapsR, sideR, kFeedColR, erR, eDryR);
        }
        else
        {
            chart (lay.tapsL, lay.numTapsL, tapL, 0, erL, eDryL);
            chart (lay.tapsR, lay.numTapsR, tapL, kFeedColR, erR, eDryR);
        }
        // pass 67: a Rev set beside the chart (none: numRev 0), heard and fed like a Rev mode's taps
        float rl = 0.0f, rr = 0.0f;
        for (int i = 0; i < lay.numRevL; ++i)
        {
            const auto& t = lay.revL[(size_t) i];
            const float x = readL (t.delay);
            rl += t.gain * x;
            col[(size_t) (i & 7)] += t.feed * x;
        }
        for (int i = 0; i < lay.numRevR; ++i)
        {
            const auto& t = lay.revR[(size_t) i];
            const float x = readR (t.delay);
            rr += t.gain * x;
            col[(size_t) ((i + kFeedColR) & 7)] += t.feed * x;
        }
        revL = lay.revGain * rl;
        revR = lay.revGain * rr;
        const float ref1 = lay.ref1Gain * input.tap (lay.ref1Delay);
        if (lay.cloudMix > 0.0f) // pass 84: the early cloud in place of the chart (the chart's feed stays)
        {
            float cl = 0.0f, cr = 0.0f;
            if (k == 0.0f)
                for (int i = 0; i < lay.numCloud; ++i)
                {
                    const auto& t = lay.cloud[(size_t) i];
                    const float x = input.tap (t.delay);
                    cl += t.gainL * x;
                    cr += t.gainR * x;
                }
            else
                for (int i = 0; i < lay.numCloud; ++i)
                {
                    const auto& t = lay.cloud[(size_t) i];
                    cl += t.gainL * readL (t.delay);
                    cr += t.gainR * readR (t.delay);
                }
            erL += lay.cloudMix * (cl - erL);
            erR += lay.cloudMix * (cr - erR);
        }
        outL = lay.erGain * erL;
        outR = lay.erGain * erR;
        dryL = nDry > 0 ? lay.erGain * eDryL : outL;
        dryR = nDry > 0 ? lay.erGain * eDryR : outR;
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
        if (varOn && --varCount <= 0)
        {
            varCount = kVarBlock;
            stepVariance ();
        }
        // mid and side memories; the side is read only in true stereo (ADR 0006)
        input.write (0.5f * (inL + inR));
        sideMemory.write (0.5f * (inL - inR));

        float wl, wr, r1, r1r, dl, dr, vl, vr;
        std::array<float, kNumLoops> d;
        readLayout (cur, wl, wr, d, r1, r1r, dl, dr, vl, vr);
        if (fadeLeft > 0)
        {
            // equal power: the old and new reads are different parts of the signal, so they add as uncorrelated
            float ol, orr, or1, or1r, odl, odr, ovl, ovr;
            std::array<float, kNumLoops> od;
            readLayout (prev, ol, orr, od, or1, or1r, odl, odr, ovl, ovr);
            const float t = kHalfPi * (float) (fadeLen - fadeLeft) / (float) fadeLen;
            const float gNew = std::sin (t), gOld = std::cos (t);
            wl = gNew * wl + gOld * ol;
            wr = gNew * wr + gOld * orr;
            dl = gNew * dl + gOld * odl;
            dr = gNew * dr + gOld * odr;
            vl = gNew * vl + gOld * ovl;
            vr = gNew * vr + gOld * ovr;
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
            if (er2On && er2DrySplit) // the E/R2 dry taps through their own copy (pass 53)
            {
                dl = erDiff[2].process (dl);
                dr = erDiff[3].process (dr);
            }
        }
        // E/R2 diffuser: the reflections only, as above
        if (er2On)
        {
            // the taps dry plus the tail (from er2PreLen on), or (REV5 Reverse) the taps through the tail; with a
            // dry split (pass 53) only the first taps play dry; all of them feed the tail, or with restOnly (pass 56)
            // only the taps after them
            if (!er2DrySplit)
            {
                dl = wl;
                dr = wr;
            }
            if (er2RestOnly) // the dry taps stay out of the tail (pass 56)
            {
                wl -= dl;
                wr -= dr;
            }
            if (er2PreLen > 0)
            {
                er2Pre[0].write (wl);
                er2Pre[1].write (wr);
                wl = er2Pre[0].tap (er2PreLen);
                wr = er2Pre[1].tap (er2PreLen);
            }
            for (size_t k = 0; k < er2Diff[0].size (); ++k)
            {
                wl = er2Diff[0][k].process (wl);
                wr = er2Diff[1][k].process (wr);
            }
            if (er2Parallel)
            {
                wl = er2DryG * dl + er2WetG * wl;
                wr = er2DryG * dr + er2WetG * wr;
            }
        }
        // pass 78: the early fill beside the reflections (before ER feedback, the tap filters and the tilt)
        if (erFillOn)
        {
            float fl = wl, fr = wr;
            for (size_t k = 0; k < erFillAp[0].size (); ++k)
            {
                fl = erFillAp[0][k].process (fl);
                fr = erFillAp[1][k].process (fr);
            }
            wl += erFillG * fl;
            wr += erFillG * fr;
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
        if (erAvgU > 0.0f)
        {
            const float xl = wl, xr = wr;
            wl = (1.0f - erAvgU) * xl + erAvgU * erAvgPrev[0];
            wr = (1.0f - erAvgU) * xr + erAvgU * erAvgPrev[1];
            erAvgPrev[0] = xl;
            erAvgPrev[1] = xr;
        }
        if (erAvgU2 > 0.0f)
        {
            const float xl = wl, xr = wr;
            wl = (1.0f - erAvgU2) * xl + erAvgU2 * erAvgPrev2[0];
            wr = (1.0f - erAvgU2) * xr + erAvgU2 * erAvgPrev2[1];
            erAvgPrev2[0] = xl;
            erAvgPrev2[1] = xr;
        }
        if (erBwCoef < 1.0f)
        {
            // the era's tap filter on the reflections (fifteenth pass; pass 39: not on the 1st reflection, which the
            // REV7 captures show as flat as the direct path); the late feed above is already taken
            erBwState[0] += erBwCoef * (wl - erBwState[0]);
            erBwState[1] += erBwCoef * (wr - erBwState[1]);
            erBwState[0] = std::abs (erBwState[0]) < 1.0e-20f ? 0.0f : erBwState[0];
            erBwState[1] = std::abs (erBwState[1]) < 1.0e-20f ? 0.0f : erBwState[1];
            wl = erBwState[0];
            wr = erBwState[1];
        }
        wl += r1 + vl; // the Rev set beside a chart (pass 67) skips the chart's diffusion, E/R2 and feedback
        wr += r1r + vr;
        ref1Out[0] = r1;
        ref1Out[1] = r1r;
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
        if (erLoOn)
        {
            wl = erLo[0].process (wl);
            wr = erLo[1].process (wr);
        }

        // the era's input bandwidth: a one-pole low-pass on everything the late networks hear
        std::array<double, kNumLoops> loopIn;
        for (size_t g = 0; g < d.size (); ++g)
        {
            float x = d[g];
            if (lateAvgU > 0.0f)
            {
                const float prev = lateAvgPrev[g];
                lateAvgPrev[g] = x;
                x = (1.0f - lateAvgU) * x + lateAvgU * prev;
            }
            if (lateAvgU2 > 0.0f)
            {
                const float prev = lateAvgPrev2[g];
                lateAvgPrev2[g] = x;
                x = (1.0f - lateAvgU2) * x + lateAvgU2 * prev;
            }
            float& s = lateBwState[g];
            s += lateBwCoef * (x - s);
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

        double lateL = 0.0, lateR = 0.0, varL = 0.0, varR = 0.0;
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
            if (varOn)
            {
                varL += kSignL[j] * ((double) varGain[0][(size_t) j] - (double) varAlpha) * out;
                varR += kSignR[j] * ((double) varGain[1][(size_t) j] - (double) varAlpha) * out;
            }
            fb[(size_t) j] = l.ap[1].process (l.ap[0].process (
                l.hiShelf.process (l.loShelf.process (l.subOn ? l.subShelf.process (y * l.gain) : y * l.gain))));
        }
        if (varOn)
        {
            lateL = varStatic[0].process (lateL) + varHp[0].process (varL);
            lateR = varStatic[1].process (lateR) + varHp[1].process (varR);
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
