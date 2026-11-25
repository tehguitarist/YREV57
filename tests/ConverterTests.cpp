#include "ReverbMeasure.h"
#include "dsp/Converter.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cmath>
#include <complex>
#include <cstring>
#include <vector>

using Catch::Approx;
using yrev::dsp::Converter;
using yrev::dsp::ConverterParams;
using yrev::dsp::EngineParams;

namespace
{
    constexpr double kPi = 3.14159265358979323846;
    constexpr int kBlock = 256;

    struct Out
    {
        std::vector<float> l, r;
    };

    // Runs `in` (both channels) through a fresh converter.
    Out run (std::vector<float> in, double fs, const ConverterParams& cp, bool bypassEngine,
             const EngineParams& ep = {}, int block = kBlock, bool gate = true)
    {
        juce::ScopedNoDenormals noDenormals;
        Converter c;
        c.prepare (fs, block);
        c.setEngineBypass (bypassEngine);
        c.setNoiseGateEnabled (gate);
        Out o{in, in};
        for (size_t pos = 0; pos < in.size (); pos += (size_t) block)
        {
            const int n = (int) std::min ((size_t) block, in.size () - pos);
            c.process (o.l.data () + pos, o.r.data () + pos, n, ep, cp);
        }
        return o;
    }

    std::vector<float> impulse (double fs, double seconds, float amp = 0.25f)
    {
        std::vector<float> x ((size_t) (fs * seconds), 0.0f);
        x[(size_t) (0.05 * fs)] = amp; // after the era fade-in
        return x;
    }

    double dbAt (const std::vector<float>& h, double fs, double hz)
    {
        std::complex<double> s{};
        for (size_t n = 0; n < h.size (); ++n)
            s += (double) h[n] * std::polar (1.0, -2.0 * kPi * hz * (double) n / fs);
        return 20.0 * std::log10 (std::abs (s) + 1.0e-30);
    }

    double rel (const std::vector<float>& h, double fs, double hz)
    {
        return dbAt (h, fs, hz) - dbAt (h, fs, 1000.0);
    }

    double rms (const std::vector<float>& x, size_t from, size_t to)
    {
        double s = 0;
        for (size_t i = from; i < to; ++i)
            s += (double) x[i] * x[i];
        return std::sqrt (s / (double) std::max<size_t> (1, to - from));
    }

    ConverterParams clean (int era)
    {
        return {era, 0.0f, false};
    }
} // namespace

TEST_CASE ("era 7 converter response matches the measured REV7 direct path")
{
    // pass 36, measured (REV7 Delay L R taps, re 1 kHz): flat to 8 kHz
    // (+0.2 dB), -0.5 dB 11.2 kHz, -2.1 dB 12 kHz, -6.1 dB 12.5 kHz, -16.9 dB 13 kHz; flat bass (no lift)
    for (const double fs : {44100.0, 48000.0, 96000.0})
    {
        CAPTURE (fs);
        const auto o = run (impulse (fs, 0.15), fs, clean (0), true);
        CHECK (rel (o.l, fs, 100.0) == Approx (0.0).margin (0.5));
        CHECK (rel (o.l, fs, 5000.0) == Approx (0.2).margin (0.5));
        CHECK (rel (o.l, fs, 8000.0) == Approx (0.2).margin (0.5));
        CHECK (rel (o.l, fs, 11200.0) == Approx (-0.5).margin (0.8));
        CHECK (rel (o.l, fs, 12000.0) == Approx (-2.1).margin (2.0));
        CHECK (rel (o.l, fs, 12500.0) == Approx (-6.1).margin (2.0));
        CHECK (rel (o.l, fs, 15500.0) < -50.0); // spec: >= 50 dB down at 15.5 kHz
        CHECK (rel (o.l, fs, 19000.0) < -60.0);
    }
}

TEST_CASE ("the FX delay's repeat keeps the converter's top octave (whole-sample reads), both eras")
{
    // M6 (2026-11-24): the hardware's fixed delays are whole native samples; a cubic read at the fraction the bridge
    // latency leaves took 0.8 / 1.7 / 3.5 dB off the era-7 repeat at 8 / 10 / 12.5 kHz
    for (const int era : {0, 1})
        for (const double fs : {44100.0, 48000.0})
        {
            CAPTURE (era, fs);
            const auto direct = run (impulse (fs, 0.15), fs, clean (era), true);
            yrev::dsp::FxParams fx;
            fx.era = era;
            fx.type = yrev::dsp::fxtype::delay;
            fx.dlyLMs = fx.dlyRMs = 50.013f; // off the sample grid at every rate
            fx.dlyLevel = 100.0f;
            auto cp = clean (era);
            cp.fx = &fx;
            EngineParams ep;
            ep.preDelayMs = 0.1f; // no initial delay on the effect, so the wet repeat and the bypass's coincide
            const auto o = run (impulse (fs, 0.25), fs, cp, true, ep);
            // the repeat alone: from 25 ms after the input on (the direct path is over by then)
            const size_t from = (size_t) ((0.05 + 0.025) * fs);
            const std::vector<float> rep (o.l.begin () + (long) from, o.l.end ());
            for (const double hz : {8000.0, 10000.0, 12000.0})
            {
                CAPTURE (hz);
                CHECK (rel (rep, fs, hz) == Approx (rel (direct.l, fs, hz)).margin (0.2));
            }
        }
}

TEST_CASE ("era 5 converter response matches the measured REV5 single path")
{
    // pass 36, measured (REV5 program 26, left, re 1 kHz): +0.5 dB 8 kHz,
    // +0.3 dB 10 kHz, +0.1 dB 12.5 kHz, +0.3 dB 16 kHz, +0.1 dB 18 kHz, -1.5 dB 20 kHz, -8.6 dB 21 kHz: flat, no hold
    // droop. The converter alone (the reflections' comb low-pass is in the engine).
    for (const double fs : {44100.0, 48000.0, 96000.0, 192000.0})
    {
        CAPTURE (fs);
        const auto o = run (impulse (fs, 0.15), fs, clean (1), true);
        CHECK (rel (o.l, fs, 100.0) == Approx (0.0).margin (0.5));
        CHECK (rel (o.l, fs, 8000.0) == Approx (0.5).margin (0.5));
        CHECK (rel (o.l, fs, 10000.0) == Approx (0.3).margin (0.5));
        CHECK (rel (o.l, fs, 12500.0) == Approx (0.1).margin (0.5));
        CHECK (rel (o.l, fs, 16000.0) == Approx (0.3).margin (0.6));
        CHECK (rel (o.l, fs, 18000.0) == Approx (0.1).margin (0.6));
        if (fs > 44100.0) // 20 kHz is the host's own edge at 44.1 kHz
            CHECK (rel (o.l, fs, 20000.0) == Approx (-1.5).margin (1.5));
    }
}

TEST_CASE ("the converter latency is fixed, known and short at every host rate")
{
    for (const double fs : {44100.0, 48000.0, 96000.0})
        for (const int era : {0, 1})
        {
            CAPTURE (fs, era);
            Converter c;
            c.prepare (fs, kBlock);
            std::vector<float> l (kBlock, 0.0f), r (kBlock, 0.0f);
            c.process (l.data (), r.data (), kBlock, {}, ConverterParams{era, 0.0f, false});
            CHECK (c.latencySamples () > 0);
            CHECK (c.latencySamples () < (int) (0.004 * fs)); // under 4 ms

            const auto o = run (impulse (fs, 0.1, 0.25f), fs, clean (era), true);
            size_t peak = 0;
            for (size_t i = 0; i < o.l.size (); ++i)
                if (std::abs (o.l[i]) > std::abs (o.l[peak]))
                    peak = i;
            const auto start = (size_t) (0.05 * fs);
            CHECK (peak + 2 >= start + (size_t) c.latencySamples ());
            CHECK (peak <= start + (size_t) c.latencySamples () + (size_t) (0.001 * fs)); // + filter group delay
        }
}

TEST_CASE ("the wet path is padded to the reported latency, so reflections arrive on time after it")
{
    const double fs = 48000.0;
    EngineParams ep;
    ep.erRevBalance = 100.0f; // reflections only
    ep.preDelayMs = 30.0f;
    ep.erMode = 0;
    auto in = impulse (fs, 0.3, 0.25f);
    const auto o = run (in, fs, clean (0), false, ep);
    size_t first = 0;
    for (size_t i = 0; i < o.l.size (); ++i)
        if (std::abs (o.l[i]) > 1.0e-3f)
        {
            first = i;
            break;
        }
    Converter probe;
    probe.prepare (fs, 512);
    const double arrivalMs = 1000.0 * ((double) first - 0.05 * fs - probe.reportedLatency ()) / fs;
    CHECK (arrivalMs == Approx (30.0).margin (1.0));
}

TEST_CASE ("16-bit truncation removes sub-LSB detail; 24-bit (character 0) keeps it")
{
    const double fs = 48000.0;
    // a 1 kHz tone at 0.4 of a 16-bit step at the converter (one step is 2 / 32768 at host scale)
    std::vector<float> tone ((size_t) (0.6 * fs));
    for (size_t i = 0; i < tone.size (); ++i)
        tone[i] = 0.4f * (2.0f / 32768.0f) * (float) std::sin (2.0 * kPi * 1000.0 * (double) i / fs);
    auto coherent = [&] (const std::vector<float>& y)
    {
        const std::vector<float> tail (y.begin () + (std::ptrdiff_t) (0.1 * fs), y.end ());
        return std::pow (10.0, dbAt (tail, fs, 1000.0) / 20.0) / (double) tail.size ();
    };
    const double in = coherent (tone);
    const double authentic = coherent (run (tone, fs, ConverterParams{0, 100.0f, false}, true).l);
    const double cleanOut = coherent (run (tone, fs, ConverterParams{0, 0.0f, false}, true).l);
    CHECK (cleanOut == Approx (in).epsilon (0.15));
    CHECK (authentic < 0.1 * in); // truncated away; only uncorrelated hiss remains
}

TEST_CASE ("era 7's D/A has the PCM53's low-level distortion; era 5 stays cleaner")
{
    // 420 Hz at -60 dB re converter full scale (the datasheet's test): THD 1.9 % typ, 4.0 % max
    const double fs = 48000.0;
    std::vector<float> tone ((size_t) (2.0 * fs));
    for (size_t i = 0; i < tone.size (); ++i)
        tone[i] = 2.0f * 0.001f * (float) std::sin (2.0 * kPi * 420.0 * (double) i / fs);
    // 420 Hz repeats every 105 samples at 44.1 kHz, so the truncation error, and with it the harmonics, depends on
    // where the native samples fall on the waveform, which any change of filter delay moves (era 5's THD read 0.74 or
    // 1.13 % with two converter fits of equal passband). Averaged over eight starting phases.
    constexpr int kPhases = 8;
    auto thd = [&] (int era)
    {
        double h = 0.0;
        for (int ph = 0; ph < kPhases; ++ph)
        {
            for (size_t i = 0; i < tone.size (); ++i)
                tone[i] = 2.0f * 0.001f *
                          (float) std::sin (2.0 * kPi * 420.0 * (double) i / fs + 2.0 * kPi * ph / (kPhases * 105.0));
            const auto y = run (tone, fs, ConverterParams{era, 100.0f, false}, true).l;
            const std::vector<float> last (y.end () - (std::ptrdiff_t) fs, y.end ()); // whole cycles: 420 in 1 s
            for (int k = 2; k <= 9; ++k)
                h += std::pow (10.0, (dbAt (last, fs, 420.0 * k) - dbAt (last, fs, 420.0)) / 10.0);
        }
        return 100.0 * std::sqrt (h / kPhases);
    };
    const double t7 = thd (0), t5 = thd (1);
    CAPTURE (t7, t5);
    CHECK (t7 > 1.0);
    CHECK (t7 < 4.0);
    CHECK (t5 < 0.6 * t7);
}

TEST_CASE ("hiss is low (pristine units), a little higher in era 7, and off at character 0")
{
    const double fs = 48000.0;
    std::vector<float> silence ((size_t) fs, 0.0f);
    auto level = [&] (int era, float character)
    {
        const auto o = run (silence, fs, ConverterParams{era, character, false}, true, {}, kBlock, false);
        return 20.0 * std::log10 (rms (o.l, (size_t) (0.1 * fs), o.l.size ()) + 1.0e-30);
    };
    const double e7 = level (0, 100.0f), e5 = level (1, 100.0f);
    CAPTURE (e7, e5);
    CHECK (e7 < -75.0); // dBFS at the host (converter full scale is +6 dBFS)
    CHECK (e5 < -80.0);
    CHECK (e5 > -105.0);
    CHECK (e7 > e5 + 1.0);
    CHECK (level (0, 0.0f) < -300.0);
}

TEST_CASE ("no phantom hiss: hiss and dither stop when no audio passes, and return with it")
{
    const double fs = 48000.0;
    EngineParams ep;
    ep.rt = 0.5f;
    std::vector<float> in ((size_t) (6.0 * fs), 0.0f);
    for (size_t i = (size_t) (0.05 * fs); i < (size_t) (0.5 * fs); ++i) // a short tone, then silence
        in[i] = 0.1f * (float) std::sin (2.0 * kPi * 440.0 * (double) i / fs);
    const auto o = run (in, fs, ConverterParams{1, 100.0f, true}, false, ep); // era 5: hiss and dither
    CHECK (rms (o.l, (size_t) (0.2 * fs), (size_t) (0.4 * fs)) > 1.0e-3);     // signal passes
    CHECK (rms (o.l, (size_t) (5.0 * fs), o.l.size ()) < 1.0e-9); // below -180 dBFS once the filters ring out

    std::vector<float> idle ((size_t) (1.0 * fs), 0.0f);
    const auto quiet = run (idle, fs, ConverterParams{0, 100.0f, true}, true);
    CHECK (rms (quiet.l, 0, quiet.l.size ()) == 0.0); // never opened
}

TEST_CASE ("the A/D clips at full scale, and content above the native Nyquist does not alias through")
{
    const double fs = 48000.0;
    std::vector<float> loud ((size_t) (0.2 * fs)), high ((size_t) (0.5 * fs));
    for (size_t i = 0; i < loud.size (); ++i)
        loud[i] = 4.0f * (float) std::sin (2.0 * kPi * 500.0 * (double) i / fs); // 6 dB over converter full scale
    for (size_t i = 0; i < high.size (); ++i)
        high[i] = 0.5f * (float) std::sin (2.0 * kPi * 20000.0 * (double) i / fs); // above 15.6 kHz
    const auto o = run (loud, fs, clean (0), true);
    float peak = 0.0f;
    for (size_t i = (size_t) (0.1 * fs); i < o.l.size (); ++i)
        peak = std::max (peak, std::abs (o.l[i]));
    // full scale is 2.0 (+6 dBFS); some overshoot from the filters raises the clipped tone (margin kept from when an
    // era-7 bass lift sat after the D/A, removed in pass 36)
    CHECK (peak < 2.6f * 1.23f);
    const auto a = run (high, fs, clean (0), true);
    CHECK (rms (a.l, (size_t) (0.2 * fs), a.l.size ()) < 0.5 * 1.0e-3); // below -60 dB re the input
}

TEST_CASE ("with the engine, the tail decays to silence (character 0) or to the hiss floor (character 100)")
{
    const double fs = 48000.0;
    EngineParams ep;
    ep.rt = 0.5f;
    auto in = impulse (fs, 4.0, 0.5f);
    const auto c0 = run (in, fs, ConverterParams{0, 0.0f, false}, false, ep);
    const auto c100 = run (in, fs, ConverterParams{0, 100.0f, false}, false, ep);
    CHECK (rms (c0.l, (size_t) (3.5 * fs), c0.l.size ()) < 1.0e-6);
    CHECK (rms (c100.l, (size_t) (3.5 * fs), c100.l.size ()) < 1.0e-6); // no dead-band limit cycle; hiss gated off
}

TEST_CASE ("the wet path blocks DC and keeps its bass to 20 Hz, in both eras")
{
    // eighteenth pass: 2nd-order high-pass at 10 Hz on the converter output; both units specify the effect path from
    // 20 Hz (REV5 factory check 20 Hz-5 kHz within +-1 dB)
    const double fs = 48000.0;
    for (const int era : {0, 1})
    {
        CAPTURE (era);
        const auto o = run (impulse (fs, 3.0), fs, clean (era), true);
        CHECK (dbAt (o.l, fs, 20.0) - dbAt (o.l, fs, 50.0) > -0.6);
        CHECK (dbAt (o.l, fs, 0.0) < dbAt (o.l, fs, 100.0) - 40.0); // the impulse response sums to (almost) nothing
        CHECK (dbAt (o.l, fs, 3.0) < dbAt (o.l, fs, 100.0) - 20.0);
    }
}

TEST_CASE ("era 7's reflections take the tap average, the Rev taps more than the charts; the 1st reflection none")
{
    // pass 39: a two-sample average on the era-7 reflections, u 0.5 on the Rev taps and 0.35 on the E/R charts (8 kHz
    // octave re 1 kHz over the first 60 ms of a reflections-only render: bare taps +8.1 dB, the chart +7.4 dB); the 1st
    // reflection is as flat as the direct path (REV7 captures)
    const double fs = 48000.0;
    auto r8For = [&] (int erMode, float tapRead = 0.0f)
    {
        EngineParams ep;
        ep.tapReadOffset = tapRead;
        ep.erRevBalance = 100.0f; // reflections only
        ep.preDelayMs = 1.0f;
        ep.erMode = erMode;
        const auto o = run (impulse (fs, 1.0), fs, clean (0), false, ep);
        size_t on = 0;
        while (on < o.l.size () && std::abs (o.l[on]) < 1.0e-3f * 0.25f)
            ++on;
        const std::vector<float> head (o.l.begin () + (long) on, o.l.begin () + (long) (on + (size_t) (0.06 * fs)));
        auto octave = [&] (double fc)
        {
            double e = 0.0;
            for (double f = fc / std::sqrt (2.0); f < fc * std::sqrt (2.0); f += 25.0)
                e += std::pow (10.0, dbAt (head, fs, f) / 10.0);
            return 10.0 * std::log10 (e);
        };
        return octave (8000.0) - octave (1000.0);
    };
    const double chart = r8For (1), rev = r8For (12);
    CAPTURE (chart, rev);
    CHECK (chart < 8.0);
    CHECK (chart > 6.5);
    CHECK (rev < chart - 1.0);

    // pass 61: the program's own read (voice_tap_read) moves the average; -0.5 reads whole samples (as bright as the
    // 1st reflection's taps), +0.5 takes a second half-sample average (darker than the Rev default)
    const double whole = r8For (12, -0.5f), darker = r8For (12, 0.5f);
    CAPTURE (whole, darker);
    CHECK (whole > rev + 2.0);
    CHECK (darker < rev - 2.0);

    // the 1st reflection alone (the taps start 50 ms later): the converter's response at 8 and 10 kHz
    EngineParams ep;
    ep.erRevBalance = 100.0f;
    ep.preDelayMs = 50.0f;
    ep.ref1DelayMs = 5.0f;
    ep.ref1Level = 100.0f;
    const auto o = run (impulse (fs, 0.3), fs, clean (0), false, ep);
    const auto direct = run (impulse (fs, 0.3), fs, clean (0), true);
    const size_t from = (size_t) (0.05 * fs), to = from + (size_t) (0.03 * fs);
    const std::vector<float> r1 (o.l.begin () + (long) from, o.l.begin () + (long) to);
    for (const double hz : {8000.0, 10000.0})
    {
        CAPTURE (hz);
        CHECK (rel (r1, fs, hz) == Approx (rel (direct.l, fs, hz)).margin (0.3));
    }
}

TEST_CASE ("the printed HIGH holds at the top of each era's band")
{
    // pass 37: the hardware captures read the printed HIGH as the RT ratio at about 10-12.5 kHz (REV7) and 16 kHz
    // (REV5); the 2 kHz third octave stays near 0.9-1.0 (measurement of the captures, HIGH 0.3). The REV7 captures read
    // HIGH 0.3 as 0.33-0.36 there; pass 39's brighter late feed moves this octave's reading (0.30 -> 0.38; the
    // third-octave ratios it was fitted on are unchanged, 0.041 rms), so era 7 is held to the captures' reading.
    // Pass 73 (corner x 1.15, reference 10.5 kHz) moves this octave to about 0.44: the third-octave T20 ratios against
    // the captures stay within 0.02 at 10-12.5 kHz (0.044 -> 0.047 rms, 2-12.5 kHz), the late 4-10 kHz level is fixed.
    const double fs = 96000.0;
    for (const int era : {0, 1})
    {
        CAPTURE (era);
        EngineParams ep;
        ep.rt = 2.0f;
        ep.hiRatio = 0.3f;
        ep.erRevBalance = 0.0f;
        ep.preDelayMs = 5.0f;
        const auto o = run (impulse (fs, 3.0, 0.5f), fs, clean (era), false, ep);
        auto rt = [&] (float hz) { return yrev::test::reverbTime (yrev::test::octaveBand (o.l, fs, hz), fs); };
        const double r1 = rt (1000.0f), top = rt (era == 0 ? 11000.0f : 16000.0f), r2 = rt (2000.0f);
        CAPTURE (r1, top, r2);
        CHECK (top / r1 == Approx (era == 0 ? 0.42 : 0.3).margin (0.08));
        CHECK (r2 / r1 > 0.85);
    }
}

TEST_CASE ("era 7's treble decay floors at short RT and low HIGH")
{
    // pass 75: the REV7 Handclaps capture (RT 0.4 s, HIGH 0.1) decays about 0.40 s at 4-8 kHz against 0.45 s at 1 kHz
    // (measurement of the capture); era 7 floors the treble decay at 0.2 s, era 5 does not (no capture)
    const double fs = 96000.0;
    double ratio[2] = {};
    for (const int era : {0, 1})
    {
        CAPTURE (era);
        EngineParams ep;
        ep.rt = 0.4f;
        ep.hiRatio = 0.1f;
        ep.erRevBalance = 0.0f;
        ep.preDelayMs = 5.0f;
        const auto o = run (impulse (fs, 1.5, 0.5f), fs, clean (era), false, ep);
        auto rt = [&] (float hz) { return yrev::test::reverbTime (yrev::test::octaveBand (o.l, fs, hz), fs); };
        ratio[era] = rt (8000.0f) / rt (1000.0f);
        CAPTURE (ratio[era]);
    }
    CHECK (ratio[0] > 0.5);
    CHECK (ratio[1] < ratio[0] - 0.1);
}

TEST_CASE ("converter output does not depend on the block size")
{
    const double fs = 48000.0;
    auto in = impulse (fs, 0.4, 0.3f);
    ConverterParams cp{1, 100.0f, true};
    cp.frozenNoise = true; // two converters: the same noise only with the frozen seed
    const auto a = run (in, fs, cp, false, {}, 1);
    const auto b = run (in, fs, cp, false, {}, 512);
    CHECK (std::memcmp (a.l.data (), b.l.data (), a.l.size () * sizeof (float)) == 0);
}

TEST_CASE ("switching era mid-stream fades in cleanly")
{
    const double fs = 48000.0;
    Converter c;
    c.prepare (fs, kBlock);
    std::vector<float> l (kBlock), r (kBlock);
    float worst = 0.0f;
    for (int b = 0; b < 200; ++b)
    {
        for (int i = 0; i < kBlock; ++i)
            l[(size_t) i] = r[(size_t) i] = 0.3f * (float) std::sin (0.05 * (b * kBlock + i));
        c.process (l.data (), r.data (), kBlock, {}, ConverterParams{b < 100 ? 0 : 1, 100.0f, true});
        for (float v : l)
        {
            REQUIRE (std::isfinite (v));
            worst = std::max (worst, std::abs (v));
        }
    }
    CHECK (c.nativeRate () == Approx (44100.0));
    CHECK (worst < 4.0f);
}

TEST_CASE ("converter and engine cost stays flat in silence after a burst (denormals)")
{
    juce::ScopedNoDenormals noDenormals;
    const double fs = 48000.0;
    Converter c;
    c.prepare (fs, kBlock);
    EngineParams ep;
    ep.rt = 1.0f;
    const ConverterParams cp{0, 0.0f, false};
    std::vector<float> l (kBlock), r (kBlock);
    auto time = [&] (int blocks, bool burst)
    {
        const auto t0 = std::chrono::steady_clock::now ();
        for (int b = 0; b < blocks; ++b)
        {
            for (int i = 0; i < kBlock; ++i)
                l[(size_t) i] = r[(size_t) i] = burst ? (float) std::sin (0.1 * (b * kBlock + i)) : 0.0f;
            c.process (l.data (), r.data (), kBlock, ep, cp);
        }
        return std::chrono::duration<double> (std::chrono::steady_clock::now () - t0).count ();
    };
    const int oneSecond = (int) (fs / kBlock);
    const double busy = time (oneSecond, true);
    time (oneSecond * 20, false);
    const double quiet = time (oneSecond, false);
    CHECK (quiet < busy * 3.0 + 0.01);
}

TEST_CASE ("the era 5 digital EQ runs in the DSP stage, before the D/A; era 7 ignores it")
{
    const double fs = 48000.0;
    std::vector<float> tone ((size_t) (0.5 * fs));
    for (size_t i = 0; i < tone.size (); ++i)
        tone[i] = 0.1f * (float) std::sin (2.0 * kPi * 1000.0 * (double) i / fs);
    auto level = [&] (int era, bool eqOn)
    {
        auto cp = clean (era);
        cp.deq.on = eqOn;
        cp.deq.g[1] = 12.0f; // mid band at 1 kHz
        const auto o = run (tone, fs, cp, true);
        return 20.0 * std::log10 (rms (o.l, (size_t) (0.25 * fs), tone.size ()));
    };
    CHECK (level (1, true) - level (1, false) == Approx (12.0).margin (0.3));
    CHECK (level (0, true) - level (0, false) == Approx (0.0).margin (0.01));

    // a boost the D/A cannot carry clips there, as a 16-bit converter would
    std::vector<float> loud (tone);
    for (auto& v : loud)
        v *= 10.0f; // +6 dBFS in at the converter's full scale; +12 dB more after the EQ
    auto cp = clean (1);
    cp.deq.on = true;
    cp.deq.g[1] = 12.0f;
    const auto o = run (loud, fs, cp, true);
    float peak = 0.0f;
    for (size_t i = (size_t) (0.25 * fs); i < o.l.size (); ++i)
        peak = std::max (peak, std::abs (o.l[i]));
    CHECK (peak < 2.0f * 1.3f); // full scale (2.0) plus reconstruction overshoot only
}

TEST_CASE ("the digital EQ's Q is the REV5's bandwidth: 0.1 narrowest, 5.0 widest")
{
    // a +12 dB peak at 1 kHz: at 1.6 kHz the narrow setting (0.1) gives far less boost than the wide one (5.0)
    const double fs = 48000.0;
    std::vector<float> tone ((size_t) (0.5 * fs));
    for (size_t i = 0; i < tone.size (); ++i)
        tone[i] = 0.1f * (float) std::sin (2.0 * kPi * 1600.0 * (double) i / fs);
    auto boost = [&] (float q)
    {
        auto cp = clean (1);
        cp.deq.on = true;
        cp.deq.g[1] = 12.0f;
        cp.deq.q[1] = q;
        const auto o = run (tone, fs, cp, true);
        auto cp0 = clean (1);
        const auto ref = run (tone, fs, cp0, true);
        return 20.0 * std::log10 (rms (o.l, (size_t) (0.25 * fs), tone.size ()) /
                                  rms (ref.l, (size_t) (0.25 * fs), tone.size ()));
    };
    CHECK (boost (0.1f) < 2.0);
    CHECK (boost (5.0f) > 9.0);
}

TEST_CASE ("eco quality: no bridge latency, the era's band limit stays; frozen noise repeats, random does not")
{
    const double fs = 48000.0;
    auto cp = clean (0);
    cp.eco = true;
    {
        Converter c;
        c.prepare (fs, kBlock);
        std::vector<float> l (kBlock, 0.0f), r (kBlock, 0.0f);
        c.process (l.data (), r.data (), kBlock, EngineParams{}, cp);
        CHECK (c.latencySamples () == 0);
        CHECK (c.nativeRate () == fs);
    }
    // a 16 kHz tone is still cut by the era-7 band limit at the host rate
    std::vector<float> tone ((size_t) (0.3 * fs)), mid (tone.size ());
    for (size_t i = 0; i < tone.size (); ++i)
    {
        tone[i] = 0.25f * (float) std::sin (2.0 * kPi * 16000.0 * (double) i / fs);
        mid[i] = 0.25f * (float) std::sin (2.0 * kPi * 1000.0 * (double) i / fs);
    }
    const auto hi = run (tone, fs, cp, true), lo = run (mid, fs, cp, true);
    const double drop =
        20.0 * std::log10 (rms (hi.l, tone.size () / 2, tone.size ()) / rms (lo.l, tone.size () / 2, tone.size ()));
    CHECK (drop < -40.0);

    // hiss: frozen gives the same noise in two fresh converters, random does not
    auto hiss = [&] (bool frozen)
    {
        auto q = ConverterParams{0, 100.0f, false};
        q.frozenNoise = frozen;
        return run (std::vector<float> ((size_t) (0.2 * fs), 0.0f), fs, q, true, {}, kBlock, false).l;
    };
    CHECK (hiss (true) == hiss (true));
    CHECK (hiss (false) != hiss (false));
}

TEST_CASE ("the bridge's per-phase weights give the per-tap kernel's output, at the usual rates")
{
    // CPU pass (2026-11-21): the precomputed polyphase path must reproduce the table-read path
    struct Case
    {
        double host, native;
        bool hold;
    };
    for (const auto& c : {Case{96000.0, 31250.0, false}, Case{88200.0, 31250.0, false}, Case{96000.0, 44100.0, true},
                          Case{88200.0, 44100.0, true}, Case{192000.0, 31250.0, false}})
    {
        yrev::dsp::RateBridge a, b;
        for (auto* br : {&a, &b})
        {
            br->prepare (c.host, 31250.0);
            br->preparePolyphase (c.native, c.hold);
            br->setNativeRate (c.native);
            br->setHoldDroop (c.hold);
        }
        b.setPolyphaseEnabled (false);
        CHECK (a.polyphaseActive ());
        CHECK (!b.polyphaseActive ());
        auto pass = [] (float l, float r, float& ol, float& orr) noexcept
        {
            ol = l;
            orr = -r;
        };
        uint32_t s = 0x1234567u;
        double err = 0.0, ref = 0.0;
        for (int i = 0; i < (int) (0.5 * c.host); ++i)
        {
            s = s * 1664525u + 1013904223u;
            const float x = (float) ((double) s / 4294967296.0 - 0.5), y = 0.5f * x;
            float al, ar, bl, brr;
            a.process (x, y, al, ar, pass);
            b.process (x, y, bl, brr, pass);
            err = std::max (err, (double) std::max (std::abs (al - bl), std::abs (ar - brr)));
            ref = std::max (ref, (double) std::abs (bl));
        }
        CHECK (ref > 0.1);
        CHECK (err < 1.0e-6 * ref);
    }
}
