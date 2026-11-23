#include "plugin/PluginProcessor.h"

#include <cmath>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using Catch::Approx;

TEST_CASE ("state round-trips through get/setStateInformation")
{
    YrevProcessor a;
    a.apvts.getParameter (yrev::id::mix)
        ->setValueNotifyingHost (a.apvts.getParameter (yrev::id::mix)->convertTo0to1 (80.0f));
    a.apvts.getParameter (yrev::id::era)->setValueNotifyingHost (1.0f);

    juce::MemoryBlock block;
    a.getStateInformation (block);
    REQUIRE (block.getSize () > 0);

    YrevProcessor b;
    b.setStateInformation (block.getData (), (int) block.getSize ());
    CHECK (b.apvts.getRawParameterValue (yrev::id::mix)->load () == Approx (80.0f).margin (0.01));
    CHECK (b.apvts.getRawParameterValue (yrev::id::era)->load () == Approx (1.0f).margin (0.01));
}

TEST_CASE ("restoring a state puts switches back on an exact step (pluginval state restoration)")
{
    YrevProcessor p;
    juce::MemoryBlock saved;
    p.getStateInformation (saved); // duck_on, fx_link, fx_sync all off

    for (const auto* id : {yrev::id::duckOn, yrev::id::fxLink, yrev::id::fxSync})
        p.apvts.getParameter (id)->setValueNotifyingHost (0.21f); // in-between value, still "off"
    p.setStateInformation (saved.getData (), (int) saved.getSize ());

    for (const auto* id : {yrev::id::duckOn, yrev::id::fxLink, yrev::id::fxSync})
    {
        CAPTURE (id);
        CHECK (p.apvts.getParameter (id)->getValue () == Approx (0.0f).margin (1.0e-6));
    }
}

TEST_CASE ("garbage state is ignored, not fatal")
{
    YrevProcessor p;
    const char junk[] = "not xml";
    p.setStateInformation (junk, (int) sizeof (junk));
    CHECK (p.apvts.getRawParameterValue (yrev::id::mix)->load () == Approx (50.0f).margin (0.01));
}

TEST_CASE ("going back to era 7 clears the era-5-only choices and the digital EQ")
{
    YrevProcessor p;
    auto set = [&] (const char* id, float real)
    {
        auto* prm = p.apvts.getParameter (id);
        prm->setValueNotifyingHost (prm->convertTo0to1 (real));
    };
    auto get = [&] (const char* id) { return p.apvts.getRawParameterValue (id)->load (); };

    set (yrev::id::era, 1.0f);
    set (yrev::id::deqOn, 1.0f);
    set (yrev::id::eqView, 1.0f);
    set (yrev::id::fxType, 4.0f); // pan
    set (yrev::id::erMode, 8.0f); // piano
    set (yrev::id::aeqOn, 1.0f);

    set (yrev::id::era, 0.0f);
    p.enforceEraLimits ();
    CHECK (get (yrev::id::deqOn) == Approx (0.0f));
    CHECK (get (yrev::id::eqView) == Approx (0.0f));
    CHECK (get (yrev::id::fxType) == Approx (0.0f));
    CHECK (get (yrev::id::erMode) == Approx (0.0f));
    CHECK (get (yrev::id::aeqOn) == Approx (1.0f)); // the analogue EQ is untouched

    set (yrev::id::era, 1.0f);
    p.enforceEraLimits ();                          // no-op in era 5
    CHECK (get (yrev::id::deqOn) == Approx (0.0f)); // comes back off
}

TEST_CASE ("era limits clamp the stored values and relax again in era 5")
{
    YrevProcessor p;
    auto set = [&] (const char* id, float real)
    {
        auto* prm = p.apvts.getParameter (id);
        prm->setValueNotifyingHost (prm->convertTo0to1 (real));
    };
    auto get = [&] (const char* id) { return p.apvts.getRawParameterValue (id)->load (); };

    set (yrev::id::era, 1.0f);
    set (yrev::id::rt, 50.0f);
    set (yrev::id::dlyL, 1200.0f);
    set (yrev::id::echoFb, -40.0f);
    set (yrev::id::aeqMidF, 6000.0f);
    p.enforceEraLimits ();
    CHECK (get (yrev::id::rt) == Approx (50.0f).margin (0.1));        // fine in era 5
    CHECK (get (yrev::id::aeqMidF) == Approx (5000.0f).margin (1.0)); // the mid band stops at 5 kHz (Q-26)

    set (yrev::id::era, 0.0f);
    p.enforceEraLimits ();
    CHECK (get (yrev::id::rt) == Approx (10.0f).margin (0.01));
    CHECK (get (yrev::id::dlyL) == Approx (900.0f).margin (0.1));
    CHECK (get (yrev::id::echoFb) == Approx (0.0f).margin (0.01));
}

TEST_CASE ("the UI scale property round-trips through state (200% and 250%)")
{
    for (const int pct : {75, 100, 150, 200, 250})
    {
        YrevProcessor a;
        a.apvts.state.setProperty ("uiScale", pct, nullptr);
        juce::MemoryBlock block;
        a.getStateInformation (block);

        YrevProcessor b;
        b.setStateInformation (block.getData (), (int) block.getSize ());
        CHECK ((int) b.apvts.state.getProperty ("uiScale") == pct);
    }
}

namespace
{
    void setReal (YrevProcessor& p, const char* pid, float v)
    {
        auto* prm = p.apvts.getParameter (pid);
        prm->setValueNotifyingHost (prm->convertTo0to1 (v));
    }
    float real (YrevProcessor& p, const char* pid)
    {
        return p.apvts.getRawParameterValue (pid)->load ();
    }
    void switchEra (YrevProcessor& p, int era)
    {
        setReal (p, yrev::id::era, (float) era);
        p.handleEraChange (); // what the async update does on the message thread
    }
} // namespace

TEST_CASE ("per-era memory (default): each era keeps its own settings; the first visit carries them over")
{
    YrevProcessor p;
    REQUIRE (p.perEraMemory ());
    switchEra (p, 0);
    setReal (p, yrev::id::rt, 5.0f);
    setReal (p, yrev::id::diffusion, 8.0f);

    switchEra (p, 1);
    CHECK (real (p, yrev::id::rt) == Approx (5.0f)); // first visit to era 5: carried over
    setReal (p, yrev::id::rt, 2.0f);
    setReal (p, yrev::id::diffusion, 3.0f);

    switchEra (p, 0);
    CHECK (real (p, yrev::id::rt) == Approx (5.0f).margin (0.01));
    CHECK (real (p, yrev::id::diffusion) == Approx (8.0f).margin (0.01));
    switchEra (p, 1);
    CHECK (real (p, yrev::id::rt) == Approx (2.0f).margin (0.01));
    CHECK (real (p, yrev::id::diffusion) == Approx (3.0f).margin (0.01));

    // the stored sets go with the session
    juce::MemoryBlock block;
    p.getStateInformation (block);
    YrevProcessor b;
    b.setStateInformation (block.getData (), (int) block.getSize ());
    CHECK (real (b, yrev::id::rt) == Approx (2.0f).margin (0.01)); // restored as saved, no swap
    switchEra (b, 0);
    CHECK (real (b, yrev::id::rt) == Approx (5.0f).margin (0.01));
}

TEST_CASE ("shared era settings carry across both ways")
{
    YrevProcessor p;
    p.apvts.state.setProperty ("eraMemory", "shared", nullptr);
    REQUIRE_FALSE (p.perEraMemory ());
    switchEra (p, 0);
    setReal (p, yrev::id::rt, 5.0f);
    switchEra (p, 1);
    setReal (p, yrev::id::rt, 2.0f);
    switchEra (p, 0);
    CHECK (real (p, yrev::id::rt) == Approx (2.0f).margin (0.01));
}

TEST_CASE ("per-era memory: a preset that changes the era loads as stored, and the era left keeps its settings")
{
    YrevProcessor p;
    switchEra (p, 0);
    setReal (p, yrev::id::rt, 7.5f);
    int rev5 = -1;
    for (int i = 0; i < p.presets.size () && rev5 < 0; ++i)
        if (p.presets.at (i).bank == "REV5")
            rev5 = i;
    REQUIRE (rev5 >= 0);
    const auto stored = p.presets.at (rev5).values;
    p.presets.load (rev5);
    p.handleEraChange (); // the async update after the load: no swap, the preset stays as loaded
    CHECK (juce::roundToInt (real (p, yrev::id::era)) == 1);
    if (const auto it = stored.find (yrev::id::rt); it != stored.end ())
        CHECK (real (p, yrev::id::rt) == Approx (it->second).margin (0.01));
    CHECK (p.getCurrentProgram () == rev5);

    switchEra (p, 0);
    CHECK (real (p, yrev::id::rt) == Approx (7.5f).margin (0.01));
    switchEra (p, 1); // back to the preset, under its own name
    CHECK (p.getCurrentProgram () == rev5);
}

TEST_CASE ("a state that selects a hidden instrument mode loads, keeps its value and plays a charted stand-in")
{
    CHECK (yrev::erModeStandIn (7) == 7);
    CHECK (yrev::erModeStandIn (8) == 0);
    CHECK (yrev::erModeStandIn (9) == 1);
    CHECK (yrev::erModeStandIn (10) == 4);
    CHECK (yrev::erModeStandIn (11) == 5);

    for (int mode = 8; mode <= 11; ++mode)
    {
        YrevProcessor a;
        a.apvts.getParameter (yrev::id::era)->setValueNotifyingHost (1.0f);
        auto* prm = a.apvts.getParameter (yrev::id::erMode);
        prm->setValueNotifyingHost (prm->convertTo0to1 ((float) mode));

        juce::MemoryBlock blob;
        a.getStateInformation (blob);
        YrevProcessor b;
        b.setStateInformation (blob.getData (), (int) blob.getSize ());
        CHECK (b.apvts.getRawParameterValue (yrev::id::erMode)->load () == Approx ((float) mode)); // left alone

        b.prepareToPlay (48000.0, 512);
        juce::AudioBuffer<float> buf (2, 512);
        juce::MidiBuffer midi;
        buf.clear ();
        buf.setSample (0, 0, 1.0f);
        buf.setSample (1, 0, 1.0f);
        b.processBlock (buf, midi);
        for (int i = 0; i < 4; ++i)
        {
            buf.clear ();
            b.processBlock (buf, midi);
        }
        CHECK (std::isfinite (buf.getRMSLevel (0, 0, 512)));
    }
}
