#include "ReverbMeasure.h"
#include "dsp/Converter.h"
#include "dsp/Engine.h"
#include "dsp/ErTables.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <complex>
#include <cstdio>
#include <cstring>
#include <vector>

using Catch::Approx;
using yrev::dsp::Engine;
using yrev::dsp::EngineParams;
namespace m = yrev::test;

namespace
{
    constexpr double kSr = 48000.0;
    constexpr int kBlock = 256;

    struct Ir
    {
        std::vector<float> l, r;
    };

    // Impulse response of a fresh engine, rendered in blocks of `block`.
    Ir impulse (const EngineParams& p, double seconds, int block = kBlock, double fs = kSr)
    {
        juce::ScopedNoDenormals noDenormals;
        Engine e;
        e.prepare (fs, block);
        const auto n = (size_t) (fs * seconds);
        Ir ir;
        ir.l.assign (n, 0.0f);
        ir.r.assign (n, 0.0f);
        ir.l[0] = ir.r[0] = 1.0f;
        for (size_t pos = 0; pos < n; pos += (size_t) block)
        {
            const int len = (int) std::min ((size_t) block, n - pos);
            e.process (ir.l.data () + pos, ir.r.data () + pos, ir.l.data () + pos, ir.r.data () + pos, len, p);
        }
        return ir;
    }

    EngineParams lateOnly (float rt, float hi = 1.0f, float lo = 1.0f)
    {
        EngineParams p;
        p.rt = rt;
        p.hiRatio = hi;
        p.loRatio = lo;
        p.erRevBalance = 0.0f; // late network only
        p.preDelayMs = 1.0f;
        return p;
    }

    double bandRt (const std::vector<float>& x, float centre, double fs = kSr)
    {
        return m::reverbTime (m::octaveBand (x, fs, centre), fs);
    }
} // namespace

TEST_CASE ("the 1 kHz reverb time follows rt")
{
    for (const float rt : {0.6f, 2.6f, 6.0f})
    {
        const auto ir = impulse (lateOnly (rt), rt * 1.2 + 0.5);
        CAPTURE (rt);
        CHECK (bandRt (ir.l, 1000.0f) == Approx (rt).epsilon (0.12));
        CHECK (bandRt (ir.r, 1000.0f) == Approx (rt).epsilon (0.12));
    }
}

TEST_CASE ("short decays follow rt at every room size, and short bass decays follow lo_ratio")
{
    // The late feed fades with rt and the input diffuser is held faster than the fastest band (gap 11,
    // docs/design/engine.md), so neither the reflection span nor the diffuser smear holds up a short decay.
    for (const float size : {0.5f, 2.0f, 5.0f, 10.0f})
        for (const float rt : {0.3f, 0.45f})
        {
            auto p = lateOnly (rt);
            p.roomSize = size;
            p.spaceMod = 2.0f;
            const auto ir = impulse (p, 1.8);
            const double r = 0.5 * (bandRt (ir.l, 1000.0f) + bandRt (ir.r, 1000.0f));
            CAPTURE (size, rt, r);
            CHECK (r == Approx (rt).epsilon (0.15));
        }
    auto p = lateOnly (0.8f, 1.0f, 0.3f);
    const auto ir = impulse (p, 1.8);
    const double ratio =
        (bandRt (ir.l, 125.0f) + bandRt (ir.r, 125.0f)) / (bandRt (ir.l, 1000.0f) + bandRt (ir.r, 1000.0f));
    CAPTURE (ratio);
    CHECK (ratio == Approx (0.3).epsilon (0.25));
}

TEST_CASE ("the reverb time holds at other sample rates")
{
    for (const double fs : {44100.0, 96000.0})
    {
        const auto ir = impulse (lateOnly (2.0f), 2.8, kBlock, fs);
        CAPTURE (fs);
        CHECK (bandRt (ir.l, 1000.0f, fs) == Approx (2.0).epsilon (0.12));
    }
}

TEST_CASE ("hi_ratio shortens the treble decay and lo_ratio sets the bass decay")
{
    const float rt = 3.0f;
    const auto flat = impulse (lateOnly (rt), 4.0);
    const double flat8k = bandRt (flat.l, 8000.0f), flat125 = bandRt (flat.l, 125.0f);
    CHECK (flat8k == Approx (rt).epsilon (0.15));
    CHECK (flat125 == Approx (rt).epsilon (0.15));

    const auto dark = impulse (lateOnly (rt, 0.4f, 2.0f), 7.0);
    CHECK (bandRt (dark.l, 1000.0f) == Approx (rt).epsilon (0.15));
    CHECK (bandRt (dark.l, 8000.0f) / bandRt (dark.l, 1000.0f) == Approx (0.4).epsilon (0.2));
    CHECK (bandRt (dark.l, 125.0f) / bandRt (dark.l, 1000.0f) == Approx (2.0).epsilon (0.2));

    // the extremes of lo_ratio, every density: the bass law holds at 0.3 as well (docs/design/engine.md)
    for (const float lo : {0.3f, 0.5f, 2.4f})
        for (const float density : {1.0f, 3.0f, 4.0f})
        {
            auto p = lateOnly (rt, 1.0f, lo);
            p.density = density;
            const auto ir = impulse (p, rt * std::max (1.0f, lo) * 1.1 + 0.6);
            CAPTURE (lo, density);
            CHECK (bandRt (ir.l, 125.0f) / bandRt (ir.l, 1000.0f) == Approx (lo).epsilon (0.2));
            CHECK (bandRt (ir.r, 125.0f) / bandRt (ir.r, 1000.0f) == Approx (lo).epsilon (0.2));
        }
}

TEST_CASE ("the shelves act where the captures put them: a gentle 8.5 kHz high shelf and 300 Hz")
{
    // Fitted to the per-band RT shape of the REV7/REV5 captures (Q-27, docs/design/engine.md, seventh pass):
    // with hi 0.5 the treble damping grows gently from about 2 kHz and the 8 kHz octave follows the ratio; with lo
    // 1.5, 500 Hz stays near the mid decay.
    const auto ir = impulse (lateOnly (3.0f, 0.5f, 1.5f), 5.0);
    const double r1k = 0.5 * (bandRt (ir.l, 1000.0f) + bandRt (ir.r, 1000.0f));
    auto ratio = [&] (float fc) { return 0.5 * (bandRt (ir.l, fc) + bandRt (ir.r, fc)) / r1k; };
    const double r2k = ratio (2000.0f), r4k = ratio (4000.0f), r8k = ratio (8000.0f), r500 = ratio (500.0f);
    const double r125 = ratio (125.0f);
    CAPTURE (r125, r500, r2k, r4k, r8k);
    CHECK (r2k > 0.85);
    CHECK (r4k > 0.6);
    CHECK (r4k < 0.9);
    CHECK (r8k == Approx (0.5).epsilon (0.2));
    CHECK (r500 < 1.25);
    CHECK (r125 == Approx (1.5).epsilon (0.2));
}

TEST_CASE ("the era input bandwidth darkens the tail without changing its decay")
{
    // lateBandwidthHz (set by the converter per era) low-passes what the late network hears; the reflections are not
    // filtered and the band decay rates stay as set (docs/design/engine.md, seventh pass).
    auto open = lateOnly (2.6f, 0.7f, 1.2f);
    auto dark = open;
    dark.lateBandwidthHz = 4000.0f;
    const auto a = impulse (open, 3.4), b = impulse (dark, 3.4);
    auto bandEnergy = [] (const std::vector<float>& x, float fc)
    {
        const auto y = m::octaveBand (x, kSr, fc);
        double e = 0.0;
        for (float v : y)
            e += (double) v * v;
        return 10.0 * std::log10 (e + 1.0e-30);
    };
    const double d1k = bandEnergy (b.l, 1000.0f) - bandEnergy (a.l, 1000.0f);
    const double d8k = bandEnergy (b.l, 8000.0f) - bandEnergy (a.l, 8000.0f);
    CAPTURE (d1k, d8k);
    CHECK (std::abs (d1k) < 1.5);
    CHECK (d8k < -4.0);
    CHECK (bandRt (b.l, 1000.0f) == Approx (bandRt (a.l, 1000.0f)).epsilon (0.05));
    CHECK (bandRt (b.l, 8000.0f) / bandRt (b.l, 1000.0f) ==
           Approx (bandRt (a.l, 8000.0f) / bandRt (a.l, 1000.0f)).epsilon (0.1));
}

TEST_CASE ("the wet LPF is first order: 6 dB per octave above its corner")
{
    // wet_lpf (REV5 LPF FRQ) is a one-pole since pass 21: fitted per capture, a one-pole beats a second-order low-pass
    // in every REV5 program family (docs/design/engine.md, twenty-first pass). At a 2 kHz corner the 4 kHz octave falls
    // about 7 dB and the 8 kHz octave about 5 dB more (a second-order filter: about 12 and 12).
    auto thru = lateOnly (1.5f, 1.0f, 1.0f);
    auto low = thru;
    low.wetLpfKHz = 2.0f;
    const auto a = impulse (thru, 2.0), b = impulse (low, 2.0);
    auto bandEnergy = [] (const std::vector<float>& x, float fc)
    {
        const auto y = m::octaveBand (x, kSr, fc);
        double e = 0.0;
        for (float v : y)
            e += (double) v * v;
        return 10.0 * std::log10 (e + 1.0e-30);
    };
    const double d4k = bandEnergy (b.l, 4000.0f) - bandEnergy (a.l, 4000.0f);
    const double d8k = bandEnergy (b.l, 8000.0f) - bandEnergy (a.l, 8000.0f);
    CAPTURE (d4k, d8k);
    CHECK (d4k < -5.0);
    CHECK (d4k > -9.0);
    CHECK (d8k - d4k < -3.5);
    CHECK (d8k - d4k > -7.5);
}

TEST_CASE ("the reflection bandwidth darkens the opening, 0 is off and the tail does not move")
{
    // erBandwidthHz (set by the converter: 7.5 kHz era 7, off in era 5 since pass 21) low-passes the reflection outputs
    // (docs/design/engine.md, fifteenth and seventeenth passes); the late feed is taken before it, so the tail is
    // unchanged.
    EngineParams open;
    open.erTableSet = 1;
    auto dark = open;
    dark.erBandwidthHz = 9000.0f;
    auto off = open;
    off.erBandwidthHz = 0.0f;
    const auto a = impulse (open, 1.0), b = impulse (dark, 1.0), c = impulse (off, 1.0);
    CHECK (a.l == c.l); // 0 = off, bit for bit
    const auto first = [] (const std::vector<float>& x, int ms)
    { return std::vector<float> (x.begin (), x.begin () + (long) (ms * 0.001 * kSr)); };
    auto bandEnergy = [] (const std::vector<float>& x, float fc)
    {
        const auto y = m::octaveBand (x, kSr, fc);
        double e = 0.0;
        for (float v : y)
            e += (double) v * v;
        return 10.0 * std::log10 (e + 1.0e-30);
    };
    // the darkening on the reflections alone (since pass 22 the era-5 late field speaks early enough to dilute a mix)
    auto erOnly = [] (EngineParams q)
    {
        q.erRevBalance = 100.0f;
        return impulse (q, 0.2);
    };
    const auto ea = first (erOnly (open).l, 150), eb = first (erOnly (dark).l, 150);
    const double d1k = bandEnergy (eb, 1000.0f) - bandEnergy (ea, 1000.0f);
    const double d8k = bandEnergy (eb, 8000.0f) - bandEnergy (ea, 8000.0f);
    CAPTURE (d1k, d8k);
    CHECK (std::abs (d1k) < 0.5);
    CHECK (d8k < -1.5);
    CHECK (d8k > -4.0);
    // the tail after the reflections is the same (late energy within 0.5 dB at 4 kHz)
    const std::vector<float> ta (a.l.begin () + (long) (0.5 * kSr), a.l.end ());
    const std::vector<float> tb (b.l.begin () + (long) (0.5 * kSr), b.l.end ());
    CHECK (std::abs (bandEnergy (tb, 4000.0f) - bandEnergy (ta, 4000.0f)) < 0.5);
}

TEST_CASE ("the 1 kHz reverb time holds at every density")
{
    for (const float density : {1.0f, 2.0f, 3.0f, 4.0f})
    {
        auto p = lateOnly (2.6f);
        p.density = density;
        const auto ir = impulse (p, 3.6);
        CAPTURE (density);
        CHECK (bandRt (ir.l, 1000.0f) == Approx (2.6).epsilon (0.1));
    }
}

TEST_CASE ("the default hall builds up: EDT a little above RT, echo density within the captures' range")
{
    // Both channels' 1 kHz energy, EDC from the impulse (pre-delay included), by Schroeder backward integration.
    // The REV7/REV5 hall captures give EDT 1.05-1.5 x RT and echo density 0.9 by 110-175 ms.
    EngineParams hall;
    const auto ir = impulse (hall, 4.0);
    const auto bl = m::octaveBand (ir.l, kSr, 1000.0f), br = m::octaveBand (ir.r, kSr, 1000.0f);
    std::vector<float> both (bl.size ());
    for (size_t i = 0; i < both.size (); ++i)
        both[i] = std::sqrt (0.5f * (bl[i] * bl[i] + br[i] * br[i]));
    const double rt = m::reverbTime (both, kSr, -5.0, -35.0), edt = m::reverbTime (both, kSr, 0.0, -10.0);
    CAPTURE (rt, edt);
    CHECK (rt == Approx (hall.rt).epsilon (0.1));
    CHECK (edt / rt > 1.03);
    CHECK (edt / rt < 1.3);
}

TEST_CASE ("the tail decays to silence and stays finite at the extremes")
{
    SECTION ("short decay reaches silence")
    {
        const auto ir = impulse (lateOnly (0.3f), 3.0);
        CHECK (m::peak (ir.l, (size_t) (2.5 * kSr), ir.l.size ()) < 1.0e-6f);
    }
    SECTION ("maximum diffusion and density at 99 s stay bounded")
    {
        auto p = lateOnly (99.0f, 1.0f, 2.4f);
        p.diffusion = 10.0f;
        p.density = 4.0f;
        const auto ir = impulse (p, 6.0);
        const float early = m::peak (ir.l, (size_t) (1.0 * kSr), (size_t) (2.0 * kSr));
        const float late = m::peak (ir.l, (size_t) (5.0 * kSr), ir.l.size ());
        CHECK (std::isfinite (late));
        CHECK (late <= early * 1.05f);
    }
    SECTION ("99 s with lo ratio 2.4 does not grow")
    {
        const auto ir = impulse (lateOnly (99.0f, 1.0f, 2.4f), 6.0);
        const float early = m::peak (ir.l, (size_t) (1.0 * kSr), (size_t) (2.0 * kSr));
        const float late = m::peak (ir.l, (size_t) (5.0 * kSr), ir.l.size ());
        CHECK (std::isfinite (late));
        CHECK (late <= early * 1.05f);
        CHECK (late > early * 0.5f); // still ringing: about -2 dB in 3 s at 99 s RT
    }
}

namespace
{
    // Spectral ripple of a late tail in [lo, hi) Hz, its decay removed: std of the fine spectrum (dB) about its
    // 1/6-octave running mean, over 0.1 s .. 1.0 s of the left channel (decaying Gaussian noise reads about 5.6 dB).
    double tailRipple (const std::vector<float>& x, float rt, double lo, double hi)
    {
        const auto a = (size_t) (0.1 * kSr), b = (size_t) (1.0 * kSr);
        std::vector<double> seg (b - a);
        for (size_t i = 0; i < seg.size (); ++i)
        {
            const double t = (double) i / kSr,
                         w = 0.5 - 0.5 * std::cos (2.0 * 3.141592653589793 * (double) i / (double) seg.size ());
            seg[i] = (double) x[a + i] * std::exp (6.907755 * t / rt) * w;
        }
        const double df = 0.25 * kSr / (double) seg.size ();
        std::vector<double> f, pw;
        for (double hz = lo / 1.12; hz < hi * 1.12; hz += df)
        {
            double re = 0.0, im = 0.0;
            const double w = 2.0 * 3.141592653589793 * hz / kSr;
            for (size_t i = 0; i < seg.size (); ++i)
            {
                re += seg[i] * std::cos (w * (double) i);
                im -= seg[i] * std::sin (w * (double) i);
            }
            f.push_back (hz);
            pw.push_back (re * re + im * im + 1.0e-30);
        }
        std::vector<double> dev;
        for (size_t k = 0; k < f.size (); k += 4)
        {
            if (f[k] < lo || f[k] >= hi)
                continue;
            double sum = 0.0;
            int count = 0;
            for (size_t j = 0; j < f.size (); ++j)
                if (f[j] >= f[k] / 1.0595 && f[j] < f[k] * 1.0595)
                {
                    sum += pw[j];
                    ++count;
                }
            dev.push_back (10.0 * std::log10 (pw[k] / (sum / count)));
        }
        double mean = 0.0, var = 0.0;
        for (double d : dev)
            mean += d / (double) dev.size ();
        for (double d : dev)
            var += (d - mean) * (d - mean) / (double) dev.size ();
        return std::sqrt (var);
    }
} // namespace

TEST_CASE ("loop mixing: the bass tail is less modal, era 7 keeps its bass modes, and nothing grows")
{
    // Nineteenth pass (docs/design/engine.md): Householder mixing of the loops' feedback, era 5 at every frequency,
    // era 7 above 400 Hz. Factory space mod 0. Per-band ripple 140-280 Hz against 1.1-2.2 kHz.
    double bass[2]{}, mid[2]{};
    for (const int era : {0, 1})
    {
        auto p = lateOnly (2.0f);
        p.erTableSet = era;
        const auto ir = impulse (p, 1.2);
        bass[era] = tailRipple (ir.l, 2.0f, 140.0, 280.0);
        mid[era] = tailRipple (ir.l, 2.0f, 1100.0, 2200.0);
        CAPTURE (era, bass[era], mid[era]);
        CHECK (mid[era] < 9.0); // unmixed 9.5 (era 7) / 9.8 (era 5); mixed 8.1 / 8.2
    }
    // era 5: unmixed 9.1, mixed 6.1; 7.8 since pass 62's sparser era-5 diffusers (towards the REV5 tails,
    // whose ripple is 11.2 dB against our 7.7)
    CHECK (bass[1] < 8.5);
    CHECK (bass[0] > bass[1] + 0.3); // era 7 keeps resolved bass modes (mixed only above 400 Hz): 8.3
    for (const int era : {0, 1})
    {
        auto p = lateOnly (99.0f, 1.0f, 2.4f);
        p.erTableSet = era;
        p.diffusion = 10.0f;
        p.density = 4.0f;
        const auto ir = impulse (p, 6.0);
        const float early = m::peak (ir.l, (size_t) (1.0 * kSr), (size_t) (2.0 * kSr));
        const float late = m::peak (ir.l, (size_t) (5.0 * kSr), ir.l.size ());
        CAPTURE (era, early, late);
        CHECK (std::isfinite (late));
        CHECK (late <= early * 1.05f);
    }
}

TEST_CASE ("the late tail is decorrelated between L and R")
{
    const auto ir = impulse (lateOnly (2.6f), 2.0);
    CHECK (std::abs (m::correlation (ir.l, ir.r, (size_t) (0.2 * kSr), ir.l.size ())) < 0.2);
}

TEST_CASE ("era-7 Spring plays its own room-size-1.0 chart and blends to the size-4.0 chart")
{
    // REV7 Spring L: the size-1.0 chart spans 33.6 ms, the size-4.0 chart scaled by 1/4 would span 48.3 ms
    // (data/er-taps/rev7; docs/design/engine.md fourteenth pass)
    EngineParams p;
    p.erRevBalance = 100.0f; // reflections only
    p.preDelayMs = 25.0f;
    p.erCount = 34;
    p.erMode = 5; // Spring
    auto span = [&] (float size)
    {
        p.roomSize = size;
        const auto x = impulse (p, 0.4).l;
        size_t first = x.size (), last = 0;
        for (size_t i = 0; i < x.size (); ++i)
            if (std::abs (x[i]) > 1.0e-4f)
            {
                first = std::min (first, i);
                last = i;
            }
        return (double) (last - first) / kSr;
    };
    const double s1 = span (1.0f), s2 = span (2.0f), s4 = span (4.0f);
    CAPTURE (s1, s2, s4);
    CHECK (s1 == Approx (0.0336).margin (0.0015));
    CHECK (s4 / 4.0 == Approx (0.0483).margin (0.0015)); // the size-4.0 chart as before
    CHECK (s2 > 0.5 * s1 * 2.0 - 0.001);                 // between the two, stretched with size
    CHECK (s2 < 0.5 * s4 + 0.001);
}

TEST_CASE ("era-7 Random scales its size-4.0 chart at every room size (pass 48)")
{
    // The REV7 captures 27 (room size 0.9) and 23 (1.6) both play the size-4.0 Random chart scaled, not its
    // size-1.0 chart: L spans 450.1 ms at size 4.0 (data/er-taps/rev7)
    EngineParams p;
    p.erRevBalance = 100.0f;
    p.preDelayMs = 25.0f;
    p.erCount = 18;
    p.erMode = 2; // Random
    auto span = [&] (float size)
    {
        p.roomSize = size;
        const auto x = impulse (p, 0.7).l;
        size_t first = x.size (), last = 0;
        for (size_t i = 0; i < x.size (); ++i)
            if (std::abs (x[i]) > 1.0e-4f)
            {
                first = std::min (first, i);
                last = i;
            }
        return (double) (last - first) / kSr;
    };
    const double s09 = span (0.9f), s16 = span (1.6f), s4 = span (4.0f);
    CAPTURE (s09, s16, s4);
    CHECK (s4 == Approx (0.4501).margin (0.002));
    CHECK (s09 == Approx (s4 * 0.9 / 4.0).margin (0.001));
    CHECK (s16 == Approx (s4 * 1.6 / 4.0).margin (0.001));
}

TEST_CASE ("the reflections start at the pre-delay and spread with room size")
{
    EngineParams p;
    p.erRevBalance = 100.0f; // reflections only
    p.preDelayMs = 25.0f;
    p.erCount = 18;
    p.erMode = 0;

    auto lastTap = [] (const std::vector<float>& x)
    {
        size_t last = 0;
        for (size_t i = 0; i < x.size (); ++i)
            if (std::abs (x[i]) > 1.0e-4f)
                last = i;
        return (double) last / kSr;
    };
    auto firstTap = [] (const std::vector<float>& x)
    {
        for (size_t i = 0; i < x.size (); ++i)
            if (std::abs (x[i]) > 1.0e-4f)
                return (double) i / kSr;
        return -1.0;
    };

    p.roomSize = 4.0f;
    const auto a = impulse (p, 0.4);
    CHECK (firstTap (a.l) == Approx (0.025).margin (0.0005));
    // Small Hall L at size 4.0 ends 122.1 ms after the first tap (pass 42: hardware capture 5 at size 2.0, x2)
    CHECK (lastTap (a.l) - firstTap (a.l) == Approx (0.1221).margin (0.001));

    p.roomSize = 1.0f;
    const auto b = impulse (p, 0.4);
    CHECK (lastTap (b.l) - firstTap (b.l) == Approx (0.1221 / 4.0).margin (0.001));

    p.erCount = 1;
    const auto c = impulse (p, 0.4);
    CHECK (lastTap (c.l) == Approx (firstTap (c.l)).margin (0.0005));
}

TEST_CASE ("each era plays its own unit's E/R charts: 18 reflections per channel (REV7; S-Hall 17), 34 (REV5)")
{
    // ER only, room size 4.0 (the charts' own size), no 1st reflection, diffusion 0 (era 5 plays bare taps there):
    // count the separate reflections per channel.
    auto count = [] (const std::vector<float>& x)
    {
        int n = 0;
        for (size_t i = 1; i < x.size (); ++i)
            if (std::abs (x[i]) > 1.0e-6f && std::abs (x[i - 1]) <= 1.0e-6f)
                ++n;
        return n;
    };
    for (const int set : {0, 1})
        for (const int mode : {0, 1, 2, 3, 5})
        {
            EngineParams p;
            p.erRevBalance = 100.0f;
            p.erCount = 34;
            p.roomSize = 4.0f;
            p.preDelayMs = 1.0f;
            p.erMode = mode;
            p.erTableSet = set;
            p.diffusion = 0.0f;
            const auto ir = impulse (p, 0.8);
            CAPTURE (set, mode);
            const int n = set == 1 ? 34 : mode == 0 ? 17 : 18; // REV7 S-Hall: 17, measured (passes 42, 48)
            // REV5 S-Hall: L taps 2 and 3 coincide, as the hardware captures of 05 / 06 play them (pass 86)
            CHECK (count (ir.l) == n - (set == 1 && mode == 0 ? 1 : 0));
            CHECK (count (ir.r) == n);
        }
    // the two sets differ (REV5 Large Hall spreads to about 370 ms at size 4, the REV7's to about 330 ms)
    EngineParams a, b;
    a.erRevBalance = b.erRevBalance = 100.0f;
    a.erCount = b.erCount = 34;
    a.roomSize = b.roomSize = 4.0f;
    b.erTableSet = 1;
    const auto ia = impulse (a, 0.8), ib = impulse (b, 0.8);
    double diff = 0.0;
    for (size_t i = 0; i < ia.l.size (); ++i)
        diff += std::abs (ia.l[i] - ib.l[i]);
    CHECK (diff > 0.1);
}

TEST_CASE ("era-5 reflections start together in both channels at the pre-delay (twentieth pass)")
{
    // The captures of the REV5 E/R programs put both channels' first reflection at the same time, where the
    // charts print them 0.5-2.7 ms apart; the REV5 tables are anchored on each channel's own first tap. Pan A/B (6, 7)
    // open both channels together too, at every liveness (the REV5 Pan-A captures, 2026-11-21). Room size 4.0 (the
    // charts' own size), so a kept offset would be the full printed one.
    auto firstTap = [] (const std::vector<float>& x)
    {
        for (size_t i = 0; i < x.size (); ++i)
            if (std::abs (x[i]) > 1.0e-6f)
                return (double) i / kSr;
        return -1.0;
    };
    for (const int mode : {0, 1, 2, 3, 4, 5, 6, 7})
        for (const float live : {0.0f, 5.0f, 10.0f})
        {
            EngineParams p;
            p.erRevBalance = 100.0f;
            p.erTableSet = 1;
            p.erMode = mode;
            p.liveness = live;
            p.roomSize = 4.0f;
            p.preDelayMs = 20.0f;
            p.diffusion = 0.0f;
            const auto ir = impulse (p, 0.3);
            CAPTURE (mode, live);
            CHECK (firstTap (ir.l) == Approx (0.020).margin (0.0002));
            CHECK (firstTap (ir.r) == Approx (firstTap (ir.l)).margin (1.5 / kSr));
        }
}

TEST_CASE ("era-5 Rev modes play the reverb programs' own taps: no room size, liveness or count; one set per mode")
{
    // Pass 41: one set per Rev mode (12-20), read from the REV5 captures, from the initial delay, unscaled;
    // density no longer picks a set (pass 22's rule); the reverb-program diffusion (0.06 per step) rings more than the
    // charted modes' (0.024).
    auto render = [] (int mode, auto&& tweak)
    {
        EngineParams p;
        p.erRevBalance = 100.0f;
        p.preDelayMs = 20.0f;
        p.erTableSet = 1;
        p.erMode = mode;
        p.density = 4.0f;
        tweak (p);
        return impulse (p, 0.2);
    };
    auto none = [] (EngineParams&) {};
    const auto hall = render (12, none);
    CHECK (render (12, [] (EngineParams& p) { p.roomSize = 9.0f; }).l == hall.l);
    CHECK (render (12, [] (EngineParams& p) { p.liveness = 0.0f; }).l == hall.l);
    CHECK (render (12, [] (EngineParams& p) { p.erCount = 3; }).l == hall.l);
    auto spikes = [] (const std::vector<float>& x)
    {
        float peak = 0.0f;
        for (float v : x)
            peak = std::max (peak, std::abs (v));
        int n = 0;
        for (size_t i = 1; i + 1 < x.size (); ++i)
            if (std::abs (x[i]) > 0.3f * peak && std::abs (x[i]) >= std::abs (x[i - 1]) &&
                std::abs (x[i]) > std::abs (x[i + 1]))
                ++n;
        return n;
    };
    const auto spring = render (17, none); // Spring: a dense set (16 / 20 taps) against Large Hall's 5 / 6
    CAPTURE (spikes (hall.l), spikes (spring.l));
    CHECK (spikes (spring.l) > 2 * spikes (hall.l));
    CHECK (render (13, none).l != hall.l);
    for (int mode = 14; mode <= 20; ++mode) // every set is its own
    {
        CAPTURE (mode);
        CHECK (render (mode, none).l != hall.l);
    }
    // pass 64: the first tap at the initial delay is a single spike (the REV5 reverb taps); a charted mode's
    // rings (its all-pass, kErDiffPerStepEr). Until pass 64 the Rev taps rang more.
    auto peakShare = [] (const std::vector<float>& x)
    {
        size_t at = 0;
        while (at < x.size () && std::abs (x[at]) < 1.0e-6f)
            ++at;
        const size_t end = std::min (x.size (), at + (size_t) (0.002 * kSr));
        size_t pk = at;
        double e = 0.0;
        for (size_t i = at; i < end; ++i)
        {
            e += (double) x[i] * x[i];
            if (std::abs (x[i]) > std::abs (x[pk]))
                pk = i;
        }
        return (double) x[pk] * x[pk] / std::max (e, 1.0e-30);
    };
    const auto charted = render (1, [] (EngineParams& p) { p.erCount = 1; });
    CAPTURE (peakShare (hall.l), peakShare (charted.l));
    CHECK (peakShare (hall.l) > 0.99);
    CHECK (peakShare (charted.l) < peakShare (hall.l) - 0.01);
}

TEST_CASE ("E/R2 adds a decaying tail beside the reflections (REV5 Reverse in series); the Rev modes ignore it")
{
    // Twenty-fifth pass: E/R2 adds four all-passes (era 7 since pass 46 beside the taps, T60 100 ms on this chart),
    // so a gated program's field is dense and ends in an exponential close instead of stopping at the last tap. The
    // all-passes keep the energy and dry and wet are scaled to unit power, so the level stays about the same.
    auto render = [] (int erType, int set, int mode)
    {
        EngineParams p;
        p.erRevBalance = 100.0f;
        p.rt = 0.3f;
        p.preDelayMs = 20.0f;
        p.erMode = mode;
        p.roomSize = 1.6f;
        p.erCount = 18;
        p.erTableSet = set;
        p.erType = erType;
        return impulse (p, 0.6);
    };
    const auto e1 = render (0, 0, 2), e2 = render (1, 0, 2);
    auto energy = [] (const std::vector<float>& x, double from, double to)
    {
        double e = 0.0;
        for (size_t i = (size_t) (from * kSr); i < std::min (x.size (), (size_t) (to * kSr)); ++i)
            e += (double) x[i] * x[i];
        return e;
    };
    auto busy = [] (const std::vector<float>& x)
    {
        int n = 0;
        for (float v : x)
            n += std::abs (v) > 1.0e-3f ? 1 : 0;
        return n;
    };
    CAPTURE (busy (e1.l), busy (e2.l));
    CHECK (busy (e2.l) > 20 * busy (e1.l)); // dense against 18 bare taps
    const double total1 = energy (e1.l, 0.0, 0.6), total2 = energy (e2.l, 0.0, 0.6);
    CHECK (total2 == Approx (total1).epsilon (0.15));
    // after the last tap (about 210 ms at room size 1.6): bare taps are silent, E/R2 decays, about 25 dB in 50 ms
    const double tail1 = energy (e1.l, 0.25, 0.3), late2a = energy (e2.l, 0.22, 0.24),
                 late2b = energy (e2.l, 0.27, 0.29);
    CHECK (tail1 < 1.0e-12);
    CAPTURE (10.0 * std::log10 (late2a / late2b));
    CHECK (10.0 * std::log10 (late2a / late2b) > 12.0);
    CHECK (10.0 * std::log10 (late2a / late2b) < 40.0);
    CHECK (render (1, 0, 12).l == render (0, 0, 12).l); // the Rev modes: their own reflections
    // Forty-sixth pass: era 7 plays the taps dry beside the tail (the REV7 E/R2 captures), so the first
    // 10 ms after the first tap follow the bare taps (capture 06 against 05: 0.96); the series chain smeared them
    auto corrEarly = [] (const std::vector<float>& a, const std::vector<float>& b)
    {
        size_t on = 0;
        while (on < a.size () && std::abs (a[on]) < 1.0e-4f)
            ++on;
        double ab = 0.0, aa = 0.0, bb = 0.0;
        for (size_t i = on; i < std::min (a.size (), on + (size_t) (0.01 * kSr)); ++i)
        {
            ab += (double) a[i] * b[i];
            aa += (double) a[i] * a[i];
            bb += (double) b[i] * b[i];
        }
        return ab / std::sqrt (std::max (aa * bb, 1.0e-30));
    };
    CAPTURE (corrEarly (e1.l, e2.l));
    CHECK (corrEarly (e1.l, e2.l) > 0.9);
    // Fifty-sixth pass: era 7's Random plays its first three taps dry and only the later ones feed the tail (the
    // REV7 capture of 23 Gate Reverb: silence between taps 1-3), so the gap after the first tap stays clean
    size_t on = 0;
    while (on < e2.l.size () && std::abs (e2.l[on]) < 1.0e-4f)
        ++on;
    const double gap = energy (e2.l, (double) on / kSr + 0.001, (double) on / kSr + 0.006);
    CAPTURE (gap, total2);
    CHECK (gap < 1.0e-6 * total2);
    // the Spring chart's tail starts 30 ms after the taps (pass 59): E/R2 equals the scaled bare taps until then
    const auto s1 = render (0, 0, 5), s2 = render (1, 0, 5);
    CAPTURE (corrEarly (s1.l, s2.l));
    CHECK (corrEarly (s1.l, s2.l) > 0.99);
    // Twenty-eighth pass: era 5 runs the same diffuser, both channels at the same lengths, where bare era-5 taps stop.
    // Forty-seventh pass: beside the taps too (the REV5 E/R2 captures), except Reverse, which stays in series
    // and closes fast (REV5 Reverse Gate falls about 37 dB in 90 ms after its peak)
    const auto f1 = render (0, 1, 2), f2 = render (1, 1, 2);
    auto lastTap = [] (const std::vector<float>& x)
    {
        size_t n = x.size ();
        while (n > 0 && std::abs (x[n - 1]) < 1.0e-6f)
            --n;
        return (double) n / kSr;
    };
    const double end1 = lastTap (f1.l);
    CAPTURE (end1, corrEarly (f1.l, f2.l));
    CHECK (corrEarly (f1.l, f2.l) > 0.9);
    CHECK (energy (f1.l, end1 + 0.005, end1 + 0.025) < 1.0e-12);
    CHECK (energy (f2.l, end1 + 0.005, end1 + 0.025) > 1.0e-7);
    const double total5a = energy (f1.l, 0.0, 0.6), total5b = energy (f2.l, 0.0, 0.6);
    CHECK (total5b == Approx (total5a).epsilon (0.15));
    const auto v1 = render (0, 1, 3), v2 = render (1, 1, 3);
    CAPTURE (corrEarly (v1.l, v2.l));
    CHECK (corrEarly (v1.l, v2.l) < 0.8); // series: the taps are smeared (0.70 since pass 58: T60 80 ms, lengths x1.0)
    CHECK (render (1, 1, 12).l == render (0, 1, 12).l); // era-5 Rev modes too
    // Fifty-third pass: on the REV5 hall charts only the first six taps play dry (REV5 06 and 30), so the
    // later taps' spikes are gone from E/R2, while the energy stays E/R1's (05 and 06 carry the same)
    auto peakIn = [] (const std::vector<float>& x, double from, double to)
    {
        float m = 0.0f;
        for (size_t i = (size_t) (from * kSr); i < std::min (x.size (), (size_t) (to * kSr)); ++i)
            m = std::max (m, std::abs (x[i]));
        return m;
    };
    const double first = 0.02; // pre-delay
    CAPTURE (peakIn (f1.l, first + 0.06, first + 0.2), peakIn (f2.l, first + 0.06, first + 0.2));
    CHECK (peakIn (f2.l, first + 0.06, first + 0.2) < 0.5f * peakIn (f1.l, first + 0.06, first + 0.2));
    CHECK (peakIn (f2.l, 0.0, first + 0.004) > 0.7f * peakIn (f1.l, 0.0, first + 0.004)); // the first taps dry
    // Fifty-seventh pass: the Plate voices' decay follows LIVENESS (hardware Electric Bass B at 3 against Kick at 1,
    // both units: T60 about 280 / 100 ms on the REV7), so at liveness 1 the tail falls much faster than at 3
    for (int set : {0, 1})
    {
        auto plate = [&] (float live)
        {
            EngineParams p;
            p.erRevBalance = 100.0f;
            p.rt = 0.3f;
            p.preDelayMs = 12.0f;
            p.erMode = 4;
            p.roomSize = 0.4f;
            p.erCount = 18;
            p.erTableSet = set;
            p.erType = 1;
            p.liveness = live;
            return impulse (p, 0.4);
        };
        const auto l1 = plate (1.0f), l3 = plate (3.0f);
        const double fall1 = 10.0 * std::log10 (energy (l1.l, 0.05, 0.07) / energy (l1.l, 0.13, 0.15)),
                     fall3 = 10.0 * std::log10 (energy (l3.l, 0.05, 0.07) / energy (l3.l, 0.13, 0.15));
        CAPTURE (set, fall1, fall3);
        CHECK (fall1 > 1.5 * fall3);
        CHECK (fall3 > 10.0); // 80 ms at T60 about 0.25 s
        // Pass 70: the tail recirculates at a period that follows room size (hardware Electric Bass B at 0.4: 13.1 ms;
        // Kick at 0.3: 8.7 ms, both units); R about 1 % longer
        auto period = [&] (const std::vector<float>& x)
        {
            const size_t a = (size_t) (0.06 * kSr), n = (size_t) (0.14 * kSr);
            int best = 0;
            double bestR = -1.0;
            for (int lag = (int) (0.003 * kSr); lag < (int) (0.04 * kSr); ++lag)
            {
                double r = 0.0;
                for (size_t i = a; i < a + n; ++i)
                    r += (double) x[i] * x[i + (size_t) lag];
                if (r > bestR)
                    bestR = r, best = lag;
            }
            return 1000.0 * best / kSr;
        };
        for (auto [size, ms] : {std::pair{0.3f, 8.67}, std::pair{0.4f, 13.1}})
        {
            auto p = [&] (float s)
            {
                EngineParams q;
                q.erRevBalance = 100.0f;
                q.rt = 0.3f;
                q.preDelayMs = 12.0f;
                q.erMode = 4;
                q.roomSize = s;
                q.erCount = 18;
                q.erTableSet = set;
                q.erType = 1;
                q.liveness = 3.0f;
                return impulse (q, 0.4);
            }(size);
            const double pl = period (p.l), pr = period (p.r);
            CAPTURE (set, size, pl, pr);
            CHECK (std::abs (pl - ms) < 0.3);
            CHECK (pr > pl);
            CHECK (pr < pl * 1.03);
        }
    }
}

TEST_CASE ("era-7 Rev modes play their taps from the initial delay, offset by it, not scaled")
{
    // REV7 OM p. 14 (OCR): "3 early reflections are programmed for each channel"; times read from the REV7
    // captures (pass 40): the first at the initial delay on both channels, the rest at fixed offsets from it (Vocal
    // Plate at 45 ms and Strings at 13 ms share them), unscaled by room size, liveness or count; L tap 2 = R + 0.8 ms
    // and L tap 3 = R - 1.05 ms in every set. Spring and Echo Room play four.
    auto render = [] (int mode, auto&& tweak)
    {
        EngineParams p;
        p.erRevBalance = 100.0f;
        p.preDelayMs = 30.0f;
        p.erMode = mode;
        tweak (p);
        return impulse (p, 0.2);
    };
    auto none = [] (EngineParams&) {};
    auto taps = [] (const std::vector<float>& x)
    {
        std::vector<double> ms;
        for (size_t i = 0; i < x.size (); ++i)
            if (std::abs (x[i]) > 1.0e-4f)
                ms.push_back (1000.0 * (double) i / kSr);
        return ms;
    };
    const auto hall = render (12, none);
    const auto l = taps (hall.l), r = taps (hall.r);
    REQUIRE (l.size () == 3);
    REQUIRE (r.size () == 3);
    CHECK (l[0] == Approx (30.0).margin (0.05));
    CHECK (r[0] == Approx (30.0).margin (0.05));
    CHECK (l[1] == Approx (30.0 + 25.80).margin (0.05));
    CHECK (r[1] == Approx (30.0 + 24.97).margin (0.05));
    CHECK (r[2] == Approx (30.0 + 45.01).margin (0.05));
    // the Plate set at two initial delays: the same offsets
    auto plateAt = [&] (float pre) { return taps (render (13, [pre] (EngineParams& p) { p.preDelayMs = pre; }).r); };
    const auto pa = plateAt (13.0f), pb = plateAt (45.0f);
    REQUIRE (pa.size () == 3);
    REQUIRE (pb.size () == 3);
    for (size_t k = 1; k < 3; ++k)
        CHECK (pb[k] - pb[0] == Approx (pa[k] - pa[0]).margin (0.05));
    CHECK (pa[1] - pa[0] == Approx (4.99).margin (0.05));
    CHECK (pa[2] - pa[0] == Approx (17.04).margin (0.05));
    // the programs' own sets (15-18): three taps, four on Spring and Echo Room
    CHECK (taps (render (15, none).l).size () == 3);
    CHECK (taps (render (16, none).l).size () == 3);
    CHECK (taps (render (17, none).l).size () == 4);
    CHECK (taps (render (18, none).r).size () == 4);
    // era 5's Rev taps keep their offsets (the REV5 sets were not seen at another delay)
    auto taps5 = [&] (float pre)
    {
        return taps (render (12,
                             [pre] (EngineParams& p)
                             {
                                 p.erTableSet = 1;
                                 p.preDelayMs = pre;
                             })
                         .l);
    };
    const auto a5 = taps5 (20.0f), b5 = taps5 (40.0f);
    REQUIRE (a5.size () >= 2);
    REQUIRE (b5.size () == a5.size ());
    CHECK (b5[1] - b5[0] == Approx (a5[1] - a5[0]).margin (0.05));
    CHECK (render (12, [] (EngineParams& p) { p.roomSize = 9.0f; }).l == hall.l);
    CHECK (render (12, [] (EngineParams& p) { p.liveness = 0.0f; }).l == hall.l);
    CHECK (render (12, [] (EngineParams& p) { p.erCount = 18; }).l == hall.l);
    CHECK (taps (render (13, none).l).size () == 3);
    CHECK (taps (render (14, none).r).size () == 3);
    CHECK (render (13, none).l != hall.l);
    CHECK (render (14, none).l != hall.l);
}

TEST_CASE ("era-5 reflections are diffused by DIFFUSION at the chart's times; era 7 plays bare taps")
{
    // one reflection, reflections only: at diffusion 0 a single sample; at 5 and 10 the same arrival time and energy
    // (an all-pass), spread over the following samples, more at 10 (sixteenth pass, docs/design/engine.md). The
    // charted modes take the E/R fit since pass 22 (0.024 per step: a light smear); the Rev modes keep the reverb fit
    // (tested below).
    auto render = [] (int set, float diffusion)
    {
        EngineParams p;
        p.erRevBalance = 100.0f;
        p.erCount = 1;
        p.preDelayMs = 20.0f;
        p.erTableSet = set;
        p.diffusion = diffusion;
        return impulse (p, 0.2);
    };
    struct Stats
    {
        size_t peakAt;
        double energy, peakShare;
    };
    auto stats = [] (const std::vector<float>& x)
    {
        size_t at = 0;
        double e = 0.0;
        for (size_t i = 0; i < x.size (); ++i)
        {
            e += (double) x[i] * x[i];
            if (std::abs (x[i]) > std::abs (x[at]))
                at = i;
        }
        return Stats{at, e, (double) x[at] * x[at] / std::max (e, 1.0e-30)};
    };
    const auto bare = stats (render (1, 0.0f).l), mid = stats (render (1, 5.0f).l), full = stats (render (1, 10.0f).l);
    CHECK (bare.peakShare > 0.999);
    CHECK (mid.peakAt == bare.peakAt);
    CHECK (full.peakAt == bare.peakAt);
    CHECK (mid.energy == Approx (bare.energy).epsilon (0.01));
    CHECK (full.energy == Approx (bare.energy).epsilon (0.01));
    CHECK (mid.peakShare < 0.99);
    CHECK (mid.peakShare > 0.9);
    CHECK (full.peakShare < mid.peakShare);
    // era 7: diffusion leaves the reflections alone
    const auto a = render (0, 0.0f), b = render (0, 10.0f);
    double diff = 0.0;
    for (size_t i = 0; i < a.l.size (); ++i)
        diff += std::abs (a.l[i] - b.l[i]) + std::abs (a.r[i] - b.r[i]);
    CHECK (diff < 1.0e-4); // the late network leaks a hair at 100 % (float rounding of the balance)
}

TEST_CASE ("era-5 Pan A and Pan B pan the reflections across the sequence; liveness sets the direction")
{
    // ER only at room size 4.0; the balance of reflection energy between the first and second half of the sequence
    // (split at 200 ms after the pre-delay), per channel.
    auto panPlay = [] (int mode, float liveness, int set = 1)
    {
        EngineParams p;
        p.erRevBalance = 100.0f;
        p.erCount = 34;
        p.roomSize = 4.0f;
        p.preDelayMs = 1.0f;
        p.erMode = mode;
        p.erTableSet = set;
        p.liveness = liveness;
        return impulse (p, 0.9);
    };
    auto earlyShare = [] (const std::vector<float>& x) // early energy / total, in the first 200 ms
    {
        const size_t split = (size_t) (0.2 * kSr);
        double e = 0.0, t = 0.0;
        for (size_t i = 0; i < x.size (); ++i)
        {
            const double v = (double) x[i] * x[i];
            t += v;
            if (i < split)
                e += v;
        }
        return e / std::max (t, 1.0e-30);
    };
    for (const int mode : {6, 7})
    {
        const auto lo = panPlay (mode, 0.0f), hi = panPlay (mode, 10.0f), mid = panPlay (mode, 5.0f);
        CAPTURE (mode);
        // the two liveness ends pan opposite ways: what one channel has early the other has late
        const double dLo = earlyShare (lo.l) - earlyShare (lo.r);
        const double dHi = earlyShare (hi.l) - earlyShare (hi.r);
        const double dMid = earlyShare (mid.l) - earlyShare (mid.r);
        CHECK (dLo * dHi < 0.0);
        CHECK (std::abs (dLo) > 0.15);
        CHECK (std::abs (dHi) > 0.15);
        // balanced near 5: the channel difference is much smaller than at the ends
        CHECK (std::abs (dMid) < 0.35 * std::min (std::abs (dLo), std::abs (dHi)));
    }
    // Pan A differs from Pan B, and from the placeholders era 7 keeps
    auto diff = [] (const Ir& a, const Ir& b)
    {
        double d = 0.0;
        for (size_t i = 0; i < a.l.size (); ++i)
            d += std::abs (a.l[i] - b.l[i]) + std::abs (a.r[i] - b.r[i]);
        return d;
    };
    CHECK (diff (panPlay (6, 0.0f), panPlay (7, 0.0f)) > 0.1);
    CHECK (diff (panPlay (6, 2.0f), panPlay (6, 8.0f)) > 0.1);
    CHECK (diff (panPlay (6, 0.0f, 0), panPlay (6, 10.0f, 0)) > 0.0);   // era 7: liveness still fades the placeholder
    CHECK (diff (panPlay (6, 5.0f, 0), panPlay (2, 5.0f, 0)) < 1.0e-6); // era 7 mode 6 is still Random
}

TEST_CASE ("output does not depend on the block size")
{
    EngineParams p;
    p.rt = 1.5f;
    const auto a = impulse (p, 0.5, 1);
    const auto b = impulse (p, 0.5, 512);
    REQUIRE (a.l.size () == b.l.size ());
    CHECK (std::memcmp (a.l.data (), b.l.data (), a.l.size () * sizeof (float)) == 0);
    CHECK (std::memcmp (a.r.data (), b.r.data (), a.r.size () * sizeof (float)) == 0);
}

TEST_CASE ("the tail length reported to the host covers the decay")
{
    EngineParams p;
    p.rt = 10.0f;
    p.loRatio = 2.0f;
    CHECK (Engine::tailSeconds (p) >= 20.0);
    p.rt = 99.0f;
    CHECK (Engine::tailSeconds (p) >= 99.0 * 2.0);
}

TEST_CASE ("processing cost stays flat in silence after a burst (denormals)")
{
    juce::ScopedNoDenormals noDenormals;
    Engine e;
    e.prepare (kSr, kBlock);
    EngineParams p;
    p.rt = 1.0f;
    std::vector<float> l (kBlock), r (kBlock);

    auto time = [&] (int blocks, bool burst)
    {
        const auto t0 = std::chrono::steady_clock::now ();
        for (int b = 0; b < blocks; ++b)
        {
            for (int i = 0; i < kBlock; ++i)
                l[(size_t) i] = r[(size_t) i] = burst ? (float) std::sin (0.1 * (b * kBlock + i)) : 0.0f;
            e.process (l.data (), r.data (), l.data (), r.data (), kBlock, p);
        }
        return std::chrono::duration<double> (std::chrono::steady_clock::now () - t0).count ();
    };
    const int oneSecond = (int) (kSr / kBlock);
    const double busy = time (oneSecond, true);
    time (oneSecond * 30, false); // let the tail fall far into the denormal range
    const double quiet = time (oneSecond, false);
    CHECK (quiet < busy * 3.0 + 0.01);
}

TEST_CASE ("space modulation keeps the decay law and makes the tail time-variant")
{
    auto p = lateOnly (2.6f, 0.7f, 1.2f);
    p.spaceMod = 10.0f;

    SECTION ("reverb time and ratios hold at full modulation")
    {
        const auto ir = impulse (p, 4.0);
        const double rt1k = bandRt (ir.l, 1000.0f);
        CHECK (rt1k == Approx (2.6).epsilon (0.12));
        CHECK (bandRt (ir.l, 8000.0f) / rt1k == Approx (0.7).epsilon (0.25));
        CHECK (bandRt (ir.l, 125.0f) / rt1k == Approx (1.2).epsilon (0.25));
        CHECK (std::abs (m::correlation (ir.l, ir.r, (size_t) (0.2 * kSr), ir.l.size ())) < 0.2);
    }
    SECTION ("an impulse later in time gives a different tail; with no modulation it is the same")
    {
        auto shifted = [] (const EngineParams& q)
        {
            // impulses at 0 and at 0.37 s into one engine; compare the two tails sample for sample
            juce::ScopedNoDenormals noDenormals;
            Engine e;
            e.prepare (kSr, kBlock);
            const size_t off = (size_t) (0.37 * kSr), len = (size_t) (0.3 * kSr);
            std::vector<float> l (off + len, 0.0f), r (off + len, 0.0f);
            l[0] = r[0] = 1.0f;
            e.process (l.data (), r.data (), l.data (), r.data (), (int) l.size (), q);
            std::vector<float> a (l.begin (), l.begin () + (long) len);
            Engine e2;
            e2.prepare (kSr, kBlock);
            std::vector<float> pre (off, 0.0f), pr (off, 0.0f);
            e2.process (pre.data (), pr.data (), pre.data (), pr.data (), (int) off, q); // advance the LFOs only
            std::vector<float> b (len, 0.0f), br (len, 0.0f);
            b[0] = br[0] = 1.0f;
            e2.process (b.data (), br.data (), b.data (), br.data (), (int) len, q);
            double diff = 0.0, energy = 0.0;
            for (size_t i = (size_t) (0.1 * kSr); i < len; ++i)
            {
                diff += (double) (a[i] - b[i]) * (a[i] - b[i]);
                energy += (double) a[i] * a[i];
            }
            return diff / std::max (energy, 1.0e-30);
        };
        CHECK (shifted (p) > 1.0e-3);
        auto still = p;
        still.spaceMod = 0.0f;
        CHECK (shifted (still) < 1.0e-12);
    }
    SECTION ("99 s at full modulation does not grow")
    {
        auto q = lateOnly (99.0f, 1.0f, 2.4f);
        q.spaceMod = 10.0f;
        q.diffusion = 10.0f;
        q.density = 4.0f;
        const auto ir = impulse (q, 6.0);
        const float early = m::peak (ir.l, (size_t) (1.0 * kSr), (size_t) (2.0 * kSr));
        const float late = m::peak (ir.l, (size_t) (5.0 * kSr), ir.l.size ());
        CHECK (std::isfinite (late));
        CHECK (late <= early * 1.1f);
    }
    SECTION ("block size does not change the modulated output")
    {
        const auto a = impulse (p, 0.5, 1);
        const auto b = impulse (p, 0.5, 512);
        CHECK (std::memcmp (a.l.data (), b.l.data (), a.l.size () * sizeof (float)) == 0);
    }
}

TEST_CASE ("reflection and pre-delay changes crossfade instead of jumping")
{
    // A steady low tone through the reflections only; switch the room size and pre-delay between two blocks and
    // look at the largest sample-to-sample step around the switch against the steady-state step on either side.
    // A hard switch steps by up to the whole signal (about 10 times the steady step here).
    auto run = [] (bool change)
    {
        juce::ScopedNoDenormals noDenormals;
        Engine e;
        e.prepare (kSr, kBlock);
        EngineParams p;
        p.erRevBalance = 100.0f;
        p.erCount = 18;
        p.roomSize = 4.0f;
        p.preDelayMs = 20.0f;
        const int blocks = (int) (0.8 * kSr) / kBlock, switchAt = blocks / 2;
        std::vector<float> out;
        std::vector<float> l (kBlock), r (kBlock);
        for (int b = 0; b < blocks; ++b)
        {
            for (int i = 0; i < kBlock; ++i)
                l[(size_t) i] = r[(size_t) i] =
                    (float) std::sin (2.0 * 3.141592653589793 * 150.0 * (b * kBlock + i) / kSr);
            if (change && b == switchAt)
            {
                p.roomSize = 1.3f;
                p.preDelayMs = 47.0f;
            }
            e.process (l.data (), r.data (), l.data (), r.data (), kBlock, p);
            out.insert (out.end (), l.begin (), l.end ());
        }
        const size_t sw = (size_t) (switchAt * kBlock);
        float steady = 0.0f, around = 0.0f;
        for (size_t i = sw - 4000; i < sw - 10; ++i)
            steady = std::max (steady, std::abs (out[i] - out[i - 1]));
        // the new layout has its own steady level: count the settled stretch after the fade too
        for (size_t i = sw + (size_t) (0.15 * kSr); i < out.size (); ++i)
            steady = std::max (steady, std::abs (out[i] - out[i - 1]));
        for (size_t i = sw; i < sw + (size_t) (0.05 * kSr); ++i)
            around = std::max (around, std::abs (out[i] - out[i - 1]));
        return std::pair{steady, around};
    };
    const auto [steady, around] = run (true);
    REQUIRE (steady > 0.0f);
    CHECK (around < 1.25f * steady);
}

TEST_CASE ("ER feedback repeats the reflection pattern after FB DLY, darker with FB HIGH")
{
    EngineParams p;
    p.erRevBalance = 100.0f; // reflections only
    p.preDelayMs = 10.0f;
    p.roomSize = 1.0f; // pattern about 30 ms long
    p.erFbDelayMs = 200.0f;
    const auto dry = impulse (p, 1.0);
    p.erFbGain = 50.0f;
    p.erFbHi = 1.0f;
    const auto fb = impulse (p, 1.0);
    // the first pass is unchanged; the copy 200 ms later is the same pattern at half the amplitude
    const size_t a = (size_t) (0.005 * kSr), b = (size_t) (0.15 * kSr), shift = (size_t) (0.2 * kSr);
    double e0 = 0.0, e1 = 0.0, diff = 0.0;
    for (size_t i = a; i < b; ++i)
    {
        e0 += (double) fb.l[i] * fb.l[i];
        e1 += (double) fb.l[i + shift] * fb.l[i + shift];
        diff += std::abs (fb.l[i] - dry.l[i]);
    }
    CHECK (diff < 1.0e-6);
    CHECK (e1 / e0 == Approx (0.25).epsilon (0.02));

    // FB HIGH 0.1 keeps the bass of the repeats but loses the treble
    auto highShare = [] (const std::vector<float>& x, size_t from, size_t to)
    {
        double d = 0.0, t = 0.0;
        for (size_t i = from + 1; i < to; ++i)
        {
            d += (double) (x[i] - x[i - 1]) * (x[i] - x[i - 1]);
            t += (double) x[i] * x[i];
        }
        return d / std::max (t, 1.0e-30);
    };
    p.erFbGain = 90.0f;
    p.erFbHi = 0.1f;
    const auto dark = impulse (p, 1.5);
    const size_t late = (size_t) (0.6 * kSr);
    CHECK (highShare (dark.l, late, late + b) < 0.2 * highShare (dark.l, a, b));

    // maximum feedback stays bounded
    p.erFbGain = 99.0f;
    p.erFbHi = 1.0f;
    p.erFbDelayMs = 0.1f;
    const auto hot = impulse (p, 3.0);
    CHECK (std::isfinite (m::peak (hot.l, 0, hot.l.size ())));
    CHECK (m::peak (hot.l, (size_t) (2.0 * kSr), hot.l.size ()) <= m::peak (hot.l, 0, (size_t) (0.5 * kSr)));
}

TEST_CASE ("negative ER feedback gain inverts each repeat and stays stable")
{
    EngineParams p;
    p.erRevBalance = 100.0f;
    p.preDelayMs = 10.0f;
    p.roomSize = 1.0f;
    p.erFbDelayMs = 200.0f;
    p.erFbHi = 1.0f;
    p.erFbGain = 50.0f;
    const auto pos = impulse (p, 1.0);
    p.erFbGain = -50.0f;
    const auto neg = impulse (p, 1.0);
    const size_t a = (size_t) (0.005 * kSr), b = (size_t) (0.15 * kSr), shift = (size_t) (0.2 * kSr);
    double dot = 0.0, e0 = 0.0, e1 = 0.0, same = 0.0;
    for (size_t i = a; i < b; ++i)
    {
        dot += (double) neg.l[i] * neg.l[i + shift];
        e0 += (double) neg.l[i] * neg.l[i];
        e1 += (double) neg.l[i + shift] * neg.l[i + shift];
        same += std::abs (neg.l[i] - pos.l[i]); // the first pass does not depend on the sign
    }
    CHECK (same < 1.0e-6);
    CHECK (dot / std::sqrt (e0 * e1) == Approx (-1.0).epsilon (0.02)); // the repeat is the pattern, flipped
    CHECK (e1 / e0 == Approx (0.25).epsilon (0.02));
    // the second repeat is back in phase
    double dot2 = 0.0;
    for (size_t i = a; i < b; ++i)
        dot2 += (double) neg.l[i] * neg.l[i + 2 * shift];
    CHECK (dot2 > 0.0);

    p.erFbGain = -99.0f;
    p.erFbDelayMs = 0.1f;
    const auto hot = impulse (p, 3.0);
    CHECK (std::isfinite (m::peak (hot.l, 0, hot.l.size ())));
    CHECK (m::peak (hot.l, (size_t) (2.0 * kSr), hot.l.size ()) <= m::peak (hot.l, 0, (size_t) (0.5 * kSr)));
}

TEST_CASE ("the secondary reverb decays at rt x rev2_time, starts after rev2_delay and is off at level 0")
{
    auto p = lateOnly (1.0f);
    const auto one = impulse (p, 4.0);
    p.rev2Level = 100.0f;
    p.rev2Time = 4.0f; // 4 s secondary under a 1 s primary
    p.rev2DelayMs = 100.0f;
    const auto two = impulse (p, 4.0);
    // the first 100 ms (plus the network's own build-up) are the primary alone
    double diff = 0.0;
    for (size_t i = 0; i < (size_t) (0.09 * kSr); ++i)
        diff += std::abs (two.l[i] - one.l[i]);
    CHECK (diff < 1.0e-6);
    // late in the tail the long secondary decay dominates: measure its slope from 1.5 s on
    auto slopeRt = [] (const std::vector<float>& x, double t0, double t1)
    {
        double e0 = 0.0, e1 = 0.0;
        const size_t w = (size_t) (0.2 * kSr);
        for (size_t i = 0; i < w; ++i)
        {
            e0 += (double) x[(size_t) (t0 * kSr) + i] * x[(size_t) (t0 * kSr) + i];
            e1 += (double) x[(size_t) (t1 * kSr) + i] * x[(size_t) (t1 * kSr) + i];
        }
        const double dbPerS = 10.0 * std::log10 (e0 / e1) / (t1 - t0);
        return 60.0 / dbPerS;
    };
    CHECK (slopeRt (two.l, 1.5, 3.5) == Approx (4.0).epsilon (0.2));
    CHECK (Engine::tailSeconds (p) >= 4.0);
}

TEST_CASE ("the REV5 factory secondary setting (x1.0, 0 ms, 100 %) thickens the tail by under 1 dB")
{
    // Thirty-second pass: REV2 LVL 100 % is a quarter of the primary's amplitude; the REV5 reverb captures, whose
    // programs print this setting, fit the engine with the secondary at about that share (C80 3.9 dB late-heavy at a
    // full share). The two networks are uncorrelated, so the late energy rises by about 10 log10 (1 + 0.25^2).
    auto p = lateOnly (2.0f);
    const auto one = impulse (p, 2.0);
    p.rev2Level = 100.0f;
    p.rev2Time = 1.0f;
    p.rev2DelayMs = 0.0f;
    const auto two = impulse (p, 2.0);
    auto energy = [] (const Ir& x)
    {
        double e = 0.0;
        for (size_t i = (size_t) (0.2 * kSr); i < (size_t) (1.2 * kSr); ++i)
            e += (double) x.l[i] * x.l[i] + (double) x.r[i] * x.r[i];
        return e;
    };
    const double rise = 10.0 * std::log10 (energy (two) / energy (one));
    CHECK (rise > 0.1);
    CHECK (rise < 1.0);
}

TEST_CASE ("REV5 1st reflections: centre to both sides, left and right to their own side, up to 1,000 ms")
{
    EngineParams p;
    p.erRevBalance = 100.0f;
    p.erCount = 1;
    p.preDelayMs = 900.0f; // the reflection cluster starts late, out of the way
    p.ref1DelayMs = 100.0f;
    p.ref1Level = 50.0f;
    p.ref1LDelayMs = 250.0f;
    p.ref1LLevel = 80.0f;
    p.ref1RDelayMs = 700.0f;
    p.ref1RLevel = 30.0f;
    const auto ir = impulse (p, 1.2);
    auto at = [] (const std::vector<float>& x, double ms) { return x[(size_t) std::lround (ms * 0.001 * kSr)]; };
    CHECK (at (ir.l, 100.0) == Approx (0.5f).margin (1e-4));
    CHECK (at (ir.r, 100.0) == Approx (0.5f).margin (1e-4));
    CHECK (at (ir.l, 250.0) == Approx (0.8f).margin (1e-4));
    CHECK (at (ir.r, 250.0) == Approx (0.0f).margin (1e-6));
    CHECK (at (ir.r, 700.0) == Approx (0.3f).margin (1e-4));
    CHECK (at (ir.l, 700.0) == Approx (0.0f).margin (1e-6));
    // and nothing else before the 900 ms pre-delay
    float stray = 0.0f;
    for (size_t i = 0; i < (size_t) (0.89 * kSr); ++i)
        if (i != (size_t) (0.1 * kSr) && i != (size_t) (0.25 * kSr) && i != (size_t) (0.7 * kSr))
            stray = std::max (stray, std::max (std::abs (ir.l[i]), std::abs (ir.r[i])));
    CHECK (stray < 1.0e-6f);
    CHECK (m::peak (ir.l, (size_t) (0.9 * kSr), ir.l.size ()) > 0.01f); // the cluster at 900 ms
}

TEST_CASE ("the late tail is mildly in phase in the bass and the low mids, uncorrelated above")
{
    // Late-tail bass coherence (kTailCoherence): the REV7/REV5 captures sit at about +0.1 to +0.25 at 125 Hz,
    // near 0 at 250 Hz and within about +0.1 from 500 Hz up. The split
    // is all-pass complementary (twenty-fourth pass), so it never pushes a band out of phase past the corner (the old
    // low-pass add-back read -0.10 at 500 Hz), and third order at 150 Hz since the twenty-ninth pass (the first-order
    // split at 300 Hz read about +0.25 late at 250 Hz). One impulse response's band correlation scatters by about
    // +/- 0.05, so the bass checks use the mean over both eras and both space-mod settings. Thirty-fourth pass: a band
    // coherence (kBandCoherence) holds 500 Hz-1 kHz mildly in phase too, as the captures (about +0.05 to +0.12).
    double sum125 = 0.0, sum250 = 0.0, sumMid = 0.0;
    int n = 0;
    for (const int era : {0, 1})
        for (const float mod : {0.0f, 2.0f})
        {
            auto p = lateOnly (2.6f);
            p.spaceMod = mod;
            p.erTableSet = era;
            const auto ir = impulse (p, 3.0);
            const auto from = (size_t) (0.1 * kSr), to = ir.l.size ();
            auto bandCorr = [&] (float fc)
            { return m::correlation (m::octaveBand (ir.l, kSr, fc), m::octaveBand (ir.r, kSr, fc), from, to); };
            const double c125 = bandCorr (125.0f), c250 = bandCorr (250.0f), c500 = bandCorr (500.0f),
                         c1k = bandCorr (1000.0f), c2k = bandCorr (2000.0f), c8k = bandCorr (8000.0f);
            CAPTURE (era, mod, c125, c250, c500, c1k, c2k, c8k);
            CHECK (c500 > -0.1);
            CHECK (std::abs (c250) < 0.15);
            CHECK (c125 < 0.5);
            CHECK (std::abs (c2k) < 0.1);
            CHECK (std::abs (c8k) < 0.1);
            sum125 += c125;
            sum250 += c250;
            sumMid += 0.5 * (c500 + c1k);
            ++n;
        }
    CAPTURE (sum125 / n, sum250 / n, sumMid / n);
    CHECK (sum125 / n > 0.08);
    CHECK (sumMid / n > 0.03);
    CHECK (sumMid / n < 0.2);
    CHECK (sum125 / n > sum250 / n + 0.04);
}

TEST_CASE ("the voice tilts move only their own group's treble")
{
    // Thirty-fifth pass: voice_er_tilt shelves the audible reflections (8 kHz), voice_late_tilt the late tail (5 kHz).
    auto render = [] (float erTilt, float lateTilt, float balance)
    {
        EngineParams p;
        p.rt = 1.5f;
        p.erMode = 0;
        p.preDelayMs = 5.0f;
        p.erRevBalance = balance;
        p.erTiltDb = erTilt;
        p.lateTiltDb = lateTilt;
        return impulse (p, 1.0);
    };
    auto band = [] (const std::vector<float>& x, float fc)
    {
        const auto y = m::octaveBand (x, kSr, fc);
        double e = 0.0;
        for (float v : y)
            e += (double) v * v;
        return 10.0 * std::log10 (e + 1e-30);
    };
    // reflections only (balance 100): the reflection tilt lifts 8 kHz, 1 kHz stays; the late tilt
    // does nothing
    const auto er0 = render (0.0f, 0.0f, 100.0f), erUp = render (6.0f, 0.0f, 100.0f),
               erLate = render (0.0f, -6.0f, 100.0f);
    // (the reflection shelf's corner is 8 kHz, so the 8 kHz octave takes about half its gain; 1 kHz none)
    CHECK (band (erUp.l, 8000.0f) - band (er0.l, 8000.0f) == Approx (3.5).margin (1.5));
    CHECK (std::abs (band (erUp.l, 1000.0f) - band (er0.l, 1000.0f)) < 0.3);
    CHECK (std::abs (band (erLate.l, 8000.0f) - band (er0.l, 8000.0f)) < 0.1);
    // late only (balance 0): the late tilt cuts 8 kHz, the reflection tilt does nothing
    const auto l0 = render (0.0f, 0.0f, 0.0f), lDown = render (0.0f, -6.0f, 0.0f), lEr = render (6.0f, 0.0f, 0.0f);
    CHECK (band (lDown.l, 8000.0f) - band (l0.l, 8000.0f) == Approx (-6.0).margin (1.5));
    CHECK (std::abs (band (lDown.l, 500.0f) - band (l0.l, 500.0f)) < 0.5);
    CHECK (std::abs (band (lEr.l, 8000.0f) - band (l0.l, 8000.0f)) < 0.1);
    // at 0 dB both tilts are off: the output is the untilted engine's, sample for sample
    const auto again = render (0.0f, 0.0f, 50.0f), base = render (0.0f, 0.0f, 50.0f);
    CHECK (again.l == base.l);
}

// Hidden: renders engine impulse responses as 24-bit WAVs for offline analysis.
// Run with: YREV57Tests "[.render]" (writes to $YREV_RENDER_DIR, default the current folder).
TEST_CASE ("render engine impulse responses", "[.render]")
{
    const auto dir = juce::File::getCurrentWorkingDirectory ().getChildFile (
        juce::SystemStats::getEnvironmentVariable ("YREV_RENDER_DIR", "."));
    dir.createDirectory ();
    struct Case
    {
        const char* name;
        EngineParams p;
    };
    EngineParams hall, longHall, plate, er;
    hall.rt = 2.6f;
    longHall.rt = 6.0f;
    longHall.loRatio = 1.4f;
    longHall.hiRatio = 0.5f;
    plate.rt = 2.0f;
    plate.erMode = 4;
    plate.hiRatio = 0.9f;
    plate.preDelayMs = 5.0f;
    plate.diffusion = 9.0f;
    plate.density = 4.0f;
    er.erRevBalance = 100.0f;
    er.erCount = 18;
    for (const auto& c : {Case{"engine-hall-2.6s", hall}, Case{"engine-longhall-6s", longHall},
                          Case{"engine-plate-2s", plate}, Case{"engine-er-largehall", er}})
    {
        const auto ir = impulse (c.p, Engine::tailSeconds (c.p) * 0.8 + 0.5);
        juce::AudioBuffer<float> buf (2, (int) ir.l.size ());
        buf.copyFrom (0, 0, ir.l.data (), (int) ir.l.size ());
        buf.copyFrom (1, 0, ir.r.data (), (int) ir.r.size ());
        const auto file = dir.getChildFile (juce::String (c.name) + ".wav");
        file.deleteFile ();
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::OutputStream> os = file.createOutputStream ();
        auto writer = wav.createWriterFor (
            os, juce::AudioFormatWriterOptions{}.withSampleRate (kSr).withNumChannels (2).withBitsPerSample (24));
        REQUIRE (writer != nullptr);
        writer->writeFromAudioSampleBuffer (buf, 0, buf.getNumSamples ());
    }

    // through the converter model as well (era 7 and era 5, character 100), and a CPU figure for the whole wet path
    for (const int variant : {0, 1, 2}) // era 7, era 5, era 7 clean (character 0: no hiss or truncation)
    {
        const int era = variant == 1 ? 1 : 0;
        const float character = variant == 2 ? 0.0f : 100.0f;
        EngineParams p = hall;
        yrev::dsp::Converter conv;
        conv.prepare (kSr, kBlock);
        const auto n = (size_t) (kSr * 4.0);
        std::vector<float> l (n, 0.0f), r (n, 0.0f);
        l[(size_t) (0.05 * kSr)] = r[(size_t) (0.05 * kSr)] = 0.5f;
        const auto t0 = std::chrono::steady_clock::now ();
        for (size_t pos = 0; pos < n; pos += kBlock)
            conv.process (l.data () + pos, r.data () + pos, (int) std::min ((size_t) kBlock, n - pos), p,
                          yrev::dsp::ConverterParams{era, character, true});
        const double secs = std::chrono::duration<double> (std::chrono::steady_clock::now () - t0).count ();
        WARN ("era " << (era == 0 ? 7 : 5) << " wet path: " << 100.0 * secs / 4.0 << " % of one core at 48 kHz");
        juce::AudioBuffer<float> buf (2, (int) n);
        buf.copyFrom (0, 0, l.data (), (int) n);
        buf.copyFrom (1, 0, r.data (), (int) n);
        const auto file = dir.getChildFile (juce::String ("conv-era") + (era == 0 ? "7" : "5") + "-hall" +
                                            (variant == 2 ? "-clean" : "") + ".wav");
        file.deleteFile ();
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::OutputStream> os = file.createOutputStream ();
        auto writer = wav.createWriterFor (
            os, juce::AudioFormatWriterOptions{}.withSampleRate (kSr).withNumChannels (2).withBitsPerSample (24));
        REQUIRE (writer != nullptr);
        writer->writeFromAudioSampleBuffer (buf, 0, buf.getNumSamples ());
    }
}

namespace
{
    // Response to an impulse on the left only, the right only or both (a, b = input L and R gains).
    Ir impulseLR (const EngineParams& p, float a, float b, double seconds)
    {
        juce::ScopedNoDenormals noDenormals;
        Engine e;
        e.prepare (kSr, kBlock);
        const auto n = (size_t) (kSr * seconds);
        Ir ir;
        ir.l.assign (n, 0.0f);
        ir.r.assign (n, 0.0f);
        ir.l[0] = a;
        ir.r[0] = b;
        for (size_t pos = 0; pos < n; pos += (size_t) kBlock)
        {
            const int len = (int) std::min ((size_t) kBlock, n - pos);
            e.process (ir.l.data () + pos, ir.r.data () + pos, ir.l.data () + pos, ir.r.data () + pos, len, p);
        }
        return ir;
    }

    double energyDb (const std::vector<float>& x, size_t from, size_t to)
    {
        double e = 1.0e-30;
        for (size_t i = from; i < to && i < x.size (); ++i)
            e += (double) x[i] * x[i];
        return 10.0 * std::log10 (e);
    }
} // namespace

TEST_CASE ("true stereo: a centred input is bit-identical to the summed input; Stereo ignores the side")
{
    EngineParams p;
    p.rt = 1.5f;
    p.ref1Level = 50.0f;
    p.ref1LLevel = 40.0f;
    p.ref1RLevel = 40.0f;
    auto ts = p;
    ts.trueStereo = 1;

    const auto summed = impulseLR (p, 1.0f, 1.0f, 0.6);
    const auto stereo = impulseLR (ts, 1.0f, 1.0f, 0.6);
    CHECK (std::memcmp (summed.l.data (), stereo.l.data (), summed.l.size () * sizeof (float)) == 0);
    CHECK (std::memcmp (summed.r.data (), stereo.r.data (), summed.r.size () * sizeof (float)) == 0);

    // summed mode: left-only and right-only inputs give the same output (one mono input, as the hardware)
    const auto l = impulseLR (p, 1.0f, 0.0f, 0.6), r = impulseLR (p, 0.0f, 1.0f, 0.6);
    CHECK (std::memcmp (l.l.data (), r.l.data (), l.l.size () * sizeof (float)) == 0);
}

TEST_CASE ("true stereo: the early field leans to the side the input comes from")
{
    EngineParams p;
    p.rt = 1.5f;
    p.erRevBalance = 100.0f; // reflections only
    p.preDelayMs = 5.0f;
    p.trueStereo = 1;
    const auto fromL = impulseLR (p, 1.0f, 0.0f, 0.3), fromR = impulseLR (p, 0.0f, 1.0f, 0.3);
    const size_t a = 0, b = (size_t) (0.25 * kSr);
    const double balL = energyDb (fromL.l, a, b) - energyDb (fromL.r, a, b);
    const double balR = energyDb (fromR.l, a, b) - energyDb (fromR.r, a, b);
    // 0.75 own + 0.25 other side: about 9.5 dB each way on top of the charts' own L/R balance
    CHECK (balL - balR > 15.0);
    CHECK (balL - balR < 22.0);
}

TEST_CASE ("hidden voice: loop and diffuser size change the sound but rt holds; crossover and ER gain act")
{
    const auto base = impulse (lateOnly (2.0f), 3.0);
    for (const float ls : {0.7f, 1.4f})
        for (const float ds : {0.6f, 1.5f})
        {
            auto p = lateOnly (2.0f);
            p.loopScale = ls;
            p.diffuserScale = ds;
            const auto ir = impulse (p, 3.0);
            CAPTURE (ls, ds);
            CHECK (bandRt (ir.l, 1000.0f) == Approx (2.0).epsilon (0.12)); // the decay law follows the new lengths
            CHECK (std::memcmp (ir.l.data (), base.l.data (), ir.l.size () * sizeof (float)) != 0);
        }

    // the low crossover moves the lo_ratio band: with a short bass decay, 250 Hz decays faster with a higher corner
    auto lo = lateOnly (2.0f, 1.0f, 0.4f);
    const double at300 = bandRt (impulse (lo, 3.0).l, 250.0f);
    lo.loXoverHz = 800.0f;
    const double at800 = bandRt (impulse (lo, 3.0).l, 250.0f);
    lo.loXoverHz = 100.0f;
    const double at100 = bandRt (impulse (lo, 3.0).l, 250.0f);
    CHECK (at800 < at300);
    CHECK (at100 > at300);

    // ER gain: +6 dB on the reflections only
    EngineParams er;
    er.erRevBalance = 100.0f;
    const auto e0 = impulse (er, 0.4);
    er.erGainDb = 6.0f;
    const auto e6 = impulse (er, 0.4);
    CHECK (m::peak (e6.l, 0, e6.l.size ()) / m::peak (e0.l, 0, e0.l.size ()) == Approx (1.995).epsilon (0.01));
}

TEST_CASE ("hidden voice: the high crossover keeps the default exact and hi_ratio on the 8 kHz octave")
{
    // the default corner is the engine's old fixed 8.5 kHz; leaving a moved corner and coming back is exact too
    const auto base = impulse (lateOnly (2.0f, 0.5f), 2.5);
    {
        juce::ScopedNoDenormals noDenormals;
        Engine e;
        e.prepare (kSr, kBlock);
        auto p = lateOnly (2.0f, 0.5f);
        p.hiXoverHz = 12000.0f;
        std::vector<float> l (kBlock), r (kBlock);
        e.process (l.data (), r.data (), l.data (), r.data (), kBlock, p);
        e.reset ();
        p.hiXoverHz = 8500.0f;
        Ir ir;
        const auto n = base.l.size ();
        ir.l.assign (n, 0.0f);
        ir.r.assign (n, 0.0f);
        ir.l[0] = ir.r[0] = 1.0f;
        for (size_t pos = 0; pos < n; pos += (size_t) kBlock)
        {
            const int len = (int) std::min ((size_t) kBlock, n - pos);
            e.process (ir.l.data () + pos, ir.r.data () + pos, ir.l.data () + pos, ir.r.data () + pos, len, p);
        }
        CHECK (std::memcmp (ir.l.data (), base.l.data (), n * sizeof (float)) == 0);
        CHECK (std::memcmp (ir.r.data (), base.r.data (), n * sizeof (float)) == 0);
    }

    // at other corners the 8 kHz octave still decays at hi_ratio x the 1 kHz RT (tolerance of the hi_ratio test),
    // and a lower corner damps 4 kHz harder (r4k keeps the last, hi 0.3)
    double r4k[3]{};
    int c = 0;
    for (const float corner : {4500.0f, 8500.0f, 12000.0f})
    {
        for (const float hi : {0.6f, 0.3f})
        {
            auto p = lateOnly (3.0f, hi);
            p.hiXoverHz = corner;
            const auto ir = impulse (p, 4.0);
            const double r1k = 0.5 * (bandRt (ir.l, 1000.0f) + bandRt (ir.r, 1000.0f));
            const double r8k = 0.5 * (bandRt (ir.l, 8000.0f) + bandRt (ir.r, 8000.0f)) / r1k;
            CAPTURE (corner, hi, r1k, r8k);
            // a 4.5 kHz corner at hi 0.3 reaches down into 1 kHz: 2.59 s before the loop mixing (nineteenth pass),
            // 2.50 s with era 7's mixing above 400 Hz; every other case stays within 15 %
            CHECK (r1k == Approx (3.0).epsilon (corner < 5000.0f && hi < 0.4f ? 0.18 : 0.15));
            CHECK (r8k == Approx (hi).epsilon (0.2));
            r4k[c] = 0.5 * (bandRt (ir.l, 4000.0f) + bandRt (ir.r, 4000.0f)) / r1k;
        }
        ++c;
    }
    CHECK (r4k[0] < r4k[1]);
    CHECK (r4k[1] < r4k[2]);

    // at the era-7 native rate a high corner is held where the shelf still reaches the octave
    constexpr double kEra7Fs = 31250.0;
    auto p = lateOnly (3.0f, 0.3f);
    p.hiXoverHz = 12000.0f;
    const auto ir = impulse (p, 4.0, kBlock, kEra7Fs);
    const double r8k = bandRt (ir.l, 8000.0f, kEra7Fs) / bandRt (ir.l, 1000.0f, kEra7Fs);
    CAPTURE (r8k);
    CHECK (r8k == Approx (0.3).epsilon (0.2));
}

TEST_CASE ("sweep the high crossover's hi_ratio accuracy", "[.sweep]")
{
    for (const double fs : {31250.0, 44100.0, 48000.0})
        for (const float corner : {4000.0f, 4500.0f, 6000.0f, 8500.0f, 10000.0f, 12000.0f, 14000.0f})
        {
            double se = 0.0;
            int n = 0;
            std::printf ("fs %.0f corner %.0f:", fs, corner);
            for (const float hi : {0.2f, 0.3f, 0.4f, 0.5f, 0.6f, 0.7f, 0.8f})
            {
                auto p = lateOnly (3.0f, hi);
                p.hiXoverHz = corner;
                const auto ir = impulse (p, 4.0, kBlock, fs);
                const double r1k = 0.5 * (bandRt (ir.l, 1000.0f, fs) + bandRt (ir.r, 1000.0f, fs));
                const double r8k = 0.5 * (bandRt (ir.l, 8000.0f, fs) + bandRt (ir.r, 8000.0f, fs)) / r1k;
                const double e = r8k / hi - 1.0;
                se += e * e;
                ++n;
                std::printf (" %.2f>%.3f", hi, r8k);
            }
            std::printf ("  rms %.1f %%\n", 100.0 * std::sqrt (se / n));
        }
}

TEST_CASE ("the voice's late onset delays the late field only")
{
    // Twenty-sixth pass: voice_late_onset makes the late network hear its input later; the reflections are untouched.
    auto render = [] (float onsetMs, float balance)
    {
        EngineParams p;
        p.erTableSet = 1;
        p.erMode = 13; // Rev Plate
        p.rt = 2.0f;
        p.preDelayMs = 10.0f;
        p.erRevBalance = balance;
        p.lateOnsetMs = onsetMs;
        return impulse (p, 0.5);
    };
    // when cumulative energy first reaches a share of the first 0.5 s
    auto reach = [] (const std::vector<float>& x, double share)
    {
        double total = 0.0, e = 0.0;
        for (float v : x)
            total += (double) v * v;
        for (size_t i = 0; i < x.size (); ++i)
            if ((e += (double) x[i] * x[i]) >= share * total)
                return (double) i / kSr;
        return 0.0;
    };
    const auto late0 = render (0.0f, 0.0f), late20 = render (20.0f, 0.0f);
    CAPTURE (reach (late0.l, 0.01), reach (late20.l, 0.01));
    CHECK (reach (late20.l, 0.01) - reach (late0.l, 0.01) == Approx (0.020).margin (0.003));
    {
        const auto a = render (20.0f, 100.0f), b = render (0.0f, 100.0f);
        double d = 0.0, peak = 0.0;
        size_t at = 0;
        for (size_t i = 0; i < a.l.size (); ++i)
        {
            if (std::abs (a.l[i] - b.l[i]) > d)
            {
                d = std::abs (a.l[i] - b.l[i]);
                at = i;
            }
            peak = std::max (peak, (double) std::abs (b.l[i]));
        }
        CAPTURE (d, peak, at);
        CHECK (d < 1.0e-6 * peak); // reflections only: unchanged (float-level leakage from the muted late path)
    }
    EngineParams p;
    const double t0 = Engine::tailSeconds (p);
    p.lateOnsetMs = 20.0f;
    CHECK (Engine::tailSeconds (p) == Approx (t0 + 0.020));
}

TEST_CASE ("a Rev set beside a chart plays from the late onset as part of the reverb (pass 67)")
{
    auto render = [] (int revTaps, float balance)
    {
        EngineParams p;
        p.erTableSet = 1;
        p.erMode = 0; // S-Hall chart
        p.roomSize = 2.0f;
        p.rt = 2.0f;
        p.preDelayMs = 10.0f;
        p.lateOnsetMs = 20.0f;
        p.erRevBalance = balance;
        p.revTaps = revTaps;
        return impulse (p, 0.3);
    };
    // reverb only: the combinations' first tap at the pre-delay plus the onset is the loudest sample to 40 ms
    const auto a = render (9, 0.0f), b = render (0, 0.0f);
    auto loudest = [] (const std::vector<float>& x)
    {
        size_t at = 0;
        for (size_t i = 0; i < (size_t) (0.040 * kSr); ++i)
            if (std::abs (x[i]) > std::abs (x[at]))
                at = i;
        return std::make_pair ((double) at / kSr, std::abs (x[at]));
    };
    const auto [ta, pa] = loudest (a.l);
    const auto [tb, pb] = loudest (b.l);
    CAPTURE (ta, pa, tb, pb);
    CHECK (ta == Approx (0.030).margin (0.0005));
    CHECK (pa > 3.0f * pb); // without the set the late field there is diffuse
    // reflections only: the set is part of the reverb, so the chart output is unchanged
    const auto c = render (9, 100.0f), d = render (0, 100.0f);
    double diff = 0.0, peak = 0.0;
    for (size_t i = 0; i < c.l.size (); ++i)
    {
        diff = std::max (diff, (double) std::abs (c.l[i] - d.l[i]));
        peak = std::max (peak, (double) std::abs (d.l[i]));
    }
    CHECK (diff < 1.0e-6 * peak);
}

TEST_CASE ("the early fill fills between the reflections and decorrelates the sides (pass 78)")
{
    auto render = [] (float fill)
    {
        EngineParams p;
        p.erMode = 0; // S-Hall chart, era 7
        p.roomSize = 2.0f;
        p.rt = 1.4f;
        p.preDelayMs = 1.0f;
        p.erRevBalance = 100.0f; // reflections only
        p.erFill = fill;
        return impulse (p, 0.1);
    };
    const auto off = render (0.0f), on = render (1.0f), zero = render (0.0f);
    // off is off: two renders at 0 agree to the sample
    for (size_t i = 0; i < off.l.size (); ++i)
        REQUIRE (off.l[i] == zero.l[i]);
    // 1 ms bins over 0-40 ms: with the fill, fewer bins lie 30 dB under the loudest (the gaps between taps)
    auto holes = [] (const std::vector<float>& x)
    {
        const size_t b = (size_t) (0.001 * kSr);
        std::vector<double> e (40, 0.0);
        for (size_t i = 0; i < 40 * b; ++i)
            e[i / b] += (double) x[i] * x[i];
        const double top = *std::max_element (e.begin (), e.end ());
        return std::count_if (e.begin (), e.end (), [top] (double v) { return v < top * 1.0e-3; });
    };
    const auto hOff = holes (off.l), hOn = holes (on.l);
    CAPTURE (hOff, hOn);
    CHECK (hOn < hOff);
    // the fill's L and R chains differ, so the sides correlate less over 0-40 ms
    auto corr = [] (const Ir& r)
    {
        double lr = 0.0, ll = 0.0, rr = 0.0;
        for (size_t i = 0; i < (size_t) (0.040 * kSr); ++i)
        {
            lr += (double) r.l[i] * r.r[i];
            ll += (double) r.l[i] * r.l[i];
            rr += (double) r.r[i] * r.r[i];
        }
        return lr / std::sqrt (ll * rr + 1.0e-30);
    };
    CAPTURE (corr (off), corr (on));
    CHECK (corr (on) < corr (off));
}

TEST_CASE ("era 5's reflection top-octave low-pass trims 18 kHz and leaves 8 kHz")
{
    // Twenty-seventh pass: the REV5 taps' comb on average, a 20 kHz Q 0.5 low-pass on the reflection output.
    auto render = [] (float topHz)
    {
        EngineParams p;
        p.erTableSet = 1;
        p.erMode = 1;
        p.erRevBalance = 100.0f;
        p.erCount = 1;
        p.diffusion = 0.0f;
        p.erTopHz = topHz;
        return impulse (p, 0.2);
    };
    auto db = [] (const std::vector<float>& x, double hz)
    {
        std::complex<double> s{};
        for (size_t n = 0; n < x.size (); ++n)
            s += (double) x[n] * std::polar (1.0, -2.0 * 3.14159265358979323846 * hz * (double) n / kSr);
        return 20.0 * std::log10 (std::abs (s) + 1.0e-30);
    };
    const auto off = render (0.0f), on = render (20000.0f);
    CHECK (db (on.l, 1000.0) - db (off.l, 1000.0) == Approx (0.0).margin (0.05));
    CHECK (db (on.l, 8000.0) - db (off.l, 8000.0) == Approx (-0.1).margin (0.2));
    CHECK (db (on.l, 18000.0) - db (off.l, 18000.0) < -1.0);
}

TEST_CASE ("era-5 Pan A keeps each channel's chart times up to liveness 5 and mirrors them above")
{
    // 2026-11-21 (E/R leftovers): liveness blends the Pan charts' levels only; the times come from the liveness-0
    // charts up to 5 (the REV5 Pan-A captures at liveness 5) and from the liveness-10 charts above.
    auto taps = [] (float live)
    {
        EngineParams p;
        p.erRevBalance = 100.0f;
        p.erTableSet = 1;
        p.erMode = 6;
        p.erCount = 34;
        p.roomSize = 4.0f;
        p.preDelayMs = 1.0f;
        p.diffusion = 0.0f;
        p.liveness = live;
        const auto ir = impulse (p, 0.6);
        std::vector<size_t> at;
        for (size_t i = 1; i + 1 < ir.r.size (); ++i)
            if (std::abs (ir.r[i]) > 1.0e-6f && std::abs (ir.r[i]) >= std::abs (ir.r[i - 1]) &&
                std::abs (ir.r[i]) > std::abs (ir.r[i + 1]))
                at.push_back (i);
        return at;
    };
    const auto t0 = taps (0.0f), t2 = taps (2.0f), t5 = taps (5.0f), t8 = taps (8.0f);
    REQUIRE (t5.size () >= 10);
    CHECK (t0 == t2);
    CHECK (t0 == t5);
    CHECK (t8 != t5);
}

TEST_CASE ("era-5 falling charts play 0.68 of their printed fade at liveness 5; rising charts as drawn")
{
    // Thirty-first pass: the REV5 S-Hall and Random captures play about two-thirds of the charted level fade; only
    // the falling charts (S-Hall, L-Hall, Random) follow them, Reverse / Plate / Spring stay as drawn.
    auto fade = [] (int mode)
    {
        EngineParams p;
        p.erRevBalance = 100.0f;
        p.erTableSet = 1;
        p.erMode = mode;
        p.erCount = 34;
        p.roomSize = 4.0f;
        p.preDelayMs = 10.0f;
        p.diffusion = 0.0f;
        p.liveness = 5.0f;
        const auto ir = impulse (p, 0.8);
        const auto& pat = yrev::dsp::er::kPatterns5[mode];
        auto level = [&] (int k)
        {
            const auto at = (size_t) std::lround ((10.0 + pat.left[k].ms) * 0.001 * kSr);
            float v = 0.0f;
            for (size_t i = at - 3; i <= at + 3; ++i)
                v = std::max (v, std::abs (ir.l[i]));
            return 20.0 * std::log10 (std::max (v, 1.0e-9f));
        };
        const int last = pat.countL - 1;
        return (level (last) - level (0)) / (double) (pat.left[last].db - pat.left[0].db);
    };
    CHECK (fade (0) == Approx (0.68).margin (0.03)); // S-Hall
    CHECK (fade (2) == Approx (0.68).margin (0.03)); // Random
    CHECK (fade (3) == Approx (1.0).margin (0.03));  // Reverse
}

TEST_CASE ("a deep treble cut keeps the 1 kHz decay at rt (thirty-third pass, gap 11)")
{
    // HIGH x0.1-0.2 used to shorten the main decay to 0.2-0.5 x rt: the deep high shelf cut into 1 kHz. Now each
    // loop's broadband gain holds 1 kHz at rt and the cut per pass is capped; the 8 kHz octave still falls well short
    // of 1 kHz (its floor is the diffusers and one loop pass, about 0.15-0.2 s).
    for (const float rt : {0.4f, 0.8f, 1.5f})
        for (const float hi : {0.1f, 0.2f})
        {
            const auto ir = impulse (lateOnly (rt, hi), rt * 1.2 + 0.5);
            CAPTURE (rt, hi);
            const double r1 = 0.5 * (bandRt (ir.l, 1000.0f) + bandRt (ir.r, 1000.0f));
            const double r8 = 0.5 * (bandRt (ir.l, 8000.0f) + bandRt (ir.r, 8000.0f));
            CHECK (r1 == Approx (rt).epsilon (0.12));
            CHECK (r8 < 0.6 * r1);
        }
}

TEST_CASE ("below liveness 5 later reflections fade 6.5 dB per 100 ms per step")
{
    // Measured on the SPX90 model's Early Ref.1 at liveness 0 and 2: each step below 5 takes a further 6.5 dB per 100 ms
    // from the reflections after the first; above 5 ours flattens the chart's fade (the manuals).
    auto drop = [] (float live)
    {
        EngineParams p;
        p.erRevBalance = 100.0f;
        p.erTableSet = 1;
        p.erMode = 0; // S-Hall
        p.erCount = 34;
        p.roomSize = 4.0f;
        p.preDelayMs = 10.0f;
        p.diffusion = 0.0f;
        p.liveness = live;
        const auto ir = impulse (p, 0.8);
        const auto& pat = yrev::dsp::er::kPatterns5[0];
        int k = 1;
        while (k < pat.countL - 1 && pat.left[k].ms - pat.left[0].ms < 40.0f)
            ++k;
        auto level = [&] (int i)
        {
            const auto at = (size_t) std::lround ((10.0 + pat.left[i].ms) * 0.001 * kSr);
            float v = 0.0f;
            for (size_t j = at - 3; j <= at + 3; ++j)
                v = std::max (v, std::abs (ir.l[j]));
            return 20.0 * std::log10 (std::max (v, 1.0e-9f));
        };
        return std::make_pair (level (k) - level (0), (double) (pat.left[k].ms - pat.left[0].ms));
    };
    const auto [d5, dt] = drop (5.0f);
    const auto [d0, dt0] = drop (0.0f);
    const auto [d2, dt2] = drop (2.0f);
    CAPTURE (dt, d5, d0, d2);
    CHECK (d0 - d5 == Approx (-6.5 * 5.0 * dt / 100.0).margin (1.5));
    CHECK (d2 - d5 == Approx (-6.5 * 3.0 * dt / 100.0).margin (1.5));
    CHECK (drop (10.0f).first > d5); // above 5 the fade flattens
}

TEST_CASE ("the secondary reverb's hidden voice: its own HIGH and a gain on its level (REV5 99)")
{
    auto p = lateOnly (2.0f, 0.8f);
    const auto one = impulse (p, 2.5);
    p.rev2Level = 100.0f;
    p.rev2Time = 1.0f;
    p.rev2DelayMs = 0.0f;
    auto secondary = [&] (float hi2, float gainDb)
    {
        auto q = p;
        q.rev2HiRatio = hi2;
        q.rev2GainDb = gainDb;
        const auto two = impulse (q, 2.5);
        std::vector<float> d (two.l.size ());
        for (size_t i = 0; i < d.size (); ++i)
            d[i] = two.l[i] - one.l[i]; // the networks add, so this is the secondary alone
        return d;
    };
    auto energy = [] (const std::vector<float>& x)
    {
        double e = 0.0;
        for (auto v : x)
            e += (double) v * v;
        return e;
    };
    const auto plain = secondary (0.0f, 0.0f);
    const auto loud = secondary (0.0f, 12.0f);
    CHECK (10.0 * std::log10 (energy (loud) / energy (plain)) == Approx (12.0).margin (0.1));
    // 0 follows the primary's HIGH; 0.3 darkens the secondary's treble decay
    const auto same = secondary (0.8f, 0.0f);
    double diff = 0.0;
    for (size_t i = 0; i < same.size (); ++i)
        diff += std::abs (same[i] - plain[i]);
    CHECK (diff < 1.0e-4);
    const auto dark = secondary (0.3f, 0.0f);
    CHECK (bandRt (dark, 8000.0f) < 0.7 * bandRt (plain, 8000.0f));
    CHECK (bandRt (dark, 1000.0f) == Approx (bandRt (plain, 1000.0f)).epsilon (0.1));
}

TEST_CASE ("era-5 Piano plays its measured field, taps and a tail to 250 ms, the right channel louder")
{
    // Pass 52: the REV5 capture of 25 Rehearsal Room (E/R1 PIANO, room size 1.0, liveness 4) is a continuous
    // field from about 16 ms to past 250 ms; Piano used to play the Small Hall chart (pattern 0) in the engine
    auto render = [] (int mode)
    {
        EngineParams p;
        p.erRevBalance = 100.0f;
        p.rt = 0.3f;
        p.preDelayMs = 10.0f;
        p.erMode = mode;
        p.roomSize = 1.0f;
        p.liveness = 4.0f;
        p.erCount = 34;
        p.erTableSet = 1;
        return impulse (p, 0.4);
    };
    auto energy = [] (const std::vector<float>& x, double from, double to)
    {
        double e = 0.0;
        for (size_t i = (size_t) (from * kSr); i < std::min (x.size (), (size_t) (to * kSr)); ++i)
            e += (double) x[i] * x[i];
        return e;
    };
    const auto piano = render (8), hall = render (0);
    CHECK (piano.l != hall.l);
    CHECK (energy (hall.l, 0.15, 0.25) < 1.0e-12); // the size-1.0 Small Hall chart is over by then
    const double early = energy (piano.l, 0.02, 0.1), late = energy (piano.l, 0.15, 0.25);
    CAPTURE (10.0 * std::log10 (early / late));
    CHECK (10.0 * std::log10 (early / late) > 5.0); // decaying, about 13 dB in the capture
    CHECK (10.0 * std::log10 (early / late) < 25.0);
    const double lr = 10.0 * std::log10 (energy (piano.r, 0.0, 0.4) / energy (piano.l, 0.0, 0.4));
    CAPTURE (lr);
    CHECK (lr == Approx (4.4).margin (1.5));
}

TEST_CASE ("era-5 Plate chart mixes its polarity, so a small room's field carries little DC (pass 69)")
{
    // Pass 69: the REV5 captures of 19-21 (PLATE, room size 0.3 / 0.4) hold their field's energy below 150 Hz
    // 9-10 dB under the 300 Hz-8 kHz band; the chart's 34 taps all positive, packed into about 3 ms, held it 2-6 dB
    // over. Signs fitted on capture 19 (per tap low confidence; the net polarity is the measured part).
    for (const auto* side : {&yrev::dsp::er::kPatterns5[4].left, &yrev::dsp::er::kPatterns5[4].right})
    {
        int neg = 0;
        for (const auto& t : *side)
            neg += t.sign < 0.0f ? 1 : 0;
        CHECK (neg > 5);
        CHECK (neg < 29);
    }
    EngineParams p;
    p.erRevBalance = 100.0f;
    p.erTableSet = 1;
    p.erMode = 4; // the Plate chart
    p.erCount = 34;
    p.roomSize = 0.3f;
    p.liveness = 1.0f;
    p.preDelayMs = 12.0f;
    p.diffusion = 5.0f;
    const auto ir = impulse (p, 0.1);
    for (const auto* x : {&ir.l, &ir.r})
    {
        double sum = 0.0, sumAbs = 0.0;
        for (size_t i = (size_t) (0.008 * kSr); i < (size_t) (0.02 * kSr); ++i)
        {
            sum += (*x)[i];
            sumAbs += std::abs ((*x)[i]);
        }
        CAPTURE (sum / sumAbs);
        CHECK (std::abs (sum) < 0.35 * sumAbs); // all positive: about 0.9
    }
}

TEST_CASE ("REV7 Random and REV5 Large Hall charts carry the captures' tap signs (pass 72)")
{
    // Pass 72: REV7 Random from hardware capture 27 (L = R on all 18 taps, capture 23 agrees 12 of 13 per channel);
    // REV5 Large Hall taps 1-6 from capture 30 (the rest lie under the E/R2 field and stay +1)
    const char* random7 = "+++-++-----+++--++";
    const auto& rnd = yrev::dsp::er::kPatterns[2];
    REQUIRE (rnd.countL == 18);
    REQUIRE (rnd.countR == 18);
    for (const auto* side : {&rnd.left, &rnd.right})
    {
        for (size_t i = 0; i < 18; ++i)
            CHECK (((*side)[i].sign < 0.0f) == (random7[i] == '-'));
    }
    const char* large5 = "+++-+-";
    for (const auto* side : {&yrev::dsp::er::kPatterns5[1].left, &yrev::dsp::er::kPatterns5[1].right})
        for (size_t i = 0; i < 6; ++i)
            CHECK (((*side)[i].sign < 0.0f) == (large5[i] == '-'));
}
