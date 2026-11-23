#include "SkinKey.h"

namespace yrev::ui
{
    namespace
    {
        // Labels that wrap differently on the key legend than the stored text.
        juce::String legendFor (const juce::String& raw)
        {
            if (raw == "LinkL/R")
                return "LINK\nL/R";
            if (raw == "Temposync")
                return "TEMPO\nSYNC";
            if (raw == "Duckon")
                return "DUCK\nON";
            if (raw == "Smallhall")
                return "SMALL\nHALL";
            if (raw == "Largehall")
                return "LARGE\nHALL";
            if (raw == "Revhall")
                return "REV\nHALL";
            if (raw == "Revplate")
                return "REV\nPLATE";
            if (raw == "LFOsync")
                return "LFO\nSYNC";
            if (raw == "FXwet")
                return "FX\nWET";
            if (raw == "Intparam")
                return "INT\nPARAM";
            if (raw == "Wetsolo")
                return "WET\nSOLO";
            if (raw == "Wet R invert")
                return "WET R\nINVERT";
            if (raw == "EQ on")
                return "EQ ON";
            if (raw == "Revflange")
                return "REV\nFLANGE";
            if (raw == "Sym-phonic")
                return "SYM-\nPHONIC";
            if (raw == "ChorusA")
                return "CHORUS\nA";
            if (raw == "ChorusB")
                return "CHORUS\nB";
            return raw.toUpperCase ();
        }
    } // namespace

    SkinKey::SkinKey (const juce::String& label, int w, int h, bool rect)
        : juce::Button (label), ownLabel (label),
          own (std::make_unique<KeyDef> (KeyDef{"", rect, "", false, 0, 0, {0, 0, w, h}})), def (*own)
    {
        text = legendFor (ownLabel);
        setSize (w, h);
        setClickingTogglesState (false);
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
    }

    SkinKey::SkinKey (const KeyDef& d) : juce::Button (d.label), def (d)
    {
        auto raw = juce::String::fromUTF8 (d.label);
        const auto first = raw.isEmpty () ? juce::juce_wchar (0) : raw[0];

        if (first == 0x25C0)
        {
            icon = Icon::left;
            raw = raw.substring (1);
        }
        else if (first == 0x25B6)
        {
            icon = Icon::right;
            raw = raw.substring (1);
        }
        else if (first == 0x25BC)
        {
            icon = Icon::down;
            raw = raw.substring (1);
        }
        else if (first == 0x2699)
        {
            icon = Icon::cog;
            raw = raw.substring (1);
        }

        numeral = juce::String (d.group) == "erakeys";
        text = numeral ? raw : legendFor (raw);
        setClickingTogglesState (false);
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
    }

    void SkinKey::setPlate (bool p)
    {
        if (plate == p)
            return;
        plate = p;
        setInterceptsMouseClicks (!p, false);
        setWantsKeyboardFocus (!p);
        repaint ();
    }

    void SkinKey::drawIcon (juce::Graphics& g, juce::Rectangle<float> r, float) const
    {
        juce::Path p;
        const auto c = r.getCentre ();
        const float h = r.getHeight ();
        switch (icon)
        {
        case Icon::left:
            p.addTriangle (c.x + h * .45f, c.y - h * .5f, c.x + h * .45f, c.y + h * .5f, c.x - h * .5f, c.y);
            break;
        case Icon::right:
            p.addTriangle (c.x - h * .45f, c.y - h * .5f, c.x - h * .45f, c.y + h * .5f, c.x + h * .5f, c.y);
            break;
        case Icon::down:
            p.addTriangle (c.x - h * .5f, c.y - h * .4f, c.x + h * .5f, c.y - h * .4f, c.x, c.y + h * .5f);
            break;
        case Icon::cog:
            p.addStar (c, 8, h * .38f, h * .52f, 0.0f);
            p.addEllipse (c.x - h * .3f, c.y - h * .3f, h * .6f, h * .6f);
            p.setUsingNonZeroWinding (false);
            break;
        default:
            return;
        }
        g.fillPath (p);
    }

    void SkinKey::paintButton (juce::Graphics& g, bool, bool down)
    {
        const auto b = getLocalBounds ().toFloat ();

        if (plate)
        {
            g.setColour (juce::Colour (0xff0c0c0d));
            g.fillRoundedRectangle (b.reduced (2), 4);
            g.setColour (juce::Colour (0xff1c1c1f));
            g.drawRoundedRectangle (b.reduced (2.5f), 4, 2);
            g.setColour (col::inkDim.withAlpha (.45f));
            g.setFont (Skin::get ().label (13));
            g.drawText ("5", getLocalBounds ().reduced (8, 6), juce::Justification::bottomRight);
            return;
        }

        const auto& sk = Skin::get ();
        const bool on = getToggleState ();
        g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
        g.setOpacity (isEnabled () ? 1.0f : .45f);
        g.drawImage (sk.key[def.rect ? 1 : 0][on ? 1 : 0][down ? 1 : 0], b, juce::RectanglePlacement::stretchToFit);
        if (badge)
        {
            const float d = b.getWidth () * .13f;
            const juce::Rectangle<float> dot (b.getRight () - d * 2.0f, b.getY () + d * .9f, d, d);
            g.setColour (juce::Colour (0xffffb02e).withAlpha (.35f));
            g.fillEllipse (dot.expanded (d * .45f));
            g.setColour (juce::Colour (0xffffb02e));
            g.fillEllipse (dot);
        }

        // Legend: slightly smaller and darker while pushed, to suggest the cap moving down.
        const float scale = down ? .93f : 1.0f;
        const float alpha = down ? .78f : 1.0f;
        const bool big = def.r.w >= 80;
        const bool mode = def.rect && def.r.w >= 100 && def.r.h >= 70;  // E/R mode keys
        const bool square = !def.rect && def.r.w >= 70 && def.r.w < 80; // FX, mod, pitch and pan keys
        const bool erSquare = !def.rect && def.r.w >= 100;              // E/R mode keys
        const float size = (mode || erSquare ? 19.0f : square ? 17.0f : big ? 16.0f : 14.0f) * scale;

        auto area =
            b.withTrimmedTop (b.getHeight () * .30f).withTrimmedBottom (b.getHeight () * .08f + 6.0f).reduced (2, 0);

        if (numeral)
        {
            g.setColour ((on ? juce::Colour (0xffff6a5c) : juce::Colour (0xff8f877a))
                             .withMultipliedBrightness (down ? .85f : 1.0f));
            g.setFont (sk.label (40.0f * scale, 0.0f));
            g.drawText (text, b.toNearestInt ().translated (0, 3),
                        juce::Justification::centred); // era numerals sit low
            return;
        }

        g.setColour (col::ink.withAlpha (alpha).withMultipliedBrightness (down ? .85f : 1.0f));
        g.setFont (sk.label (size));

        if (icon != Icon::none)
        {
            const float iconH = (icon == Icon::cog ? 19.0f : (big ? 13.0f : 11.0f)) * scale;
            const float gap = icon == Icon::cog ? 2.0f : 8.0f;
            const float textH = size * 1.05f;
            const float total = iconH + gap + textH;
            const float top = area.getCentreY () - total * .5f - 3.0f; // legends sit a touch high
            drawIcon (g, juce::Rectangle<float> (area.getX (), top, area.getWidth (), iconH), scale);
            g.drawText (text, juce::Rectangle<float> (area.getX (), top + iconH + gap, area.getWidth (), textH),
                        juce::Justification::centred);
            return;
        }

        if (text == "?")
        {
            g.setFont (sk.label (34.0f * scale, 0.0f));
            g.drawText (text, area.toNearestInt (), juce::Justification::centred);
            return;
        }

        g.drawFittedText (text, area.toNearestInt (), juce::Justification::centred, 2, 1.0f);
    }
} // namespace yrev::ui
