#pragma once

#include "Skin.h"
#include "UiLayout.h"

namespace yrev::ui
{
    juce::String formatValue (const juce::String& fmt, double v);
    double parseValue (const juce::String& fmt, const juce::String& text, double current, double lo, double hi);

    // Draws the knob cap (rotating image), tick marks, and the LED value screen.
    class PanelLookAndFeel final : public juce::LookAndFeel_V4
    {
    public:
        PanelLookAndFeel ();

        void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos, float start, float end,
                               juce::Slider&) override;
        juce::Slider::SliderLayout getSliderLayout (juce::Slider&) override;
        void drawLabel (juce::Graphics&, juce::Label&) override;
        juce::Font getLabelFont (juce::Label&) override;
        juce::Label* createSliderTextBox (juce::Slider&) override;
    };

    class SkinKnob final : public juce::Slider
    {
    public:
        explicit SkinKnob (const KnobDef& d);
        const KnobDef& def;
        juce::Rectangle<int> dialLocal, valueLocal; // relative to this component
        static juce::Rectangle<int> boundsFor (const KnobDef& d);
        const char* paramId = nullptr;                          // set for parameter-backed knobs
        void setLimits (double lo, double hi, double interval); // narrows the knob travel (era limits)
    };
} // namespace yrev::ui
