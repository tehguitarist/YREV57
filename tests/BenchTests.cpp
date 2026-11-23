// CPU benchmark and reference renders (hidden: run with `YREV57Tests "[.bench]"`). Not part of the test count.
//
//   YREV57Tests "[.bench]"                      real-time factor per preset, host rate and block size
//   YREV_BENCH_OUT=/tmp/dir YREV57Tests "[.bench]"   also writes each case's output as raw float32 stereo
//   YREV_BENCH_RUNS=n                           timed runs per case (median reported; default 5)
//   YREV_BENCH_FILTER=text                      only the cases whose label contains text
//   YREV_BENCH_RATES=44100,88200 YREV_BENCH_BLOCKS=1   other host rates and block sizes than the defaults
//
// The input is deterministic (seeded noise bursts with silences, so tails, gates and noise gates all run) and the
// processor's noise seed is frozen, so two builds' outputs can be compared sample for sample.
#include "plugin/PluginProcessor.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

namespace
{
    // ---- Config ----
    constexpr double kBenchSeconds = 20.0;
    constexpr int kDefaultRuns = 5;
    constexpr double kBurstSeconds = 0.5, kGapSeconds = 1.5; // noise burst, then a silence for the tail
    constexpr float kInputLevel = 0.25f;

    struct Case
    {
        const char* label;
        const char* preset;
        const char* bank;
        bool eco;
    };
    const Case kCases[] = {
        {"r7-large-hall", "Large Hall", "REV7", false},      {"r5-large-hall-rev2", "Large Hall", "REV5", false},
        {"r5-echo-rev", "Echo & Rev & G", "REV5", false},    {"r5-chorus-rev", "Chorus & Rev & G", "REV5", false},
        {"r5-pitch-rev", "PC & Rev & G", "REV5", false},     {"r7-early-ref1", "Early Reflection 1", "REV7", false},
        {"cla-drum-room", "CLA Drum Room", "SECRET", false}, {"r7-large-hall-eco", "Large Hall", "REV7", true},
    };
    const std::vector<double> kRates = {48000.0, 96000.0};
    const std::vector<int> kBlocks = {256, 32};

    // a comma-separated list from the environment, or the defaults
    template <typename T> std::vector<T> listFromEnv (const char* name, const std::vector<T>& defaults)
    {
        const char* v = std::getenv (name);
        if (v == nullptr)
            return defaults;
        std::vector<T> out;
        for (const char* c = v; *c != 0;)
        {
            char* end = nullptr;
            const double x = std::strtod (c, &end);
            if (end == c)
                break;
            out.push_back ((T) x);
            c = *end == ',' ? end + 1 : end;
        }
        return out.empty () ? defaults : out;
    }

    int presetIndex (YrevProcessor& p, const char* name, const char* bank)
    {
        for (int i = 0; i < p.presets.size (); ++i)
            if (p.presets.at (i).name == name && p.presets.at (i).bank == bank)
                return i;
        return -1;
    }

    // deterministic input: pink-ish noise bursts (one-pole tilted white noise) separated by silences
    std::vector<float> makeInput (double fs, int channel)
    {
        const auto n = (size_t) (kBenchSeconds * fs);
        std::vector<float> x (n, 0.0f);
        uint32_t s = 0x9e3779b9u + (uint32_t) channel * 7919u;
        float lp = 0.0f;
        const auto period = (size_t) ((kBurstSeconds + kGapSeconds) * fs), burst = (size_t) (kBurstSeconds * fs);
        for (size_t i = 0; i < n; ++i)
        {
            s ^= s << 13;
            s ^= s >> 17;
            s ^= s << 5;
            const float w = (float) ((double) s / 2147483648.0 - 1.0);
            lp += 0.1f * (w - lp);
            x[i] = (i % period) < burst ? kInputLevel * (0.6f * w + 2.0f * lp) : 0.0f;
        }
        return x;
    }

    // processes the whole input once; returns seconds of wall time; out gets the output when non-null
    double runOnce (const Case& c, double fs, int block, const std::vector<float>& inL, const std::vector<float>& inR,
                    std::vector<float>* out)
    {
        YrevProcessor p;
        auto& st = p.apvts.state;
        st.setProperty ("noiseSeed", "frozen", nullptr);
        st.setProperty ("eraMemory", "shared", nullptr);
        st.setProperty ("quality", c.eco ? "eco" : "native", nullptr);
        const int idx = presetIndex (p, c.preset, c.bank);
        REQUIRE (idx >= 0);
        p.presets.load (idx);
        p.setPlayConfigDetails (2, 2, fs, block);
        p.prepareToPlay (fs, block);
        juce::AudioBuffer<float> buf (2, block);
        juce::MidiBuffer midi;
        const auto n = inL.size ();
        if (out != nullptr)
            out->assign (2 * n, 0.0f);
        double total = 0.0;
        for (size_t pos = 0; pos < n; pos += (size_t) block)
        {
            const int len = (int) std::min<size_t> ((size_t) block, n - pos);
            buf.setSize (2, len, false, false, true);
            buf.copyFrom (0, 0, inL.data () + pos, len);
            buf.copyFrom (1, 0, inR.data () + pos, len);
            const auto t0 = std::chrono::steady_clock::now ();
            p.processBlock (buf, midi);
            total += std::chrono::duration<double> (std::chrono::steady_clock::now () - t0).count ();
            if (out != nullptr)
                for (int i = 0; i < len; ++i)
                {
                    (*out)[2 * (pos + (size_t) i)] = buf.getSample (0, i);
                    (*out)[2 * (pos + (size_t) i) + 1] = buf.getSample (1, i);
                }
        }
        return total;
    }
} // namespace

TEST_CASE ("CPU benchmark and reference renders", "[.bench]")
{
    const char* outDir = std::getenv ("YREV_BENCH_OUT");
    const char* filter = std::getenv ("YREV_BENCH_FILTER");
    const int runs = std::getenv ("YREV_BENCH_RUNS") != nullptr
                         ? std::max (1, std::atoi (std::getenv ("YREV_BENCH_RUNS")))
                         : kDefaultRuns;
    std::printf ("%-22s %6s %5s %9s %9s\n", "case", "rate", "block", "%core", "min%core");
    for (const auto& c : kCases)
        for (double fs : listFromEnv ("YREV_BENCH_RATES", kRates))
        {
            const auto inL = makeInput (fs, 0), inR = makeInput (fs, 1);
            for (int block : listFromEnv ("YREV_BENCH_BLOCKS", kBlocks))
            {
                char label[96];
                std::snprintf (label, sizeof (label), "%s-%gk-b%d", c.label, fs / 1000.0, block);
                if (filter != nullptr && std::string (label).find (filter) == std::string::npos)
                    continue;
                std::vector<double> t;
                std::vector<float> rendered;
                for (int r = 0; r < runs; ++r)
                    t.push_back (runOnce (c, fs, block, inL, inR, r == 0 && outDir != nullptr ? &rendered : nullptr));
                std::sort (t.begin (), t.end ());
                const double median = t[t.size () / 2];
                std::printf ("%-22s %6.0f %5d %8.2f%% %8.2f%%\n", c.label, fs, block, 100.0 * median / kBenchSeconds,
                             100.0 * t.front () / kBenchSeconds);
                std::fflush (stdout);
                if (outDir != nullptr && !rendered.empty ())
                {
                    const std::string path = std::string (outDir) + "/" + label + ".f32";
                    if (auto* f = std::fopen (path.c_str (), "wb"))
                    {
                        std::fwrite (rendered.data (), sizeof (float), rendered.size (), f);
                        std::fclose (f);
                    }
                }
            }
        }
}
