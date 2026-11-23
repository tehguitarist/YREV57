#include "dsp/Gate.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <utility>
#include <vector>

namespace
{
    constexpr double kRate = 44100.0;

    float dbToGain (float db)
    {
        return std::pow (10.0f, db / 20.0f);
    }
    int ms (float t, double rate = kRate)
    {
        return static_cast<int> (std::lround (t * 0.001 * rate));
    }

    yrev::dsp::Gate makeGate (float level, float dly, float hold, float rel, double rate = kRate)
    {
        yrev::dsp::Gate g;
        g.prepare (48000.0);
        g.setSampleRate (rate);
        g.setParams ({level, dly, hold, rel});
        return g;
    }

    // Key: `burst` samples of `amp` starting at sample `start`, silence elsewhere. Returns the gain sequence.
    std::vector<float> run (yrev::dsp::Gate& g, int total, int start, int burst, float amp = 0.5f)
    {
        std::vector<float> out (static_cast<size_t> (total));
        for (int i = 0; i < total; ++i)
            out[static_cast<size_t> (i)] = g.process ((i >= start && i < start + burst) ? amp : 0.0f);
        return out;
    }
} // namespace

TEST_CASE ("Gate off is exactly gain 1")
{
    auto g = makeGate (0.0f, 10.0f, 100.0f, 100.0f);
    REQUIRE_FALSE (g.enabled ());
    for (int i = 0; i < 1000; ++i)
        REQUIRE (g.process (i % 7 == 0 ? 0.9f : 0.0f) == 1.0f);
}

TEST_CASE ("Gate opens after the trigger delay and not before")
{
    auto g = makeGate (50.0f, 10.0f, 200.0f, 100.0f);
    const int start = 100, delay = ms (10.0f);
    auto out = run (g, 5000, start, 10);
    for (int i = 0; i < start + delay; ++i)
        REQUIRE (out[static_cast<size_t> (i)] == 0.0f);
    REQUIRE (out[static_cast<size_t> (start + delay)] > 0.0f);
    // Fully open within the 1 ms attack
    REQUIRE (out[static_cast<size_t> (start + delay + ms (1.0f))] == Catch::Approx (1.0f));
}

TEST_CASE ("Gate holds at 1 for the hold time then falls 60 dB over the release, then to exactly 0")
{
    auto g = makeGate (50.0f, 10.0f, 50.0f, 40.0f);
    const int start = 100, burst = 1, delay = ms (10.0f), hold = ms (50.0f), rel = ms (40.0f);
    const int last = start + burst - 1 + delay; // delayed last above-threshold sample
    auto out = run (g, 8000, start, burst);
    for (int i = last + ms (1.0f) + 1; i <= last + hold; ++i)
        REQUIRE (out[static_cast<size_t> (i)] == Catch::Approx (1.0f));
    REQUIRE (out[static_cast<size_t> (last + hold + 1)] < 1.0f);
    // exponential: -60 dB at the release time, -120 dB (then exactly 0) at twice it
    REQUIRE (out[static_cast<size_t> (last + hold + rel / 2)] == Catch::Approx (0.0316f).epsilon (0.02));
    REQUIRE (out[static_cast<size_t> (last + hold + rel)] == Catch::Approx (0.001f).epsilon (0.02));
    REQUIRE (out[static_cast<size_t> (last + hold + 2 * rel + 2)] == 0.0f);
    for (int i = last + hold + 1; i < last + hold + 2 * rel; ++i)
        REQUIRE (out[static_cast<size_t> (i + 1)] <= out[static_cast<size_t> (i)]);
}

TEST_CASE ("A key below the threshold never opens the gate; higher levels need louder keys")
{
    {
        auto g = makeGate (50.0f, 1.0f, 100.0f, 100.0f);
        auto out = run (g, 4000, 10, 2000, dbToGain (-45.0f)); // 50 % is about -29 dBFS
        for (float v : out)
            REQUIRE (v == 0.0f);
    }
    {
        auto g = makeGate (1.0f, 1.0f, 100.0f, 100.0f);
        auto out = run (g, 4000, 10, 100, dbToGain (-50.0f));
        REQUIRE (out[500] > 0.0f);
    }
    {
        auto g = makeGate (99.0f, 1.0f, 100.0f, 100.0f);
        auto out = run (g, 4000, 10, 2000, dbToGain (-6.0f));
        for (float v : out)
            REQUIRE (v == 0.0f);
    }
    {
        auto g = makeGate (99.0f, 1.0f, 100.0f, 100.0f);
        auto out = run (g, 4000, 10, 100, 1.0f);
        REQUIRE (out[500] > 0.0f);
    }
}

TEST_CASE ("Trigger level spans -53 to -5 dBFS, linear in dB (the measured SPX90 model)")
{
    const auto opens = [] (float pct, float db)
    {
        auto g = makeGate (pct, 1.0f, 100.0f, 100.0f);
        auto out = run (g, 4000, 10, 100, dbToGain (db));
        return out[500] > 0.0f;
    };
    for (const auto& [pct, db] : {std::pair{1.0f, -53.0f}, {50.0f, -29.24f}, {100.0f, -5.0f}})
    {
        CHECK (opens (pct, db + 0.5f));
        CHECK_FALSE (opens (pct, db - 0.5f));
    }
}

TEST_CASE ("Retriggering during the hold extends it")
{
    const int hold = ms (50.0f), delay = ms (5.0f);
    auto a = makeGate (50.0f, 5.0f, 50.0f, 20.0f);
    auto b = makeGate (50.0f, 5.0f, 50.0f, 20.0f);
    const int second = 100 + hold / 2;
    std::vector<float> ga, gb;
    for (int i = 0; i < 6000; ++i)
    {
        ga.push_back (a.process (i == 100 ? 0.5f : 0.0f));
        gb.push_back (b.process ((i == 100 || i == second) ? 0.5f : 0.0f));
    }
    const int probe = 100 + delay + hold + 5; // single burst has started releasing, the retriggered one has not
    REQUIRE (ga[static_cast<size_t> (probe)] < 1.0f);
    REQUIRE (gb[static_cast<size_t> (probe)] == Catch::Approx (1.0f));
    REQUIRE (gb[static_cast<size_t> (second + delay + hold + ms (40.0f) + 2)] == 0.0f);
}

TEST_CASE ("Gate follows a sample-rate switch with times in milliseconds")
{
    yrev::dsp::Gate g;
    g.prepare (44100.0);
    g.setParams ({50.0f, 10.0f, 20.0f, 10.0f});
    for (double rate : {31250.0, 44100.0, 31250.0})
    {
        g.setSampleRate (rate);
        g.reset ();
        const int delay = ms (10.0f, rate), hold = ms (20.0f, rate), rel = ms (10.0f, rate);
        std::vector<float> out;
        for (int i = 0; i < 4000; ++i)
            out.push_back (g.process (i == 0 ? 0.5f : 0.0f));
        REQUIRE (out[static_cast<size_t> (delay - 1)] == 0.0f);
        REQUIRE (out[static_cast<size_t> (delay)] > 0.0f);
        REQUIRE (out[static_cast<size_t> (delay + hold + rel)] == Catch::Approx (0.001f).epsilon (0.05));
        REQUIRE (out[static_cast<size_t> (delay + hold + 2 * rel + 2)] == 0.0f);
    }
}

TEST_CASE ("Per-sample use is deterministic after reset")
{
    auto g = makeGate (30.0f, 3.0f, 30.0f, 30.0f);
    auto first = run (g, 3000, 50, 20);
    g.reset ();
    auto second = run (g, 3000, 50, 20);
    REQUIRE (first == second);
}
