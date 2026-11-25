#include "Converter.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <numeric>
#include <random>

namespace yrev::dsp
{
    namespace
    {
        // ---- Config ----
        constexpr double kPi = 3.14159265358979323846;
        constexpr double kNativeEra7 = 31250.0; // 4 MHz / 128 (ADR 0001)
        constexpr double kNativeEra5 = 44100.0; // 5.6448 MHz / 128
        // Converter full scale relative to the host: +6 dBFS host = A/D full scale (design choice: some headroom for
        // full-level mixes; the input level control drives it harder). Emphasis adds up to +10 dB at HF before it.
        constexpr float kFullScale = 2.0f;
        // Hiss at the D/A, re converter full scale, assuming units in pristine condition (a project choice; the
        // published residual noise is about -60 dB, Q-23). Anchored on a REV1 capture's floor (about -97 dB
        // re peak); the REV7 (slightly cheaper, no dither, quantisation noise noted by a 1987 review) gets a little
        // more than the REV5. Inference.
        constexpr double kHissDbFsEra7 = -88.0, kHissDbFsEra5 = -92.0;
        // PCM53 (REV7 D/A) low-level distortion: THD 1.9 % typ / 4.0 % max at -60 dB, 0.02 % at -20 dB (420 Hz;
        // Burr-Brown 1984 Product Data Book p. 6-162, Q-16). Modelled as a differential linearity error at the major
        // carry (bipolar zero): non-zero codes move towards zero by half this step each side (silence stays silent).
        // -1.5 LSB (0.0023 % FSR, inside the 0.005 % max) gives 1.9 % at -60 dB and 0.02 % at -20 dB together with
        // our magnitude truncation (fitted, engine.md converter section; the sign is inference). Era 5's PCM56 has no
        // data here, so it stays ideal.
        constexpr float kMajorCarryLsbEra7 = -1.5f;
        // Late-network input bandwidth per era (one-pole low-pass on what the reverb network hears). Fitted to 19
        // REV7/REV5 captures, whose tails are darker than their openings by a roughly constant amount beyond
        // what the matched decay explains (seventh pass: 4 kHz / 9 kHz). Seventeenth pass: after the refitted jobs
        // (eleventh pass) and the era-7 bass lift and late taps, the 4 kHz REV7 tail had drifted 2-3 dB too dark
        // at 6.3-12.5 kHz (late colour 1.81 dB rms, 2.5-8 kHz, 100-500 ms); 5.5 kHz gives 0.56 dB. Era 5 still fits
        // (0.78 dB). Measurement of the captures; that the hardware filters its reverb input is inference
        // (gap 5; docs/design/engine.md, seventh and seventeenth
        // passes).
        // Nineteenth pass (loop mixing, factory space mod 0, matched jobs refitted at mod 0): the tails came out about
        // 1.5 dB bright at 2-12.5 kHz in 100-300 ms (space mod 2's interpolation had been darkening them slightly,
        // inference); 4.5 / 7 kHz give late colour 0.56 / 0.94 dB rms (5.5 / 9 kHz: 1.07 / 1.16).
        // Twenty-first pass (2026-11-20): era 5 has no fitted bandwidth (0 = off). With the converter alone, the six
        // REV5 E/R captures match within 0.6-1.1 dB rms (2.3-2.9 with 9 kHz on the reflections), and the reverb
        // captures' darkening is the program LPF (`wet_lpf`, a one-pole): the 7 / 9 kHz values had stood in for it, so
        // presets applied the printed LPF twice (7-15 dB rms too dark). Measurement of the captures; engine.md,
        // pass 21.
        // Pass 39 (2026-11-24): era 7 11 kHz, after the half-sample average below (kLateTapAverageEra7). Without it
        // the late colour fits best (100-300 / 300-800 ms 1.18 / 0.75 dB rms) but C80 at 8 kHz reads 2.0 dB late-heavy
        // against the captures (was 0.7 dB early-heavy); 11 kHz gives 0.34 / 1.76 dB and C80 1.0 dB. The two late
        // windows pull apart because our treble decays faster than the hardware's, which input filtering cannot fix.
        constexpr float kLateBandwidthEra7 = 11000.0f, kLateBandwidthEra5 = 0.0f;
        constexpr float kMonoSumGain = 0.70710678f; // era 5, Rev > FX: the reverb's equal-power mono sum (pass 55)
        // Bandwidth on the reflection outputs (reflections and 1st reflections). Era 5 (fifteenth pass): the REV5
        // openings (first 50 ms) are darker than our reflections by about 2 dB at 8 kHz and 4 dB at 12.5 kHz; a
        // one-pole at the same 9 kHz closes the median (0-20 ms residual within 0.5 dB, 3-16 kHz). Era 7 (seventeenth
        // pass): the REV7 openings were 2-5 dB darker than ours at 3-10 kHz (0-20 ms) and the 8 kHz octave held 5.6 dB
        // more of its energy in the first 50 ms than the captures' (C50 at 8 kHz, 9 of 10); 7.5 kHz (at the 31.25 kHz
        // native rate) takes the opening colour from 3.5 / 1.7 / 2.1 to 1.7 / 0.6 / 1.2 dB rms (0-20 / 20-50 / 50-100
        // ms, 3-12.5 kHz). Measurement of the captures; where the hardware band-limits its reflections is inference.
        // Pass 39: era 7 off too (was 7.5 kHz); the tap averages below replace it, and the 1st reflection no longer
        // passes it.
        constexpr float kErBandwidthEra5 = 0.0f, kErBandwidthEra7 = 0.0f; // era 5: off since pass 21 (was 9 kHz)
        // Era-7 tap filtering (pass 39, 2026-11-24): a two-sample average, y = (1 - u) x + u x[-1], on the reflections
        // (not the 1st reflection) and on what the late network hears. The REV7 taps sit below the direct path
        // by -3.5 / -6 / -9.6 dB at 8 / 10 / 12.5 kHz on the halls and plates, which is a half-sample average at
        // 31.25 kHz (-3.2 / -5.4 / -10.2 dB) and which no one-pole fits (any corner that closes 10-12.5 kHz darkens
        // 4-8 kHz); the E/R charts' taps fall less (-0.6 / -2.4 / -5.2 dB). Grid of factory-bank renders against the
        // captures (colour re 1 kHz, 2-12.5 kHz, rms of the family median): Rev taps 0-50 ms 2.34 -> 0.45 dB (u 0.5),
        // E/R charts 0-50 ms 1.39 -> 0.58 dB (u 0.35), late 100-300 / 300-800 ms 1.83 / 1.75 -> 0.34 / 1.76 dB (u 0.5,
        // with the 11 kHz one-pole above). Measurement of the captures; that the hardware reads its taps between two
        // samples is inference.
        // 1st-reflection level against the reflections (pass 43, 2026-11-24): the printed level is not the tap's
        // amplitude against our energy-normalised reflections. On the hardware E/R captures with a 1st reflection the
        // reflection field (2-200 ms, 300 Hz-8 kHz) sits re the 1st reflection 9.3 dB higher than ours (REV7 19-21,
        // 25-28; spread 5.6-10.5) and 14.3 dB higher (REV5 19-21, 25; 4.5-17.6). Scaled per era on all three 1st
        // reflections (measurement of the captures; how the hardware scales its level is not documented).
        constexpr float kRef1GainEra7 = 0.343f, kRef1GainEra5 = 0.193f; // -9.3 / -14.3 dB
        constexpr float kErTapAverageEra7 = 0.5f, kErTapAverageChartEra7 = 0.35f, kLateTapAverageEra7 = 0.5f;
        // Pass 61 (2026-11-25): the read is the program's, not the family's. Every tap of a REV7 capture has
        // the same treble (1.2 ms windows, 8 / 10 / 12.5 kHz re 1-4 kHz, spread within a program about 1 dB), but the
        // programs fall into groups: as the direct path (Strings, Live Reference, Piano: -2 / -10 dB at 10 / 12.5 kHz),
        // a little under it (Snare, Organ, Brass, both E/R presets: -3 / -14), the half-sample read (the halls, the
        // plates, Reverb Flange: -6 / -17) and darker (Gate Reverb, Handclaps: -11 / -23). No panel value predicts the
        // group, so a hidden per-preset offset (voice_tap_read) moves both averages; above 0.5 a second average stage
        // takes the rest. Measurement of the captures; how the hardware reads its taps is inference.
        constexpr float kTapReadMax = 0.5f;
        // Era-5 reflections' top octave: the REV5 E/R taps' comb on average (twenty-seventh pass, Engine.cpp kErTopQ).
        // Off since pass 36 (0 = off): the REV5 taps carry no comb (tap-to-tap spread <= 1.4 dB to 12 kHz), and
        // without it the era-5 openings sit on the captures at 16 kHz (median +1.3 -> +0.3 dB, capture minus ours).
        constexpr float kErTopEra5 = 0.0f;
        // Where the printed HIGH holds, per era (pass 37): the RT ratio at the top of each unit's band. The hardware
        // captures read the printed value at about 10-12.5 kHz (REV7) and at 16 kHz (REV5), third-octave T20 ratios re
        // 1 kHz on eight / ten reverb programs (measurement of the captures).
        // Fitted against a grid of renders (corner x hi) and checked on renders at the printed values: era 7 keeps the
        // voice corner (8.5 kHz) with the reference at 10 kHz (swept 9-14 kHz), era 5 plays the corner x 12 / 8.5 (12
        // kHz by default) with the reference at 16 kHz; ratio error 2-12.5 / 2-16 kHz 0.117 / 0.244 -> 0.041 / 0.032
        // rms (the 8 kHz-octave reading had left the tails 6-10 dB dark late on).
        // Pass 73 (final sweep): era 7's tails still read 2-4 dB dark at 4-10 kHz from 200 ms on (median over the ten
        // REV7 reverb programs, three drift seeds); a grid of corner x reference (x1.1-1.4, 10-11.5 kHz) fits best at
        // the corner x 1.15 (9.8 kHz by default) with the reference at 10.5 kHz: 300-800 ms colour 3.16 -> 2.80 dB
        // rms per program (measurement against the captures; inference for the fit).
        constexpr float kHiRefEra7 = 10500.0f, kHiRefEra5 = 16000.0f;
        constexpr float kHiXoverScaleEra7 = 1.15f, kHiXoverScaleEra5 = 12000.0f / 8500.0f;
        // Pass 75 (final sweep): the REV7 Handclaps capture (RT 0.4 s, HIGH 0.1) decays about 0.40 s at 4-8 kHz over
        // 50-600 ms (mid 0.45 s; measurement of the capture), where the printed ratio would give 0.04 s; every other
        // REV7 reverb program prints a treble decay of 0.5 s or more. Era 7's treble decay floors at 0.2 s (the shelf
        // plateau; grid 0.15-0.4 s on that capture, inference for the mechanism). Era 5: no capture, off.
        constexpr float kHiRtFloorEra7 = 0.2f;
        // Wet-path DC block (eighteenth pass): a 2nd-order Butterworth high-pass at 10 Hz on the converter output, both
        // eras. The chart reflections are all positive, so the cluster carries a DC step (about 4.5 % of the first 50
        // ms's energy) and the late network a long infrasonic tail (our wet IR held +5 / +9 dB more at 5 Hz than at
        // 100 Hz, era 7 / 5). Both units specify the effect path from 20 Hz (REV5 factory check 20 Hz-5 kHz within
        // +-1 dB), and no analogue output passes DC (inference): -0.26 dB at 20 Hz.
        constexpr float kDcBlockHz = 10.0f;
        // Rate bridge (twenty-seventh pass, was 0.95 with 16 zero crossings): the hardware has no resampler, so the
        // bridge should be transparent to 20 kHz; at 0.95 its two passes took 0.8 / 4.1 dB at 19 / 20 kHz off the era-5
        // top octave, at 0.97 with 24 zero crossings 0.0 / 0.6 dB (Converter.h kZeroCrossings).
        constexpr double kBridgePassband = 0.97; // resampler cut-off as a fraction of the lower Nyquist
        constexpr double kKaiserBeta = 8.0;
        // Per-phase bridge weights (RateBridge::preparePolyphase): at most this many per native rate (8 bytes
        // each); rate pairs that need more keep the per-tap table reads. CPU pass, 2026-11-21.
        constexpr double kMaxPolyWeights = 1 << 19;
        // Noise gate: no phantom hiss from an idle plug-in. Hiss (and era-5 dither) fade out when the signal they ride
        // on stays below the threshold, and come back quickly when it returns.
        constexpr double kGateThresholdDbFs = -110.0; // host scale
        constexpr double kGateHoldSeconds = 0.1, kGateAttackSeconds = 0.005, kGateReleaseSeconds = 0.3;
        constexpr double kFadeSeconds = 0.03; // fade the wet in after an era switch
        // Emphasis network from the schematic values (03-signal-path.md):
        // zero 3,092 Hz, pole 10,281 Hz, +10.4 dB HF asymptote. De-emphasis is its inverse (REV5 values confirm a
        // near-exact pair; REV7 de-emphasis not transcribed, Q-14).
        constexpr double kEmphZeroHz = 3092.0, kEmphPoleHz = 10281.0, kEmphWarpHz = 6000.0;
        constexpr double kDroopWarpHz = 10000.0;
        // Bilinear prewarp at each era's measured cliff, so it lands where it was measured. Era 5 is fitted on the
        // realised chain at 96 kHz (REALISED fit: this warp and the bridge), so keep them
        // in step with the fit. Pass 36: era 5 at its own edge (was 17 kHz, which cramped the elliptic prototype).
        constexpr double kEra7DesignWarpHz = 12200.0, kEra5DesignWarpHz = 20077.0;
        // Era-5 D/A hold droop in the bridge (twenty-seventh pass): off since pass 36. The REV5 is flat within
        // 0.3 dB to 19 kHz with no droop (measurement of the capture); the
        // taps it was fitted on carried the E/R programs' 10 kHz program LPF (inference). The bridge keeps
        // the option (RateBridge::setHoldDroop).
        constexpr bool kHoldDroopEra5 = false;
        constexpr double kOversampleBelowHz = 88000.0;
        constexpr uint32_t kFrozenSeed = 0x12345678u;  // 'frozen' noise seed: renders repeat sample for sample
        constexpr uint32_t kVarSeedSalt = 0x5bd1e995u; // the engine's time-variance seed: the noise seed, salted
        // Era-7 time variance (ADR 0007, pass 71): the share of the late output's energy whose read gains drift, and
        // the drift's period. Fitted on the REV7 repeat sweeps (1 s against 120 s through the plug-in, the
        // nine reverb programs, three seed pairs): 50-1000 ms coherence about 0.5 flat over 250 Hz-4 kHz, the
        // hardware's 0.47-0.59; 0.4 / 0.45 / 0.5 and 1 / 3 / 8 s tried, 8 s too slow. Era 5 is time-invariant
        // (0.97-1.00) and stays static.
        constexpr float kLateVarShareEra7 = 0.5f;
        constexpr float kLateVarSecEra7 = 3.0f;
        constexpr double kHalfBandBeta = 8.0;
        const conv::AnalogSection kEmphasis[] = {
            {{0, conv::kRefHz / kEmphZeroHz, 1}, {0, conv::kRefHz / kEmphPoleHz, 1}}};
        const conv::AnalogSection kDeemphasis[] = {
            {{0, conv::kRefHz / kEmphPoleHz, 1}, {0, conv::kRefHz / kEmphZeroHz, 1}}};

        double besselI0 (double x)
        {
            double sum = 1.0, term = 1.0;
            for (int k = 1; k < 50; ++k)
            {
                term *= (x / (2.0 * k)) * (x / (2.0 * k));
                sum += term;
                if (term < 1.0e-12 * sum)
                    break;
            }
            return sum;
        }

        template <size_t N> constexpr int count (const conv::AnalogSection (&)[N])
        {
            return (int) N;
        }
    } // namespace

    // ---- AnalogCascade ----

    void AnalogCascade::design (const conv::AnalogSection* sections, int n, double warpHz, double fs)
    {
        count = std::min (n, kMaxSections);
        const double fw = std::min (warpHz, 0.45 * fs);
        const double c = 2.0 * kPi * fw / std::tan (kPi * fw / fs); // prewarped bilinear constant
        const double K = c / (2.0 * kPi * conv::kRefHz);            // in normalised s
        for (int i = 0; i < count; ++i)
        {
            const auto& s = sections[i];
            double nb0, nb1, nb2, na0, na1, na2;
            if (std::abs (s.a[0]) < 1.0e-300 && std::abs (s.b[0]) < 1.0e-300) // first order: (b1 s + b2) / (a1 s + a2)
            {
                nb0 = s.b[1] * K + s.b[2];
                nb1 = s.b[2] - s.b[1] * K;
                nb2 = 0.0;
                na0 = s.a[1] * K + s.a[2];
                na1 = s.a[2] - s.a[1] * K;
                na2 = 0.0;
            }
            else
            {
                const double K2 = K * K;
                nb0 = s.b[0] * K2 + s.b[1] * K + s.b[2];
                nb1 = 2.0 * (s.b[2] - s.b[0] * K2);
                nb2 = s.b[0] * K2 - s.b[1] * K + s.b[2];
                na0 = s.a[0] * K2 + s.a[1] * K + s.a[2];
                na1 = 2.0 * (s.a[2] - s.a[0] * K2);
                na2 = s.a[0] * K2 - s.a[1] * K + s.a[2];
            }
            for (auto& f1 : f[i])
                f1.setCoefficients (nb0 / na0, nb1 / na0, nb2 / na0, na1 / na0, na2 / na0);
        }
    }

    void AnalogCascade::reset ()
    {
        for (auto& s : f)
            for (auto& f1 : s)
                f1.reset ();
    }

    // ---- HalfBand ----

    void HalfBand::prepare ()
    {
        const double c = (kTaps - 1) * 0.5, i0b = besselI0 (kHalfBandBeta);
        double sum = 0.0;
        for (int i = 0; i < kTaps; ++i)
        {
            const double t = (double) i - c;
            const double sinc = std::abs (t) < 1.0e-9 ? 1.0 : std::sin (0.5 * kPi * t) / (0.5 * kPi * t);
            const double w = besselI0 (kHalfBandBeta * std::sqrt (std::max (0.0, 1.0 - (t / c) * (t / c)))) / i0b;
            h[(size_t) i] = 0.5 * sinc * w; // cut-off at a quarter of the doubled rate (the host Nyquist)
            sum += h[(size_t) i];
        }
        for (auto& v : h)
            v /= sum;
        for (int k = 0; k < kPairs; ++k)
            hPair[(size_t) k] = h[(size_t) (2 * k)];
        hCentre = h[(size_t) kCentre];
        reset ();
    }

    void HalfBand::reset ()
    {
        for (auto* hist : {&upHist, &downHist})
            for (auto& s : *hist)
                s = {0.0, 0.0};
        upPos = downPos = 0;
    }

    void HalfBand::up (const double (&x)[2], double (&y0)[2], double (&y1)[2]) noexcept
    {
        // zero-stuffed input: even taps make the even output, odd taps the odd one. The even taps are the odd offsets
        // from the centre (h[2k] = h[94 - 2k], read at k and 47 - k); of the odd taps only the centre is not zero.
        upPos = (upPos + 1) & (kHist - 1);
        upHist[(size_t) upPos] = upHist[(size_t) (upPos + kHist)] = {x[0], x[1]};
        const auto* v = &upHist[(size_t) (upPos + kHist)]; // v[-k] is the input k samples ago
        // four partial sums per channel (kPairs is a multiple of four): one running sum is a chain of dependent
        // multiply-adds
        double a[4][2]{};
        for (int k = 0; k < kPairs; k += 4)
            for (int j = 0; j < 4; ++j)
            {
                const double hk = hPair[(size_t) (k + j)];
                a[j][0] += hk * (v[-(k + j)][0] + v[-(kCentre - k - j)][0]);
                a[j][1] += hk * (v[-(k + j)][1] + v[-(kCentre - k - j)][1]);
            }
        constexpr int kMid = kCentre / 2; // the centre tap's input: (kTaps - 1) / 4 samples ago
        y0[0] = 2.0 * ((a[0][0] + a[1][0]) + (a[2][0] + a[3][0]));
        y0[1] = 2.0 * ((a[0][1] + a[1][1]) + (a[2][1] + a[3][1]));
        y1[0] = 2.0 * hCentre * v[-kMid][0];
        y1[1] = 2.0 * hCentre * v[-kMid][1];
    }

    void HalfBand::down (const double (&x0)[2], const double (&x1)[2], double (&y)[2]) noexcept
    {
        downPos = (downPos + 1) & (kHist - 1);
        downHist[(size_t) downPos] = downHist[(size_t) (downPos + kHist)] = {x0[0], x0[1]};
        downPos = (downPos + 1) & (kHist - 1);
        downHist[(size_t) downPos] = downHist[(size_t) (downPos + kHist)] = {x1[0], x1[1]};
        const auto* v = &downHist[(size_t) (downPos + kHist)]; // v[-i] meets tap i
        double a[4][2]{};                                      // partial sums, as up ()
        for (int k = 0; k < kPairs; k += 4)
            for (int j = 0; j < 4; ++j) // taps 2 (k + j) and kTaps - 1 - 2 (k + j)
            {
                const double hk = hPair[(size_t) (k + j)];
                const int t = 2 * (k + j);
                a[j][0] += hk * (v[-t][0] + v[-(kTaps - 1 - t)][0]);
                a[j][1] += hk * (v[-t][1] + v[-(kTaps - 1 - t)][1]);
            }
        y[0] = ((a[0][0] + a[1][0]) + (a[2][0] + a[3][0])) + hCentre * v[-kCentre][0];
        y[1] = ((a[0][1] + a[1][1]) + (a[2][1] + a[3][1])) + hCentre * v[-kCentre][1];
    }

    // ---- RateBridge ----

    void RateBridge::prepare (double hostRate, double lowestNativeRate)
    {
        hostFs = hostRate;
        table.resize ((size_t) (kZeroCrossings * kTableRes + 2));
        const double i0b = besselI0 (kKaiserBeta);
        for (size_t i = 0; i < table.size (); ++i)
        {
            const double u = (double) i / kTableRes; // zero crossings
            const double sinc = u < 1.0e-9 ? 1.0 : std::sin (kPi * u) / (kPi * u);
            const double t = std::min (1.0, u / kZeroCrossings);
            table[i] = (float) (sinc * besselI0 (kKaiserBeta * std::sqrt (1.0 - t * t)) / i0b);
        }
        // The up kernel integrated over one native sample: a held step through the same low-pass. The up path's
        // cut-off is fixed (kBridgePassband of the native Nyquist, the host being faster), so one native sample is
        // kBridgePassband zero crossings wide.
        holdWidth = kBridgePassband;
        holdTable.assign ((size_t) ((kZeroCrossings + holdWidth) * kTableRes + 2), 0.0f);
        {
            constexpr int kSub = 64; // integration steps per table cell
            for (size_t i = 0; i < holdTable.size (); ++i)
            {
                const double u = (double) i / kTableRes;
                double s = 0.0;
                for (int k = 0; k < kSub; ++k)
                {
                    const double v = u - 0.5 * holdWidth + holdWidth * (k + 0.5) / kSub;
                    s += kernel (std::abs (v));
                }
                holdTable[i] = (float) (s / kSub); // mean over the step, so the DC gain stays that of the plain kernel
            }
        }
        // worst cases: the widest down kernel (host much faster than native) and the widest up kernel
        const double maxRatio = std::max (1.0, hostRate / lowestNativeRate);
        const double maxHalfHost = kZeroCrossings / kBridgePassband * maxRatio + 4.0;
        const double maxHalfNative = kZeroCrossings / kBridgePassband + 4.0;
        auto ringFor = [] (double span)
        {
            int64_t n = 1;
            while ((double) n < span)
                n <<= 1;
            return n;
        };
        const int64_t hostLen = ringFor (2.0 * maxHalfHost + 16.0);
        const int64_t nativeLen = ringFor (2.0 * maxHalfNative + 2.0 * maxHalfHost / maxRatio + 64.0);
        for (auto& r : hostRing)
            r.assign ((size_t) (2 * hostLen), 0.0); // mirrored (put)
        for (auto& r : nativeRing)
            r.assign ((size_t) (2 * nativeLen), 0.0);
        hostMask = hostLen - 1;
        nativeMask = nativeLen - 1;
        numPolySets = 0;
        poly = nullptr;
        setNativeRate (lowestNativeRate);
    }

    void RateBridge::setNativeRate (double nativeRate)
    {
        bypass = std::abs (nativeRate - hostFs) < 1.0e-6;
        nativeFs = nativeRate;
        ratio = hostFs / nativeRate;                                  // host samples per native sample
        fcDown = 0.5 * kBridgePassband * std::min (1.0, 1.0 / ratio); // cycles per host sample
        fcUp = 0.5 * kBridgePassband * std::min (1.0, ratio);         // cycles per native sample
        halfDown = kZeroCrossings / (2.0 * fcDown);                   // host samples
        halfUp = kZeroCrossings / (2.0 * fcUp);                       // native samples
        halfUpHold = halfUp + 0.5;                                    // the step adds half a sample each side
        updateLatency ();
        selectPolyphase ();
        reset ();
    }

    void RateBridge::setHoldDroop (bool on)
    {
        hold = on && ratio > 1.0;
        updateLatency ();
        selectPolyphase ();
    }

    void RateBridge::updateLatency ()
    {
        latency = bypass ? 0 : (int) std::ceil (halfDown + (hold ? halfUpHold : halfUp) * ratio) + 2;
    }

    void RateBridge::reset ()
    {
        for (auto* rings : {&hostRing, &nativeRing})
            for (auto& r : *rings)
                std::fill (r.begin (), r.end (), 0.0);
        hostIndex = 0;
        nativeIndex = 0;
    }

    float RateBridge::kernel (double x) const noexcept
    {
        return lerp (table, x * kTableRes);
    }

    void RateBridge::convolve (const std::array<std::vector<double>, 2>& ring, int64_t mask, double centre, double half,
                               double fc, float& l, float& r, bool holdUp) const noexcept
    {
        // indices before the first sample hold nothing; the span never exceeds the ring, so with the mirrored copy
        // (put) it reads contiguously from the first index
        const auto first = std::max<int64_t> (0, (int64_t) std::ceil (centre - half)),
                   last = (int64_t) std::floor (centre + half);
        const double scale = 2.0 * fc;
        const double* pl = ring[0].data () + (first & mask);
        const double* pr = ring[1].data () + (first & mask);
        const auto& tab = holdUp ? holdTable : table;
        double sl = 0.0, sr = 0.0;
        for (int64_t i = first, k = 0; i <= last; ++i, ++k)
        {
            const double w = scale * lerp (tab, std::abs (scale * ((double) i - centre)) * kTableRes);
            sl += w * pl[k];
            sr += w * pr[k];
        }
        l = (float) sl;
        r = (float) sr;
    }

    void RateBridge::buildKernel (PolyKernel& k, int64_t phases, double half, double fc, bool holdUp)
    {
        // phase m: the centre sits m / phases past a whole input sample; the same taps and weights as convolve ()
        const double scale = 2.0 * fc;
        const auto& tab = holdUp ? holdTable : table;
        k.taps = 0;
        k.offset.assign ((size_t) phases, 0);
        for (int64_t m = 0; m < phases; ++m)
        {
            const double frac = (double) m / (double) phases;
            const int first = (int) std::ceil (frac - half), last = (int) std::floor (frac + half);
            k.offset[(size_t) m] = first;
            k.taps = std::max (k.taps, last - first + 1);
        }
        k.taps = (k.taps + 3) & ~3; // whole groups of four (zero weights past the end)
        k.w.assign ((size_t) (phases * k.taps), 0.0);
        for (int64_t m = 0; m < phases; ++m)
        {
            const double frac = (double) m / (double) phases;
            const int first = k.offset[(size_t) m], last = (int) std::floor (frac + half);
            for (int i = first; i <= last; ++i)
            {
                k.w[(size_t) (m * k.taps + (i - first))] =
                    scale * lerp (tab, std::abs (scale * ((double) i - frac)) * kTableRes);
            }
        }
    }

    void RateBridge::preparePolyphase (double nativeRate, bool holdOn)
    {
        holdOn = holdOn && hostFs > nativeRate; // as setHoldDroop
        const double hr = std::round (hostFs), nr = std::round (nativeRate);
        if (numPolySets >= (int) polySets.size () || std::abs (hr - hostFs) > 1.0e-6 ||
            std::abs (nr - nativeRate) > 1.0e-6 || nr < 1.0 || std::abs (hr - nr) < 0.5)
            return;
        const auto g = std::gcd ((int64_t) hr, (int64_t) nr);
        const int64_t p = (int64_t) hr / g, q = (int64_t) nr / g;
        // the kernels' half widths for this rate (as setNativeRate)
        const double r = hostFs / nativeRate;
        const double fd = 0.5 * kBridgePassband * std::min (1.0, 1.0 / r),
                     fu = 0.5 * kBridgePassband * std::min (1.0, r);
        const double hd = kZeroCrossings / (2.0 * fd), hu = kZeroCrossings / (2.0 * fu) + (holdOn ? 0.5 : 0.0);
        if ((double) q * (2.0 * hd + 5.0) + (double) p * (2.0 * hu + 5.0) > (double) kMaxPolyWeights)
            return; // an unusual rate pair: too many phases, keep the per-tap reads
        auto& set = polySets[(size_t) numPolySets++];
        set.nativeRate = nativeRate;
        set.hold = holdOn;
        set.p = p;
        set.q = q;
        buildKernel (set.down, q, hd, fd, false);
        buildKernel (set.up, p, hu, fu, holdOn);
        selectPolyphase ();
    }

    void RateBridge::selectPolyphase () noexcept
    {
        poly = nullptr;
        if (bypass || !polyEnabled)
            return;
        for (int i = 0; i < numPolySets; ++i)
            if (std::abs (polySets[(size_t) i].nativeRate - nativeFs) < 1.0e-6 && polySets[(size_t) i].hold == hold)
                poly = &polySets[(size_t) i];
    }

    namespace
    {
        // sum of w[k] x[k] for both channels, `taps` a multiple of four; four partial sums per channel
        inline void dot (const double* w, const double* xl, const double* xr, int taps, float& l, float& r) noexcept
        {
            double l0 = 0.0, l1 = 0.0, l2 = 0.0, l3 = 0.0, r0 = 0.0, r1 = 0.0, r2 = 0.0, r3 = 0.0;
            for (int k = 0; k < taps; k += 4)
            {
                l0 += w[k] * xl[k];
                l1 += w[k + 1] * xl[k + 1];
                l2 += w[k + 2] * xl[k + 2];
                l3 += w[k + 3] * xl[k + 3];
                r0 += w[k] * xr[k];
                r1 += w[k + 1] * xr[k + 1];
                r2 += w[k + 2] * xr[k + 2];
                r3 += w[k + 3] * xr[k + 3];
            }
            l = (float) ((l0 + l1) + (l2 + l3));
            r = (float) ((r0 + r1) + (r2 + r3));
        }
    } // namespace

    void RateBridge::polyDown (float& l, float& r) const noexcept
    {
        // native output n is centred on host sample n p / q
        const int64_t num = nativeIndex * poly->p, whole = num / poly->q, m = num - whole * poly->q;
        const auto& k = poly->down;
        const int64_t first = whole + k.offset[(size_t) m];
        const auto at = (size_t) (first & hostMask); // indices before 0 read the ring's zeros (as convolve skips them)
        dot (k.w.data () + m * k.taps, hostRing[0].data () + at, hostRing[1].data () + at, k.taps, l, r);
    }

    void RateBridge::polyUp (int64_t hostOut, float& l, float& r) const noexcept
    {
        // host output j is centred on native sample j q / p (floor division: j is negative for the first outputs)
        const int64_t num = hostOut * poly->q;
        int64_t whole = num / poly->p, m = num - whole * poly->p;
        if (m < 0)
        {
            m += poly->p;
            --whole;
        }
        const auto& k = poly->up;
        const int64_t first = whole + k.offset[(size_t) m];
        const auto at = (size_t) (first & nativeMask);
        dot (k.w.data () + m * k.taps, nativeRing[0].data () + at, nativeRing[1].data () + at, k.taps, l, r);
    }

    // ---- Converter ----

    void Converter::prepare (double sampleRate, int maxBlockSize)
    {
        hostFs = sampleRate > 0.0 ? sampleRate : 48000.0;
        oversample = hostFs < kOversampleBelowHz ? 2 : 1;
        innerFs = hostFs * oversample;
        // eco mode runs them at the host rate, so prepare for whichever is higher
        maxDspFs = std::max (kNativeEra5, hostFs);
        engine.prepare (kNativeEra7, maxBlockSize, maxDspFs);
        fxSlot.prepare (maxDspFs, 1);
        gate.prepare (maxDspFs);
        for (auto& r : fxOffset)
            r.assign ((size_t) std::ceil (maxDspFs * 1.0) + 2, 0.0f); // the REV5's 1,000 ms initial delay
        fxOffsetPos = 0;
        std::random_device rd; // a different noise seed per instance unless frozen
        randomSeed = (uint32_t) rd () | 1u;
        bridge.prepare (innerFs, kNativeEra7);
        bridge.preparePolyphase (kNativeEra7, false); // the eras' settings (configure)
        bridge.preparePolyphase (kNativeEra5, kHoldDroopEra5);
        halfBand.prepare ();
        // the plug-in's reported latency: the larger of the two eras' bridge delays (ADR 0001 update 2026-11-25)
        reported = 0;
        for (int e = 0; e < 2; ++e)
        {
            era = -1;
            configure (e, false);
            reported = std::max (reported, latency);
        }
        for (auto& r : padRing)
            r.assign ((size_t) reported + 1, 0.0f);
        era = -1;
        configure (0, false);
    }

    void Converter::configure (int newEra, bool newEco)
    {
        era = newEra == 1 ? 1 : 0;
        eco = newEco;
        // Eco (ADR 0001's cheaper alternative): the DSP stage runs at the host rate, the analogue stages at the host
        // rate too; the era's band limit, emphasis, 16-bit words, hiss and dither stay, the native-rate aliasing goes.
        oversample = (!eco && hostFs < kOversampleBelowHz) ? 2 : 1;
        innerFs = hostFs * oversample;
        native = eco ? std::min (hostFs, maxDspFs) : (era == 0 ? kNativeEra7 : kNativeEra5);
        const auto* stage = era == 0 ? conv::kEra7Stage : conv::kEra5Stage;
        const int stages = era == 0 ? count (conv::kEra7Stage) : count (conv::kEra5Stage);
        const double warp = era == 0 ? kEra7DesignWarpHz : kEra5DesignWarpHz;
        aaIn.design (stage, stages, warp, innerFs);
        recon.design (stage, stages, warp, innerFs);
        if (era == 0)
            droop.design (conv::kEra7Droop, count (conv::kEra7Droop), kDroopWarpHz, innerFs);
        else
            droop.design (conv::kEra5Droop, count (conv::kEra5Droop), kDroopWarpHz, innerFs);
        emphasis.design (kEmphasis, 1, kEmphWarpHz, innerFs);
        deemphasis.design (kDeemphasis, 1, kEmphWarpHz, innerFs);
        {
            // RBJ high-pass, Q 1/sqrt(2)
            const double w0 = 2.0 * kPi * kDcBlockHz / innerFs, cs = std::cos (w0),
                         al = std::sin (w0) / std::sqrt (2.0);
            const double a0 = 1.0 + al;
            for (auto& b : dcBlock)
                b.setCoefficients ((1.0 + cs) / 2.0 / a0, -(1.0 + cs) / a0, (1.0 + cs) / 2.0 / a0, -2.0 * cs / a0,
                                   (1.0 - al) / a0);
        }
        if (!eco)
        {
            bridge.setNativeRate (native);
            bridge.setHoldDroop (era == 1 && kHoldDroopEra5);
        }
        // half-band group delay: (kTaps - 1) samples at the doubled rate for the round trip
        const double hb = oversample > 1 ? (HalfBand::kTaps - 1) / 2.0 : 0.0;
        latency = eco ? 0 : (int) std::lround ((double) bridge.latencySamples () / oversample + hb);
        padSamples = std::max (0, reported - latency); // pads the wet up to the reported latency
        engine.setSampleRate (native);
        fxSlot.setSampleRate (native);
        gate.setSampleRate (native);
        deqDirty = true;
        reset ();
        fade = 0.0f;
        fadeStep = (float) (1.0 / (kFadeSeconds * hostFs));
        envDecay = (float) std::exp (-1.0 / (kGateHoldSeconds * native));
        gateAttack = (float) (1.0 - std::exp (-1.0 / (kGateAttackSeconds * native)));
        gateRelease = (float) (1.0 - std::exp (-1.0 / (kGateReleaseSeconds * native)));
    }

    void Converter::reset ()
    {
        for (auto* c : {&aaIn, &recon, &droop, &emphasis, &deemphasis})
            c->reset ();
        for (auto& b : dcBlock)
            b.reset ();
        bridge.reset ();
        halfBand.reset ();
        engine.reset ();
        engine.seedVariance ((frozen ? kFrozenSeed : randomSeed) ^ kVarSeedSalt);
        fxSlot.reset ();
        gate.reset ();
        for (auto& r : fxOffset)
            std::fill (r.begin (), r.end (), 0.0f);
        fxOffsetPos = 0;
        for (auto& r : padRing)
            std::fill (r.begin (), r.end (), 0.0f);
        padPos = 0;
        for (auto& band : deq)
            for (auto& f : band)
                f.reset ();
        rng = frozen ? kFrozenSeed : randomSeed;
        envIn = envOut = gateIn = gateOut = 0.0f;
    }

    void Converter::followGate (float x, float& env, float& gate) noexcept
    {
        static const float threshold = (float) std::pow (10.0, kGateThresholdDbFs / 20.0);
        env = std::max (std::abs (x), env * envDecay);
        const float target = (env > threshold || !noiseGate) ? 1.0f : 0.0f;
        gate += (target - gate) * (target > gate ? gateAttack : gateRelease);
        if (gate < 1.0e-6f && target == 0.0f)
            gate = 0.0f;
    }

    float Converter::noise () noexcept
    {
        rng ^= rng << 13;
        rng ^= rng >> 17;
        rng ^= rng << 5;
        return (float) ((double) rng / 2147483648.0 - 1.0);
    }

    float Converter::quantise (float x, bool dither, float carryLsb) noexcept
    {
        float u = x / kFullScale;
        if (dither)
            u += 0.5f * (noise () + noise ()) * step; // TPDF, +-1 LSB (level unknown, Q-08)
        u = std::clamp (u, -1.0f, 1.0f - step);       // the converter sticks at its ends
        u = std::trunc (u / step) * step;             // magnitude truncation: decays to silence (learnings 2.3)
        if (u != 0.0f)
            u += carryLsb * std::copysign (0.5f * step, u); // D/A major-carry error (zero codes stay zero)
        return u * kFullScale;
    }

    void Converter::updateDeq (const DigitalEqParams& d)
    {
        // field by field: the struct has padding after its bools, so no memcmp
        const bool same = !deqDirty && d.loShelf == lastDeq.loShelf && d.hiShelf == lastDeq.hiShelf &&
                          std::memcmp (d.f, lastDeq.f, sizeof (d.f)) == 0 &&
                          std::memcmp (d.g, lastDeq.g, sizeof (d.g)) == 0 &&
                          std::memcmp (d.q, lastDeq.q, sizeof (d.q)) == 0;
        if (same)
            return;
        lastDeq = d;
        deqDirty = false;
        constexpr float kShelfQ = 0.707f;
        const float fMax = (float) (0.45 * native);
        for (int b = 0; b < 3; ++b)
            for (int c = 0; c < 2; ++c)
            {
                const float f = std::min (d.f[b], fMax);
                if (b == 0 && d.loShelf)
                    deq[b][c].setLowShelf (native, f, kShelfQ, d.g[b]);
                else if (b == 2 && d.hiShelf)
                    deq[b][c].setHighShelf (native, f, kShelfQ, d.g[b]);
                else
                    // the REV5's Q is a bandwidth: "0.1 produces the narrowest bandwidth ... 5.0 the widest" (OM
                    // p. 15, page image), the reverse of a filter Q; 1 / value keeps the factory 1.0 as before (the
                    // exact law is unknown; inference, 2026-11-22 manual audit)
                    deq[b][c].setPeak (native, f, 1.0f / std::max (0.1f, d.q[b]), d.g[b]);
            }
    }

    void Converter::process (float* left, float* right, int n, const EngineParams& engineParams,
                             const ConverterParams& p)
    {
        if (p.frozenNoise != frozen) // reseed at once, so a frozen render starts from the fixed seed
        {
            frozen = p.frozenNoise;
            rng = frozen ? kFrozenSeed : randomSeed;
            engine.seedVariance ((frozen ? kFrozenSeed : randomSeed) ^ kVarSeedSalt);
        }
        if (p.era != era || p.eco != eco)
            configure (p.era, p.eco);

        const float c = std::clamp (p.character, 0.0f, 100.0f) * 0.01f;
        step = (float) std::pow (2.0, -(15.0 + 8.0 * (1.0 - c))); // 16 bits at 100 %, 24 bits at 0 %
        hissGain = (float) (c * std::pow (10.0, (era == 0 ? kHissDbFsEra7 : kHissDbFsEra5) / 20.0) * std::sqrt (3.0) *
                            kFullScale);
        const bool dither = era == 1 && p.dither && c > 0.0f;

        // panel timing: the wet path is padded to the reported latency and the dry is delayed by the same, so the
        // delays play as printed (no bridge delay taken off them any more)
        EngineParams ep = engineParams;
        const float ref1Gain = era == 0 ? kRef1GainEra7 : kRef1GainEra5;
        ep.ref1Level *= ref1Gain;
        ep.ref1LLevel *= ref1Gain;
        ep.ref1RLevel *= ref1Gain;
        ep.lateBandwidthHz = era == 0 ? kLateBandwidthEra7 : kLateBandwidthEra5;
        ep.erBandwidthHz = era == 0 ? kErBandwidthEra7 : kErBandwidthEra5;
        // pass 61: the program's own tap read, an offset on the era's averages (voice_tap_read)
        const float tapRead = std::clamp (ep.tapReadOffset, -kTapReadMax, kTapReadMax);
        ep.erTapAverage =
            era == 0 ? std::clamp ((isRevTapMode (ep.erMode) ? kErTapAverageEra7 : kErTapAverageChartEra7) + tapRead,
                                   0.0f, 1.0f)
                     : 0.0f;
        ep.lateTapAverage = era == 0 ? std::clamp (kLateTapAverageEra7 + tapRead, 0.0f, 1.0f) : 0.0f;
        ep.erTopHz = era == 0 ? 0.0f : kErTopEra5;
        ep.hiRefHz = era == 0 ? kHiRefEra7 : kHiRefEra5;
        ep.hiXoverScale = era == 0 ? kHiXoverScaleEra7 : kHiXoverScaleEra5;
        ep.hiRtFloorSec = era == 0 ? kHiRtFloorEra7 : 0.0f;
        ep.erTableSet = era == 0 ? 0 : 1;                      // REV7 or REV5 E/R charts
        ep.lateVarShare = era == 0 ? kLateVarShareEra7 : 0.0f; // ADR 0007
        ep.lateVarSec = kLateVarSecEra7;
        if (!engineBypass)
            engine.update (ep);

        // FX slot at the native rate; the effect delays play as printed (see above).
        const bool fxOn = p.fx != nullptr;
        const bool fxFirst = p.fxOrder == 0;
        const bool fxActive = fxOn && p.fx->type != fxtype::off;
        float carried = 0.0f;
        if (fxOn)
        {
            fxNative = *p.fx;
            fxNative.repeatAdvanceMs = 0.0f;
            // the slot hears the reverb alone (era 5 sums it to mono first, below, so the mod types' own sum is unity)
            fxNative.afterReverb = !fxFirst && p.fxWet && era != 1;
            carried = FxSlot::dryCarried (fxNative);
        }
        const bool fxIdle = fxOn && fxNative.type == fxtype::off; // Off and settled: the slot's call is skipped

        auto gp = p.gate;
        if (gp.onInput)
            gp.trigDelayMs = 0.1f; // a noise gate opens on the signal itself (GATE LVL has no trigger delay)
        gate.setParams (gp);
        const bool gateOn = gate.enabled (), noiseGateIn = gateOn && gp.onInput, trigGateOut = gateOn && !gp.onInput;
        const bool deqOn = era == 1 && p.deq.on;
        if (deqOn)
            updateDeq (p.deq);

        const float revMix = std::clamp (p.revMix, 0.0f, 1.0f);
        // the effect's offset: the programmed initial delay above the 0.1 ms minimum every effect program prints
        const size_t offsetCap = fxOffset[0].size ();
        const size_t fxOffsetSamples =
            offsetCap < 2
                ? 0
                : std::min (offsetCap - 1,
                            (size_t) std::lround (std::max (0.0f, engineParams.preDelayMs - 0.1f) * 0.001 * native));
        const bool fxWet = p.fxWet;
        auto nativeStage = [this, dither, fxOn, fxFirst, fxActive, fxIdle, carried, noiseGateIn, trigGateOut, deqOn,
                            revMix, fxWet, fxOffsetSamples,
                            offsetCap] (float l, float r, float& ol, float& orr) noexcept
        {
            followGate (std::max (std::abs (l), std::abs (r)), envIn, gateIn);
            const bool ditherNow = dither && gateIn > 0.5f; // a step, not a fade: TPDF either fully on or off
            l = quantise (l, ditherNow);                    // A/D
            r = quantise (r, ditherNow);
            const float key = std::max (std::abs (l), std::abs (r)); // the gate's key: the program's input
            if (noiseGateIn) // REV5 GATE LVL: the noise gate on the program input, ahead of the effect and the reverb
            {
                const float g = gate.process (key);
                l *= g;
                r *= g;
            }
            // FX routing. Hardware (REV5 combination block diagrams, OM printed pp. 37-39): the effect runs through
            // the reverb and around it. FX > Rev: the effect's own part (output less the dry it carries: delay and
            // echo give their repeats, the rest their whole output) plus REV MIX x the reverb of the effect. Rev > FX:
            // the dry plus REV MIX x the reverb, summed into the effect. FX wet (plugin choice, 2026-11-22): no
            // bypass. FX > Rev is the reverb of the effect, plus delay or echo repeats so clean echoes stay; Rev > FX
            // is the effect of the reverb. With FX off the wet is the reverb either way.
            float ownL = 0.0f, ownR = 0.0f;
            const float dl = l, dr = r;
            if (fxActive && fxFirst)
            {
                fxSlot.process (&l, &r, 1, fxNative);
                ownL = l - carried * dl;
                ownR = r - carried * dr;
                if (fxOffsetSamples > 0) // the effect heard after the initial delay
                {
                    fxOffset[0][fxOffsetPos] = ownL;
                    fxOffset[1][fxOffsetPos] = ownR;
                    const size_t rd = (fxOffsetPos + offsetCap - fxOffsetSamples) % offsetCap;
                    ownL = fxOffset[0][rd];
                    ownR = fxOffset[1][rd];
                    fxOffsetPos = (fxOffsetPos + 1) % offsetCap;
                }
            }
            float rl = l, rr = r, f1l = 0.0f, f1r = 0.0f;
            if (!engineBypass)
            {
                engine.processSample (l, r, rl, rr);
                f1l = engine.lastRef1L (); // the 1st reflections, kept at full level beside REV MIX: both manuals
                f1r = engine.lastRef1R (); // give every program them (REV7 OM p. 18; REV5 p. 14 and the FX plots)
            }
            // Era 5, Rev > FX: the reverb reaches the effect in mono, at equal power (the REV5 captures of 95
            // and 96: L/R correlation 1.00 up to 2 kHz, side 15 dB under mid, at the level of 91-93, the same network
            // at L/R correlation 0.1; pass 55). The effects sum their input to mono anyway, but at half amplitude,
            // which lost 3 dB of an uncorrelated reverb through the bypass; delay and echo now repeat a mono reverb.
            // Era 7 has no combination programs and keeps its stereo reverb.
            if (fxActive && !fxFirst && era == 1)
            {
                rl = rr = kMonoSumGain * (rl + rr);
                f1l = f1r = kMonoSumGain * (f1l + f1r);
            }
            if (fxActive && fxFirst)
            {
                ol = fxWet ? rl : ownL + revMix * (rl - f1l) + f1l;
                orr = fxWet ? rr : ownR + revMix * (rr - f1r) + f1r;
                if (fxWet && carried >= 1.0f) // delay and echo: the repeats beside the reverb
                {
                    ol += ownL;
                    orr += ownR;
                }
            }
            else if (fxActive && !fxWet) // Rev > FX through the hardware's bypass
            {
                ol = dl + revMix * (rl - f1l) + f1l;
                orr = dr + revMix * (rr - f1r) + f1r;
                fxSlot.process (&ol, &orr, 1, fxNative);
                ol -= carried * dl;
                orr -= carried * dr;
            }
            else
            {
                ol = rl;
                orr = rr;
                if (fxOn && !(fxIdle && fxSlot.idle (fxNative)))
                    fxSlot.process (&ol, &orr, 1, fxNative); // Rev > FX wet; Off passes through and runs the fades
            }
            // REV5 gate on the effect output, keyed by the input ("the level of the input signal required to
            // trigger opening of the gate", owner's manual p. 18; last in the combination chains, p. 36)
            if (trigGateOut)
            {
                const float g = gate.process (key);
                ol *= g;
                orr *= g;
            }
            if (deqOn) // REV5 digital EQ, in the DSP ahead of the D/A (placement relative to the reverb: Q-15)
                for (auto& band : deq)
                {
                    ol = band[0].process (ol);
                    orr = band[1].process (orr);
                }
            followGate (std::max (std::abs (ol), std::abs (orr)), envOut, gateOut);
            const float hiss = hissGain * gateOut;
            const float carry = era == 0 ? kMajorCarryLsbEra7 : 0.0f;
            ol = quantise (ol, false, carry); // D/A
            orr = quantise (orr, false, carry);
            if (hiss > 0.0f)
            {
                ol += hiss * noise ();
                orr += hiss * noise ();
            }
        };

        // one sample at the inner (possibly doubled) rate through the analogue stages and the bridge
        auto inner = [&] (double (&x)[2]) noexcept // in place, both channels
        {
            double l = x[0], r = x[1];
            if (era == 0) // REV7: LPF before emphasis
            {
                l = emphasis.process (0, aaIn.process (0, l));
                r = emphasis.process (1, aaIn.process (1, r));
            }
            else // REV5: emphasis before LPF
            {
                l = aaIn.process (0, emphasis.process (0, l));
                r = aaIn.process (1, emphasis.process (1, r));
            }
            float ol = 0.0f, orr = 0.0f;
            if (eco)
                nativeStage ((float) l, (float) r, ol, orr);
            else
                bridge.process ((float) l, (float) r, ol, orr, nativeStage);
            x[0] = dcBlock[0].process (deemphasis.process (0, droop.process (0, recon.process (0, ol))));
            x[1] = dcBlock[1].process (deemphasis.process (1, droop.process (1, recon.process (1, orr))));
        };

        for (int i = 0; i < n; ++i)
        {
            double l = left[i], r = right[i];
            if (oversample == 2)
            {
                const double x[2] = {l, r};
                double u0[2], u1[2], y[2];
                halfBand.up (x, u0, u1);
                inner (u0);
                inner (u1);
                halfBand.down (u0, u1, y);
                l = y[0];
                r = y[1];
            }
            else
            {
                double x[2] = {l, r};
                inner (x);
                l = x[0];
                r = x[1];
            }
            if (padSamples > 0)
            {
                const size_t len = padRing[0].size (), rd = (padPos + len - (size_t) padSamples) % len;
                const double inL = l, inR = r;
                l = padRing[0][rd];
                r = padRing[1][rd];
                padRing[0][padPos] = (float) inL;
                padRing[1][padPos] = (float) inR;
                padPos = (padPos + 1) % len;
            }
            if (fade < 1.0f)
                fade = std::min (1.0f, fade + fadeStep);
            left[i] = (float) l * fade;
            right[i] = (float) r * fade;
        }
    }
} // namespace yrev::dsp
