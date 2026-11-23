#include "dsp/Chain.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <vector>

using Catch::Approx;
using yrev::dsp::Chain;
using yrev::dsp::ChainParams;

namespace
{
    constexpr double kSr = 48000.0;
    constexpr int kBlock = 480;

    struct Run
    {
        std::vector<float> l, r;
    };

    // Runs `seconds` of a stereo test signal through the chain and returns the whole output.
    Run run (Chain& c, const ChainParams& p, float freq, float ampL, float ampR, double seconds = 0.5)
    {
        const int total = (int) (kSr * seconds);
        Run out;
        out.l.resize ((size_t) total);
        out.r.resize ((size_t) total);
        for (int i = 0; i < total; ++i)
        {
            const float s = std::sin (2.0f * 3.14159265f * freq * (float) i / (float) kSr);
            out.l[(size_t) i] = ampL * s;
            out.r[(size_t) i] = ampR * s;
        }
        for (int pos = 0; pos < total; pos += kBlock)
            c.process (out.l.data () + pos, out.r.data () + pos, std::min (kBlock, total - pos), p);
        return out;
    }

    float rms (const std::vector<float>& v)
    {
        // last quarter, after the smoothing ramps and filters have settled
        const size_t from = v.size () * 3 / 4;
        double sum = 0;
        for (size_t i = from; i < v.size (); ++i)
            sum += (double) v[i] * v[i];
        return (float) std::sqrt (sum / (double) (v.size () - from));
    }

    Chain make ()
    {
        Chain c;
        c.prepare (kSr, kBlock);
        c.setEngineEnabled (false); // these tests cover the chain around the engine
        return c;
    }
} // namespace

TEST_CASE ("a centred signal passes at unity with the defaults")
{
    auto c = make ();
    const auto out = run (c, ChainParams{}, 1000.0f, 0.5f, 0.5f);
    CHECK (rms (out.l) == Approx (0.5f * 0.7071f).epsilon (0.01));
    CHECK (rms (out.r) == Approx (rms (out.l)).epsilon (0.001));
}

TEST_CASE ("mix 0 is the dry signal and mix 100 is the wet signal")
{
    ChainParams p;
    p.mix = 0.0f;
    auto c = make ();
    auto dry = run (c, p, 1000.0f, 0.8f, 0.2f);
    CHECK (rms (dry.l) == Approx (0.8f * 0.7071f).epsilon (0.01));
    CHECK (rms (dry.r) == Approx (0.2f * 0.7071f).epsilon (0.01));

    p.mix = 100.0f; // stereo wet input is the L+R average on both sides
    auto c2 = make ();
    auto wet = run (c2, p, 1000.0f, 0.8f, 0.2f);
    CHECK (rms (wet.l) == Approx (0.5f * 0.7071f).epsilon (0.01));
    CHECK (rms (wet.r) == Approx (rms (wet.l)).epsilon (0.001));
}

TEST_CASE ("input level feeds the wet path only; output level scales everything")
{
    ChainParams p;
    p.mix = 100.0f;
    p.inLevelDb = -6.0206f;
    auto c = make ();
    CHECK (rms (run (c, p, 1000.0f, 0.5f, 0.5f).l) == Approx (0.25f * 0.7071f).epsilon (0.01));

    p = ChainParams{};
    p.mix = 0.0f;
    p.inLevelDb = -20.0f;
    auto c2 = make ();
    CHECK (rms (run (c2, p, 1000.0f, 0.5f, 0.5f).l) == Approx (0.5f * 0.7071f).epsilon (0.01)); // dry untouched

    p = ChainParams{};
    p.outLevelDb = -6.0206f;
    auto c3 = make ();
    CHECK (rms (run (c3, p, 1000.0f, 0.5f, 0.5f).l) == Approx (0.25f * 0.7071f).epsilon (0.01));
}

TEST_CASE ("wet input modes: mono is the left input, true stereo keeps the channels apart")
{
    ChainParams p;
    p.mix = 100.0f;
    p.wetInput = 0;
    auto c = make ();
    auto mono = run (c, p, 1000.0f, 0.8f, 0.0f);
    CHECK (rms (mono.l) == Approx (0.8f * 0.7071f).epsilon (0.01));
    CHECK (rms (mono.r) == Approx (rms (mono.l)).epsilon (0.001));

    p.wetInput = 2;
    auto c2 = make ();
    auto ts = run (c2, p, 1000.0f, 0.8f, 0.2f);
    CHECK (rms (ts.l) == Approx (0.8f * 0.7071f).epsilon (0.01));
    CHECK (rms (ts.r) == Approx (0.2f * 0.7071f).epsilon (0.01));
}

TEST_CASE ("wet solo ignores the mix, bypass returns the dry signal, mute silences")
{
    ChainParams p;
    p.mix = 0.0f;
    p.wetSolo = true;
    auto c = make ();
    CHECK (rms (run (c, p, 1000.0f, 0.8f, 0.2f).l) ==
           Approx (0.5f * 0.7071f).epsilon (0.01)); // the wet average, not the dry 0.8

    p = ChainParams{};
    p.mix = 100.0f;
    p.bypass = true;
    p.outLevelDb = -20.0f; // bypass is untouched by the output level
    auto c2 = make ();
    CHECK (rms (run (c2, p, 1000.0f, 0.8f, 0.2f).l) == Approx (0.8f * 0.7071f).epsilon (0.01));

    p = ChainParams{};
    p.mute = true;
    auto c3 = make ();
    CHECK (rms (run (c3, p, 1000.0f, 0.8f, 0.2f).l) < 1.0e-4f);
}

TEST_CASE ("wet R phase invert flips only the wet right channel")
{
    ChainParams p;
    p.mix = 100.0f;
    p.outPhaseR = true;
    auto c = make ();
    const auto out = run (c, p, 1000.0f, 0.5f, 0.5f);
    const size_t i = out.l.size () - 7;
    CHECK (out.r[i] == Approx (-out.l[i]).margin (1.0e-4));
}

TEST_CASE ("width 0 makes the wet output mono and width 200 widens it")
{
    ChainParams p;
    p.mix = 100.0f;
    p.wetInput = 2; // true stereo so there is something to narrow
    p.width = 0.0f;
    auto c = make ();
    const auto narrow = run (c, p, 1000.0f, 0.8f, 0.2f);
    const size_t i = narrow.l.size () - 11;
    CHECK (narrow.l[i] == Approx (narrow.r[i]).margin (0.01));

    p.width = 200.0f;
    auto c2 = make ();
    const auto wide = run (c2, p, 1000.0f, 0.8f, 0.2f);
    CHECK (std::abs (rms (wide.l) - rms (wide.r)) > std::abs (rms (narrow.l) - rms (narrow.r)));
}

TEST_CASE ("the ducker lowers the wet signal while the dry input is loud")
{
    ChainParams p;
    p.mix = 100.0f;
    p.duckOn = true;
    p.duckRangeDb = -20.0f;
    p.duckThreshDb = -30.0f;
    p.duckAttackMs = 1.0f;
    auto c = make ();
    CHECK (rms (run (c, p, 1000.0f, 0.5f, 0.5f).l) == Approx (0.05f * 0.7071f).epsilon (0.05));

    p.duckOn = false;
    auto c2 = make ();
    CHECK (rms (run (c2, p, 1000.0f, 0.5f, 0.5f).l) == Approx (0.5f * 0.7071f).epsilon (0.01));
}

TEST_CASE ("the analogue EQ boosts the wet path (the digital EQ is tested with the converter)")
{
    ChainParams p;
    p.mix = 100.0f;
    p.aeqOn = true;
    p.aeqMidF = 1000.0f;
    p.aeqMidG = 12.0f;
    auto c = make ();
    CHECK (rms (run (c, p, 1000.0f, 0.1f, 0.1f).l) == Approx (0.1f * 0.7071f * 3.98f).epsilon (0.05)); // +12 dB
}

TEST_CASE ("block sizes larger than prepared and of one sample both work")
{
    ChainParams p;
    p.mix = 100.0f;
    auto c = make ();
    std::vector<float> l (2000, 0.25f), r (2000, 0.25f);
    c.process (l.data (), r.data (), 2000, p); // larger than the prepared 480
    for (int i = 0; i < 100; ++i)
        c.process (l.data () + i, r.data () + i, 1, p);
    CHECK (std::isfinite (l[1999]));
}

TEST_CASE ("the FX slot feeds the reverb and is heard (FX > Rev), or follows it (Rev > FX)")
{
    // impulse through the full wet path (engine on), wet only
    auto impulse = [] (const ChainParams& p, double seconds)
    {
        Chain c;
        c.prepare (kSr, kBlock);
        const int total = (int) (kSr * seconds);
        std::vector<float> l ((size_t) total, 0.0f), r ((size_t) total, 0.0f);
        l[0] = r[0] = 1.0f;
        for (int pos = 0; pos < total; pos += kBlock)
            c.process (l.data () + pos, r.data () + pos, std::min (kBlock, total - pos), p);
        return l;
    };
    auto energy = [] (const std::vector<float>& x, double from, double to)
    {
        double e = 0.0;
        for (size_t i = (size_t) (from * kSr); i < (size_t) (to * kSr); ++i)
            e += (double) x[i] * x[i];
        return e;
    };

    ChainParams p;
    p.wetSolo = true;
    p.engine.rt = 0.5f;
    p.fx.type = yrev::dsp::fxtype::delay;
    p.fx.dlyLMs = p.fx.dlyRMs = 300.0f;
    p.fx.dlyLevel = 100.0f;
    p.fx.dlyFb = 0.0f;
    p.engine.preDelayMs = 0.1f; // the effect programs' printed initial delay: no offset

    auto off = p;
    off.fx.type = yrev::dsp::fxtype::off;
    const auto dry = impulse (off, 1.0);

    SECTION ("FX > Rev: the repeat is heard directly, not only through the reverb")
    {
        const auto x = impulse (p, 1.0);
        // the repeat at 300 ms (through the converter model, so band-limited): a clear peak, on time, since the
        // bridge delay comes off the repeat time as it does off the pre-delay
        float peak = 0.0f, before = 0.0f;
        size_t at = 0;
        for (size_t i = (size_t) (0.295 * kSr); i < (size_t) (0.305 * kSr); ++i)
            if (std::abs (x[i]) > peak)
            {
                peak = std::abs (x[i]);
                at = i;
            }
        for (size_t i = (size_t) (0.25 * kSr); i < (size_t) (0.29 * kSr); ++i)
            before = std::max (before, std::abs (x[i]));
        CHECK (peak > 0.2f);
        CHECK (peak > 4.0f * before);
        CHECK ((double) at / kSr == Approx (0.300).margin (0.0005));
        // and the reverb of the repeat adds energy after it
        CHECK (energy (x, 0.32, 0.6) > 1.5 * energy (dry, 0.32, 0.6));

        // INITIAL DELAY offsets the effect from the direct sound (REV5 OM p. 14): the repeat moves to 300 + 100 ms
        auto q = p;
        q.engine.preDelayMs = 100.1f;
        const auto y = impulse (q, 1.0);
        float pk = 0.0f;
        size_t atQ = 0;
        for (size_t i = (size_t) (0.39 * kSr); i < (size_t) (0.41 * kSr); ++i)
            if (std::abs (y[i]) > pk)
            {
                pk = std::abs (y[i]);
                atQ = i;
            }
        CHECK (pk > 0.2f);
        CHECK ((double) atQ / kSr == Approx (0.400).margin (0.0005));
    }
    SECTION ("Rev > FX: the whole reverb is repeated")
    {
        p.fxOrder = 1;
        const auto x = impulse (p, 1.0);
        // up to the repeat the output is the reverb alone
        CHECK (energy (x, 0.0, 0.29) == Approx (energy (dry, 0.0, 0.29)).epsilon (1.0e-3));
        CHECK (energy (x, 0.3, 0.6) > 1.5 * energy (dry, 0.3, 0.6));
    }
    SECTION ("the reported tail covers the repeats")
    {
        p.fx.era = 1; // delay feedback is REV5 only
        p.fx.dlyFb = p.fx.dlyFbR = 90.0f;
        CHECK (Chain::tailSeconds (p) > Chain::tailSeconds (off) + 1.0);
    }
}

TEST_CASE ("the REV5 gate cuts the reverb tail after hold and release")
{
    auto impulseTail = [] (const ChainParams& p)
    {
        Chain c;
        c.prepare (kSr, kBlock);
        const int total = (int) (kSr * 1.5);
        std::vector<float> l ((size_t) total, 0.0f), r ((size_t) total, 0.0f);
        for (int i = 0; i < 480; ++i) // a 10 ms burst to trigger the gate
            l[(size_t) i] = r[(size_t) i] = (i % 2 == 0 ? 0.5f : -0.5f);
        for (int pos = 0; pos < total; pos += kBlock)
            c.process (l.data () + pos, r.data () + pos, std::min (kBlock, total - pos), p);
        return l;
    };
    ChainParams p;
    p.era = 1;
    p.wetSolo = true;
    p.character = 0.0f; // no hiss, so silence is silence
    p.engine.rt = 4.0f;
    const auto open = impulseTail (p);
    p.gate.levelPct = 50.0f;
    p.gate.trigDelayMs = 0.1f;
    p.gate.holdMs = 200.0f;
    p.gate.releaseMs = 100.0f;
    const auto gated = impulseTail (p);
    double eOpen = 0.0, eGated = 0.0, eHeld = 0.0, eHeldOpen = 0.0;
    for (size_t i = (size_t) (0.5 * kSr); i < open.size (); ++i)
    {
        eOpen += (double) open[i] * open[i];
        eGated += (double) gated[i] * gated[i];
    }
    for (size_t i = (size_t) (0.05 * kSr); i < (size_t) (0.15 * kSr); ++i)
    {
        eHeld += (double) gated[i] * gated[i];
        eHeldOpen += (double) open[i] * open[i];
    }
    CHECK (eOpen > 1.0e-4);
    CHECK (eGated < 1.0e-12 * eOpen + 1.0e-20);         // closed after 10 ms + 200 ms hold + 100 ms release
    CHECK (eHeld == Approx (eHeldOpen).epsilon (0.01)); // open during the hold
}

TEST_CASE ("the REV5 GATE LVL noise gate sits on the input: tails ring on, quiet input is cut")
{
    auto burstTail = [] (const ChainParams& p, float amp)
    {
        Chain c;
        c.prepare (kSr, kBlock);
        const int total = (int) (kSr * 1.5);
        std::vector<float> l ((size_t) total, 0.0f), r ((size_t) total, 0.0f);
        for (int i = 0; i < 960; ++i) // a 20 ms burst at 1 kHz
            l[(size_t) i] = r[(size_t) i] = amp * std::sin (2.0f * 3.14159265f * 1000.0f * (float) i / (float) kSr);
        for (int pos = 0; pos < total; pos += kBlock)
            c.process (l.data () + pos, r.data () + pos, std::min (kBlock, total - pos), p);
        double e = 0.0;
        for (size_t i = (size_t) (0.5 * kSr); i < l.size (); ++i)
            e += (double) l[i] * l[i];
        return e;
    };
    ChainParams p;
    p.era = 1;
    p.wetSolo = true;
    p.character = 0.0f;
    p.engine.rt = 4.0f;
    const double open = burstTail (p, 0.5f);
    p.gate.levelPct = 50.0f;
    p.gate.holdMs = 50.0f;
    p.gate.releaseMs = 50.0f;
    p.gate.onInput = true;
    CHECK (burstTail (p, 0.5f) == Approx (open).epsilon (0.1)); // the tail rings past hold and release
    const double quietOpen = [&]
    {
        auto q = p;
        q.gate.levelPct = 0.0f;
        return burstTail (q, 0.001f);
    }();
    CHECK (quietOpen > 1.0e-12);
    CHECK (burstTail (p, 0.001f) < 1.0e-6 * quietOpen); // -60 dB never opens a 50 % gate
}

TEST_CASE ("the output safety limiter holds the ceiling; off, the level passes")
{
    ChainParams p;
    p.mix = 0.0f;         // dry only, so the level is exact
    p.outLevelDb = 12.0f; // a 0.5 tone becomes about 2.0 peak
    auto c = make ();
    const auto on = run (c, p, 440.0f, 0.5f, 0.5f);
    float pk = 0.0f;
    for (auto v : on.l)
        pk = std::max (pk, std::abs (v));
    CHECK (pk <= 0.989f + 1.0e-6f);
    p.limiter = false;
    auto c2 = make ();
    const auto off = run (c2, p, 440.0f, 0.5f, 0.5f);
    float pk2 = 0.0f;
    for (auto v : off.l)
        pk2 = std::max (pk2, std::abs (v));
    CHECK (pk2 > 1.9f);
}

namespace
{
    // Runs `in` (left = right) through the chain in blocks, calling `at (sample)` before each block so a test can
    // change the settings mid-stream. Returns the left output.
    template <typename Fn> std::vector<float> stream (Chain& c, std::vector<float> in, ChainParams& p, Fn&& at)
    {
        std::vector<float> r = in;
        for (int pos = 0; pos < (int) in.size (); pos += kBlock)
        {
            at (pos);
            const int n = std::min (kBlock, (int) in.size () - pos);
            c.process (in.data () + pos, r.data () + pos, n, p);
        }
        return in;
    }

    float rmsDb (const std::vector<float>& x, double from, double to)
    {
        double e = 0.0;
        const size_t a = (size_t) (from * kSr), b = (size_t) (to * kSr);
        for (size_t i = a; i < b; ++i)
            e += (double) x[i] * x[i];
        return (float) (10.0 * std::log10 (e / (double) (b - a) + 1.0e-30));
    }

    std::vector<float> burst (double seconds, double burstSeconds)
    {
        std::vector<float> x ((size_t) (kSr * seconds), 0.0f);
        uint32_t s = 1u;
        for (size_t i = 0; i < (size_t) (kSr * burstSeconds); ++i)
        {
            s = s * 1664525u + 1013904223u;
            x[i] = 0.25f * ((float) (s >> 8) / 8388608.0f - 1.0f);
        }
        return x;
    }

    ChainParams wetOnly ()
    {
        ChainParams p;
        p.wetSolo = true;
        p.character = 0.0f; // no hiss, so two runs can be compared sample for sample
        p.engine.rt = 3.0f;
        return p;
    }
} // namespace

TEST_CASE ("an era change crossfades the engine: the old tail rings out on its own settings")
{
    const auto in = burst (1.6, 0.3);
    auto p = wetOnly ();

    Chain still;
    still.prepare (kSr, kBlock);
    auto pa = p;
    const auto ref = stream (still, in, pa, [] (int) {});

    Chain c;
    c.prepare (kSr, kBlock);
    const int switchAt = (int) (0.5 * kSr) / kBlock * kBlock; // after the burst: the new engine hears silence
    auto pb = p;
    const auto out = stream (c, in, pb,
                             [&] (int pos)
                             {
                                 if (pos == switchAt)
                                     pb.era = 1;
                             });

    CHECK (c.switchCount () == 1);
    CHECK (c.ringingOut ()); // RT 3 s: still ringing a second later
    // before the switch the two runs agree; after it the tail goes on as if nothing had changed (not cut, not reset)
    const double s = (double) switchAt / kSr;
    CHECK (rmsDb (out, s - 0.2, s) == Approx (rmsDb (ref, s - 0.2, s)).margin (0.01));
    CHECK (rmsDb (out, s, s + 0.3) == Approx (rmsDb (ref, s, s + 0.3)).margin (0.2));
    CHECK (rmsDb (out, s + 0.6, s + 1.0) == Approx (rmsDb (ref, s + 0.6, s + 1.0)).margin (0.2));
}

TEST_CASE ("a switch under steady input has no gap or jump, and the old engine stops once silent")
{
    // steady noise into a short reverb; switch era, then a preset (program) change
    auto in = burst (4.0, 1.5);
    auto p = wetOnly ();
    p.engine.rt = 0.5f;
    Chain c;
    c.prepare (kSr, kBlock);
    const int first = (int) (0.6 * kSr) / kBlock * kBlock, second = (int) (1.0 * kSr) / kBlock * kBlock;
    bool ringingAfter = true;
    const auto out = stream (c, in, p,
                             [&] (int pos)
                             {
                                 if (pos == first)
                                     p.era = 1;
                                 if (pos == second)
                                     p.program = 7;
                                 if (pos == (int) in.size () - kBlock)
                                     ringingAfter = c.ringingOut ();
                             });
    CHECK (c.switchCount () == 2);
    CHECK_FALSE (ringingAfter); // RT 0.5 s: silent well within the 2.5 s after the burst

    // 10 ms windows around each switch stay within a few dB of the level before it
    for (int at : {first, second})
    {
        const double s = (double) at / kSr;
        const float before = rmsDb (out, s - 0.1, s);
        for (double t = s; t < s + 0.15; t += 0.01)
        {
            const float w = rmsDb (out, t, t + 0.01);
            CHECK (w < before + 3.0f);
            CHECK (w > before - 4.0f);
        }
    }

    // and nothing jumps sample to sample beyond what the signal itself does
    float maxStepBefore = 0.0f, maxStepAround = 0.0f;
    for (int i = first - 4800; i < first; ++i)
        maxStepBefore = std::max (maxStepBefore, std::abs (out[(size_t) i + 1] - out[(size_t) i]));
    for (int i = first; i < first + 4800; ++i)
        maxStepAround = std::max (maxStepAround, std::abs (out[(size_t) i + 1] - out[(size_t) i]));
    CHECK (maxStepAround < 1.5f * maxStepBefore);
}

TEST_CASE ("a switch while the old engine rings waits for it to fade, then the newest settings win")
{
    const auto in = burst (1.0, 0.2);
    auto p = wetOnly ();
    Chain c;
    c.prepare (kSr, kBlock);
    int countAt60 = -1;
    stream (c, in, p,
            [&] (int pos)
            {
                if (pos == 9600) // 0.2 s
                    p.era = 1;
                if (pos == 9600 + kBlock)
                    p.program = 1;
                if (pos == 9600 + 2 * kBlock)
                    p.program = 2; // replaces the waiting change
                if (pos == 9600 + 3 * kBlock)
                    countAt60 = c.switchCount ();
            });
    CHECK (countAt60 == 1);        // the second switch waited for the old engine (30 ms)
    CHECK (c.switchCount () == 2); // and then ran once, with the newest settings
}

TEST_CASE ("settings that are not era, eco or program apply in place, without a switch")
{
    const auto in = burst (0.5, 0.2);
    auto p = wetOnly ();
    Chain c;
    c.prepare (kSr, kBlock);
    stream (c, in, p,
            [&] (int pos)
            {
                if (pos == 4800)
                {
                    p.engine.rt = 1.0f;
                    p.engine.roomSize = 1.0f;
                    p.fx.type = yrev::dsp::fxtype::delay;
                }
            });
    CHECK (c.switchCount () == 0);
    CHECK_FALSE (c.ringingOut ());
}

TEST_CASE ("wet input True stereo reaches the engine: a left-only input leans left; Stereo does not")
{
    auto balance = [] (int wetInput)
    {
        auto p = wetOnly ();
        p.wetInput = wetInput;
        p.engine.erRevBalance = 100.0f; // reflections only
        p.engine.preDelayMs = 5.0f;
        Chain c;
        c.prepare (kSr, kBlock);
        std::vector<float> l ((size_t) (0.3 * kSr), 0.0f), r = l;
        l[0] = 1.0f;
        for (int pos = 0; pos < (int) l.size (); pos += kBlock)
            c.process (l.data () + pos, r.data () + pos, std::min (kBlock, (int) l.size () - pos), p);
        return rmsDb (l, 0.0, 0.25) - rmsDb (r, 0.0, 0.25);
    };
    const float stereo = balance (1), trueStereo = balance (2);
    CHECK (trueStereo - stereo > 6.0f);
}
