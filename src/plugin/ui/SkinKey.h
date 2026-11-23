#pragma once

#include "Skin.h"
#include "UiLayout.h"

namespace yrev::ui
{
    // A front-panel key. Shows the pushed image only while the mouse is held; the parameter changes on release.
    class SkinKey final : public juce::Button
    {
        juce::String ownLabel; // declared first: `def` may refer to `own`
        std::unique_ptr<KeyDef> own;

    public:
        explicit SkinKey (const KeyDef& d);
        SkinKey (const juce::String& label, int w, int h, bool rect = false); // free-standing key (overlays)

        // Era-5-only keys are inert blank plates while era 7 is selected.
        void setPlate (bool isPlate);
        // A small amber dot in the corner (the INT PARAM key: one of its hidden controls is in use).
        void setBadge (bool on)
        {
            if (badge != on)
            {
                badge = on;
                repaint ();
            }
        }
        void setLegend (const juce::String& legend)
        {
            if (text != legend)
            {
                text = legend;
                repaint ();
            }
        }
        const KeyDef& def;

        void paintButton (juce::Graphics&, bool over, bool down) override;

    private:
        enum class Icon
        {
            none,
            left,
            right,
            down,
            cog
        };
        Icon icon = Icon::none;
        juce::String text;
        bool numeral = false, plate = false, badge = false;

        void drawIcon (juce::Graphics&, juce::Rectangle<float>, float scale) const;
    };
} // namespace yrev::ui
