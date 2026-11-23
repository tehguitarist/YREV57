#include "plugin/PluginProcessor.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <set>

using Catch::Approx;

namespace
{
    // The saved-state / automation contract: these IDs must never change once released (see CLAUDE.md).
    const char* const kFrozenIds[] = {"era",
                                      "wet_input",
                                      "in_level",
                                      "mix",
                                      "out_phase_r",
                                      "bypass",
                                      "character",
                                      "dither",
                                      "aeq_on",
                                      "aeq_lo_f",
                                      "aeq_lo_g",
                                      "aeq_mid_f",
                                      "aeq_mid_g",
                                      "aeq_hi_f",
                                      "aeq_hi_g",
                                      "width",
                                      "wet_solo",
                                      "duck_on",
                                      "duck_range",
                                      "duck_thresh",
                                      "duck_attack",
                                      "duck_release",
                                      "out_level",
                                      "mute",
                                      "rt",
                                      "hi_ratio",
                                      "lo_ratio",
                                      "pre_delay",
                                      "diffusion",
                                      "ref1_delay",
                                      "ref1_level",
                                      "er_rev_balance",
                                      "density",
                                      "wet_lpf",
                                      "space_mod",
                                      "rev2_time",
                                      "rev2_delay",
                                      "rev2_level",
                                      "gate_level",
                                      "gate_trig_delay",
                                      "gate_hold",
                                      "gate_release",
                                      "er_mode",
                                      "liveness",
                                      "room_size",
                                      "er_count",
                                      "er_fb_gain",
                                      "er_fb_delay",
                                      "er_fb_hi",
                                      "fx_type",
                                      "fx_order",
                                      "fx_link",
                                      "fx_sync",
                                      "fx_lpf",
                                      "dly_l",
                                      "dly_r",
                                      "dly_level",
                                      "dly_fb",
                                      "echo_l",
                                      "echo_r",
                                      "echo_fb",
                                      "echo_damp",
                                      "echo_level",
                                      "mod_type",
                                      "lfo_rate",
                                      "mod_depth",
                                      "dm_depth",
                                      "am_depth",
                                      "mod_delay",
                                      "mod_fb",
                                      "mod_rt",
                                      "mod_lpf",
                                      "pitch_sel",
                                      "pitch_shift",
                                      "pitch_fine",
                                      "pitch_delay",
                                      "pitch_fb",
                                      "pan_dir",
                                      "pan_rate",
                                      "pan_depth",
                                      "eq_view",
                                      "deq_on",
                                      "deq_lo_f",
                                      "deq_lo_g",
                                      "deq_lo_q",
                                      "deq_lo_shelf",
                                      "deq_mid_f",
                                      "deq_mid_g",
                                      "deq_mid_q",
                                      "deq_hi_f",
                                      "deq_hi_g",
                                      "deq_hi_q",
                                      "deq_hi_shelf",
                                      "dly_fb_r",
                                      "dly_high",
                                      "echo_fb_r",
                                      "pitch2_shift",
                                      "pitch2_fine",
                                      "pitch2_delay",
                                      "ref1_l_delay",
                                      "ref1_l_level",
                                      "ref1_r_delay",
                                      "ref1_r_level",
                                      "voice_loop_size",
                                      "voice_diff_size",
                                      "voice_lo_xover",
                                      "voice_er_gain",
                                      "gate_input",
                                      "voice_hi_xover",
                                      "er_type",
                                      "voice_late_onset",
                                      "voice_er_tilt",
                                      "voice_late_tilt",
                                      "rev_mix",
                                      "fx_wet",
                                      "voice_rev2_hi",
                                      "voice_rev2_gain"};
} // namespace

TEST_CASE ("every frozen parameter ID exists and IDs are unique")
{
    YrevProcessor proc;
    std::set<juce::String> ids;
    for (auto* p : proc.getParameters ())
    {
        auto* withId = dynamic_cast<juce::AudioProcessorParameterWithID*> (p);
        REQUIRE (withId != nullptr);
        CHECK (ids.insert (withId->paramID).second);
    }
    for (auto* id : kFrozenIds)
        CHECK (ids.count (id) == 1);
}

TEST_CASE ("the era switch is 7 (1985) first, then 5 (1987)")
{
    YrevProcessor proc;
    auto* era = dynamic_cast<juce::AudioParameterChoice*> (proc.apvts.getParameter (yrev::id::era));
    REQUIRE (era != nullptr);
    REQUIRE (era->choices.size () == 2);
    CHECK (era->choices[0] == "7 (1985)");
    CHECK (era->choices[1] == "5 (1987)");
    CHECK (era->getIndex () == 0);
}

TEST_CASE ("wet input has mono, stereo and true stereo, defaulting to the authentic stereo sum")
{
    YrevProcessor proc;
    auto* wet = dynamic_cast<juce::AudioParameterChoice*> (proc.apvts.getParameter (yrev::id::wetInput));
    REQUIRE (wet != nullptr);
    REQUIRE (wet->choices.size () == 3);
    CHECK (wet->choices[0] == "Mono");
    CHECK (wet->choices[1] == "Stereo");
    CHECK (wet->choices[2] == "True Stereo");
    CHECK (wet->getIndex () == 1);
}

TEST_CASE ("defaults follow docs/design/parameters.md")
{
    YrevProcessor proc;
    auto value = [&] (const char* id) { return proc.apvts.getRawParameterValue (id)->load (); };
    CHECK (value (yrev::id::mix) == Approx (50.0f).margin (0.01));
    CHECK (value (yrev::id::inLevel) == Approx (0.0f).margin (0.01));
    CHECK (value (yrev::id::character) == Approx (100.0f).margin (0.01));
    CHECK (value (yrev::id::aeqOn) == Approx (0.0f).margin (0.01));
    CHECK (value (yrev::id::dither) == Approx (1.0f).margin (0.01));
    CHECK (value (yrev::id::aeqLoF) == Approx (200.0f).margin (0.01));
    CHECK (value (yrev::id::aeqMidF) == Approx (1000.0f).margin (0.01));
    CHECK (value (yrev::id::aeqHiF) == Approx (8000.0f).margin (0.01));
    CHECK (value (yrev::id::width) == Approx (100.0f).margin (0.01));
    CHECK (value (yrev::id::duckOn) == 0.0f);
    CHECK (value (yrev::id::wetSolo) == 0.0f);
    CHECK (value (yrev::id::duckRange) == Approx (-10.0f).margin (0.01));
    CHECK (value (yrev::id::duckThresh) == Approx (-20.0f).margin (0.01));
}

TEST_CASE ("panel controls default to the prototype's values")
{
    YrevProcessor proc;
    auto value = [&] (const char* id) { return proc.apvts.getRawParameterValue (id)->load (); };
    CHECK (value ("rt") == Approx (2.6f).margin (0.01));
    CHECK (value ("pre_delay") == Approx (30.0f).margin (0.05));
    CHECK (value ("er_mode") == Approx (1.0f));  // Large Hall
    CHECK (value ("fx_type") == Approx (0.0f));  // Delay
    CHECK (value ("mod_type") == Approx (2.0f)); // Chorus A
    CHECK (value ("dly_r") == Approx (200.0f).margin (0.5));
    CHECK (value ("wet_lpf") == Approx (17.0f)); // thru
}

TEST_CASE ("reverb and FX LPFs snap to the hardware-style steps")
{
    for (const char* pid : {"wet_lpf", "fx_lpf", "mod_lpf"})
    {
        YrevProcessor proc;
        auto* p = dynamic_cast<juce::RangedAudioParameter*> (proc.apvts.getParameter (pid));
        REQUIRE (p != nullptr);
        auto setTo = [&] (float khz)
        {
            p->setValueNotifyingHost (p->convertTo0to1 (khz));
            return proc.apvts.getRawParameterValue (pid)->load ();
        };
        CHECK (setTo (6.3f) == Approx (6.3f));
        CHECK (setTo (6.5f) == Approx (6.3f));
        CHECK (setTo (3.6f) == Approx (3.6f));
        CHECK (setTo (7.0f) == Approx (7.0f));
        CHECK (setTo (17.0f) == Approx (17.0f)); // thru
        CHECK (setTo (1.0f) == Approx (1.0f));
    }
}

TEST_CASE ("ER mode lists the fifteen modes, Rev Hall, Rev Plate and Rev Room last")
{
    YrevProcessor proc;
    auto* er = dynamic_cast<juce::AudioParameterChoice*> (proc.apvts.getParameter ("er_mode"));
    REQUIRE (er != nullptr);
    CHECK (er->choices.size () == 15);
    CHECK (er->choices[0] == "Small Hall");
    CHECK (er->choices[11] == "Guitar");
    CHECK (er->choices[12] == "Rev Hall");
    CHECK (er->choices[13] == "Rev Plate");
    CHECK (er->choices[14] == "Rev Room");
    CHECK (yrev::erModeEra7 (12) == 12); // the Rev modes play in both eras
    CHECK (yrev::erModeEra7 (14) == 14);
    CHECK (yrev::erModeEra7 (7) == 0);
    CHECK (yrev::erModeEra7 (3) == 3);
}

TEST_CASE ("hardware ranges: echo high damp, mid EQ, reverb flange (Q-13, Q-26, Q-07)")
{
    YrevProcessor proc;
    auto range = [&] (const char* id) { return proc.apvts.getParameterRange (id); };
    CHECK (range (yrev::id::erFbGain).start == Approx (-99.0f));
    CHECK (range (yrev::id::erFbGain).end == Approx (99.0f));
    CHECK (range (yrev::id::echoDamp).start == Approx (0.0f));
    CHECK (range (yrev::id::echoDamp).end == Approx (10.0f));
    CHECK (range (yrev::id::echoDamp).interval == Approx (1.0f));
    CHECK (range (yrev::id::aeqMidF).end == Approx (5000.0f));
    CHECK (yrev::echoDampToHigh (10.0f) == Approx (1.0f)); // HIGH x0-x10 (ROM chart): value / 10, held at x0.1
    CHECK (yrev::echoDampToHigh (9.0f) == Approx (0.9f));  // the REV7 Stereo Echo, as the REV5 twin's x0.9
    CHECK (yrev::echoDampToHigh (0.0f) == Approx (0.1f));

    auto set = [&] (const char* id, float real)
    {
        auto* prm = proc.apvts.getParameter (id);
        prm->setValueNotifyingHost (prm->convertTo0to1 (real));
    };
    set (yrev::id::modDelay, 80.0f);
    set (yrev::id::modType, (float) yrev::dsp::modtype::revFlange);
    proc.enforceEraLimits ();
    CHECK (proc.apvts.getRawParameterValue (yrev::id::modDelay)->load () == Approx (30.0f).margin (0.01));
    set (yrev::id::modType, (float) yrev::dsp::modtype::flange);
    set (yrev::id::modDelay, 80.0f);
    proc.enforceEraLimits ();
    CHECK (proc.apvts.getRawParameterValue (yrev::id::modDelay)->load () == Approx (80.0f).margin (0.01));
    // era 5 keeps a long flange too (the 5 ms is the REV5's phasing range, clamped in FxSlot; thirty-second pass)
    set (yrev::id::era, 1.0f);
    proc.enforceEraLimits ();
    CHECK (proc.apvts.getRawParameterValue (yrev::id::modDelay)->load () == Approx (80.0f).margin (0.01));
}

TEST_CASE ("the hidden voice parameters are not automatable and default to the engine's fixed values")
{
    YrevProcessor proc;
    for (const char* id :
         {"voice_loop_size", "voice_diff_size", "voice_lo_xover", "voice_er_gain", "gate_input", "voice_hi_xover",
          "er_type", "voice_late_onset", "voice_er_tilt", "voice_late_tilt", "voice_rev2_hi", "voice_rev2_gain"})
    {
        auto* prm = proc.apvts.getParameter (id);
        REQUIRE (prm != nullptr);
        CHECK_FALSE (prm->isAutomatable ());
    }
    CHECK (proc.apvts.getRawParameterValue ("voice_loop_size")->load () == Approx (1.0f));
    CHECK (proc.apvts.getRawParameterValue ("voice_diff_size")->load () == Approx (1.0f));
    CHECK (proc.apvts.getRawParameterValue ("voice_lo_xover")->load () == Approx (300.0f));
    CHECK (proc.apvts.getRawParameterValue ("voice_er_gain")->load () == Approx (0.0f));
    CHECK (proc.apvts.getRawParameterValue ("voice_hi_xover")->load () == Approx (8500.0f));
    CHECK (proc.apvts.getRawParameterValue ("er_type")->load () == Approx (0.0f));
    CHECK (proc.apvts.getRawParameterValue ("voice_late_onset")->load () == Approx (0.0f));
    CHECK (proc.apvts.getRawParameterValue ("voice_er_tilt")->load () == Approx (0.0f));
    CHECK (proc.apvts.getRawParameterValue ("voice_late_tilt")->load () == Approx (0.0f));
    CHECK (proc.apvts.getRawParameterValue ("voice_rev2_hi")->load () == Approx (0.0f));
    CHECK (proc.apvts.getRawParameterValue ("voice_rev2_gain")->load () == Approx (0.0f));
}
