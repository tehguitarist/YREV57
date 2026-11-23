#include "SkinKnob.h"

#include <cmath>

namespace yrev::ui
{
    namespace
    {
        constexpr float kSweep = juce::MathConstants<float>::pi * 0.75f; // +/-135 degrees
        constexpr int kTicks = 11;
        constexpr int kTickPad = 10;

        juce::String trimNum (double v, int dp)
        {
            auto s = juce::String (v, dp);
            if (s.contains ("."))
                s = s.trimCharactersAtEnd ("0").trimCharactersAtEnd (".");
            return s;
        }

        juce::String signedStr (double v, int dp)
        {
            return (v > 0 ? "+" : "") + juce::String (v, dp);
        }
    } // namespace

    juce::String formatValue (const juce::String& f, double v)
    {
        if (f == "ms")
            return v >= 1000 ? juce::String (v / 1000, 1) + " S"
                             : (v < 10 ? juce::String (v, 1) : juce::String (juce::roundToInt (v))) + " MS";
        if (f == "s")
            return juce::String (v, 1) + " S";
        if (f == "pct")
            return juce::String (juce::roundToInt (v)) + " %";
        if (f == "x")
            return juce::String (v, 2);
        if (f == "x1")
            return juce::String (v, 1) + " X";
        if (f == "hz")
            return v >= 1000 ? trimNum (v / 1000, 1) + " KHZ" : juce::String (juce::roundToInt (v)) + " HZ";
        if (f == "db")
            return signedStr (v, 1) + " DB";
        if (f == "out")
            return v <= -59.5 ? juce::String ("-INF DB") : signedStr (v, 1) + " DB";
        if (f == "lpf")
            return v >= 16.5 ? juce::String ("THRU") : trimNum (v, v < 10 ? 2 : 1) + " KHZ";
        if (f == "st")
            return (v > 0 ? "+" : "") + juce::String (juce::roundToInt (v)) + " ST";
        if (f == "ct")
            return (v > 0 ? "+" : "") + juce::String (juce::roundToInt (v)) + " CT";
        if (f == "sgn")
            return (v > 0 ? "+" : "") + juce::String (juce::roundToInt (v)) + " %";
        if (f == "rate")
            return (v < 10 ? juce::String (v, 1) : juce::String (juce::roundToInt (v))) + " HZ";
        if (f == "q")
            return juce::String (v, 1);
        if (f == "in1")
            return juce::String (v, 1);
        return juce::String (juce::roundToInt (v)); // "int"
    }

    double parseValue (const juce::String& f, const juce::String& textIn, double current, double lo, double hi)
    {
        auto t = textIn.trim ().toLowerCase ();
        if (t.contains ("inf"))
            return lo;
        if (t.contains ("thru"))
            return hi;
        if (!t.containsAnyOf ("0123456789"))
            return current;

        double v = t.retainCharacters ("0123456789.-+").getDoubleValue ();
        const bool kilo = t.contains ("k");
        const bool secs = t.endsWithChar ('s') && !t.endsWith ("ms") && f == "ms";
        if (kilo && f == "hz")
            v *= 1000.0;
        if (secs)
            v *= 1000.0;
        return juce::jlimit (lo, hi, v);
    }

    // ---- Look and feel -------------------------------------------------------------------
    PanelLookAndFeel::PanelLookAndFeel ()
    {
        setColour (juce::Label::textColourId, col::led);
        setColour (juce::TextEditor::backgroundColourId, juce::Colour (0xff0d0506));
        setColour (juce::TextEditor::textColourId, col::led);
        setColour (juce::TextEditor::highlightColourId, col::led.withAlpha (.35f));
        setColour (juce::TextEditor::highlightedTextColourId, juce::Colours::white);
        setColour (juce::TextEditor::outlineColourId, juce::Colour (0xff6a2a26));
        setColour (juce::TextEditor::focusedOutlineColourId, juce::Colour (0xff6a2a26));
        setColour (juce::CaretComponent::caretColourId, col::led);
    }

    juce::Label* PanelLookAndFeel::createSliderTextBox (juce::Slider& s)
    {
        auto* l = new juce::Label ();
        l->setJustificationType (juce::Justification::centred);
        l->setMouseCursor (juce::MouseCursor::IBeamCursor);
        juce::ignoreUnused (s);
        return l;
    }

    juce::Font PanelLookAndFeel::getLabelFont (juce::Label& l)
    {
        return Skin::get ().lcd (l.getHeight () > 33 ? 27.0f : 21.0f);
    }

    void PanelLookAndFeel::drawLabel (juce::Graphics& g, juce::Label& l)
    {
        const auto b = l.getLocalBounds ().toFloat ();
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff060203), 0, b.getY (), juce::Colour (0xff0d0506), 0,
                                                 b.getBottom (), false));
        g.fillRoundedRectangle (b, 3);
        g.setColour (juce::Colour (0xff34343a));
        g.drawRoundedRectangle (b.expanded (.5f), 3.5f, 1.0f);
        g.setColour (juce::Colour (0xff040404));
        g.drawRoundedRectangle (b.reduced (1), 3, 2.0f);

        if (l.isBeingEdited ())
            return;

        const bool solo = (bool) l.getProperties ().getWithDefault ("solo", false);
        const auto c = solo ? col::amber : col::led;
        const auto text = l.getText ();
        g.setFont (getLabelFont (l));
        const auto area = l.getLocalBounds ().reduced (4, 1).translated (0, 2);
        g.setColour (c.withAlpha (.22f));
        g.drawText (text, area.translated (0, 0), juce::Justification::centred);
        g.setColour (c.withAlpha (.35f));
        g.drawText (text, area.expanded (1), juce::Justification::centred);
        g.setColour (c);
        g.drawText (text, area, juce::Justification::centred);
    }

    juce::Slider::SliderLayout PanelLookAndFeel::getSliderLayout (juce::Slider& s)
    {
        auto& k = static_cast<SkinKnob&> (s);
        return {k.dialLocal, k.valueLocal};
    }

    void PanelLookAndFeel::drawRotarySlider (juce::Graphics& g, int, int, int, int, float pos, float, float,
                                             juce::Slider& s)
    {
        auto& k = static_cast<SkinKnob&> (s);
        const auto d = k.dialLocal.toFloat ();
        const auto c = d.getCentre ();
        const float r = d.getWidth () * .5f;
        const bool dim = s.getProperties ().getWithDefault ("dim", false);

        g.setColour (col::ink.withAlpha (dim ? .4f : .85f));
        for (int i = 0; i < kTicks; ++i)
        {
            const float a = -kSweep + (2.0f * kSweep) * (float) i / (float) (kTicks - 1);
            const float sx = std::sin (a), cy = -std::cos (a);
            g.drawLine (c.x + sx * (r + 3), c.y + cy * (r + 3), c.x + sx * (r + kTickPad - 1),
                        c.y + cy * (r + kTickPad - 1), 2.0f);
        }

        const float angle = -kSweep + pos * (2.0f * kSweep);
        const float capD = d.getWidth () * .94f;
        const auto& cap = Skin::get ().knobCap;
        const auto dest = juce::Rectangle<float> (capD, capD).withCentre (c);

        // soft drop shadow
        g.setColour (juce::Colours::black.withAlpha (.45f));
        g.fillEllipse (dest.translated (0, 4).expanded (1));

        g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
        g.setOpacity (dim ? .55f : 1.0f);
        g.drawImageTransformed (
            cap, juce::AffineTransform::scale (capD / (float) cap.getWidth (), capD / (float) cap.getHeight ())
                     .translated (dest.getX (), dest.getY ())
                     .rotated (angle, c.x, c.y));
    }

    // ---- Knob ----------------------------------------------------------------------------
    juce::Rectangle<int> SkinKnob::boundsFor (const KnobDef& d)
    {
        juce::Rectangle<int> b (d.dial.x, d.dial.y, d.dial.w, d.dial.h);
        b = b.expanded (kTickPad + 2);
        return b.getUnion ({d.value.x, d.value.y, d.value.w, d.value.h});
    }

    void SkinKnob::setLimits (double lo, double hi, double interval)
    {
        juce::NormalisableRange<double> r (lo, hi, interval);
        if (def.log && lo > 0.0)
            r.setSkewForCentre (std::sqrt (lo * hi));
        setNormalisableRange (r);
        updateText ();
    }

    SkinKnob::SkinKnob (const KnobDef& d) : def (d)
    {
        const auto bnds = boundsFor (d);
        setBounds (bnds);
        dialLocal =
            juce::Rectangle<int> (d.dial.x, d.dial.y, d.dial.w, d.dial.h).translated (-bnds.getX (), -bnds.getY ());
        valueLocal =
            juce::Rectangle<int> (d.value.x, d.value.y, d.value.w, d.value.h).translated (-bnds.getX (), -bnds.getY ());

        setSliderStyle (juce::Slider::RotaryVerticalDrag);
        setRotaryParameters (-kSweep, kSweep, true);
        setTextBoxStyle (juce::Slider::TextBoxBelow, false, d.value.w, d.value.h);
        setTextBoxIsEditable (true);
        setMouseDragSensitivity (260);
        setScrollWheelEnabled (true);
        setPopupDisplayEnabled (false, false, nullptr);
    }
} // namespace yrev::ui
