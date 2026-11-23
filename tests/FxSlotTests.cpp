#include "dsp/FxSlot.h"
#include "dsp/LoopHighCut.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <vector>

using namespace yrev::dsp;

namespace
{
    constexpr double kSr = 48000.0;
    constexpr double kPi = 3.14159265358979323846;

    struct Stereo
    {
        std::vector<float> l, r;
        explicit Stereo (size_t n = 0) : l (n, 0.0f), r (n, 0.0f) {}
    };

    // Runs the whole signal through a fresh slot in blocks of `block`.
    Stereo run (const FxParams& p, Stereo in, int block = 512, double sr = kSr)
    {
        FxSlot slot;
        slot.prepare (sr, block);
        const int n = (int) in.l.size ();
        for (int pos = 0; pos < n; pos += block)
        {
            const int c = std::min (block, n - pos);
            slot.process (in.l.data () + pos, in.r.data () + pos, c, p);
        }
        return in;
    }

    Stereo impulse (size_t n, size_t at = 0)
    {
        Stereo s (n);
        s.l[at] = 1.0f;
        s.r[at] = 1.0f;
        return s;
    }

    Stereo sine (size_t n, double freq, float gain = 0.5f)
    {
        Stereo s (n);
        for (size_t i = 0; i < n; ++i)
            s.l[i] = s.r[i] = gain * (float) std::sin (2.0 * kPi * freq * (double) i / kSr);
        return s;
    }

    double energy (const std::vector<float>& x, size_t a, size_t b)
    {
        double e = 0.0;
        for (size_t i = a; i < b && i < x.size (); ++i)
            e += (double) x[i] * x[i];
        return e;
    }

    double goertzel (const std::vector<float>& x, size_t a, size_t b, double freq)
    {
        double re = 0.0, im = 0.0;
        for (size_t i = a; i < b; ++i)
        {
            const double w = 2.0 * kPi * freq * (double) i / kSr;
            re += x[i] * std::cos (w);
            im += x[i] * std::sin (w);
        }
        return (re * re + im * im) / (double) ((b - a) * (b - a));
    }

    // Mean Goertzel power over short frames: a pitch shifter's splices step the output phase every window, so a long
    // single-bin transform only sees the tone coherently when the window happens to hold whole cycles.
    double goertzelFrames (const std::vector<float>& x, size_t a, size_t b, double freq, size_t frame = 1024)
    {
        double sum = 0.0;
        size_t n = 0;
        for (size_t s = a; s + frame <= b; s += frame, ++n)
            sum += goertzel (x, s, s + frame, freq);
        return n > 0 ? sum / (double) n : 0.0;
    }

    // Power in a band around a shifted tone: the 10 ms shifter window splices |ratio - 1| / 10 ms times a second, which
    // spreads the shifted line into sidebands that far either side (an octave up: 100 Hz), as on the measured model.
    double bandFrames (const std::vector<float>& x, size_t a, size_t b, double lo, double hi)
    {
        double sum = 0.0;
        for (double f = lo; f <= hi; f += 10.0)
            sum += goertzelFrames (x, a, b, f);
        return sum;
    }

    double peak (const std::vector<float>& x, size_t a = 0)
    {
        double m = 0.0;
        for (size_t i = a; i < x.size (); ++i)
            m = std::max (m, (double) std::fabs (x[i]));
        return m;
    }

    FxParams delayParams ()
    {
        FxParams p;
        p.type = fxtype::delay;
        p.dlyLMs = 10.0f;
        p.dlyRMs = 20.0f;
        p.dlyLevel = 100.0f;
        p.dlyFb = 0.0f;
        return p;
    }

    FxParams modParams (int type)
    {
        FxParams p;
        p.type = fxtype::mod;
        p.modType = type;
        return p;
    }
} // namespace

TEST_CASE ("FX slot Off passes audio through bit-exact")
{
    FxParams p;
    p.type = fxtype::off;
    p.lpfKHz = 2.0f;
    auto in = sine (4096, 1000.0);
    auto out = run (p, in);
    CHECK (out.l == in.l);
    CHECK (out.r == in.r);
}

TEST_CASE ("FX slot delay puts an impulse at the right sample on each channel")
{
    const auto out = run (delayParams (), impulse (4096));
    CHECK (out.l[0] == 1.0f);
    CHECK (out.l[480] == Catch::Approx (1.0f).margin (1e-5));
    CHECK (out.r[960] == Catch::Approx (1.0f).margin (1e-5));
    CHECK (out.l[479] == Catch::Approx (0.0f).margin (1e-5));
    CHECK (out.r[480] == Catch::Approx (0.0f).margin (1e-5));
    CHECK (out.l[1000] == Catch::Approx (0.0f).margin (1e-5));
}

TEST_CASE ("FX slot delay link makes R follow L and era limits clamp the time")
{
    auto p = delayParams ();
    p.link = true;
    auto out = run (p, impulse (4096));
    CHECK (out.r[480] == Catch::Approx (1.0f).margin (1e-5));

    p = delayParams ();
    p.dlyLMs = 5000.0f; // clamps to 900 ms in era 7
    p.era = 0;
    auto in = impulse (50000);
    out = run (p, in);
    CHECK (out.l[43200] == Catch::Approx (1.0f).margin (1e-5));
    p.era = 1;
    CHECK (FxSlot::tailSeconds (p) == Catch::Approx (2.9).margin (1e-6)); // REV5: 2,900 ms
}

TEST_CASE ("FX slot delay feedback decays and never grows at maximum feedback")
{
    auto p = delayParams ();
    p.era = 1; // the REV7 delay has no feedback
    p.dlyFb = p.dlyFbR = 99.0f;
    p.dlyRMs = 10.0f;
    auto in = impulse (48000 * 3);
    auto out = run (p, in);
    CHECK (peak (out.l, 1) <= 1.0 + 1e-4);
    CHECK (energy (out.l, 48000 * 2, 48000 * 3) < energy (out.l, 0, 48000));

    p.dlyFb = p.dlyFbR = -99.0f;
    out = run (p, in);
    CHECK (peak (out.l, 1) <= 1.0 + 1e-4);

    p.dlyFb = 50.0f;
    p.dlyFbR = 25.0f; // separate Rch feedback (REV5)
    out = run (p, in);
    CHECK (out.l[960] == Catch::Approx (0.5f).margin (1e-4));
    CHECK (out.r[960] == Catch::Approx (0.25f).margin (1e-4)); // R at 10 ms here

    p.era = 0; // REV7: one delay level, no feedback
    out = run (p, in);
    CHECK (out.l[480] == Catch::Approx (1.0f).margin (1e-4));
    CHECK (out.l[960] == Catch::Approx (0.0f).margin (1e-6));
}

TEST_CASE ("FX slot delay HIGH lowers the treble feedback only")
{
    // a long run of repeats: HIGH 0.1 keeps the bass repeating but loses the treble
    auto ratio = [] (float high)
    {
        auto p = delayParams ();
        p.era = 1;
        p.dlyLMs = p.dlyRMs = 20.0f;
        p.dlyFb = p.dlyFbR = 90.0f;
        p.dlyHigh = high;
        auto out = run (p, impulse (48000));
        const double lo = goertzel (out.l, 9600, 48000, 200.0), hi = goertzel (out.l, 9600, 48000, 8000.0);
        return hi / std::max (lo, 1e-30);
    };
    CHECK (ratio (0.1f) < 0.01 * ratio (1.0f));
}

TEST_CASE ("FX slot HIGH is a one-pole low-pass in the loop whose 5 kHz gain is HIGH")
{
    // the coefficient solve, at both native rates
    for (double fs : {31250.0, 44100.0})
        for (double h : {0.1, 0.3, 0.55, 0.9})
        {
            const double a = yrev::dsp::loopHighCoef (h, fs), w = 2.0 * kPi * 5000.0 / fs;
            const double mag = a / std::sqrt (1.0 - 2.0 * (1.0 - a) * std::cos (w) + (1.0 - a) * (1.0 - a));
            CHECK (mag == Catch::Approx (h).epsilon (1e-6));
        }
    CHECK (yrev::dsp::loopHighCoef (1.0, 31250.0) == 1.0);

    // in the slot: the second repeat (filtered once) against the first (unfiltered), feedback 50 %
    auto p = delayParams ();
    p.era = 1;
    p.dlyLMs = p.dlyRMs = 20.0f;
    p.dlyFb = p.dlyFbR = 50.0f;
    p.dlyHigh = 0.55f;
    const auto out = run (p, impulse (8192));
    const auto gain = [&] (double f)
    { return std::sqrt (goertzel (out.l, 1920, 2880, f) / goertzel (out.l, 960, 1920, f)); };
    CHECK (gain (5000.0) == Catch::Approx (0.5 * 0.55).epsilon (0.05));
    CHECK (gain (200.0) == Catch::Approx (0.5).epsilon (0.05));
    CHECK (gain (10000.0) < 0.5 * 0.55); // a low-pass keeps falling, where a shelf would level off
}

TEST_CASE ("FX slot echo damping darkens the repeats")
{
    // A 1 kHz-to-20 kHz measure: the ratio of first-difference energy to signal energy in the later repeats.
    auto brightness = [] (float damp)
    {
        FxParams p;
        p.type = fxtype::echo;
        p.echoLMs = p.echoRMs = 50.0f;
        p.echoFb = 80.0f;
        p.echoLevel = 100.0f;
        p.echoDamp = damp;
        auto out = run (p, impulse (48000));
        // Repeats from 0.3 s onwards.
        const size_t a = 14400;
        double diff = 0.0, tot = 0.0;
        for (size_t i = a + 1; i < out.l.size (); ++i)
        {
            const double d = out.l[i] - out.l[i - 1];
            diff += d * d;
            tot += (double) out.l[i] * out.l[i];
        }
        return diff / std::max (tot, 1e-30);
    };
    CHECK (brightness (0.2f) < 0.5 * brightness (1.0f));
}

TEST_CASE ("FX slot echo feeds each channel back to itself")
{
    FxParams p;
    p.type = fxtype::echo;
    p.echoLMs = p.echoRMs = 50.0f;
    p.echoFb = 60.0f;
    p.echoLevel = 100.0f;
    Stereo in (24000);
    in.l[0] = 1.0f;
    auto out = run (p, in);
    CHECK (out.l[2400] == Catch::Approx (1.0f).margin (1e-4));
    CHECK (out.l[4800] == Catch::Approx (0.6f).margin (1e-3));
    CHECK (peak (out.r) < 1e-6); // nothing crosses over (REV5 p. 26, SPX90)

    // REV5: separate Rch feedback, signed; the REV7 shares one gain
    p.era = 1;
    p.echoFbR = -50.0f;
    in.r[0] = 1.0f;
    out = run (p, in);
    CHECK (out.r[4800] == Catch::Approx (-0.5f).margin (1e-3));
    p.era = 0;
    out = run (p, in);
    CHECK (out.r[4800] == Catch::Approx (0.6f).margin (1e-3));
}

TEST_CASE ("FX slot tempo sync snaps delay times to note values")
{
    auto p = delayParams ();
    p.sync = true;
    p.hostBpm = 120.0; // a quarter note is 500 ms
    p.dlyLMs = 480.0f;
    p.dlyRMs = 130.0f; // nearest is a dotted 1/16... 125 ms (1/16 = 125)
    const auto out = run (p, impulse (48000));
    CHECK (out.l[24000] == Catch::Approx (1.0f).margin (1e-4));
    CHECK (out.r[6000] == Catch::Approx (1.0f).margin (1e-4));
}

TEST_CASE ("FX slot chorus varies over time and stays bounded")
{
    auto p = modParams (modtype::chorusA);
    p.lfoRateHz = 2.0f;
    p.dmDepth = 100.0f;
    p.amDepth = 40.0f;
    const auto in = sine (48000 * 2, 1000.0);
    const auto out = run (p, in);
    CHECK (peak (out.l) <= 1.0);
    CHECK (peak (out.r) <= 1.0);
    // The envelope of the output (RMS per 50 ms) moves around as the combs sweep.
    double lo = 1e9, hi = 0.0;
    for (size_t a = 9600; a + 2400 < out.l.size (); a += 2400)
    {
        const double e = energy (out.l, a, a + 2400);
        lo = std::min (lo, e);
        hi = std::max (hi, e);
    }
    CHECK (hi > 1.1 * lo);
}

TEST_CASE ("FX slot three voice chorus decorrelates L and R")
{
    auto p = modParams (modtype::chorusA);
    p.lfoRateHz = 1.0f;
    p.dmDepth = 100.0f;
    p.amDepth = 50.0f;
    Stereo in (48000 * 2);
    unsigned seed = 12345;
    for (size_t i = 0; i < in.l.size (); ++i)
    {
        seed = seed * 1664525u + 1013904223u;
        in.l[i] = in.r[i] = (float) ((seed >> 8) & 0xffff) / 32768.0f - 1.0f;
    }
    const auto out = run (p, in);
    double ll = 0, rr = 0, lr = 0;
    for (size_t i = 9600; i < out.l.size (); ++i)
    {
        ll += (double) out.l[i] * out.l[i];
        rr += (double) out.r[i] * out.r[i];
        lr += (double) out.l[i] * out.r[i];
    }
    const double corr = lr / std::sqrt (ll * rr);
    CHECK (corr < 0.95);
    CHECK (corr > -0.5);
}

TEST_CASE ("FX slot symphonic and phasing run and stay bounded")
{
    const auto in = sine (48000, 700.0);
    for (int t : {modtype::symphonic, modtype::chorusB, modtype::phasing})
    {
        auto p = modParams (t);
        p.modDelayMs = 3.0f;
        p.modDepth = 100.0f;
        p.lfoRateHz = 1.0f;
        const auto out = run (p, in);
        CHECK (peak (out.l) <= 1.0);
        CHECK (peak (out.r) <= 1.0);
        CHECK (energy (out.l, 24000, 48000) > 0.0);
    }
}

TEST_CASE ("FX slot flange at maximum feedback is stable")
{
    auto p = modParams (modtype::flange);
    p.modFb = 99.0f;
    p.modDelayMs = 1.2f;
    p.modDepth = 100.0f;
    p.lfoRateHz = 5.0f;
    auto in = sine (48000 * 4, 440.0);
    auto out = run (p, in);
    CHECK (std::isfinite (peak (out.l)));
    CHECK (peak (out.l) < 200.0);
    // Silence after the signal lets it decay.
    Stereo burst (48000 * 12);
    std::copy (in.l.begin (), in.l.begin () + 4800, burst.l.begin ());
    std::copy (in.l.begin (), in.l.begin () + 4800, burst.r.begin ());
    out = run (p, burst);
    CHECK (energy (out.l, 48000 * 10, 48000 * 12) < energy (out.l, 0, 48000 * 2));
}

TEST_CASE ("FX slot flange lengthens L while R shortens")
{
    auto p = modParams (modtype::flange);
    p.modFb = 0.0f;
    p.modDelayMs = 5.0f;
    p.modDepth = 50.0f; // +-2 ms
    p.lfoRateHz = 0.5f;
    // An impulse 0.125 cycles into the LFO (sine positive): L is late and R is early.
    const auto out = run (p, impulse (48000 * 2, 12000 - 240));
    // Find the delayed pulse positions after the input impulse.
    auto pulseAt = [] (const std::vector<float>& x, size_t from)
    {
        size_t best = from;
        for (size_t i = from; i < from + 1000; ++i)
            if (x[i] > x[best])
                best = i;
        return best;
    };
    const size_t dl = pulseAt (out.l, 12000 - 240 + 20) - (12000 - 240);
    const size_t dr = pulseAt (out.r, 12000 - 240 + 20) - (12000 - 240);
    CHECK (dl > dr + 100);
}

TEST_CASE ("FX slot reverb flange is the flange at a fixed 30 % feedback, with no decay comb of its own")
{
    // its REV TIME is the program's reverb (the engine): mod_rt changes nothing here, and nothing rings on
    auto p = modParams (modtype::revFlange);
    p.modRtS = 0.5f;
    Stereo burst (48000 * 2);
    for (size_t i = 0; i < 2400; ++i)
        burst.l[i] = burst.r[i] = (i % 40 < 20) ? 0.5f : -0.5f;
    const auto a = run (p, burst);
    p.modRtS = 10.0f;
    const auto b = run (p, burst);
    CHECK (energy (a.l, 0, a.l.size ()) == Catch::Approx (energy (b.l, 0, b.l.size ())));
    CHECK (energy (a.l, 12000, 48000 * 2) < 1e-6 * energy (a.l, 0, 2400)); // silent 0.2 s after the burst ends

    auto f = modParams (modtype::flange); // the same as a flange at 30 %
    f.modFb = 30.0f;
    f.modDelayMs = p.modDelayMs;
    const auto c = run (f, burst);
    CHECK (energy (c.l, 0, c.l.size ()) == Catch::Approx (energy (a.l, 0, a.l.size ())).epsilon (0.02));
}

TEST_CASE ("FX slot tremolo modulates amplitude at the set rate")
{
    auto p = modParams (modtype::tremolo);
    p.modDepth = 100.0f;
    p.lfoRateHz = 4.0f;
    const auto out = run (p, sine (48000 * 2, 1000.0));
    // Envelope: peak per 5 ms; its dominant period is 1/4 s, so the envelope correlates with itself 12000 samples
    // later.
    std::vector<double> env;
    for (size_t a = 0; a + 240 <= out.l.size (); a += 240)
        env.push_back (peak (std::vector<float> (out.l.begin () + (long) a, out.l.begin () + (long) a + 240)));
    const double mx = *std::max_element (env.begin (), env.end ());
    const double mn = *std::min_element (env.begin (), env.end ());
    CHECK (mn < 0.3 * mx);
    // Count envelope minima: 4 Hz over 2 s is about 8 cycles.
    int minima = 0;
    for (size_t i = 1; i + 1 < env.size (); ++i)
        if (env[i] < env[i - 1] && env[i] <= env[i + 1] && env[i] < 0.5 * mx)
            ++minima;
    CHECK (minima >= 7);
    CHECK (minima <= 9);
}

TEST_CASE ("FX slot pan moves energy from left to right")
{
    FxParams p;
    p.type = fxtype::pan;
    p.panDir = pandir::leftToRight;
    p.panRateHz = 1.0f;
    p.panDepth = 100.0f;
    const auto out = run (p, sine (48000, 500.0));
    // First quarter second is left-heavy, the last quarter right-heavy.
    CHECK (energy (out.l, 1000, 12000) > 2.0 * energy (out.r, 1000, 12000));
    CHECK (energy (out.r, 36000, 47000) > 2.0 * energy (out.l, 36000, 47000));

    p.panDir = pandir::rightToLeft;
    const auto rev = run (p, sine (48000, 500.0));
    CHECK (energy (rev.r, 1000, 12000) > 2.0 * energy (rev.l, 1000, 12000));

    // L <-> R (REV5 OM p. 36): left to right and back once per cycle, so left at both ends, right in the middle
    p.panDir = pandir::backAndForth;
    const auto bf = run (p, sine (48000, 500.0));
    CHECK (energy (bf.l, 500, 4000) > 2.0 * energy (bf.r, 500, 4000));
    CHECK (energy (bf.r, 22000, 26000) > 2.0 * energy (bf.l, 22000, 26000));
    CHECK (energy (bf.l, 44000, 47500) > 2.0 * energy (bf.r, 44000, 47500));
    CHECK (peak (bf.l) <= 1.0);
}

TEST_CASE ("FX slot pan uses linear gains: unity hard to one side, 0.5 each at the centre")
{
    FxParams p;
    p.type = fxtype::pan;
    p.panDir = pandir::leftToRight;
    p.panRateHz = 0.1f;
    p.panDepth = 0.0f; // held at the centre
    const auto in = sine (48000, 500.0);
    const auto centre = run (p, in);
    CHECK (energy (centre.l, 4800, 48000) == Catch::Approx (0.25 * energy (in.l, 4800, 48000)).epsilon (0.01));
    CHECK (energy (centre.r, 4800, 48000) == Catch::Approx (0.25 * energy (in.l, 4800, 48000)).epsilon (0.01));

    p.panDepth = 100.0f; // L>R starts hard left
    const auto left = run (p, in);
    CHECK (energy (left.l, 480, 1440) == Catch::Approx (energy (in.l, 480, 1440)).epsilon (0.02));
    CHECK (energy (left.r, 480, 1440) < 1e-3 * energy (in.l, 480, 1440));
}

TEST_CASE ("FX slot pitch shift of 440 Hz by +12 semitones lands at 880 Hz")
{
    FxParams p;
    p.type = fxtype::pitch;
    p.era = 1;
    p.pitchShiftSt = 12.0f;
    p.pitchDelayMs = 0.1f;
    const auto out = run (p, sine (48000, 440.0));
    const double e440 = bandFrames (out.l, 12000, 48000, 400.0, 480.0);
    const double e880 = bandFrames (out.l, 12000, 48000, 760.0, 1000.0);
    CHECK (e880 > 20.0 * e440);

    p.pitchShiftSt = -12.0f;
    const auto down = run (p, sine (48000, 880.0));
    CHECK (goertzelFrames (down.l, 12000, 48000, 440.0) > 20.0 * goertzelFrames (down.l, 12000, 48000, 880.0));

    p.pitchShiftSt = 0.0f;
    p.pitchFineCents = 0.0f;
    const auto same = run (p, sine (48000, 440.0));
    CHECK (goertzelFrames (same.l, 12000, 48000, 440.0) > 0.01);
}

TEST_CASE ("FX slot pitch B gives two centre voices and pitch C one voice per side")
{
    FxParams p;
    p.type = fxtype::pitch;
    p.era = 1;
    p.pitchShiftSt = 12.0f; // 880 Hz from a 440 Hz input
    p.pitch2ShiftSt = 7.0f; // about 659 Hz
    p.pitchDelayMs = p.pitch2DelayMs = 0.1f;
    const double f2 = 440.0 * std::pow (2.0, 7.0 / 12.0);

    p.pitchSel = 1; // B
    auto out = run (p, sine (48000, 440.0));
    CHECK (bandFrames (out.l, 12000, 48000, 760.0, 1000.0) > 0.002);
    CHECK (bandFrames (out.l, 12000, 48000, f2 - 60.0, f2 + 60.0) > 0.002);
    CHECK (energy (out.l, 0, 48000) == Catch::Approx (energy (out.r, 0, 48000)));

    p.pitchSel = 2; // C
    out = run (p, sine (48000, 440.0));
    CHECK (bandFrames (out.l, 12000, 48000, 760.0, 1000.0) >
           20.0 * bandFrames (out.l, 12000, 48000, f2 - 60.0, f2 + 60.0));
    CHECK (bandFrames (out.r, 12000, 48000, f2 - 60.0, f2 + 60.0) >
           20.0 * bandFrames (out.r, 12000, 48000, 760.0, 1000.0));

    p.pitchDelayMs = 400.0f; // C clamps its delays to 200 ms (REV5 p. 35)
    p.pitchSel = 2;
    out = run (p, impulse (48000));
    size_t first = 0;
    for (size_t i = 0; i < out.l.size () && first == 0; ++i)
        if (std::fabs (out.l[i]) > 1e-3f)
            first = i;
    CHECK (first < (size_t) (0.26 * kSr));
}

TEST_CASE ("FX slot type changes fade out, mute briefly and fade in")
{
    // a steady tone through a delay, then switch to chorus: no step larger than the steady ones
    FxSlot slot;
    slot.prepare (kSr, 256);
    auto p = delayParams ();
    std::vector<float> l (256), r (256), out;
    for (int b = 0; b < 200; ++b)
    {
        if (b == 100)
            p = modParams (modtype::chorusA);
        for (int i = 0; i < 256; ++i)
            l[(size_t) i] = r[(size_t) i] = 0.5f * (float) std::sin (2.0 * kPi * 200.0 * (b * 256 + i) / kSr);
        slot.process (l.data (), r.data (), 256, p);
        out.insert (out.end (), l.begin (), l.end ());
    }
    float steady = 0.0f, around = 0.0f;
    for (size_t i = 20000; i < 25000; ++i)
        steady = std::max (steady, std::fabs (out[i] - out[i - 1]));
    for (size_t i = 25600; i < 25600 + 2000; ++i)
        around = std::max (around, std::fabs (out[i] - out[i - 1]));
    CHECK (around < 1.2f * steady);
    // and it is briefly silent in between
    float quiet = 1.0f;
    for (size_t i = 25600; i < 25600 + 1000; ++i)
        quiet = std::min (quiet, std::fabs (out[i]) + std::fabs (out[i + 1]));
    CHECK (quiet < 0.02f);
}

TEST_CASE ("FX slot pitch feedback stays bounded")
{
    FxParams p;
    p.type = fxtype::pitch;
    p.pitchShiftSt = 7.0f;
    p.pitchFb = 99.0f;
    p.pitchDelayMs = 20.0f;
    const auto out = run (p, sine (48000 * 5, 330.0));
    CHECK (peak (out.l) < 60.0);
}

TEST_CASE ("FX slot output low-pass darkens and thru leaves the signal")
{
    FxParams p = delayParams ();
    p.dlyLevel = 0.0f;
    p.lpfKHz = 17.0f;
    const auto in = sine (48000, 8000.0);
    const auto thru = run (p, in);
    CHECK (thru.l == in.l);
    p.lpfKHz = 1.0f;
    const auto dark = run (p, in);
    CHECK (energy (dark.l, 24000, 48000) < 0.05 * energy (in.l, 24000, 48000));
}

TEST_CASE ("FX slot low-pass is first order, as the reverb's LPF")
{
    // The REV5 FX programs share the reverb's LPF FRQ control; ours is a one-pole like wet_lpf (2026-11-20): about -3
    // dB at the cut-off and about -6 dB per octave above it (a second-order filter would be about -12).
    FxParams p = delayParams ();
    p.dlyLevel = 0.0f;
    p.lpfKHz = 2.0f;
    auto gainAt = [&] (double hz)
    {
        const auto in = sine (48000, hz);
        const auto out = run (p, in);
        return 10.0 * std::log10 (energy (out.l, 24000, 48000) / energy (in.l, 24000, 48000));
    };
    const double g2 = gainAt (2000.0), g4 = gainAt (4000.0), g8 = gainAt (8000.0);
    CAPTURE (g2, g4, g8);
    CHECK (g2 == Catch::Approx (-3.0).margin (0.5));
    CHECK (g4 - g8 == Catch::Approx (5.5).margin (1.5));
}

TEST_CASE ("FX slot output does not depend on the block size")
{
    const auto in = sine (9000, 330.0);
    for (int type : {fxtype::delay, fxtype::echo, fxtype::mod, fxtype::pitch, fxtype::pan})
        for (int mt : {modtype::flange, modtype::revFlange, modtype::chorusA, modtype::phasing, modtype::tremolo,
                       modtype::symphonic})
        {
            if (type != fxtype::mod && mt != modtype::flange)
                continue;
            FxParams p;
            p.type = type;
            p.modType = mt;
            p.lpfKHz = 6.0f;
            p.pitchShiftSt = 5.0f;
            p.pitchFb = 40.0f;
            p.echoDamp = 0.4f;
            p.dlyLevel = 80.0f;
            p.dlyFb = 40.0f;
            p.dlyLMs = 17.3f;
            p.modLpfKHz = 8.0f;
            p.modFb = 60.0f;
            p.panDepth = 100.0f;
            const auto a = run (p, in, 1);
            const auto b = run (p, in, 512);
            const auto c = run (p, in, 77);
            CHECK (a.l == b.l);
            CHECK (a.r == b.r);
            CHECK (c.l == b.l);
        }
}

TEST_CASE ("FX slot works at other sample rates and after re-preparing")
{
    FxSlot slot;
    auto p = delayParams ();
    for (double sr : {44100.0, 96000.0, 32000.0})
    {
        slot.prepare (sr, 256);
        std::vector<float> l (4096, 0.0f), r (4096, 0.0f);
        l[0] = r[0] = 1.0f;
        slot.process (l.data (), r.data (), 4096, p);
        const size_t at = (size_t) std::lround (0.010 * sr);
        CHECK (l[at] == Catch::Approx (1.0f).margin (1e-4));
    }
}

TEST_CASE ("FX slot tail seconds is sane")
{
    FxParams p;
    p.type = fxtype::off;
    CHECK (FxSlot::tailSeconds (p) == 0.0);
    p.type = fxtype::pan;
    CHECK (FxSlot::tailSeconds (p) == 0.0);

    p = delayParams ();
    p.era = 1;
    p.dlyLMs = p.dlyRMs = 100.0f;
    p.dlyFb = 0.0f;
    CHECK (FxSlot::tailSeconds (p) == Catch::Approx (0.1).margin (1e-6));
    p.dlyFb = 50.0f;
    const double t50 = FxSlot::tailSeconds (p);
    p.dlyFb = 90.0f;
    const double t90 = FxSlot::tailSeconds (p);
    CHECK (t50 > 0.1);
    CHECK (t90 > t50);
    p.dlyFb = 99.0f;
    CHECK (FxSlot::tailSeconds (p) <= 60.0);

    p = modParams (modtype::revFlange); // a short flange tail: the reverb's tail is the engine's
    p.modRtS = 4.0f;
    CHECK (FxSlot::tailSeconds (p) < 0.2);
}

TEST_CASE ("FX slot silence settles to exact zero without denormals")
{
    FxParams p;
    p.type = fxtype::echo;
    p.echoLMs = p.echoRMs = 5.0f;
    p.echoFb = 50.0f;
    p.echoDamp = 0.3f;
    p.lpfKHz = 4.0f;
    Stereo in = impulse (48000 * 3);
    FxSlot slot;
    slot.prepare (kSr, 512);
    for (size_t pos = 0; pos < in.l.size (); pos += 512)
    {
        const int c = (int) std::min<size_t> (512, in.l.size () - pos);
        slot.process (in.l.data () + pos, in.r.data () + pos, c, p);
    }
    bool denormal = false;
    for (size_t i = 48000; i < in.l.size (); ++i)
        for (float v : {in.l[i], in.r[i]})
            if (std::fpclassify (v) == FP_SUBNORMAL)
                denormal = true;
    CHECK_FALSE (denormal);
    CHECK (in.l.back () == 0.0f);
}

TEST_CASE ("FX slot LFO sync locks the mod LFO to a note value at the host tempo")
{
    // tremolo at full depth on a steady input: the gain minima are one LFO period apart
    FxParams p = modParams (modtype::tremolo);
    p.modDepth = 100.0f;
    p.lfoRateHz = 1.9f; // nearest note value at 120 BPM: a quarter note, 2 Hz
    p.sync = true;
    p.hostBpm = 120.0;
    Stereo in (48000 * 3);
    std::fill (in.l.begin (), in.l.end (), 1.0f);
    std::fill (in.r.begin (), in.r.end (), 1.0f);
    const auto out = run (p, in);
    std::vector<size_t> minima;
    for (size_t i = 24000; i + 1 < out.l.size (); ++i)
        if (out.l[i] < out.l[i - 1] && out.l[i] <= out.l[i + 1] && out.l[i] < 0.05f)
            if (minima.empty () || i - minima.back () > 4800)
                minima.push_back (i);
    REQUIRE (minima.size () >= 3);
    const double period = (double) (minima.back () - minima.front ()) / (double) (minima.size () - 1) / 48000.0;
    CHECK (period == Catch::Approx (0.5).margin (0.002));
}

TEST_CASE ("FX period A/B options: each changes only the effects it belongs to")
{
    const auto tone = sine (24000, 330.0);
    const auto diff = [] (const Stereo& a, const Stereo& b)
    {
        double e = 0.0;
        for (size_t i = 0; i < a.l.size (); ++i)
            e += std::pow ((double) a.l[i] - b.l[i], 2.0) + std::pow ((double) a.r[i] - b.r[i], 2.0);
        return e;
    };
    const auto finite = [] (const Stereo& s)
    {
        for (size_t i = 0; i < s.l.size (); ++i)
            if (!std::isfinite (s.l[i]) || !std::isfinite (s.r[i]) || std::fabs (s.l[i]) > 4.0f)
                return false;
        return true;
    };
    FxParams base;
    base.type = fxtype::mod;
    base.lfoRateHz = 2.0f;
    base.modDepth = base.dmDepth = 80.0f;

    for (int mt : {modtype::flange, modtype::chorusA, modtype::phasing, modtype::symphonic})
    {
        auto p = base;
        p.modType = mt;
        const auto ref = run (p, tone);
        for (int opt = 0; opt < 3; ++opt)
        {
            auto q = p;
            (opt == 0 ? q.steppedSweep : opt == 1 ? q.linearRead : q.triangleLfo) = true;
            const auto out = run (q, tone);
            CHECK (finite (out));
            CHECK (diff (out, ref) > 1.0e-6);
        }
        auto q = p;
        q.tremoloDm = true; // tremolo only
        CHECK (diff (run (q, tone), ref) == 0.0);
    }

    // tremolo: the delay sweep adds a little pitch movement but keeps the level envelope
    auto t = base;
    t.modType = modtype::tremolo;
    const auto ref = run (t, tone);
    t.tremoloDm = true;
    const auto dm = run (t, tone);
    CHECK (finite (dm));
    CHECK (diff (dm, ref) > 1.0e-4);
    CHECK (energy (dm.l, 4800, 24000) == Catch::Approx (energy (ref.l, 4800, 24000)).epsilon (0.05));

    // the delay type has no sweep and reads at whole-sample times here: the sweep options leave it alone
    FxParams d;
    d.type = fxtype::delay;
    d.dlyLMs = d.dlyRMs = 100.0f;
    const auto dref = run (d, tone);
    d.steppedSweep = d.triangleLfo = d.tremoloDm = true;
    CHECK (diff (run (d, tone), dref) == 0.0);
}

TEST_CASE ("FX stepped sweep holds the swept delay on 256 steps")
{
    // An impulse train through a slow, deep flange: with steps the delay sits still between step changes, so many
    // successive output frames repeat exactly; without, they never do.
    FxParams p;
    p.type = fxtype::mod;
    p.modType = modtype::flange;
    p.lfoRateHz = 0.1f;
    p.modDepth = 100.0f;
    p.modDelayMs = 5.0f;
    p.modFb = 0.0f;
    Stereo in (48000);
    for (size_t i = 0; i < in.l.size (); i += 480)
        in.l[i] = in.r[i] = 1.0f;
    const auto repeats = [&] (bool stepped)
    {
        auto q = p;
        q.steppedSweep = stepped;
        const auto out = run (q, in);
        int same = 0;
        for (size_t k = 4800; k + 960 < out.l.size (); k += 480)
        {
            bool eq = true;
            for (size_t j = 0; j < 480 && eq; ++j)
                eq = out.l[k + j] == out.l[k + 480 + j];
            same += eq ? 1 : 0;
        }
        return same;
    };
    CHECK (repeats (false) == 0);
    CHECK (repeats (true) > 10);
}

TEST_CASE ("FX slot symphonic follows the measured SPX90-model law: one 4.1 ms tap at depth 0, a 0-8.2 ms sweep at 50 %")
{
    // 2026-11-22, measured on a third-party SPX90 Symphonic model (FxSlot.cpp kSym*): at depth 0 the three voices sit on the
    // 4.1 ms centre, so the output is one tap there and no direct signal; at 50 % the two LFOs just reach both ends
    // of the 8.19 ms line.
    auto p = modParams (modtype::symphonic);
    p.modDepth = 0.0f;
    p.lfoRateHz = 0.7f;
    const auto out = run (p, impulse (4800, 100));
    size_t at = 0;
    float pk = 0.0f;
    for (size_t i = 0; i < out.l.size (); ++i)
        if (std::abs (out.l[i]) > pk)
            pk = std::abs (out.l[i]), at = i;
    CHECK ((double) (at - 100) / kSr * 1000.0 == Catch::Approx (4.096).margin (0.05));
    CHECK (std::abs (out.l[100]) < 1.0e-3f); // no direct signal
    double sum = 0.0;                        // the tap's gain (a fractional delay spreads it over a few samples)
    for (size_t i = at - 8; i <= at + 8; ++i)
        sum += out.l[i];
    CHECK (sum == Catch::Approx (1.414).margin (0.05)); // two voices at 0.707, in phase

    // at 50 %, clicks every 40 ms over 20 s: the strongest arrival's delay spans about 0 to 8.2 ms
    p.modDepth = 50.0f;
    const size_t n = (size_t) (20.0 * kSr), period = (size_t) (0.04 * kSr);
    Stereo clicks;
    clicks.l.assign (n, 0.0f);
    clicks.r.assign (n, 0.0f);
    for (size_t i = 0; i < n; i += period)
        clicks.l[i] = clicks.r[i] = 1.0f;
    const auto y = run (p, clicks);
    double lo = 1e9, hi = 0.0;
    for (size_t k = 0; k + period < n; k += period)
        for (size_t i = 0; i < (size_t) (0.009 * kSr); ++i)
            if (std::abs (y.r[k + i]) > 0.3f)
            {
                lo = std::min (lo, (double) i / kSr * 1000.0);
                hi = std::max (hi, (double) i / kSr * 1000.0);
            }
    CAPTURE (lo, hi);
    CHECK (lo < 0.5);
    CHECK (hi > 7.5);
    CHECK (hi < 8.4);
}
