#include "YrevEditor.h"

#include <cmath>

#include "ui/UiTooltips.h"

using namespace yrev;
using namespace yrev::ui;

namespace
{
    // ---- Config -------------------------------------------------------------------------
    constexpr int kDefaultW = 1250, kDefaultH = 500; // 125 % of the 1000 x 400 nominal size
    constexpr int kMinW = 750, kMaxW = 3750;
    constexpr int kTimerHz = 30;
    constexpr float kMeterFallDb = 1.6f;  // per tick, about 48 dB/s
    constexpr float kClipLinear = 0.989f; // -0.1 dBFS
    constexpr juce::uint32 kLcdMessageMs = 1800;
    constexpr float kLogoSize = 64.0f, kLogoSlant = 0.30f, kLogoStroke = 3.5f; // name: about 17 degrees
    constexpr int kLogoLabelInset = 8;    // "Digital reverberator" sits a little right of the name (prototype padding)
    constexpr float kUnusedAlpha = 0.35f; // knobs the selected mod type or pitch program does not use

    constexpr int kFxBits[] = {1, 2, 4, 8, 16, 32};
    constexpr float kSegThresholdDb[8] = {0, -1, -3, -6, -10, -15, -20, -30};
    const char* const kScale[8] = {"CLIP", "0", "-3", "-6", "-10", "-15", "-20", "-30"};

    struct Bound
    {
        const char* knobId;
        const char* paramId;
        const char* fmt;
    }; // fmt null = keep the prototype's
    constexpr Bound kKnobParams[] = {{"input", id::inLevel, "db"},
                                     {"out", id::outLevel, "out"},
                                     {"mix", id::mix, "pct"},
                                     {"width", id::width, "pct"},
                                     {"char", id::character, "pct"},
                                     {"duckr", id::duckRange, "db"},
                                     {"duckt", id::duckThresh, "db"},
                                     {"ducka", id::duckAttack, "ms"},
                                     {"duckrl", id::duckRelease, "ms"},
                                     {"a_lof", id::aeqLoF, "hz"},
                                     {"a_log", id::aeqLoG, "db"},
                                     {"a_midf", id::aeqMidF, "hz"},
                                     {"a_midg", id::aeqMidG, "db"},
                                     {"a_hif", id::aeqHiF, "hz"},
                                     {"a_hig", id::aeqHiG, "db"},
                                     {"d_lof", id::deqLoF, "hz"},
                                     {"d_log", id::deqLoG, "db"},
                                     {"d_loq", id::deqLoQ, "q"},
                                     {"d_midf", id::deqMidF, "hz"},
                                     {"d_midg", id::deqMidG, "db"},
                                     {"d_midq", id::deqMidQ, "q"},
                                     {"d_hif", id::deqHiF, "hz"},
                                     {"d_hig", id::deqHiG, "db"},
                                     {"d_hiq", id::deqHiQ, "q"},
                                     // reverb
                                     {"rt", id::rt, nullptr},
                                     {"hi", id::hiRatio, nullptr},
                                     {"lo", id::loRatio, nullptr},
                                     {"pre", id::preDelay, nullptr},
                                     {"r1d", id::ref1Delay, nullptr},
                                     {"r1l", id::ref1Level, nullptr},
                                     {"r1ld", id::ref1LDelay, nullptr},
                                     {"r1ll", id::ref1LLevel, nullptr},
                                     {"r1rd", id::ref1RDelay, nullptr},
                                     {"r1rl", id::ref1RLevel, nullptr},
                                     {"dif", id::diffusion, nullptr},
                                     {"erb", id::erRevBal, nullptr},
                                     {"dens", id::density, nullptr},
                                     {"smod", id::spaceMod, nullptr},
                                     {"rlpf", id::wetLpf, nullptr},
                                     {"s2t", id::rev2Time, nullptr},
                                     {"s2d", id::rev2Delay, nullptr},
                                     {"s2l", id::rev2Level, nullptr},
                                     {"glev", id::gateLevel, nullptr},
                                     {"gtd", id::gateTrigDelay, nullptr},
                                     {"ghold", id::gateHold, nullptr},
                                     {"grel", id::gateRelease, nullptr},
                                     // early reflections
                                     {"size", id::roomSize, nullptr},
                                     {"live", id::liveness, nullptr},
                                     {"ern", id::erCount, nullptr},
                                     {"fbg", id::erFbGain, nullptr},
                                     {"fbd", id::erFbDelay, nullptr},
                                     {"fbh", id::erFbHi, nullptr},
                                     // FX slot
                                     {"dl", id::dlyL, nullptr},
                                     {"dr", id::dlyR, nullptr},
                                     {"dlev", id::dlyLevel, nullptr},
                                     {"dfb", id::dlyFb, nullptr},
                                     {"dfbr", id::dlyFbR, nullptr},
                                     {"dhigh", id::dlyHigh, nullptr},
                                     {"fxlpf", id::fxLpf, nullptr},
                                     {"el", id::echoL, nullptr},
                                     {"er_", id::echoR, nullptr},
                                     {"efb", id::echoFb, nullptr},
                                     {"edamp", id::echoDamp, nullptr},
                                     {"efbr", id::echoFbR, nullptr},
                                     {"elev", id::echoLevel, nullptr},
                                     {"lfo", id::lfoRate, nullptr},
                                     {"depth", id::modDepth, nullptr},
                                     {"dm", id::dmDepth, nullptr},
                                     {"am", id::amDepth, nullptr},
                                     {"mdel", id::modDelay, nullptr},
                                     {"mfb", id::modFb, nullptr},
                                     {"mrt", id::modRt, nullptr},
                                     {"rmix", id::revMix, nullptr},
                                     {"mlpf", id::modLpf, nullptr},
                                     {"shift", id::pitchShift, nullptr},
                                     {"fine", id::pitchFine, nullptr},
                                     {"pdel", id::pitchDelay, nullptr},
                                     {"pfb", id::pitchFb, nullptr},
                                     {"shift2", id::pitch2Shift, nullptr},
                                     {"fine2", id::pitch2Fine, nullptr},
                                     {"pdel2", id::pitch2Delay, nullptr},
                                     {"prate", id::panRate, "rate"},
                                     {"pdepth", id::panDepth, nullptr}};

    // Knobs a mod type or pitch program does not use are greyed out (as in the prototype's MODUSE / PITCHUSE).
    // Mod: Flange, Rev Flange, Chorus A, Chorus B, Phasing, Tremolo, Symphonic. Pitch: A, B, C (REV5 pp. 32-36).
    bool knobInUse (const juce::String& knobId, int modType, int pitchSel)
    {
        static const juce::StringArray modKnobs{"lfo", "depth", "dm", "am", "mdel", "mfb", "mrt"};
        static const juce::StringArray modUse[] = {{"lfo", "depth", "mdel", "mfb"},
                                                   {"lfo", "depth", "mdel"}, // REV TIME is the reverb's
                                                   {"lfo", "dm", "am"},
                                                   {"lfo", "dm", "am"},
                                                   {"lfo", "depth", "mdel"},
                                                   {"lfo", "depth"},
                                                   {"lfo", "depth"}};
        static const juce::StringArray pitchKnobs{"shift", "fine", "pdel", "pfb", "shift2", "fine2", "pdel2"};
        static const juce::StringArray pitchUse[] = {{"shift", "fine", "pdel", "pfb"},
                                                     {"shift", "fine", "pdel", "shift2", "fine2", "pdel2"},
                                                     {"shift", "fine", "pdel", "shift2", "fine2", "pdel2"}};
        if (modKnobs.contains (knobId))
            return modUse[juce::jlimit (0, 6, modType)].contains (knobId);
        if (pitchKnobs.contains (knobId))
            return pitchUse[juce::jlimit (0, 2, pitchSel)].contains (knobId);
        return true;
    }

    struct BoundKey
    {
        const char* label;
        const char* paramId;
    };
    constexpr BoundKey kKeyParams[] = {
        {"Bypass", id::bypass},   {"Duckon", id::duckOn}, {"Dither", id::dither},  {"Wet R invert", id::outPhaseR},
        {"Wetsolo", id::wetSolo}, {"Mute", id::mute},     {"LinkL/R", id::fxLink}, {"Temposync", id::fxSync},
        {"LFOsync", id::fxSync},  {"FXwet", id::fxWet}};

    // Radio groups of keys backed by a choice parameter: the key's position in the group is the choice index.
    struct RadioParam
    {
        const char* group;
        const char* paramId;
    };
    constexpr RadioParam kRadioParams[] = {{"er", id::erMode},         {"fxhead", id::fxType},
                                           {"orderkeys", id::fxOrder}, {"modtypes", id::modType},
                                           {"pitchsel", id::pitchSel}, {"pandir", id::panDir}};

    template <size_t N> juce::String tipFor (const Tip (&table)[N], const juce::String& key)
    {
        for (const auto& t : table)
            if (key == t.key)
                return juce::String::fromUTF8 (t.text);
        return {};
    }

    juce::Rectangle<int> toRect (const Rect& r)
    {
        return {r.x, r.y, r.w, r.h};
    }

    // The instrument ER modes Organ, Brass and Guitar have no data yet (Q-01): their keys stay out of the panel and
    // the processor plays a charted stand-in if a state or automation selects them. Era 5's Piano plays its measured
    // pattern (pass 52) and has an era-5-only key beside REV E/R (2026-11-25).
    bool isHiddenModeKey (const KeyDef& d)
    {
        if (juce::String (d.group) != "ermodes1")
            return false;
        for (const char* n : {"Organ", "Brass", "Guitar"})
            if (juce::String (d.label) == n)
                return true;
        return false;
    }

    // The ER mode keys' choice index by label (er_mode): the panel shows them out of choice order (the hidden
    // instrument modes 8-11 have no keys; one REV E/R key stands for every Rev set, 12-20), so the
    // index cannot be the key's position.
    int erModeIndexOf (const juce::String& label)
    {
        constexpr const char* kLabels[] = {"Smallhall", "Largehall", "Random", "Reverse", "Plate",  "Spring", "Pan A",
                                           "Pan B",     "Piano",     "Organ",  "Brass",   "Guitar", "RevE/R"};
        for (int i = 0; i < (int) std::size (kLabels); ++i)
            if (label == kLabels[i])
                return i;
        jassertfalse;
        return 0;
    }

    juce::String radioGroupOf (const juce::String& g)
    {
        if (g == "ermodes1" || g == "ermodes2")
            return "er";
        if (g == "fxhead" || g == "orderkeys" || g == "modtypes" || g == "pitchsel" || g == "pandir")
            return g;
        return {};
    }

    void drawPlate (juce::Graphics& g, juce::Rectangle<int> r)
    {
        g.setColour (juce::Colour (0xff0c0c0d));
        g.fillRoundedRectangle (r.toFloat (), 4);
        g.setColour (juce::Colour (0xff1c1c1f));
        g.drawRoundedRectangle (r.toFloat ().reduced (1), 4, 2);
        g.setColour (col::inkDim.withAlpha (.45f));
        g.setFont (Skin::get ().label (13));
        g.drawText ("5", r.reduced (8, 6), juce::Justification::bottomRight);
    }
} // namespace

// ---- Canvas: paints the panel and handles the clicks that are not buttons or knobs ---------------
class YrevEditor::Canvas final : public juce::Component, public juce::TooltipClient
{
public:
    juce::String getTooltip () override
    {
        if (!ed.tipsOn)
            return {};
        const auto p = getMouseXYRelative ();
        auto in = [&] (const Rect& r) { return toRect (r).contains (p); };
        if (in (kMeterInCols) || in (kMeterInClipHit))
            return tipFor (kTips_areas, "meter_in");
        if (in (kMeterOutCols) || in (kMeterOutClipHit))
            return tipFor (kTips_areas, "meter_out");
        if (in (kPaneMono))
            return tipFor (kTips_areas, "wet_mono");
        if (in (kPaneStereo))
            return tipFor (kTips_areas, "wet_stereo");
        if (in (kPaneTrue))
            return tipFor (kTips_areas, "wet_true");
        if (in (kLcd))
            return tipFor (kTips_areas, "lcd");
        if (graphRect ().contains (p))
            return tipFor (kTips_areas, "eqgraph");
        return {};
    }

    explicit Canvas (YrevEditor& e) : ed (e) { setSize (kDesignW, kDesignH); }

    void paint (juce::Graphics& g) override
    {
        const auto& sk = Skin::get ();
        g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
        g.drawImage (sk.background, getLocalBounds ().toFloat (), juce::RectanglePlacement::stretchToFit);

        paintFrames (g);
        paintDisplay (g);
        paintKnobLabels (g);
        paintEq (g);
        paintEqHeadings (g);
        paintMeter (g, kMeterInCols, 0, false, "IN");
        paintMeter (g, kMeterOutCols, 2, true, "OUT");

        g.setColour (col::ink);
        g.setFont (sk.label (22, .14f));
        g.drawText (juce::String ("V") + JucePlugin_VersionString, toRect (kVersion).expanded (30, 0),
                    juce::Justification::centredRight);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        const auto p = e.getPosition ();
        dragBand = -1;
        if (graphRect ().contains (p))
        {
            float best = 24.0f;
            const bool on = eqOn ();
            for (int i = 0; i < 3; ++i)
            {
                const float d = juce::Point<float> (fx (param (bands ()[i].f)), gy (on ? param (bands ()[i].g) : 0.0f))
                                    .getDistanceFrom (e.position);
                if (d < best)
                {
                    best = d;
                    dragBand = i;
                }
            }
            if (dragBand >= 0)
            {
                for (const auto& b : {bands ()[dragBand].f, bands ()[dragBand].g})
                    if (auto* pr = ed.proc.apvts.getParameter (b))
                        pr->beginChangeGesture ();
                dragNode (e);
            }
        }
        if (toRect (kMeterInClipHit).contains (p))
        {
            ed.clip[0] = ed.clip[1] = false;
            repaint (toRect (kMeterInCols).expanded (4));
        }
        if (toRect (kMeterOutClipHit).contains (p))
        {
            ed.clip[2] = ed.clip[3] = false;
            repaint (toRect (kMeterOutCols).expanded (4));
        }

        const Rect* panes[] = {&kPaneMono, &kPaneStereo, &kPaneTrue};
        const char* names[] = {"WET: MONO", "WET: STEREO", "WET: TRUE STEREO"};
        for (int i = 0; i < 3; ++i)
            if (toRect (*panes[i]).contains (p))
            {
                if (auto* prm = ed.proc.apvts.getParameter (id::wetInput))
                    prm->setValueNotifyingHost (prm->convertTo0to1 ((float) i));
                ed.say (names[i]);
            }
    }

    void mouseDrag (const juce::MouseEvent& e) override { dragNode (e); }
    void mouseUp (const juce::MouseEvent&) override
    {
        if (dragBand >= 0)
            for (const auto& b : {bands ()[dragBand].f, bands ()[dragBand].g})
                if (auto* pr = ed.proc.apvts.getParameter (b))
                    pr->endChangeGesture ();
        dragBand = -1;
    }

private:
    YrevEditor& ed;

    // ---- EQ graph: analogue view (fixed Q, peaking bands) or digital view (Q, shelving low and high) ----
    struct EqBand
    {
        const char* f;
        const char* g;
        const char* q;
        const char* shelf;
    };
    int dragBand = -1;

    bool digital () const { return ed.eqDigital (); }
    const EqBand* bands () const
    {
        static constexpr EqBand analog[3] = {{id::aeqLoF, id::aeqLoG, nullptr, nullptr},
                                             {id::aeqMidF, id::aeqMidG, nullptr, nullptr},
                                             {id::aeqHiF, id::aeqHiG, nullptr, nullptr}};
        static constexpr EqBand dig[3] = {{id::deqLoF, id::deqLoG, id::deqLoQ, id::deqLoShelf},
                                          {id::deqMidF, id::deqMidG, id::deqMidQ, nullptr},
                                          {id::deqHiF, id::deqHiG, id::deqHiQ, id::deqHiShelf}};
        return digital () ? dig : analog;
    }
    bool eqOn () const { return param (digital () ? id::deqOn : id::aeqOn) > .5f; }
    juce::Rectangle<int> graphRect () const { return toRect (digital () ? kEqGraphDigital : kEqGraph); }
    juce::Rectangle<float> graphArea () const { return graphRect ().toFloat ().reduced (3); }
    float fx (float f) const
    {
        return graphArea ().getX () + std::log (f / 20.0f) / std::log (1000.0f) * graphArea ().getWidth ();
    }
    float xf (float x) const
    {
        return 20.0f * std::pow (1000.0f, (x - graphArea ().getX ()) / graphArea ().getWidth ());
    }
    float gy (float g) const
    {
        const auto a = graphArea ();
        return a.getCentreY () - g / 15.0f * (a.getHeight () * .5f - 10.0f);
    }
    float yg (float y) const
    {
        const auto a = graphArea ();
        return (a.getCentreY () - y) / (a.getHeight () * .5f - 10.0f) * 15.0f;
    }
    float param (const char* pid) const { return ed.proc.apvts.getRawParameterValue (pid)->load (); }

    float response (float f) const
    {
        float total = 0;
        const auto* b = bands ();
        for (int i = 0; i < 3; ++i)
        {
            const float o = std::log2 (f / param (b[i].f));
            const float gdb = param (b[i].g);
            if (b[i].shelf != nullptr && param (b[i].shelf) > .5f)
                total += gdb * (i == 0 ? 1.0f / (1.0f + std::exp (o * 3.0f)) : 1.0f / (1.0f + std::exp (-o * 3.0f)));
            else
            {
                const float q = b[i].q != nullptr ? param (b[i].q) : 1.0f;
                const float w = 1.1f / juce::jmax (0.3f, q);
                total += gdb / (1.0f + (o / w) * (o / w));
            }
        }
        return total;
    }

    void paintEq (juce::Graphics& g) const
    {
        const auto a = graphArea ();
        const bool on = eqOn ();
        g.setColour (juce::Colour (0xff0e2812));
        g.fillRoundedRectangle (graphRect ().toFloat (), 3);
        g.setColour (juce::Colour (0xff060606));
        g.drawRoundedRectangle (graphRect ().toFloat ().reduced (1.5f), 3, 3);

        juce::Graphics::ScopedSaveState ss (g);
        g.reduceClipRegion (a.toNearestInt ());
        const auto font = Skin::get ().label (13, .04f);
        g.setFont (font);
        for (float f : {100.0f, 1000.0f, 10000.0f})
        {
            g.setColour (juce::Colour (150, 190, 80).withAlpha (.25f));
            g.drawVerticalLine ((int) fx (f), a.getY (), a.getBottom ());
            g.setColour (juce::Colour (0xff8ea84a));
            g.drawText (f >= 1000 ? juce::String ((int) (f / 1000)) + "K" : juce::String ((int) f), (int) fx (f) + 4,
                        (int) a.getBottom () - 18, 40, 14, juce::Justification::left);
        }
        for (int gdb : {12, 6, 0, -6, -12})
        {
            g.setColour (juce::Colour (150, 190, 80).withAlpha (.25f));
            g.drawHorizontalLine ((int) gy ((float) gdb), a.getX (), a.getRight ());
            g.setColour (juce::Colour (0xff8ea84a));
            g.drawText ((gdb > 0 ? "+" : "") + juce::String (gdb), (int) a.getX () + 3, (int) gy ((float) gdb) - 15, 30,
                        14, juce::Justification::left);
        }

        const auto trace = on ? juce::Colour (0xffd6e84a) : juce::Colour (0xff5c6e2c);
        juce::Path curve;
        bool first = true;
        for (float x = a.getX (); x <= a.getRight (); x += 4.0f)
        {
            const float y = gy (on ? response (xf (x)) : 0.0f);
            if (first)
                curve.startNewSubPath (x, y);
            else
                curve.lineTo (x, y);
            first = false;
        }
        g.setColour (trace);
        g.strokePath (curve, juce::PathStrokeType (2.5f));
        const auto* b = bands ();
        for (int i = 0; i < 3; ++i)
        {
            const juce::Rectangle<float> n (16, 16);
            const auto r = n.withCentre ({fx (param (b[i].f)), gy (on ? param (b[i].g) : 0.0f)});
            g.setColour (juce::Colour (0xff0e2812));
            g.fillRect (r);
            g.setColour (trace);
            g.drawRect (r, 2.0f);
        }
    }

    void dragNode (const juce::MouseEvent& e)
    {
        if (dragBand < 0)
            return;
        const auto& b = bands ()[dragBand];
        auto setP = [this] (const char* pid, float v)
        {
            if (auto* p = ed.proc.apvts.getParameter (pid))
                p->setValueNotifyingHost (p->convertTo0to1 (v));
        };
        setP (b.f, xf ((float) e.x));
        setP (b.g, juce::jlimit (-15.0f, 15.0f, yg ((float) e.y)));
        repaint (graphRect ());
    }

    void paintEqHeadings (juce::Graphics& g) const
    {
        const char* names[3] = {"LOW", "MID", "HIGH"};
        const char* an[3][2] = {{"a_lof", "a_log"}, {"a_midf", "a_midg"}, {"a_hif", "a_hig"}};
        const char* dg[3][2] = {{"d_lof", "d_log"}, {"d_midf", "d_midg"}, {"d_hif", "d_hig"}};
        g.setColour (col::ink);
        g.setFont (Skin::get ().label (20, .12f));
        for (int i = 0; i < 3; ++i)
        {
            juce::Rectangle<int> u;
            const auto& ids = digital () ? dg[i] : an[i];
            for (const auto& d : kKnobs)
                if (juce::String (d.id) == ids[0] || juce::String (d.id) == ids[1])
                    u = u.isEmpty () ? toRect (d.labelR) : u.getUnion (toRect (d.labelR));
            if (!u.isEmpty ())
                g.drawText (names[i], u.getX (), u.getY () - 30, u.getWidth (), 22, juce::Justification::centred);
        }
    }

    void paintFrames (juce::Graphics& g) const
    {
        const auto& sk = Skin::get ();
        for (const auto& f : kFrames)
        {
            const auto r = toRect (f.r).toFloat ();
            g.setGradientFill (juce::ColourGradient (juce::Colours::black.withAlpha (.18f), 0, r.getY (),
                                                     juce::Colours::black.withAlpha (.34f), 0, r.getBottom (), false));
            g.fillRoundedRectangle (r, 4);
            g.setColour (col::rule);
            g.drawRoundedRectangle (r.reduced (1), 4, 2);

            const auto t = toRect (f.titleR);
            const auto title = juce::String (f.title).toUpperCase ();
            const auto font = sk.label (28, .12f);
            g.setFont (font);
            g.setColour (col::ink);
            g.drawText (title, t, juce::Justification::centredLeft);
            const float tw = juce::GlyphArrangement::getStringWidth (font, title);
            const float x0 = (float) t.getX () + tw + 14.0f;
            g.setColour (col::rule);
            g.fillRect (x0, (float) t.getCentreY () + 3.0f, (float) t.getRight () - x0, 2.0f);
        }
    }

    void paintDisplay (juce::Graphics& g) const
    {
        const auto& sk = Skin::get ();

        // logo: the name's outline, filled and stroked for a heavier face than the embedded semi-bold, slanted
        g.setColour (col::ink);
        {
            juce::GlyphArrangement ga;
            ga.addLineOfText (sk.label (kLogoSize, .05f), "YREV57", (float) kLogo.x, (float) kLogo.y - 10.0f);
            juce::Path name;
            ga.createPath (name);
            const float baseY = (float) kLogo.y - 10.0f;
            name.applyTransform (juce::AffineTransform (1, -kLogoSlant, kLogoSlant * baseY, 0, 1, 0));
            g.fillPath (name);
            g.strokePath (name, juce::PathStrokeType (kLogoStroke, juce::PathStrokeType::mitered)); // sharp corners
        }
        g.setColour (col::inkDim);
        g.setFont (sk.label (17, .14f));
        g.drawText ("DIGITAL REVERBERATOR", toRect (kLogo).translated (kLogoLabelInset, -3).withWidth (300),
                    juce::Justification::centredLeft);

        // display panel
        const auto dp = toRect (kDisplayPanel).toFloat ();
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff050506), 0, dp.getY (), juce::Colour (0xff0a0a0b), 0,
                                                 dp.getBottom (), false));
        g.fillRoundedRectangle (dp, 4);
        g.setColour (juce::Colour (0xff2a2a2d));
        g.drawRoundedRectangle (dp.reduced (1.5f), 4, 3);

        // LCD
        const auto lcd = toRect (kLcd).toFloat ();
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xffc3ea58), 0, lcd.getY (), juce::Colour (0xffa9d53c),
                                                 0, lcd.getBottom (), false));
        g.fillRoundedRectangle (lcd, 4);
        g.setColour (juce::Colour (0xff0a0a0a));
        g.drawRoundedRectangle (lcd.reduced (3), 4, 6);
        g.setColour (col::lcdInk);
        g.setFont (sk.lcd (40, .04f));
        g.drawText (ed.lcdLine1 (), toRect (kL1), juce::Justification::centredLeft);
        g.drawText (ed.lcdLine2, toRect (kL2), juce::Justification::centredLeft);

        // wet input legend
        const char* names[] = {"MONO", "STEREO", "TRUE STEREO"};
        const Rect* panes[] = {&kPaneMono, &kPaneStereo, &kPaneTrue};
        const int mode = juce::roundToInt (ed.proc.apvts.getRawParameterValue (id::wetInput)->load ());
        for (int i = 0; i < 3; ++i)
        {
            const auto r = toRect (*panes[i]);
            const bool on = i == mode;
            if (on)
            {
                g.setGradientFill (juce::ColourGradient (juce::Colour (0x48ff3b2f), (float) r.getCentreX (),
                                                         (float) r.getCentreY (), juce::Colour (0x00ff3b2f),
                                                         (float) r.getX (), (float) r.getCentreY (), true));
                g.fillRect (r);
            }
            g.setFont (sk.label (18, .12f).boldened ());
            g.setColour (on ? juce::Colour (0xff4a1f1b) : juce::Colour (0xff3b1d1a));
            if (on)
            {
                g.setColour (juce::Colour (0xffff3b2f).withAlpha (.35f));
                g.drawText (names[i], r.expanded (2), juce::Justification::centred);
                g.setColour (juce::Colour (0xffff5547));
            }
            g.drawText (names[i], r, juce::Justification::centred);
        }
    }

    void paintKnobLabels (juce::Graphics& g) const
    {
        const auto& sk = Skin::get ();
        g.setFont (sk.label (19));
        for (int i = 0; i < ed.knobs.size (); ++i)
        {
            const auto* k = ed.knobs[i];
            if (!k->isVisible () || k->def.view == 3) // the drawer paints its own
                continue;
            g.setColour (k->isEnabled () ? col::ink : col::ink.withAlpha (kUnusedAlpha));
            g.drawText (juce::String (k->def.label).toUpperCase (), toRect (k->def.labelR).expanded (20, 0),
                        juce::Justification::centred);
        }

        // inert era-5 plates while era 7 is selected
        if (ed.era == 0)
            for (const auto& d : kKnobs)
                if (d.era5 && d.view != 3 && (d.fxMask == 0 || (d.fxMask & ed.fxBit)))
                    drawPlate (
                        g, toRect (d.labelR).getUnion (toRect (d.dial)).getUnion (toRect (d.value)).expanded (6, 2));
    }

    void paintMeter (juce::Graphics& g, const Rect& colsR, int firstCh, bool scaleLeft, const char* name) const
    {
        const auto& sk = Skin::get ();
        const auto cols = toRect (colsR);
        constexpr int colW = 22, colGap = 5, segGap = 4;
        const float segH = ((float) cols.getHeight () - 7.0f * segGap) / 8.0f;

        for (int c = 0; c < 2; ++c)
        {
            const float x = (float) cols.getX () + (float) c * (colW + colGap);
            for (int i = 0; i < 8; ++i)
            {
                const bool on =
                    i == 0 ? ed.clip[(size_t) (firstCh + c)] : ed.dispDb[(size_t) (firstCh + c)] >= kSegThresholdDb[i];
                const juce::Rectangle<float> r (x, (float) cols.getY () + (float) i * (segH + segGap), (float) colW,
                                                segH);
                const auto lit = i < 2    ? juce::Colour (0xffff3b2f)
                                 : i == 2 ? juce::Colour (0xfff2d83a)
                                          : juce::Colour (0xff3ddc3d);
                if (on)
                {
                    g.setColour (lit.withAlpha (.25f));
                    g.fillRoundedRectangle (r.expanded (2), 3);
                }
                g.setColour (on ? lit : juce::Colour (0xff1a1a1b));
                g.fillRoundedRectangle (r, 2);
            }
        }

        const float rowH = (float) cols.getHeight () / 8.0f;
        const int sx = scaleLeft ? cols.getX () - 6 - 34 : cols.getRight () + 6;
        g.setColour (col::ink);
        g.setFont (sk.label (13, 0));
        for (int i = 0; i < 8; ++i)
            g.drawText (kScale[i],
                        juce::Rectangle<float> ((float) sx, (float) cols.getY () + rowH * (float) i, 34.0f, rowH),
                        juce::Justification::centred);

        g.setFont (sk.label (15, .14f));
        g.drawText (name, cols.getX (), cols.getBottom () + 5, cols.getWidth (), 16, juce::Justification::centred);
    }
};

// ---- INT PARAM drawer: the REV5's fine controls (docs/design/ui-layout.md) ----------------------------
class YrevEditor::IntParamDrawer final : public Overlay
{
public:
    explicit IntParamDrawer (YrevEditor& e) : ed (e)
    {
        panel = toRect (kDrawer.r);
        setBounds (0, 0, kDesignW, kDesignH);
    }

    void paint (juce::Graphics& g) override
    {
        Overlay::paint (g); // dims the panel behind, draws the drawer body
        const auto& sk = Skin::get ();

        const auto t = toRect (kDrawer.titleR);
        const auto title = juce::String (kDrawer.title).toUpperCase ();
        const auto font = sk.label (28, .12f);
        g.setFont (font);
        g.setColour (col::ink);
        g.drawText (title, t, juce::Justification::centredLeft);
        const float x0 = (float) t.getX () + juce::GlyphArrangement::getStringWidth (font, title) + 14.0f;
        g.setColour (col::rule);
        g.fillRect (x0, (float) t.getCentreY () + 3.0f, (float) t.getRight () - x0, 2.0f);

        g.setFont (sk.label (19, .14f));
        g.setColour (col::inkDim);
        for (const auto& gr : kDrawerGroups)
            g.drawText (juce::String (gr.title).toUpperCase (), toRect (gr.r).expanded (40, 0),
                        juce::Justification::centred);

        g.setFont (sk.label (19));
        for (auto* k : ed.knobs)
            if (k->def.view == 3 && k->isVisible ())
            {
                g.setColour (k->isEnabled () ? col::ink : col::ink.withAlpha (kUnusedAlpha));
                g.drawText (juce::String (k->def.label).toUpperCase (), toRect (k->def.labelR).expanded (20, 0),
                            juce::Justification::centred);
            }
        if (ed.era == 0) // inert era-5 plates
            for (const auto& d : kKnobs)
                if (d.view == 3 && d.era5)
                    drawPlate (
                        g, toRect (d.labelR).getUnion (toRect (d.dial)).getUnion (toRect (d.value)).expanded (6, 2));
    }

private:
    YrevEditor& ed;
};

// ---- Editor ---------------------------------------------------------------------------------------
YrevEditor::YrevEditor (YrevProcessor& p) : juce::AudioProcessorEditor (p), proc (p)
{
    setLookAndFeel (&lnf);
    canvas = std::make_unique<Canvas> (*this);
    addAndMakeVisible (*canvas);

    tipsOn = (bool) proc.apvts.state.getProperty ("tooltips", true);
    buildKnobs ();
    buildKeys ();
    applyTooltips ();

    browser = std::make_unique<PresetBrowser> (proc.presets, [this] (const juce::String& m) { say (m); });
    browser->onClose = [this] { canvas->repaint (); };
    canvas->addChildComponent (*browser);
    settingsPanel = std::make_unique<SettingsPanel> (proc.apvts, [this] (int pct) { setSize (pct * 10, pct * 4); });
    settingsPanel->onClose = [this] { canvas->repaint (); };
    canvas->addChildComponent (*settingsPanel);
    applyTooltips (); // again, now the overlays' keys exist
    drawer = std::make_unique<IntParamDrawer> (*this);
    drawer->onClose = [this]
    {
        if (intKey != nullptr)
            intKey->setToggleState (false, juce::dontSendNotification);
        canvas->repaint ();
    };
    canvas->addChildComponent (*drawer);
    for (auto* k : knobs)
        if (k->def.view == 3)
            drawer->addAndMakeVisible (k); // same coordinates: the drawer covers the whole canvas
    for (auto* k : keys)
        if (k->def.view == 3)
            drawer->addAndMakeVisible (k);
    setResizable (true, false);
    setResizeLimits (kMinW, (int) (kMinW / 2.5), kMaxW, (int) (kMaxW / 2.5));
    getConstrainer ()->setFixedAspectRatio (2.5);
    setSize (kDefaultW, kDefaultH);
    if (proc.apvts.state.hasProperty ("uiScale"))
    {
        const int pct = juce::jlimit (75, 250, (int) proc.apvts.state.getProperty ("uiScale"));
        setSize (pct * 10, pct * 4);
    }

    hiddenMode = proc.presets.current () >= 0 && proc.presets.at (proc.presets.current ()).bank == "SECRET";
    refresh (true);
    startTimerHz (kTimerHz);
}

YrevEditor::~YrevEditor ()
{
    stopTimer ();
    setLookAndFeel (nullptr);
}

void YrevEditor::resized ()
{
    canvas->setTransform (juce::AffineTransform::scale ((float) getWidth () / (float) kDesignW));
}

int YrevEditor::radioSignature () const
{
    int sig = 0;
    for (auto pid : {id::erMode, id::fxType, id::fxOrder, id::modType, id::pitchSel, id::panDir})
        sig = sig * 13 + juce::roundToInt (proc.apvts.getRawParameterValue (pid)->load ());
    return sig;
}

void YrevEditor::applyTooltips ()
{
    for (auto* k : keys)
    {
        // band-shape keys show Peak or Shelf by their state, not their layout label
        const auto tipKey = k->def.view == 2 ? juce::String (k->getToggleState () ? "Shelf" : "Peak")
                                             : juce::String::fromUTF8 (k->def.label);
        k->setTooltip (tipsOn ? tipFor (kTips_keys, tipKey) : juce::String ());
        if (juce::String (k->def.label) == "?")
            k->setToggleState (tipsOn, juce::dontSendNotification);
    }
    for (auto* k : knobs)
    {
        k->setTooltip (tipsOn ? tipFor (kTips_knobs, k->def.id) : juce::String ());
        for (auto* c : k->getChildren ())
            if (auto* l = dynamic_cast<juce::Label*> (c))
                l->setTooltip (tipsOn ? tipFor (kTips_areas, "value") : juce::String ());
    }
    // overlay keys (settings, preset browser): looked up by their label, which is the button's name
    for (juce::Component* overlay : {(juce::Component*) browser.get (), (juce::Component*) settingsPanel.get ()})
        if (overlay != nullptr)
            for (auto* c : overlay->getChildren ())
                if (auto* b = dynamic_cast<juce::Button*> (c))
                    b->setTooltip (tipsOn ? tipFor (kTips_overlay, b->getName ()) : juce::String ());
}

bool YrevEditor::eqDigital () const
{
    return era == 1 && proc.apvts.getRawParameterValue (id::eqView)->load () > .5f;
}

int YrevEditor::eqViewNow () const
{
    return eqDigital () ? 2 : 1;
}

juce::String YrevEditor::lcdLine1 () const
{
    const auto name = proc.presets.currentName ();
    auto s = (name.isEmpty () ? juce::String ("INIT") : name).toUpperCase ().substring (0, 24);
    if (proc.presets.isEdited () || (name.isEmpty () && false))
        s += " *";
    return s;
}

void YrevEditor::stepPreset (int delta)
{
    const int n = proc.presets.size ();
    if (n == 0)
        return;
    const int cur = proc.presets.current ();
    int next = cur < 0 ? (delta > 0 ? 0 : n - 1) : (cur + delta + n) % n;
    // Prev / Next skip the hidden bank (shift-click the unlit era key to reach it)
    for (int guard = 0; guard < n && proc.presets.at (next).bank == "SECRET"; ++guard)
        next = (next + delta + n) % n;
    proc.presets.load (next);
    say ("RT = " + formatValue ("s", proc.apvts.getRawParameterValue (id::rt)->load ()));
}

void YrevEditor::storeCurrentPreset ()
{
    auto& pm = proc.presets;
    const int cur = pm.current ();
    if (cur >= 0 && pm.at (cur).user)
    {
        say (pm.storeCurrent () ? "STORED" : "STORE FAILED");
        return;
    }
    const auto base = (cur >= 0 ? pm.at (cur).name : juce::String ("User preset")) + " ed";
    auto name = base;
    for (int n = 2; !pm.storeAs (name) && n < 100; ++n)
        name = base + " " + juce::String (n);
    say ("STORED");
}

void YrevEditor::openPresets ()
{
    browser->open ();
}
void YrevEditor::openHiddenPresets ()
{
    hiddenMode = true;
    browser->openHidden ();
    refresh (false); // both era keys light at once
}
void YrevEditor::openIntParam ()
{
    drawer->open ();
    if (intKey != nullptr)
        intKey->setToggleState (true, juce::dontSendNotification);
}

void YrevEditor::openSettings ()
{
    settingsPanel->open ();
}

void YrevEditor::say (const juce::String& line2)
{
    lcdLine2 = line2.toUpperCase ().substring (0, 16);
    lcdUntil = juce::Time::getMillisecondCounter () + kLcdMessageMs;
    canvas->repaint (toRect (kLcd));
}

int YrevEditor::currentFxBit () const
{
    const int i = juce::jlimit (0, 5, juce::roundToInt (proc.apvts.getRawParameterValue (id::fxType)->load ()));
    return kFxBits[i];
}

void YrevEditor::buildKnobs ()
{
    for (const auto& d : kKnobs)
    {
        auto* k = knobs.add (new SkinKnob (d));
        canvas->addAndMakeVisible (k);

        const Bound* bound = nullptr;
        for (const auto& b : kKnobParams)
            if (juce::String (b.knobId) == d.id)
                bound = &b;

        juce::String fmt = (bound && bound->fmt != nullptr) ? bound->fmt : d.fmt;
        if (bound)
        {
            k->paramId = bound->paramId;
            auto* prm = proc.apvts.getParameter (bound->paramId);
            sliderAtt.push_back (std::make_unique<juce::SliderParameterAttachment> (*prm, *k, nullptr));
            k->setDoubleClickReturnValue (true, (double) prm->convertFrom0to1 (prm->getDefaultValue ()));
        }
        else
        {
            const double lo = d.lo, hi = d.hi, step = d.step;
            juce::NormalisableRange<double> r (
                lo, hi, [lo, hi, lg = d.log] (double, double, double t)
                { return lg ? lo * std::pow (hi / lo, t) : lo + (hi - lo) * t; },
                [lo, hi, lg = d.log] (double, double, double v)
                { return lg ? std::log (v / lo) / std::log (hi / lo) : (v - lo) / (hi - lo); },
                [lo, hi, step] (double, double, double v)
                {
                    if (step > 0)
                        v = lo + std::round ((v - lo) / step) * step;
                    return juce::jlimit (lo, hi, v);
                });
            k->setNormalisableRange (r);
            k->setValue (d.def, juce::dontSendNotification);
            k->setDoubleClickReturnValue (true, d.def);
        }

        const double lo = k->getMinimum (), hi = k->getMaximum ();
        k->textFromValueFunction = [fmt] (double v) { return formatValue (fmt, v); };
        k->valueFromTextFunction = [fmt, k, lo, hi] (const juce::String& t)
        { return parseValue (fmt, t, k->getValue (), lo, hi); };
        k->updateText ();

        k->onValueChange = [this, k, fmt]
        { say (juce::String (k->def.label) + " " + formatValue (fmt, k->getValue ())); };

        if (juce::String (d.id) == "mix")
            mixKnob = k;
        if (juce::String (d.id) == "edamp")
        {
            echoDampKnob = k;
            setEchoDampText ();
        }
    }
}

void YrevEditor::setEchoDampText ()
{
    auto* k = echoDampKnob;
    if (k == nullptr)
        return;
    const bool ratio = era == 1;
    k->textFromValueFunction = [ratio] (double v)
    {
        return ratio ? formatValue ("x", (double) yrev::echoDampToHigh ((float) v))
                     : juce::String (juce::roundToInt (v));
    };
    k->valueFromTextFunction = [ratio, k] (const juce::String& t)
    {
        if (!ratio)
            return parseValue ("int", t, k->getValue (), 0.0, 10.0);
        const double high = parseValue ("x", t, (double) yrev::echoDampToHigh ((float) k->getValue ()), 0.1, 1.0);
        return juce::jlimit (0.0, 10.0, high * 10.0);
    };
    k->onValueChange = [this, k] { say (juce::String (k->def.label) + " " + k->getTextFromValue (k->getValue ())); };
    k->updateText ();
}

void YrevEditor::buildKeys ()
{
    for (const auto& d : kKeys)
    {
        auto* k = keys.add (new SkinKey (d));
        canvas->addAndMakeVisible (k);
        k->setBounds (toRect (d.r));
        const juce::String label (d.label), group (d.group);

        // Parameter-bound toggles
        const char* pid = nullptr;
        for (const auto& b : kKeyParams)
            if (label == b.label)
                pid = b.paramId;
        if (pid != nullptr)
        {
            k->setClickingTogglesState (true);
            buttonAtt.push_back (
                std::make_unique<juce::ButtonParameterAttachment> (*proc.apvts.getParameter (pid), *k, nullptr));
            continue;
        }

        if (label == "On" && (group == "eqA" || group == "eqD"))
        {
            k->setClickingTogglesState (true);
            buttonAtt.push_back (std::make_unique<juce::ButtonParameterAttachment> (
                *proc.apvts.getParameter (group == "eqA" ? id::aeqOn : id::deqOn), *k, nullptr));
            continue;
        }

        if (label == "Analog" || label == "Digital")
        {
            const int idx = label == "Digital" ? 1 : 0;
            k->onClick = [this, idx]
            {
                if (auto* prm = proc.apvts.getParameter (id::eqView))
                    prm->setValueNotifyingHost ((float) idx);
            };
            continue;
        }

        if (d.view == 2 && (label == "Peak" || label == "Shelf"))
        {
            const char* pid = d.r.x < 2200 ? id::deqLoShelf : id::deqHiShelf;
            k->setClickingTogglesState (true);
            buttonAtt.push_back (
                std::make_unique<juce::ButtonParameterAttachment> (*proc.apvts.getParameter (pid), *k, nullptr));
            continue;
        }

        if (label == "?")
        {
            k->onClick = [this, k]
            {
                tipsOn = !tipsOn;
                proc.apvts.state.setProperty ("tooltips", tipsOn, nullptr);
                k->setToggleState (tipsOn, juce::dontSendNotification);
                applyTooltips ();
                say (tipsOn ? "TOOLTIPS ON" : "TOOLTIPS OFF");
            };
            continue;
        }

        if (group == "erakeys")
        {
            const int idx = label == "7" ? 0 : 1;
            k->onClick = [this, idx]
            {
                // hidden bank: shift-click the era key that is not lit opens the SECRET presets; the era stays
                const bool lit = juce::roundToInt (proc.apvts.getRawParameterValue (id::era)->load ()) == idx;
                if (juce::ModifierKeys::getCurrentModifiers ().isShiftDown () && !lit && !hiddenMode)
                {
                    openHiddenPresets ();
                    return;
                }
                // a plain click leaves CLA mode (the loaded preset keeps playing) and switches era as normal
                const bool leaving = hiddenMode;
                if (leaving)
                {
                    hiddenMode = false;
                    if (browser->showingHidden ())
                        browser->close ();
                }
                if (auto* prm = proc.apvts.getParameter (id::era))
                    prm->setValueNotifyingHost (prm->convertTo0to1 ((float) idx));
                say (idx == 0 ? "ERA 7 (1985)" : "ERA 5 (1987)");
                if (leaving)
                    refresh (false); // one key lit again, also when the era does not change
            };
            continue;
        }

        if (const auto rg = radioGroupOf (group); rg.isNotEmpty ())
        {
            const char* choiceId = nullptr;
            for (const auto& r : kRadioParams)
                if (rg == r.group)
                    choiceId = r.paramId;
            const int index = rg == "er" ? erModeIndexOf (label) : radioCount[rg]++;
            keyIndex[k] = index;
            if (choiceId != nullptr)
            {
                k->onClick = [this, choiceId, index]
                {
                    auto* prm = proc.apvts.getParameter (choiceId);
                    if (prm == nullptr)
                        return;
                    // the REV E/R key keeps whichever Rev set the preset chose; from any other mode it picks Rev Hall
                    if (choiceId == id::erMode && index == yrev::kRevHallErMode &&
                        yrev::isRevErMode (juce::roundToInt (prm->convertFrom0to1 (prm->getValue ()))))
                        return;
                    prm->setValueNotifyingHost (prm->convertTo0to1 ((float) index));
                };
            }
            else
            {
                k->onClick = [this, rg, label]
                {
                    radioSel[rg] = label;
                    refresh (true);
                };
            }
            continue;
        }

        // Momentary keys (preset, store, compare, init, setup): wired when the preset system exists.
        if (label.contains ("Prev"))
            k->onClick = [this] { stepPreset (-1); };
        else if (label.contains ("Next"))
            k->onClick = [this] { stepPreset (+1); };
        else if (label.contains ("Preset"))
            k->onClick = [this] { browser->open (); };
        else if (label == "Store")
            k->onClick = [this] { storeCurrentPreset (); };
        else if (label == "Compare")
            k->onClick = [this]
            {
                proc.presets.toggleCompare ();
                say (proc.presets.comparing () ? "COMPARE: STORED" : "COMPARE: EDITED");
            };
        else if (label == "Init")
            k->onClick = [this]
            {
                proc.presets.init ();
                say ("INITIALISED");
            };
        else if (label.contains ("Setup"))
            k->onClick = [this] { settingsPanel->open (); };
        else if (label == "Intparam")
        {
            intKey = k;
            k->onClick = [this, k]
            {
                if (drawer->isVisible ())
                    drawer->close ();
                else
                {
                    drawer->open ();
                    k->setToggleState (true, juce::dontSendNotification);
                }
            };
        }
    }
}

void YrevEditor::refresh (bool force)
{
    const int newEra = juce::roundToInt (proc.apvts.getRawParameterValue (id::era)->load ());
    const bool newSolo = proc.apvts.getRawParameterValue (id::wetSolo)->load () > .5f;
    const int newFx = currentFxBit ();
    const bool structural =
        force || newEra != era || newFx != fxBit || radioSignature () != radioSig || eqViewNow () != eqViewShown;
    eqViewShown = eqViewNow ();
    radioSig = radioSignature ();

    era = newEra;
    fxBit = newFx;
    if (newSolo != wetSolo || force)
    {
        wetSolo = newSolo;
        if (mixKnob != nullptr)
        {
            for (auto* c : mixKnob->getChildren ())
                if (auto* l = dynamic_cast<juce::Label*> (c))
                {
                    l->getProperties ().set ("solo", wetSolo);
                    l->repaint ();
                }
            mixKnob->textFromValueFunction = [this] (double v)
            { return wetSolo ? juce::String ("WET") : formatValue ("pct", v); };
            mixKnob->updateText ();
        }
    }
    if (structural)
        setEchoDampText ();

    // Radio and era key states; both era keys light while the hidden bank is open or one of its presets is loaded
    const int curPreset = proc.presets.current ();
    const bool hiddenLit =
        hiddenMode && (browser->showingHidden () || (curPreset >= 0 && proc.presets.at (curPreset).bank == "SECRET"));
    if (!hiddenLit)
        hiddenMode = false; // a normal preset loaded or the hidden list closed without a pick
    for (auto* k : keys)
    {
        const juce::String label (k->def.label), group (k->def.group);
        if (group == "erakeys")
            k->setToggleState (hiddenLit || (label == "7") == (era == 0), juce::dontSendNotification);
        else if (const auto rg = radioGroupOf (group); rg.isNotEmpty ())
        {
            const char* choiceId = nullptr;
            for (const auto& r : kRadioParams)
                if (rg == r.group)
                    choiceId = r.paramId;
            const int value =
                choiceId != nullptr ? juce::roundToInt (proc.apvts.getRawParameterValue (choiceId)->load ()) : -1;
            const bool revKey = choiceId == id::erMode && keyIndex[k] == yrev::kRevHallErMode;
            const bool on = choiceId != nullptr ? (revKey ? yrev::isRevErMode (value) : value == keyIndex[k])
                                                : radioSel[rg] == label;
            k->setToggleState (on, juce::dontSendNotification);
        }

        const bool inFx = k->def.fxMask == 0 || (k->def.fxMask & fxBit) != 0;
        const bool inView = k->def.view == 0 || k->def.view == 3 || k->def.view == eqViewNow ();
        k->setVisible (inFx && inView && !isHiddenModeKey (k->def));
        k->setPlate (k->def.era5 && era == 0);

        if (label == "Analog" || label == "Digital")
            k->setToggleState ((label == "Digital") == eqDigital (), juce::dontSendNotification);
        else if (k->def.view == 2)
        {
            const juce::String shape = k->getToggleState () ? "Shelf" : "Peak";
            k->setLegend (shape.toUpperCase ());
            k->setTooltip (tipsOn ? tipFor (kTips_keys, shape) : juce::String ()); // follows the shape it shows
        }
    }

    if (force || newEra != lastLimitEra)
    {
        lastLimitEra = newEra;
        for (auto* k : knobs)
            if (k->paramId != nullptr)
                if (const auto* l = eraLimitFor (k->paramId))
                {
                    auto* prm = dynamic_cast<juce::RangedAudioParameter*> (proc.apvts.getParameter (k->paramId));
                    k->setLimits (newEra == 0 ? l->min7 : l->min5, newEra == 0 ? l->max7 : l->max5,
                                  prm->getNormalisableRange ().interval);
                    k->setValue (prm->convertFrom0to1 (prm->getValue ()), juce::dontSendNotification);
                }
    }

    const int modType = juce::roundToInt (proc.apvts.getRawParameterValue (id::modType)->load ());
    const int pitchSel = juce::roundToInt (proc.apvts.getRawParameterValue (id::pitchSel)->load ());
    for (auto* k : knobs)
    {
        const bool inFx = k->def.fxMask == 0 || (k->def.fxMask & fxBit) != 0;
        const bool inView = k->def.view == 0 || k->def.view == 3 || k->def.view == eqViewNow ();
        k->setVisible (inFx && inView && !(k->def.era5 && era == 0));
        const bool used = knobInUse (k->def.id, modType, pitchSel);
        k->setEnabled (used);
        k->setAlpha (used ? 1.0f : kUnusedAlpha);
    }

    if (intKey != nullptr)
        intKey->setBadge (drawerInUse ());

    if (structural)
    {
        canvas->repaint ();
        if (drawer->isVisible ())
            drawer->repaint ();
    }
}

bool YrevEditor::drawerInUse () const
{
    // any drawer control away from its default that acts in this era (era-5-only ones are inert in era 7)
    for (auto* k : knobs)
        if (k->def.view == 3 && k->paramId != nullptr && !(k->def.era5 && era == 0))
            if (auto* prm = proc.apvts.getParameter (k->paramId))
                if (std::abs (prm->getValue () - prm->getDefaultValue ()) > 1.0e-4f)
                    return true;
    return proc.apvts.getRawParameterValue (id::duckOn)->load () > .5f;
}

void YrevEditor::timerCallback ()
{
    refresh (false);

    // meters: peak with a fixed fall rate; clip lamps latch until clicked
    bool changedIn = false, changedOut = false;
    for (size_t i = 0; i < 4; ++i)
    {
        const float peak = proc.meterPeak[i].exchange (0.0f, std::memory_order_relaxed);
        const float db = peak > 1.0e-5f ? juce::Decibels::gainToDecibels (peak) : -90.0f;
        const float next = juce::jmax (db, dispDb[i] - kMeterFallDb);
        (i < 2 ? changedIn : changedOut) |=
            (std::abs (next - dispDb[i]) > 1.0e-4f) || (peak >= kClipLinear && !clip[i]);
        dispDb[i] = next;
        if (peak >= kClipLinear)
            clip[i] = true;
    }
    {
        float sig = 0;
        for (auto pid : {id::aeqOn,   id::aeqLoF, id::aeqLoG, id::aeqMidF, id::aeqMidG,    id::aeqHiF,  id::aeqHiG,
                         id::deqOn,   id::deqLoF, id::deqLoG, id::deqLoQ,  id::deqLoShelf, id::deqMidF, id::deqMidG,
                         id::deqMidQ, id::deqHiF, id::deqHiG, id::deqHiQ,  id::deqHiShelf, id::eqView})
            sig = sig * 1.0001f + proc.apvts.getRawParameterValue (pid)->load ();
        if (std::abs (sig - eqSig) > 1.0e-6f)
        {
            eqSig = sig;
            canvas->repaint (toRect (kEqGraph).getUnion (toRect (kEqGraphDigital)).expanded (60, 40));
        }
    }
    if (changedIn)
        canvas->repaint (toRect (kMeterInCols).expanded (50, 24));
    if (changedOut)
        canvas->repaint (toRect (kMeterOutCols).expanded (50, 24));

    {
        for (auto* k : keys)
            if (juce::String (k->def.label) == "Compare")
                k->setToggleState (proc.presets.comparing (), juce::dontSendNotification);
        const auto l1 = lcdLine1 ();
        if (l1 != lastL1)
        {
            lastL1 = l1;
            canvas->repaint (toRect (kLcd));
        }
        if (browser->isVisible ())
            browser->refresh ();
    }

    if (lcdUntil != 0 && juce::Time::getMillisecondCounter () > lcdUntil)
    {
        lcdUntil = 0;
        lcdLine2 = {};
    }

    if (lcdUntil == 0)
    {
        const auto rt = "RT = " + formatValue ("s", proc.apvts.getRawParameterValue (id::rt)->load ());
        if (rt != lcdLine2)
        {
            lcdLine2 = rt;
            canvas->repaint (toRect (kLcd));
        }
    }
}
