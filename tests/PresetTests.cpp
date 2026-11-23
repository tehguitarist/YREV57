#include "plugin/PluginProcessor.h"

#include <set>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using Catch::Approx;

namespace
{
    float value (YrevProcessor& p, const char* id)
    {
        return p.apvts.getRawParameterValue (id)->load ();
    }

    // first preset of that name, or the one in `bank` when given (names repeat across the read-only banks)
    int indexOf (YrevProcessor& p, const char* name, const char* bank = nullptr)
    {
        for (int i = 0; i < p.presets.size (); ++i)
            if (p.presets.at (i).name == name && (bank == nullptr || p.presets.at (i).bank == bank))
                return i;
        return -1;
    }

    juce::File tempDir ()
    {
        auto d =
            juce::File::getSpecialLocation (juce::File::tempDirectory)
                .getChildFile ("yrev57-preset-test-" + juce::String (juce::Random::getSystemRandom ().nextInt64 ()));
        d.createDirectory ();
        return d;
    }
} // namespace

TEST_CASE ("factory presets load and reset what they do not set")
{
    YrevProcessor p;
    p.presets.setUserDirectory (tempDir ());
    REQUIRE (p.presets.size () >= 15);

    const int spring = indexOf (p, "Spring Tank");
    REQUIRE (spring >= 0);
    p.presets.load (spring);
    CHECK (value (p, "hi_ratio") == Approx (0.3f).margin (0.01));
    CHECK (value (p, "er_mode") == Approx (5.0f)); // Spring
    CHECK (value (p, "fx_type") == Approx (5.0f)); // Off
    CHECK (p.presets.currentName () == "Spring Tank");
    CHECK (!p.presets.isEdited ());

    // A preset that leaves a control out puts it back to the default.
    p.apvts.getParameter ("dly_l")->setValueNotifyingHost (p.apvts.getParameter ("dly_l")->convertTo0to1 (500.0f));
    p.presets.load (spring);
    CHECK (value (p, "dly_l") == Approx (100.0f).margin (0.5));
}

TEST_CASE ("editing marks the preset edited, compare swaps back and forth, init resets")
{
    YrevProcessor p;
    p.presets.setUserDirectory (tempDir ());
    p.presets.load (indexOf (p, "Large Hall"));
    CHECK (!p.presets.isEdited ());

    p.apvts.getParameter ("rt")->setValueNotifyingHost (p.apvts.getParameter ("rt")->convertTo0to1 (5.0f));
    CHECK (p.presets.isEdited ());

    p.presets.toggleCompare ();
    CHECK (p.presets.comparing ());
    CHECK (value (p, "rt") == Approx (2.6f).margin (0.05));
    p.presets.toggleCompare ();
    CHECK (value (p, "rt") == Approx (5.0f).margin (0.05));

    p.presets.init ();
    CHECK (p.presets.current () == -1);
    CHECK (value (p, "rt") == Approx (2.6f).margin (0.05));
    CHECK (value (p, "era") == Approx (0.0f));
}

TEST_CASE ("user presets store, reload, rename and delete; factory names are protected")
{
    YrevProcessor p;
    p.presets.setUserDirectory (tempDir ());
    const int before = p.presets.size ();

    CHECK (!p.presets.storeAs ("Large Hall")); // factory name
    p.apvts.getParameter ("rt")->setValueNotifyingHost (p.apvts.getParameter ("rt")->convertTo0to1 (7.0f));
    REQUIRE (p.presets.storeAs ("My Room"));
    CHECK (p.presets.size () == before + 1);
    CHECK (p.presets.at (p.presets.current ()).user);

    p.presets.load (indexOf (p, "Large Hall"));
    CHECK (value (p, "rt") == Approx (2.6f).margin (0.05));
    p.presets.load (indexOf (p, "My Room"));
    CHECK (value (p, "rt") == Approx (7.0f).margin (0.1));

    REQUIRE (p.presets.rename (indexOf (p, "My Room"), "Renamed"));
    CHECK (indexOf (p, "My Room") == -1);
    CHECK (indexOf (p, "Renamed") >= 0);
    CHECK (!p.presets.remove (indexOf (p, "Large Hall"))); // read-only
    CHECK (p.presets.remove (indexOf (p, "Renamed")));
    CHECK (p.presets.size () == before);
}

TEST_CASE ("the current preset name survives a state round trip")
{
    YrevProcessor a;
    a.presets.setUserDirectory (tempDir ());
    a.presets.load (indexOf (a, "Vocal Plate"));
    juce::MemoryBlock block;
    a.getStateInformation (block);

    YrevProcessor b;
    b.presets.setUserDirectory (tempDir ());
    b.setStateInformation (block.getData (), (int) block.getSize ());
    CHECK (b.presets.currentName () == "Vocal Plate");
    CHECK (b.getCurrentProgram () == indexOf (b, "Vocal Plate"));
}

TEST_CASE ("presets are keyed by bank: a REV5 program survives a state round trip as the REV5 one")
{
    YrevProcessor a;
    a.presets.setUserDirectory (tempDir ());
    const int rev5 = indexOf (a, "Large Hall", "REV5");
    REQUIRE (rev5 >= 0);
    REQUIRE (indexOf (a, "Large Hall") != rev5); // FACTORY (and REV7) have a "Large Hall" too
    a.presets.load (rev5);
    juce::MemoryBlock block;
    a.getStateInformation (block);

    YrevProcessor b;
    b.presets.setUserDirectory (tempDir ());
    b.setStateInformation (block.getData (), (int) block.getSize ());
    CHECK (b.getCurrentProgram () == indexOf (b, "Large Hall", "REV5"));
}

TEST_CASE ("manual programs: bank sizes, names unique per bank, every value in range, every preset loads")
{
    YrevProcessor p;
    p.presets.setUserDirectory (tempDir ());

    int rev7 = 0, rev5 = 0;
    std::set<juce::String> names;
    for (int i = 0; i < p.presets.size (); ++i)
    {
        const auto& pr = p.presets.at (i);
        if (pr.user)
            continue;
        CHECK (names.insert (pr.bank + "/" + pr.name).second); // bank + name is the saved-state key
        rev7 += pr.bank == "REV7";
        rev5 += pr.bank == "REV5";

        for (const auto& [pid, v] : pr.values)
        {
            auto* prm = dynamic_cast<juce::RangedAudioParameter*> (p.apvts.getParameter (pid));
            INFO (pr.name << " / " << pid);
            REQUIRE (prm != nullptr); // no unknown ids
            CHECK (yrev::PresetManager::isPresetParam (pid));
            const auto r = prm->getNormalisableRange ().getRange ();
            CHECK (v >= r.getStart () - 1.0e-4f);
            CHECK (v <= r.getEnd () + 1.0e-4f);
        }

        if (pr.bank == "REV7" || pr.bank == "REV5")
        {
            const bool era5 = pr.bank == "REV5";
            CHECK (pr.values.at ("era") == Approx (era5 ? 1.0f : 0.0f));
            // load it and read every stored value back (the parameter snaps to its legal step)
            p.presets.load (i);
            CHECK (!p.presets.isEdited ());
            for (const auto& [pid, v] : pr.values)
            {
                auto* prm = dynamic_cast<juce::RangedAudioParameter*> (p.apvts.getParameter (pid));
                const float legal = prm->convertFrom0to1 (prm->convertTo0to1 (v));
                INFO (pr.name << " / " << pid);
                CHECK (value (p, pid.toRawUTF8 ()) ==
                       Approx (legal).margin (1.0e-3 * juce::jmax (1.0f, std::abs (legal))));
            }
        }
    }
    CHECK (rev7 == 30); // REV7 ROM chart, printed p. 27
    CHECK (rev5 == 39); // REV5 ROM chart: programs 1-30 and 91-99, printed pp. 56-94
    CHECK (p.presets.size () >= 15 + 30 + 39);

    // spot checks against the printed charts
    p.presets.load (indexOf (p, "Large Hall", "REV5"));
    CHECK (value (p, "wet_lpf") == Approx (6.3f));
    CHECK (value (p, "er_rev_balance") == Approx (54.0f)); // as printed (100 % = ER only, as on the REV5)
    p.presets.load (indexOf (p, "Reverb & Gate", "REV5"));
    CHECK (value (p, "gate_level") == Approx (65.0f));
    CHECK (value (p, "gate_hold") == Approx (150.0f));
    p.presets.load (indexOf (p, "Handclaps", "REV7"));
    CHECK (value (p, "lo_ratio") == Approx (2.0f));
    CHECK (value (p, "era") == Approx (0.0f));
}

TEST_CASE ("preset loads, compare and state restores bump the load sequence (whole-engine crossfade)")
{
    YrevProcessor p;
    p.presets.setUserDirectory (tempDir ());
    const auto s0 = p.presets.loadSequence ();
    CHECK ((s0 & 1u) == 0u);
    p.presets.load (indexOf (p, "Vocal Plate"));
    const auto s1 = p.presets.loadSequence ();
    CHECK (s1 == s0 + 2u); // odd while loading, even again after
    juce::MemoryBlock block;
    p.getStateInformation (block);
    p.setStateInformation (block.getData (), (int) block.getSize ());
    CHECK (p.presets.loadSequence () == s1 + 2u);
}

TEST_CASE ("a preset with a negative ER feedback gain loads it signed")
{
    YrevProcessor p;
    p.presets.setUserDirectory (tempDir ());
    auto* prm = p.apvts.getParameter ("er_fb_gain");
    prm->setValueNotifyingHost (prm->convertTo0to1 (-70.0f));
    REQUIRE (p.presets.storeAs ("Negative Feedback"));
    p.presets.init ();
    CHECK (value (p, "er_fb_gain") == Approx (0.0f));
    const int i = indexOf (p, "Negative Feedback");
    REQUIRE (i >= 0);
    p.presets.load (i);
    CHECK (value (p, "er_fb_gain") == Approx (-70.0f).margin (0.01));
}

TEST_CASE ("REV5 Rehearsal Room keeps its name and plays a charted stand-in for its printed PIANO mode")
{
    YrevProcessor p;
    p.presets.setUserDirectory (tempDir ());
    const int idx = indexOf (p, "Rehearsal Room", "REV5");
    REQUIRE (idx >= 0);
    p.presets.load (idx);
    CHECK (p.presets.currentName () == "Rehearsal Room");
    CHECK (value (p, "er_mode") == Approx (0.0f)); // Small Hall; the instrument modes are hidden (Q-01)
    CHECK (value (p, "era") == Approx (1.0f));
}

TEST_CASE ("hidden voice values are stored in user presets and load back")
{
    YrevProcessor p;
    p.presets.setUserDirectory (tempDir ());
    auto* prm = p.apvts.getParameter ("voice_loop_size");
    prm->setValueNotifyingHost (prm->convertTo0to1 (1.25f));
    auto* hx = p.apvts.getParameter ("voice_hi_xover");
    hx->setValueNotifyingHost (hx->convertTo0to1 (5000.0f));
    REQUIRE (p.presets.storeAs ("Bigger Loops"));
    p.presets.init ();
    CHECK (value (p, "voice_loop_size") == Approx (1.0f));
    CHECK (value (p, "voice_hi_xover") == Approx (8500.0f));
    p.presets.load (indexOf (p, "Bigger Loops"));
    CHECK (value (p, "voice_loop_size") == Approx (1.25f).margin (0.001));
    CHECK (value (p, "voice_hi_xover") == Approx (5000.0f).margin (1.0));
}

TEST_CASE ("manual reverb programs carry a family voice; reflection-only programs keep the defaults")
{
    YrevProcessor p;
    p.presets.setUserDirectory (tempDir ());
    p.presets.load (indexOf (p, "Vocal Plate", "REV7"));
    CHECK (value (p, "voice_loop_size") == Approx (0.8f).margin (0.001));
    CHECK (value (p, "voice_lo_xover") == Approx (380.0f).margin (0.5));
    CHECK (value (p, "voice_hi_xover") == Approx (8500.0f));  // per-program corners wait for the listening pass
    p.presets.load (indexOf (p, "Chorus & Rev & G", "REV5")); // a Large Hall based combination
    CHECK (value (p, "voice_loop_size") == Approx (1.1f).margin (0.001));
    p.presets.load (indexOf (p, "Kick", "REV5")); // reflections only: no late network to voice
    CHECK (value (p, "voice_loop_size") == Approx (1.0f));
    CHECK (value (p, "voice_er_gain") == Approx (0.0f));
}

TEST_CASE ("CLA Drum Room is in the hidden SECRET bank only")
{
    // reached by shift-clicking the era key that is not lit (YrevEditor); no tab, and Prev / Next skip it
    YrevProcessor p;
    int secret = 0;
    for (int i = 0; i < p.presets.size (); ++i)
        secret += p.presets.at (i).bank == "SECRET";
    CHECK (secret == 1);
    CHECK (indexOf (p, "CLA Drum Room", "SECRET") >= 0);
    CHECK (indexOf (p, "CLA Drum Room", "FACTORY") < 0);
}

TEST_CASE ("REV5 trigger-gate programs keep the gate on the effect output; GATE LVL programs gate the input")
{
    YrevProcessor p;
    p.presets.setUserDirectory (tempDir ());
    // program 23 prints both GATE LVL and TRG LEVEL: the trigger gate wins, so the reverb tail is cut (pp. 18, 78)
    for (const char* name : {"Reverb & Gate", "Echo & Rev & G", "PC & Rev & G"})
    {
        const int idx = indexOf (p, name, "REV5");
        REQUIRE (idx >= 0);
        p.presets.load (idx);
        CHECK (value (p, "gate_input") < 0.5f);
    }
    const int rg = indexOf (p, "Reverb & Gate", "REV5");
    p.presets.load (rg);
    CHECK (value (p, "gate_level") == Approx (65.0f));

    const int hall = indexOf (p, "Large Hall", "REV5"); // prints GATE LVL 0 % on INT PARAM: the input noise gate
    REQUIRE (hall >= 0);
    p.presets.load (hall);
    CHECK (value (p, "gate_input") >= 0.5f);
}

TEST_CASE ("both Reverb Flange programs are the reverb engine with a 30 % flange before it, not a decaying comb")
{
    YrevProcessor p;
    p.presets.setUserDirectory (tempDir ());
    for (const char* bank : {"REV7", "REV5"})
    {
        const int idx = indexOf (p, "Reverb Flange", bank);
        REQUIRE (idx >= 0);
        p.presets.load (idx);
        CHECK (value (p, "rt") == Approx (2.5f));
        CHECK (value (p, "fx_type") == Approx (2.0f));  // mod
        CHECK (value (p, "mod_type") == Approx (0.0f)); // the plain flange, not the reverb-flange comb
        CHECK (value (p, "mod_fb") == Approx (30.0f));
        CHECK (value (p, "mod_delay") == Approx (1.2f).margin (0.01));
        CHECK (value (p, "er_mode") == Approx (12.0f)); // Rev Hall taps
    }
}

TEST_CASE ("REV5 Stereo Echo keeps its printed times with equal feedback on both sides")
{
    YrevProcessor p;
    p.presets.setUserDirectory (tempDir ());
    const int idx = indexOf (p, "Stereo Echo", "REV5");
    REQUIRE (idx >= 0);
    p.presets.load (idx);
    CHECK (value (p, "echo_l") == Approx (170.0f));
    CHECK (value (p, "echo_r") == Approx (178.0f));
    CHECK (value (p, "echo_fb") == Approx (59.0f)); // printed 60 / 58: the mean, so the repeats do not drift left
    CHECK (value (p, "echo_fb_r") == Approx (59.0f));
}

TEST_CASE ("FX routing: the hardware bypass with REV MIX, and FX wet with no bypass")
{
    // wet only (mix 100), REV5 Large Hall with chorus A before the reverb: energy during a noise burst and after it
    const double sr = 48000.0;
    const int bs = 256, n = (int) (3.0 * sr);
    juce::Random rng (3);
    std::vector<float> burst ((size_t) n, 0.0f);
    for (int i = (int) (0.2 * sr); i < (int) (1.0 * sr); ++i)
        burst[(size_t) i] = 0.25f * (rng.nextFloat () * 2.0f - 1.0f);
    struct E
    {
        double during, tail;
    };
    const auto run = [&] (float fxType, float revMix, bool wet, float order = 0.0f)
    {
        YrevProcessor p;
        p.presets.setUserDirectory (tempDir ());
        p.presets.load (indexOf (p, "Large Hall", "REV5"));
        const auto set = [&] (const char* id, float v)
        { p.apvts.getParameter (id)->setValueNotifyingHost (p.apvts.getParameter (id)->convertTo0to1 (v)); };
        set ("mix", 100.0f);
        set ("fx_type", fxType);
        set ("mod_type", 2.0f); // chorus A
        set ("am_depth", 0.0f); // (its AM lowers the average level by design; keep it out of the routing check)
        set ("fx_order", order);
        set ("rev_mix", revMix);
        set ("fx_wet", wet ? 1.0f : 0.0f);
        p.setPlayConfigDetails (2, 2, sr, bs);
        p.prepareToPlay (sr, bs);
        juce::AudioBuffer<float> b (2, bs);
        juce::MidiBuffer m;
        E e{0.0, 0.0};
        for (int s = 0; s < n; s += bs)
        {
            for (int i = 0; i < bs; ++i)
                b.setSample (0, i, burst[(size_t) (s + i)]), b.setSample (1, i, burst[(size_t) (s + i)]);
            p.processBlock (b, m);
            for (int i = 0; i < bs; ++i)
            {
                const double v = (double) b.getSample (0, i) * b.getSample (0, i);
                const int t = s + i;
                if (t > (int) (0.4 * sr) && t < (int) (1.0 * sr))
                    e.during += v;
                else if (t > (int) (1.3 * sr) && t < (int) (2.5 * sr))
                    e.tail += v;
            }
        }
        return e;
    };
    const auto off = run (5.0f, 100.0f, false);
    const auto hw0 = run (2.0f, 0.0f, false);   // effect only: no reverb tail
    const auto hw45 = run (2.0f, 45.0f, false); // the combinations' printed REV MIX
    const auto hw100 = run (2.0f, 100.0f, false);
    const auto wet = run (2.0f, 100.0f, true); // reverb of the chorus, nothing beside it
    CHECK (hw0.tail < 1e-4 * off.tail);
    CHECK (hw45.tail / hw100.tail == Approx (0.45 * 0.45).epsilon (0.1));
    CHECK (hw100.during > 4.0 * off.during); // the chorus beside the reverb is far louder than the reverb
    CHECK (wet.during == Approx (off.during).epsilon (0.4)); // fully wet: about the reverb's own level
    // Rev > FX through the bypass hears the dry too; FX wet hears only the reverb
    const auto rfHw = run (2.0f, 100.0f, false, 1.0f);
    const auto rfWet = run (2.0f, 100.0f, true, 1.0f);
    CHECK (rfHw.during > 4.0 * rfWet.during);
    CHECK (rfWet.during == Approx (off.during).epsilon (0.4));
}

TEST_CASE ("effect-only programs keep their 1st reflections (REV MIX 0 scales only the reverb)")
{
    // both manuals give every program the 1st reflections (REV7 OM p. 18, REV5 p. 14): a centre tap at 50 ms on the
    // REV5 Delay L,R program at 100 % wet
    YrevProcessor p;
    p.presets.setUserDirectory (tempDir ());
    p.presets.load (indexOf (p, "Delay L,R", "REV5"));
    const auto set = [&] (const char* id, float v)
    { p.apvts.getParameter (id)->setValueNotifyingHost (p.apvts.getParameter (id)->convertTo0to1 (v)); };
    set ("mix", 100.0f);
    set ("ref1_delay", 50.0f);
    set ("ref1_level", 100.0f);
    const double sr = 48000.0;
    const int bs = 256;
    p.setPlayConfigDetails (2, 2, sr, bs);
    p.prepareToPlay (sr, bs);
    juce::AudioBuffer<float> b (2, bs);
    juce::MidiBuffer m;
    float tap = 0.0f, before = 0.0f;
    for (int s = 0; s < (int) (0.2 * sr); s += bs)
    {
        b.clear ();
        if (s == 0)
            b.setSample (0, 0, 0.5f), b.setSample (1, 0, 0.5f);
        p.processBlock (b, m);
        for (int i = 0; i < bs; ++i)
        {
            const float v = std::abs (b.getSample (0, i));
            const int t = s + i;
            if (t > (int) (0.045 * sr) && t < (int) (0.055 * sr))
                tap = std::max (tap, v);
            else if (t > (int) (0.03 * sr) && t < (int) (0.044 * sr))
                before = std::max (before, v);
        }
    }
    CHECK (tap > 0.05f);
    CHECK (tap > 10.0f * before);
}
