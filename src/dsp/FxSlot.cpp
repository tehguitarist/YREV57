#include "FxSlot.h"
#include "LoopHighCut.h"

#include <algorithm>
#include <cmath>
#include <functional>

namespace yrev::dsp
{
    namespace
    {
        // ---- Config -----------------------------------------------------------------------------------------
        constexpr double kPi = 3.14159265358979323846;
        // Delay: REV7 p. 14 0.1-900 ms; REV5 p. 25 0.1-2900 ms. Echo: REV7 0.1-450 ms; REV5 p. 26 0.1-1400 ms.
        constexpr double kMaxDelayMsEra7 = 900.0, kMaxDelayMsEra5 = 2900.0, kMaxEchoMsEra7 = 450.0,
                         kMaxEchoMsEra5 = 1400.0;
        constexpr double kMaxPitchCMs = 200.0;        // REV5 p. 35: Pitch Change C delays 0.1-200 ms
        constexpr double kPitchPairGain = 0.70710678; // Pitch B: two centre voices summed (inference)
        constexpr double kSwitchFadeMs = 10.0;        // each way, on a type change
        constexpr double kMaxLfoHzEra7 = 20.0, kMaxLfoHzEra5 = 40.0;
        // MOD DLY: Stereo Flange 0.1-100 ms, Reverb Flange 0.1-30 ms (REV5 OM p. 29, OCR; ROM chart pp. 64-65, page
        // images); phasing 0.1-8 ms (REV7), 0.1-5 ms on the REV5 (OM p. 31, OCR, and ROM chart p. 68, page image; the
        // page's text calls it "the flange effect", copied from p. 29, which was once read as the era-5 flange range).
        constexpr double kMaxFlangeMs = 100.0, kMaxRevFlangeMs = 30.0, kMaxPhasingMsEra7 = 8.0, kMaxPhasingMsEra5 = 5.0;
        constexpr double kMaxPitchDelayMs = 400.0;
        constexpr double kDmSwingMs = 4.0;    // delay modulation at 100 % depth (REV7 manual)
        constexpr double kChorusBaseMs = 4.0; // chorus base delay (REV7 manual)
        // Symphonic (2026-11-22): measured on a third-party SPX90 Symphonic model (running at 31.25 kHz
        // with a 12 kHz band; a third-party plug-in, measurement not a hardware spec; click train and
        // tone). Three voices off one 8.19 ms line (256 samples at 31.25 kHz), centred on
        // 4.1 ms; each voice's delay is the sum of two LFOs, the set rate (+-3.0 ms x depth) and a slow one at about
        // rate / 11 (+-5.2 ms x depth), clipped to the line, so 50 % just reaches both ends. Slow LFO phases 120
        // degrees apart; fast LFO: left 0, right +92, centre -133 degrees. Equal voices, no amplitude modulation, no
        // direct signal: left = centre + left voice, right = centre + right voice, each at 0.707. The plug-in is
        // linear, adds no noise and passes its full host band (flat to 24 kHz at 96 kHz): the era's converter sets
        // ours. Times in ms, so both eras sweep the same.
        constexpr double kSymCentreMs = 4.096, kSymMaxMs = 8.192;
        constexpr double kSymFastMs = 3.0, kSymSlowMs = 5.19,
                         kSymSlowRatio = 0.0905; // 0.0899 x at 0.65 Hz, 0.092 x at 1.96 Hz
        constexpr double kSymFastPhase[3] = {-133.4 / 360.0, 0.0, 92.4 / 360.0}; // centre, left, right (cycles)
        constexpr double kSymSlowPhase[3] = {-120.0 / 360.0, 0.0, 120.0 / 360.0};
        // Flange (direct + delayed) and phasing (two taps): 0.707 each, unity average power like the chorus and
        // the symphonic (was 0.5, inference, which left both about 3 dB down; 2026-11-22)
        constexpr float kModPairGain = 0.70711f;
        constexpr float kEqualPowerSum = 0.70711f;   // the mod types' mono sum after the reverb
        constexpr float kChorusVoiceGain = 0.70711f; // chorus A/B: two voices per side at unity average power
        constexpr float kSymVoiceGain =
            0.70711f; // as measured: unity average power at 50 %, +3 dB with the voices in phase
        // Tremolo built like the chorus (REV7 owner's manual p. 19, OCR: "virtually the same manner as the Chorus
        // preset, with the amplitude modulation emphasized"; the other parameters "add extra richness"). The delay
        // sweep beside the AM is small: base and swing are inference (about 15 cents of vibrato at 6 Hz, 100 %).
        constexpr double kTremoloBaseMs = 1.0, kTremoloSwingMs = 0.3;
        // Stepped sweeps: the YM3807 sends 8-bit MOD words (schematic read, inference that they set the delay
        // offset directly), so a swept delay takes 256 positions across its swing.
        constexpr double kModSteps = 255.0;
        constexpr double kRevFlangeFb = 0.30;     // fixed by the REV7
        constexpr double kPhasingLeftRatio = 0.9; // inference: left slightly earlier than right
        // Shifter window: about 10 ms on a third-party SPX90 model (2026-11-22: splices at 100 Hz for an
        // octave up, two taps half a window apart). Was 30 ms, and 50 ms before that (inference).
        constexpr double kPitchWindowMs = 10.0;
        constexpr double kMaxFb = 0.99;
        constexpr double kSmoothMs = 20.0;
        constexpr double kPanSmoothMs = 3.0;
        constexpr int kMinDelaySamples = 2;
        constexpr float kFlush = 1.0e-30f;
        constexpr double kTailCapSeconds = 60.0;
        constexpr double kLineAMs = 3000.0, kLineBMs = 100.0;

        inline float flush (float x) noexcept
        {
            return std::fabs (x) < kFlush ? 0.0f : x;
        }
        inline double clampd (double x, double lo, double hi)
        {
            return std::min (std::max (x, lo), hi);
        }

        // Note values as multiples of a quarter note: 1/16, 1/8 triplet, 1/8, dotted 1/8, 1/4 triplet, 1/4,
        // dotted 1/4, 1/2, dotted 1/2, whole.
        constexpr double kNoteBeats[] = {0.25, 1.0 / 3.0, 0.5, 0.75, 2.0 / 3.0, 1.0, 1.5, 2.0, 3.0, 4.0};
        // LFO sync adds slow values (2, 4 and 8 bars of 4/4): an LFO at 0.1 Hz spans several bars
        constexpr double kLfoBeats[] = {0.25, 1.0 / 3.0, 0.5, 0.75, 2.0 / 3.0, 1.0, 1.5,
                                        2.0,  3.0,       4.0, 8.0,  16.0,      32.0};

        inline double sine (double cycles) noexcept
        {
            return std::sin (2.0 * kPi * cycles);
        }
        inline double wrap1 (double x) noexcept
        {
            return x - std::floor (x);
        }
        // Triangle with the sine's phase: 0 at 0 cycles, +1 at 0.25, -1 at 0.75.
        inline double triangle (double cycles) noexcept
        {
            return 4.0 * std::fabs (wrap1 (cycles - 0.25) - 0.5) - 1.0;
        }
    } // namespace

    namespace fxdetail
    {
        void Line::allocate (int maxDelaySamples)
        {
            int size = 16;
            while (size < maxDelaySamples + 8)
                size <<= 1;
            buf.assign ((size_t) size, 0.0f);
            mask = size - 1;
            pos = 0;
            maxDelay = (double) (size - 4);
        }

        void Line::clear ()
        {
            std::fill (buf.begin (), buf.end (), 0.0f);
            pos = 0;
        }

        void Line::write (float x) noexcept
        {
            buf[(size_t) pos] = flush (x);
            pos = (pos + 1) & mask;
        }

        float Line::read (double d) const noexcept
        {
            d = std::min (std::max (d, (double) kMinDelaySamples), maxDelay);
            const int n0 = (int) d;
            const float u = 1.0f - (float) (d - n0); // 0 at the older sample, 1 at the newer
            const auto at = [this] (int k) { return buf[(size_t) ((pos - k) & mask)]; };
            if (linear)
                return at (n0 + 1) + u * (at (n0) - at (n0 + 1));
            const float xm1 = at (n0 + 2), x0 = at (n0 + 1), x1 = at (n0), x2 = at (n0 - 1);
            const float c0 = x0;
            const float c1 = 0.5f * (x1 - xm1);
            const float c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2;
            const float c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
            return ((c3 * u + c2) * u + c1) * u + c0;
        }

        void Lowpass1::set (double sampleRate, float cutoffKHz)
        {
            active = cutoffKHz < 16.5f;
            if (!active)
                return;
            const double f = clampd ((double) cutoffKHz * 1000.0, 20.0, sampleRate * 0.45);
            const double k = std::tan (kPi * f / sampleRate);
            b0 = (float) (k / (1.0 + k)); // y = b0 (x + x[-1]) - a1 y[-1]
            a1 = (float) ((k - 1.0) / (k + 1.0));
        }

        float Lowpass1::process (float x) noexcept
        {
            const float y = b0 * x + z1;
            z1 = flush (b0 * x - a1 * y);
            return y;
        }
    } // namespace fxdetail

    float FxSlot::dryCarried (const FxParams& p)
    {
        switch (p.type)
        {
        case fxtype::delay:
        case fxtype::echo:
            return 1.0f;
        default:
            return 0.0f;
        }
    }

    void FxSlot::prepare (double sampleRate, int)
    {
        maxSr = sampleRate > 0.0 ? sampleRate : 48000.0;
        for (auto& l : lineA)
            l.allocate ((int) std::ceil (kLineAMs * 0.001 * maxSr));
        for (auto& l : lineB)
            l.allocate ((int) std::ceil (kLineBMs * 0.001 * maxSr));
        setSampleRate (maxSr);
    }

    void FxSlot::setSampleRate (double sampleRate)
    {
        sr = std::clamp (sampleRate > 0.0 ? sampleRate : maxSr, 8000.0, maxSr);
        smoothA = 1.0 - std::exp (-1.0 / (kSmoothMs * 0.001 * sr));
        fadeLen = std::max (1, (int) std::lround (kSwitchFadeMs * 0.001 * sr));
        // per-rate coefficients, worked out here rather than per call (the converter calls once per sample)
        highFor = -1.0; // recompute the HIGH low-pass for the new rate
        panA = (float) (1.0 - std::exp (-1.0 / (kPanSmoothMs * 0.001 * sr)));
        reset ();
    }

    void FxSlot::reset ()
    {
        clearState ();
        lastType = lastModType = -1;
        primed = false;
        fadeOutLeft = fadeInLeft = primeLeft = 0;
        lastLpf = lastModLpf = -1.0f;
        for (auto& f : fxLpf)
            f.reset ();
        for (auto& f : modLpf)
            f.reset ();
    }

    void FxSlot::clearState ()
    {
        for (auto& l : lineA)
            l.clear ();
        for (auto& l : lineB)
            l.clear ();
        for (auto& s : sTime)
            s.reset ();
        sLevel.reset ();
        sFb.reset ();
        sA.reset ();
        sB.reset ();
        sRate.reset ();
        damp = {};
        dampPrev = {};
        panGain = {0.5f, 0.5f}; // the centre
        phase = 0.0;
        phaseSlow = 0.0;
        shiftPhase = shiftPhase2 = 0.0;
    }

    double FxSlot::syncedMs (double ms, const FxParams& p) const
    {
        if (!p.sync || p.hostBpm <= 1.0)
            return ms;
        const double beatMs = 60000.0 / p.hostBpm;
        double best = ms, bestErr = 1.0e30;
        for (double b : kNoteBeats)
        {
            const double err = std::fabs (std::log (b * beatMs / std::max (ms, 0.01)));
            if (err < bestErr)
            {
                bestErr = err;
                best = b * beatMs;
            }
        }
        return best;
    }

    void FxSlot::process (float* l, float* r, int n, const FxParams& p)
    {
        if (n <= 0)
            return;
        const auto changed = [this] (const FxParams& q)
        { return q.type != lastType || (q.type == fxtype::mod && q.modType != lastModType); };
        if (!primed)
        {
            primed = true;
            lastType = p.type;
            lastModType = p.modType;
        }
        else if (fadeOutLeft == 0 && changed (p))
        {
            fadeOutLeft = fadeLen; // finish the old effect under a fade, then switch
            fadeInLeft = 0;
        }
        if (fadeOutLeft == 0)
            oldParams = p;

        int i = 0;
        while (i < n)
        {
            if (fadeOutLeft > 0)
            {
                const int m = std::min (fadeOutLeft, n - i);
                runType (l + i, r + i, m, oldParams);
                for (int k = 0; k < m; ++k)
                {
                    const float g = (float) (fadeOutLeft - k - 1) / (float) fadeLen;
                    l[i + k] *= g;
                    r[i + k] *= g;
                }
                fadeOutLeft -= m;
                i += m;
                if (fadeOutLeft == 0)
                {
                    clearState ();
                    for (auto& f : fxLpf)
                        f.reset ();
                    for (auto& f : modLpf)
                        f.reset ();
                    lastType = p.type;
                    lastModType = p.modType;
                    oldParams = p;
                    primeLeft = fadeLen;  // run the new effect muted first so its lines fill (no step when a
                    fadeInLeft = fadeLen; // voice with no direct path, such as the chorus, arrives mid fade)
                }
                continue;
            }
            if (primeLeft > 0)
            {
                const int m = std::min (primeLeft, n - i);
                runType (l + i, r + i, m, p);
                std::fill (l + i, l + i + m, 0.0f);
                std::fill (r + i, r + i + m, 0.0f);
                primeLeft -= m;
                i += m;
                continue;
            }
            const int m = n - i;
            runType (l + i, r + i, m, p);
            for (int k = 0; k < m && fadeInLeft > 0; ++k, --fadeInLeft)
            {
                const float g = 1.0f - (float) fadeInLeft / (float) fadeLen;
                l[i + k] *= g;
                r[i + k] *= g;
            }
            i += m;
        }
    }

    void FxSlot::runType (float* l, float* r, int n, const FxParams& p)
    {
        const int type = p.type;
        if (type < 0 || type >= fxtype::off)
            return;
        for (auto* ln : {&lineA[0], &lineA[1], &lineB[0], &lineB[1]})
            ln->linear = p.linearRead;

        switch (type)
        {
        case fxtype::delay:
            processRepeats (l, r, n, p, false);
            break;
        case fxtype::echo:
            processRepeats (l, r, n, p, true);
            break;
        case fxtype::mod:
            processModulation (l, r, n, p);
            break;
        case fxtype::pitch:
            processPitch (l, r, n, p);
            break;
        case fxtype::pan:
            processPan (l, r, n, p);
            break;
        default:
            break;
        }

        if (std::fabs (p.lpfKHz - lastLpf) > 1.0e-6f)
        {
            for (auto& f : fxLpf)
                f.set (sr, p.lpfKHz);
            lastLpf = p.lpfKHz;
        }
        if (fxLpf[0].active)
            for (int i = 0; i < n; ++i)
            {
                l[i] = fxLpf[0].process (l[i]);
                r[i] = fxLpf[1].process (r[i]);
            }

        if (type == fxtype::mod)
        {
            if (std::fabs (p.modLpfKHz - lastModLpf) > 1.0e-6f)
            {
                for (auto& f : modLpf)
                    f.set (sr, p.modLpfKHz);
                lastModLpf = p.modLpfKHz;
            }
            if (modLpf[0].active)
                for (int i = 0; i < n; ++i)
                {
                    l[i] = modLpf[0].process (l[i]);
                    r[i] = modLpf[1].process (r[i]);
                }
        }
    }

    // Delay and echo: output = input + delayed x level; each channel's delayed signal feeds back to its own input
    // ("the left or right channel delay signal fed back to the input ... for the corresponding channel", REV5 owner's
    // manual pp. 25-26; SPX90 the same). HIGH (x0.1..1.0) "reduces the high-frequency feedback": a one-pole low-pass
    // in the loop whose gain at 5 kHz is HIGH (LoopHighCut.h; measured on a third-party SPX90 model), so each repeat
    // is darker than the last; the first repeat is not filtered. REV7: the delay has no feedback and one level (p. 14);
    // the echo has one feedback gain (0-99 %) and HIGH DAMP (p. 14, 0-10; the processor maps it to this ratio). REV5:
    // delay up to 2,900 ms, echo up to 1,400 ms, L and R feedback -99..+99 %.
    void FxSlot::processRepeats (float* l, float* r, int n, const FxParams& p, bool echo)
    {
        const bool era5 = p.era == 1;
        const double maxMs =
            echo ? (era5 ? kMaxEchoMsEra5 : kMaxEchoMsEra7) : (era5 ? kMaxDelayMsEra5 : kMaxDelayMsEra7);
        const double lMs = clampd (syncedMs (echo ? p.echoLMs : p.dlyLMs, p), 0.1, maxMs);
        const double rMs = p.link ? lMs : clampd (syncedMs (echo ? p.echoRMs : p.dlyRMs, p), 0.1, maxMs);
        // Whole samples once settled (M6, 2026-11-24): the hardware's fixed delays are whole native samples, and the
        // cubic read at a fraction took 0.8 / 1.7 / 3.5 dB off the repeats at 8 / 10 / 12.5 kHz (era 7, the bridge
        // latency leaving a fraction). Moves the time by at most half a sample; a glide still reads between samples.
        const double tgtL = std::round (lMs * 0.001 * sr), tgtR = std::round (rMs * 0.001 * sr);
        // The output read sits the bridge delay ahead of the loop read (whole samples): the first repeat keeps panel
        // timing and the loop period stays the panel time (it took the latency off every repeat before, 2.6 ms each).
        const double adv = std::round (std::max (0.0f, p.repeatAdvanceMs) * 0.001 * sr);
        const double tgtLevel = clampd (echo ? p.echoLevel : p.dlyLevel, 0.0, 100.0) * 0.01;
        const double fbLo = era5 ? -kMaxFb : 0.0;
        double fbL = 0.0, fbR = 0.0, high = 1.0;
        if (echo)
        {
            fbL = clampd (p.echoFb * 0.01, fbLo, kMaxFb);
            fbR = (era5 && !p.link) ? clampd (p.echoFbR * 0.01, fbLo, kMaxFb) : fbL;
            high = clampd (p.echoDamp, 0.1, 1.0);
        }
        else if (era5)
        {
            fbL = clampd (p.dlyFb * 0.01, fbLo, kMaxFb);
            fbR = p.link ? fbL : clampd (p.dlyFbR * 0.01, fbLo, kMaxFb);
            high = clampd (p.dlyHigh, 0.1, 1.0);
        }
        if (std::not_equal_to<double>{}(high, highFor)) // per-sample calls: solve only when HIGH changes
        {
            highFor = high;
            highA = (float) loopHighCoef (high, sr);
        }
        // Era-7 echo: a two-tap average in the loop, y = (1 - c) x + c x[n-1] with c = (1 - HIGH) / 2, so HIGH is the
        // gain at the native Nyquist (15.6 kHz). Fitted on the REV7 Stereo Echo capture (HIGH DAMP 9): -0.45 /
        // -0.71 dB per repeat at 8 / 11.2 kHz, matched at -0.45 / -0.73; the SPX90 one-pole at 5 kHz took -1.8 / -2.6
        // (inference: the same two-sample averages as the era-7 taps, pass 39). Era 5 keeps the measured one-pole.
        const bool fir = echo && !era5 && high < kLoopHighThru;
        const float c = fir ? (float) (0.5 * (1.0 - high)) : 0.0f;
        const bool lowpass = !fir && highA < 1.0f;
        const float g = highA;
        for (int i = 0; i < n; ++i)
        {
            const double dl = sTime[0].tick (tgtL, smoothA), dr = sTime[1].tick (tgtR, smoothA);
            const float lev = (float) sLevel.tick (tgtLevel, smoothA);
            const float fl = (float) sFb.tick (fbL, smoothA), fr = (float) sA.tick (fbR, smoothA);
            const float yl = lineA[0].read (dl - adv), yr = lineA[1].read (dr - adv);
            float ul = yl, ur = yr;
            if (adv > 0.0)
            {
                ul = lineA[0].read (dl);
                ur = lineA[1].read (dr);
            }
            if (fir)
            {
                const float xl = ul, xr = ur;
                ul = xl + c * (dampPrev[0] - xl);
                ur = xr + c * (dampPrev[1] - xr);
                dampPrev = {xl, xr};
            }
            else if (lowpass)
            {
                ul = damp[0] = flush (damp[0] + g * (ul - damp[0]));
                ur = damp[1] = flush (damp[1] + g * (ur - damp[1]));
            }
            const float il = l[i], ir = r[i];
            lineA[0].write (il + fl * ul);
            lineA[1].write (ir + fr * ur);
            l[i] = il + lev * yl;
            r[i] = ir + lev * yr;
        }
    }

    void FxSlot::processModulation (float* l, float* r, int n, const FxParams& p)
    {
        const double maxHz = p.era == 1 ? kMaxLfoHzEra5 : kMaxLfoHzEra7;
        double rateT = clampd (p.lfoRateHz, 0.1, maxHz);
        if (p.sync && p.hostBpm > 1.0)
        {
            // LFO sync (fx_sync, shared with delay and echo): one LFO cycle per the nearest note value at the host
            // tempo (plugin feature, not on the hardware)
            const double beatS = 60.0 / p.hostBpm;
            double best = rateT, bestErr = 1.0e30;
            for (double b : kLfoBeats)
            {
                const double hz = 1.0 / (b * beatS);
                const double err = std::fabs (std::log (hz / rateT));
                if (hz <= maxHz && err < bestErr)
                {
                    bestErr = err;
                    best = hz;
                }
            }
            rateT = best;
        }
        const double msToS = 0.001 * sr;
        const double depth = clampd (p.modDepth, 0.0, 100.0) * 0.01;
        const int mt = p.modType;
        const bool tri = p.triangleLfo;
        const auto lfo = [tri] (double cycles) { return tri ? triangle (cycles) : sine (cycles); };
        // a sweep position in -1..1 (or 0..1 with `unipolar`), on 256 steps when stepped
        const bool stepped = p.steppedSweep;
        const auto sweep = [stepped] (double s, bool unipolar = false)
        {
            if (!stepped)
                return s;
            return unipolar ? std::round (s * kModSteps) / kModSteps
                            : std::round ((s + 1.0) * 0.5 * kModSteps) / kModSteps * 2.0 - 1.0;
        };

        for (int i = 0; i < n; ++i)
        {
            const double rate = sRate.tick (rateT, smoothA);
            phase = wrap1 (phase + rate / sr);
            const float il = l[i], ir = r[i];
            // mono sum: unity for the mono wet input (FX > Rev); equal power after the reverb, whose L and R are
            // uncorrelated (a plain half-sum lost 3 dB there, 2026-11-22)
            const float m = (p.afterReverb ? kEqualPowerSum : 0.5f) * (il + ir);
            float ol = il, orr = ir;

            if (mt == modtype::symphonic)
            {
                // two LFOs per voice (kSym*): the set rate and a slow one at rate / 11, clipped to the 8.19 ms line
                phaseSlow = wrap1 (phaseSlow + rate * kSymSlowRatio / sr);
                const double dm = sTime[0].tick (depth, smoothA);
                float v[3];
                for (int k = 0; k < 3; ++k)
                {
                    const double ms = kSymCentreMs + dm * (kSymFastMs * sweep (lfo (phase + kSymFastPhase[k])) +
                                                           kSymSlowMs * sweep (lfo (phaseSlow + kSymSlowPhase[k])));
                    v[k] = lineA[0].read (clampd (ms, 0.0, kSymMaxMs) * msToS);
                }
                lineA[0].write (m);
                ol = kSymVoiceGain * (v[0] + v[1]);
                orr = kSymVoiceGain * (v[0] + v[2]);
            }
            else if (mt == modtype::chorusA || mt == modtype::chorusB)
            {
                // Three voices (centre, left, right) at LFO phases 0, 120 and 240 degrees; delay and level both
                // follow the LFO. No direct signal: "the input is split into three signals" (REV7 OM p. 19, SPX90 the
                // same); each side is its own voice and the centre at 0.707, unity average power as the Symphonic
                // (was 0.5 direct + 0.25 per voice, inference, which left the chorus about 5 dB down and its wet part
                // 11 dB down; 2026-11-22).
                const double dm = sTime[0].tick (clampd (p.dmDepth, 0.0, 100.0) * 0.01, smoothA);
                const double am = sTime[1].tick (clampd (p.amDepth, 0.0, 100.0) * 0.01, smoothA);
                const double baseS = kChorusBaseMs * msToS;
                const double swingS = kDmSwingMs * msToS * dm;
                float v[3];
                for (int k = 0; k < 3; ++k)
                {
                    const double s = lfo (phase + k / 3.0);
                    v[k] = lineA[0].read (baseS + swingS * sweep (s)) * (float) (1.0 - am * (0.5 + 0.5 * s));
                }
                lineA[0].write (m);
                ol = kChorusVoiceGain * (v[0] + v[1]);
                orr = kChorusVoiceGain * (v[0] + v[2]);
            }
            else if (mt == modtype::tremolo)
            {
                // Amplitude modulation, L and R at 120 and 240 degrees (inference; stereo input preserved). With
                // tremoloDm each side also passes a short delay swept by the same LFO, as the chorus voices are.
                const double am = sA.tick (depth, smoothA);
                const double sl = lfo (phase + 1.0 / 3.0), sr2 = lfo (phase + 2.0 / 3.0);
                float xl = il, xr = ir;
                if (p.tremoloDm)
                {
                    const double baseS = kTremoloBaseMs * msToS, swingS = kTremoloSwingMs * msToS * am;
                    xl = lineA[0].read (baseS + swingS * sweep (sl));
                    xr = lineA[1].read (baseS + swingS * sweep (sr2));
                    lineA[0].write (il);
                    lineA[1].write (ir);
                }
                ol = xl * (float) (1.0 - am * (0.5 + 0.5 * sl));
                orr = xr * (float) (1.0 - am * (0.5 + 0.5 * sr2));
            }
            else if (mt == modtype::flange || mt == modtype::revFlange)
            {
                // L lengthens while R shortens. The swing is limited so the delay never crosses zero (inference).
                // Reverb flange: the same flange with its feedback fixed at 30 % and the base held to 30 ms; its REV
                // TIME is the program's reverb (the engine), not a comb here (2026-11-22: our 37 ms decay comb rang
                // like a spring; both units' Reverb Flange programs are the engine with this flange before it).
                const double maxBase = mt == modtype::revFlange ? kMaxRevFlangeMs : kMaxFlangeMs;
                const double baseS = sB.tick (clampd (p.modDelayMs, 0.1, maxBase), smoothA) * msToS;
                const double swingS = std::max (
                    0.0, std::min (sA.tick (depth, smoothA) * kDmSwingMs * msToS, baseS - (kMinDelaySamples + 0.5)));
                const bool rev = mt == modtype::revFlange;
                const float fb = (float) sFb.tick (rev ? kRevFlangeFb : clampd (p.modFb * 0.01, 0.0, kMaxFb), smoothA);
                const double s = sweep (lfo (phase));
                const float yl = lineA[0].read (baseS + swingS * s), yr = lineA[1].read (baseS - swingS * s);
                lineA[0].write (il + fb * yl);
                lineA[1].write (ir + fb * yr);
                ol = kModPairGain * (il + yl);
                orr = kModPairGain * (ir + yr);
            }
            else // phasing
            {
                // Fixed L and R delays; only the centre voice sweeps between zero and the right delay.
                const double rS =
                                 clampd (p.modDelayMs, 0.1, p.era == 1 ? kMaxPhasingMsEra5 : kMaxPhasingMsEra7) * msToS,
                             lS = rS * kPhasingLeftRatio;
                const double dC =
                    sA.tick (depth, smoothA) * rS *
                    sweep (0.5 - 0.5 * (tri ? triangle (phase + 0.25) : std::cos (2.0 * kPi * phase)), true);
                const float tl = lineA[0].read (lS), tr = lineA[0].read (rS), tc = lineA[0].read (dC);
                lineA[0].write (m);
                ol = kModPairGain * (tl + tc);
                orr = kModPairGain * (tr + tc);
            }
            l[i] = ol;
            r[i] = orr;
        }
    }

    // Pitch (REV5 owner's manual pp. 32-36): the classic delay-line shifter, two taps sweeping a short line and
    // crossfaded with a raised-cosine window (algorithm and window shape inference; the 10 ms window length is
    // measured on a third-party SPX90 model). The wet input is mono, as on
    // the hardware. A: one voice, then its delay, with feedback into the shifter so each repeat shifts again
    // (p. 32), output in the centre. B: two independent voices, both in the centre, no feedback (p. 34). C: one voice
    // to each side, delays 0.1-200 ms, no feedback (p. 35). The output is the shifted voices only; the mix control
    // brings back the dry. The MIDI base key is not modelled (DAW automation covers it).
    void FxSlot::processPitch (float* l, float* r, int n, const FxParams& p)
    {
        const int sel = std::clamp (p.pitchSel, 0, 2);
        const double maxMs = sel == 2 ? kMaxPitchCMs : kMaxPitchDelayMs;
        // the voices' ratios only change with their settings
        const auto ratioOf = [] (PitchRatio& c, float st, float ct)
        {
            if (std::not_equal_to<float>{}(st, c.st) || std::not_equal_to<float>{}(ct, c.ct))
            {
                c.st = st;
                c.ct = ct;
                c.ratio = std::pow (2.0, (clampd (st, -12.0, 12.0) + clampd (ct, -100.0, 100.0) * 0.01) / 12.0);
            }
            return c.ratio;
        };
        const double windowS = kPitchWindowMs * 0.001 * sr;
        const double dPhase1 = (1.0 - ratioOf (pitchRatio[0], p.pitchShiftSt, p.pitchFineCents)) / windowS;
        const double dPhase2 = (1.0 - ratioOf (pitchRatio[1], p.pitch2ShiftSt, p.pitch2FineCents)) / windowS;
        const double tgt1 = clampd (p.pitchDelayMs, 0.1, maxMs) * 0.001 * sr;
        const double tgt2 = clampd (p.pitch2DelayMs, 0.1, maxMs) * 0.001 * sr;
        const double tgtFb = sel == 0 ? clampd (p.pitchFb * 0.01, 0.0, kMaxFb) : 0.0;
        const auto shift = [&] (double& ph, double dPh) noexcept
        {
            ph = wrap1 (ph + dPh);
            const double p1 = ph, p2 = wrap1 (ph + 0.5);
            const double sn = std::sin (kPi * p1);
            const float w1 = (float) (sn * sn), w2 = 1.0f - w1;
            return w1 * lineB[0].read (kMinDelaySamples + p1 * windowS) +
                   w2 * lineB[0].read (kMinDelaySamples + p2 * windowS);
        };
        for (int i = 0; i < n; ++i)
        {
            const double d1 = sTime[0].tick (tgt1, smoothA), d2 = sTime[1].tick (tgt2, smoothA);
            const float fb = (float) sFb.tick (tgtFb, smoothA);
            const float m = 0.5f * (l[i] + r[i]);
            const float y1 = lineA[0].read (d1);
            const float s1 = shift (shiftPhase, dPhase1);
            lineA[0].write (s1);
            if (sel == 0)
            {
                lineB[0].write (m + fb * y1);
                l[i] = r[i] = y1;
                continue;
            }
            const float y2 = lineA[1].read (d2);
            lineA[1].write (shift (shiftPhase2, dPhase2));
            lineB[0].write (m);
            if (sel == 1)
                l[i] = r[i] = (float) kPitchPairGain * (y1 + y2);
            else
            {
                l[i] = y1;
                r[i] = y2;
            }
        }
    }

    // Auto-pan: the mono sum is panned with linear gains (L = 1 - pos, R = pos: unity when hard to one side, 0.5 each
    // at the centre), the law of a third-party SPX90 model's Triggered Pan (2026-11-22; was equal-power,
    // inference). L>R and R>L are ramps (one sweep per LFO cycle);
    // L <-> R sweeps left to right and back once per cycle (REV5 OM p. 36 prints L>R, L<R, L<>R; read as a back and
    // forth sweep like the E/R mode PAN B, p. 20; was "middle-out", inference, until the 2026-11-22 manual audit).
    // Depth scales the sweep around the centre.
    void FxSlot::processPan (float* l, float* r, int n, const FxParams& p)
    {
        const double rate = clampd (p.panRateHz, 0.1, kMaxLfoHzEra5);
        const double depth = clampd (p.panDepth, 0.0, 100.0) * 0.01;
        const float a = panA;
        for (int i = 0; i < n; ++i)
        {
            phase = wrap1 (phase + rate / sr);
            double raw;
            if (p.panDir == pandir::rightToLeft)
                raw = 1.0 - phase;
            else if (p.panDir == pandir::backAndForth) // L <-> R: left to right and back once per cycle
                raw = phase < 0.5 ? 2.0 * phase : 2.0 - 2.0 * phase;
            else
                raw = phase;
            const double pos = 0.5 + (raw - 0.5) * depth;
            const float tl = (float) (1.0 - pos), tr = (float) pos;
            panGain[0] += a * (tl - panGain[0]);
            panGain[1] += a * (tr - panGain[1]);
            const float m = 0.5f * (l[i] + r[i]);
            l[i] = m * panGain[0];
            r[i] = m * panGain[1];
        }
    }

    double FxSlot::tailSeconds (const FxParams& p)
    {
        const auto repeats = [] (double timeMs, double fbPct)
        {
            const double fb = std::fabs (clampd (fbPct * 0.01, -kMaxFb, kMaxFb));
            const double t = timeMs * 0.001;
            if (fb < 1.0e-3)
                return t;
            return std::min (kTailCapSeconds, t * (1.0 + std::log (1.0e-3) / std::log (fb)));
        };
        switch (p.type)
        {
        case fxtype::delay:
        {
            const double maxMs = p.era == 1 ? kMaxDelayMsEra5 : kMaxDelayMsEra7;
            const double fb = p.era == 1 ? std::max (std::fabs (p.dlyFb), std::fabs (p.dlyFbR)) : 0.0;
            return repeats (clampd (std::max (p.dlyLMs, p.dlyRMs), 0.1, maxMs), fb);
        }
        case fxtype::echo:
        {
            const double maxMs = p.era == 1 ? kMaxEchoMsEra5 : kMaxEchoMsEra7;
            const double fb = p.era == 1 ? std::max (std::fabs (p.echoFb), std::fabs (p.echoFbR)) : p.echoFb;
            return repeats (clampd (std::max (p.echoLMs, p.echoRMs), 0.1, maxMs), fb);
        }
        case fxtype::mod:
            if (p.modType == modtype::revFlange)
                return repeats (clampd (p.modDelayMs, 0.1, 30.0) + 5.0, 30.0f);
            if (p.modType == modtype::flange)
                return repeats (clampd (p.modDelayMs, 0.1, 100.0) + 5.0, p.modFb);
            return 0.1;
        case fxtype::pitch:
            return repeats (clampd (std::max (p.pitchDelayMs, p.pitch2DelayMs), 0.1, kMaxPitchDelayMs) + kPitchWindowMs,
                            p.pitchSel == 0 ? p.pitchFb : 0.0f);
        default:
            return 0.0;
        }
    }
} // namespace yrev::dsp
