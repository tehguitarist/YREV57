#include "plugin/PluginProcessor.h"
#include "plugin/YrevEditor.h"
#include "plugin/ui/SkinKey.h"

#include <catch2/catch_test_macros.hpp>
#include <cstdlib>
#include <functional>
#include <map>
#include <set>

TEST_CASE ("editor builds, resizes and paints")
{
    YrevProcessor p;
    std::unique_ptr<juce::AudioProcessorEditor> ed (p.createEditor ());
    REQUIRE (ed != nullptr);
    CHECK (ed->getWidth () * 2 == ed->getHeight () * 5); // 2.5:1
    ed->setSize (1000, 400);
    CHECK (ed->getHeight () == 400);

    auto img = ed->createComponentSnapshot (ed->getLocalBounds ());
    CHECK (img.getWidth () == 1000);
}

TEST_CASE ("every key and knob has a tooltip, overlays included")
{
    YrevProcessor p;
    std::unique_ptr<juce::AudioProcessorEditor> ed (p.createEditor ());
    REQUIRE (ed != nullptr);
    int checked = 0;
    std::function<void (juce::Component&)> walk = [&] (juce::Component& c)
    {
        auto* tc = dynamic_cast<juce::TooltipClient*> (&c);
        const bool control = dynamic_cast<juce::Slider*> (&c) != nullptr ||
                             (dynamic_cast<juce::Button*> (&c) != nullptr && c.getName ().isNotEmpty ());
        if (control && tc != nullptr)
        {
            INFO ("control without a tooltip: " << c.getName ());
            CHECK (tc->getTooltip ().isNotEmpty ());
            ++checked;
        }
        for (auto* child : c.getChildren ())
            walk (*child);
    };
    walk (*ed);
    CHECK (checked > 150); // knobs, keys and the overlays' keys
}

// Development aid, not run by default: writes PNGs of the panel to $YREV_SNAPSHOT_DIR.
//   YREV_SNAPSHOT_DIR=/tmp/shots ./YREV57Tests "[.snapshot]"
TEST_CASE ("write editor snapshots", "[.snapshot]")
{
    const char* dir = std::getenv ("YREV_SNAPSHOT_DIR");
    REQUIRE (dir != nullptr);
    juce::File out (dir);
    out.createDirectory ();

    auto save = [&] (YrevProcessor& proc, const char* name)
    {
        std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor ());
        ed->setSize (2500, 1000);
        auto img = ed->createComponentSnapshot (ed->getLocalBounds (), true, 1.0f);
        juce::PNGImageFormat png;
        juce::FileOutputStream os (out.getChildFile (name));
        os.setPosition (0);
        os.truncate ();
        REQUIRE (os.openedOk ());
        png.writeImageToStream (img, os);
    };

    YrevProcessor p7;
    save (p7, "shot-era7.png");

    YrevProcessor p5;
    p5.apvts.getParameter (yrev::id::era)->setValueNotifyingHost (1.0f);
    p5.apvts.getParameter (yrev::id::aeqOn)->setValueNotifyingHost (1.0f);
    p5.apvts.getParameter (yrev::id::aeqMidG)
        ->setValueNotifyingHost (p5.apvts.getParameter (yrev::id::aeqMidG)->convertTo0to1 (6.0f));
    save (p5, "shot-era5.png");

    p5.apvts.getParameter (yrev::id::fxType)
        ->setValueNotifyingHost (p5.apvts.getParameter (yrev::id::fxType)->convertTo0to1 (3.0f));
    save (p5, "shot-pitch.png");
    auto setIndex = [] (YrevProcessor& p, const char* pid, float v)
    { p.apvts.getParameter (pid)->setValueNotifyingHost (p.apvts.getParameter (pid)->convertTo0to1 (v)); };
    setIndex (p5, yrev::id::pitchSel, 1.0f); // Pitch B: feedback greyed, second voice live
    save (p5, "shot-pitch-b.png");
    p5.apvts.getParameter (yrev::id::fxType)
        ->setValueNotifyingHost (p5.apvts.getParameter (yrev::id::fxType)->convertTo0to1 (4.0f));
    save (p5, "shot-pan.png");
    setIndex (p5, yrev::id::fxType, 1.0f);
    save (p5, "shot-echo.png");
    setIndex (p5, yrev::id::fxType, 2.0f);
    setIndex (p5, yrev::id::modType, 0.0f); // Flange: DM, AM and Rev time greyed
    save (p5, "shot-mod.png");
    setIndex (p7, yrev::id::fxType, 1.0f);
    save (p7, "shot-era7-echo.png");
    setIndex (p7, yrev::id::fxType, 0.0f);

    p5.apvts.getParameter (yrev::id::fxType)->setValueNotifyingHost (0.0f);
    p5.apvts.getParameter (yrev::id::eqView)->setValueNotifyingHost (1.0f);
    p5.apvts.getParameter (yrev::id::deqOn)->setValueNotifyingHost (1.0f);
    p5.apvts.getParameter (yrev::id::deqLoShelf)->setValueNotifyingHost (1.0f);
    p5.apvts.getParameter (yrev::id::deqLoG)
        ->setValueNotifyingHost (p5.apvts.getParameter (yrev::id::deqLoG)->convertTo0to1 (8.0f));
    save (p5, "shot-digital.png");

    {
        std::unique_ptr<juce::AudioProcessorEditor> ed (p7.createEditor ());
        ed->setSize (2500, 1000);
        static_cast<YrevEditor&> (*ed).openPresets ();
        juce::PNGImageFormat png;
        juce::FileOutputStream os (out.getChildFile ("shot-presets.png"));
        os.setPosition (0);
        os.truncate ();
        png.writeImageToStream (ed->createComponentSnapshot (ed->getLocalBounds (), true, 1.0f), os);
    }
    {
        std::unique_ptr<juce::AudioProcessorEditor> ed (p7.createEditor ());
        ed->setSize (2500, 1000);
        static_cast<YrevEditor&> (*ed).openHiddenPresets ();
        juce::PNGImageFormat png;
        juce::FileOutputStream os (out.getChildFile ("shot-presets-hidden.png"));
        os.setPosition (0);
        os.truncate ();
        png.writeImageToStream (ed->createComponentSnapshot (ed->getLocalBounds (), true, 1.0f), os);
    }
    // the browser on the REV7 and REV5 manual-program banks (opens on the loaded preset's bank)
    for (const auto& [bank, pick] : {std::pair{"REV7", "Reverse Gate"}, std::pair{"REV5", "PC & Rev & G"}})
    {
        YrevProcessor pp;
        for (int i = 0; i < pp.presets.size (); ++i)
            if (pp.presets.at (i).name == pick && pp.presets.at (i).bank == bank)
                pp.presets.load (i);
        std::unique_ptr<juce::AudioProcessorEditor> ed (pp.createEditor ());
        ed->setSize (2500, 1000);
        static_cast<YrevEditor&> (*ed).openPresets ();
        juce::PNGImageFormat png;
        juce::FileOutputStream os (
            out.getChildFile (pick[1] == '7' ? "shot-presets-rev7.png" : "shot-presets-rev5.png"));
        os.setPosition (0);
        os.truncate ();
        png.writeImageToStream (ed->createComponentSnapshot (ed->getLocalBounds (), true, 1.0f), os);
    }
    for (auto* proc : {&p7, &p5})
    {
        if (proc == &p5) // a drawer control in use, to show the dot on the INT PARAM key
            proc->apvts.getParameter (yrev::id::gateLevel)->setValueNotifyingHost (0.3f);
        std::unique_ptr<juce::AudioProcessorEditor> ed (proc->createEditor ());
        ed->setSize (2500, 1000);
        juce::PNGImageFormat png;
        juce::FileOutputStream os0 (out.getChildFile (proc == &p7 ? "shot-era7-closed.png" : "shot-era5-badge.png"));
        os0.setPosition (0);
        os0.truncate ();
        png.writeImageToStream (ed->createComponentSnapshot (ed->getLocalBounds (), true, 1.0f), os0);
        static_cast<YrevEditor&> (*ed).openIntParam ();
        juce::FileOutputStream os (out.getChildFile (proc == &p7 ? "shot-intparam-era7.png" : "shot-intparam.png"));
        os.setPosition (0);
        os.truncate ();
        png.writeImageToStream (ed->createComponentSnapshot (ed->getLocalBounds (), true, 1.0f), os);
    }
    {
        std::unique_ptr<juce::AudioProcessorEditor> ed (p7.createEditor ());
        ed->setSize (2500, 1000);
        static_cast<YrevEditor&> (*ed).openSettings ();
        juce::PNGImageFormat png;
        juce::FileOutputStream os (out.getChildFile ("shot-settings-250.png")); // 2500 x 1000 is the 250 % size
        os.setPosition (0);
        os.truncate ();
        png.writeImageToStream (ed->createComponentSnapshot (ed->getLocalBounds (), true, 1.0f), os);
    }
    {
        std::unique_ptr<juce::AudioProcessorEditor> ed (p7.createEditor ());
        ed->setSize (1000, 400); // the 100 % size
        static_cast<YrevEditor&> (*ed).openSettings ();
        juce::PNGImageFormat png;
        juce::FileOutputStream os (out.getChildFile ("shot-settings-100.png"));
        os.setPosition (0);
        os.truncate ();
        png.writeImageToStream (ed->createComponentSnapshot (ed->getLocalBounds (), true, 1.0f), os);
    }
}

TEST_CASE ("the instrument ER modes (Piano, Organ, Brass, Guitar) are not in the mode selector")
{
    YrevProcessor p;
    p.apvts.getParameter ("era")->setValueNotifyingHost (1.0f); // era 5 shows Pan A and Pan B
    std::unique_ptr<juce::AudioProcessorEditor> ed (p.createEditor ());
    REQUIRE (ed != nullptr);
    std::set<juce::String> shown;
    std::function<void (juce::Component&)> walk = [&] (juce::Component& c)
    {
        if (dynamic_cast<juce::Button*> (&c) != nullptr && c.isVisible ())
            shown.insert (c.getName ());
        for (auto* child : c.getChildren ())
            walk (*child);
    };
    walk (*ed);
    for (const char* hidden : {"Piano", "Organ", "Brass", "Guitar"})
    {
        INFO (hidden);
        CHECK (shown.count (hidden) == 0);
    }
    CHECK (shown.count ("Pan B") == 1); // the charted modes are still there
    CHECK (shown.count ("Spring") == 1);
}

TEST_CASE ("CLA mode: both era keys lit on the hidden preset; a plain era click leaves it")
{
    YrevProcessor p;
    int secret = -1;
    for (int i = 0; i < p.presets.size (); ++i)
        if (p.presets.at (i).bank == "SECRET")
            secret = i;
    REQUIRE (secret >= 0);
    p.presets.load (secret);
    std::unique_ptr<juce::AudioProcessorEditor> ed (p.createEditor ());
    REQUIRE (ed != nullptr);
    std::map<juce::String, yrev::ui::SkinKey*> era;
    std::function<void (juce::Component&)> walk = [&] (juce::Component& c)
    {
        if (auto* k = dynamic_cast<yrev::ui::SkinKey*> (&c); k != nullptr && juce::String (k->def.group) == "erakeys")
            era[k->def.label] = k;
        for (auto* child : c.getChildren ())
            walk (*child);
    };
    walk (*ed);
    REQUIRE (era.size () == 2);
    CHECK (era["7"]->getToggleState ());
    CHECK (era["5"]->getToggleState ());

    // a plain click on either key leaves CLA mode: one key lit, the clicked era
    const bool on5 = juce::roundToInt (p.apvts.getRawParameterValue ("era")->load ()) == 1;
    auto* other = on5 ? era["7"] : era["5"];
    other->onClick ();
    CHECK (other->getToggleState ());
    CHECK_FALSE ((on5 ? era["5"] : era["7"])->getToggleState ());
    CHECK (juce::roundToInt (p.apvts.getRawParameterValue ("era")->load ()) == (on5 ? 0 : 1));

    // and back again: normal alternation
    auto* first = on5 ? era["5"] : era["7"];
    first->onClick (); // (key lights follow on the editor timer, which does not run here)
    CHECK (juce::roundToInt (p.apvts.getRawParameterValue ("era")->load ()) == (on5 ? 1 : 0));
}
