#pragma once

#include <array>
#include <map>
#include <memory>
#include <vector>

#include "PluginProcessor.h"
#include "ui/Overlays.h"
#include "ui/Skin.h"
#include "ui/SkinKey.h"
#include "ui/SkinKnob.h"

// Front-panel editor. All geometry is in the 2500 x 1000 design space (ui/UiLayout.h, generated from the
// approved prototype); the canvas is scaled to fit, so the window is freely resizable at a fixed 2.5:1.
class YrevEditor final : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit YrevEditor (YrevProcessor&);
    ~YrevEditor () override;

    void resized () override;
    void openPresets ();
    void openHiddenPresets (); // the SECRET bank (also: shift-click the unlit era key)
    void openSettings ();
    void openIntParam (); // the INT PARAM drawer

private:
    class Canvas;
    class IntParamDrawer;

    void timerCallback () override;
    void buildKeys ();
    void buildKnobs ();
    void refresh (bool force);
    void say (const juce::String& line2);
    bool eqDigital () const;
    int eqViewNow () const;
    void applyTooltips ();
    int currentFxBit () const;

    // Keeps the skin (images, fonts) alive while an editor exists. Declared first so it is destroyed last, after every
    // component that paints with it.
    juce::SharedResourcePointer<yrev::ui::Skin> skinHandle;

    YrevProcessor& proc;
    yrev::ui::PanelLookAndFeel lnf;
    std::unique_ptr<Canvas> canvas;
    std::unique_ptr<yrev::ui::PresetBrowser> browser;
    std::unique_ptr<yrev::ui::SettingsPanel> settingsPanel;
    std::unique_ptr<IntParamDrawer> drawer; // the INT PARAM drawer (view 3 controls)
    yrev::ui::SkinKey* intKey = nullptr;
    bool drawerInUse () const;
    juce::String lastL1, lastRt;
    juce::String lcdLine1 () const;
    void stepPreset (int delta);
    void storeCurrentPreset ();

    juce::OwnedArray<yrev::ui::SkinKey> keys;
    juce::OwnedArray<yrev::ui::SkinKnob> knobs;
    std::vector<std::unique_ptr<juce::SliderParameterAttachment>> sliderAtt;
    std::vector<std::unique_ptr<juce::ButtonParameterAttachment>> buttonAtt;

    // Non-parameter panel state (the engine parameters arrive with the DSP; see docs/design/parameters.md).
    std::map<juce::String, juce::String> radioSel; // group -> selected label
    std::map<juce::String, int> radioCount;
    std::map<const yrev::ui::SkinKey*, int> keyIndex;
    int radioSig = -1;
    int radioSignature () const;
    juce::String lcdLine2;
    juce::uint32 lcdUntil = 0;
    bool wetSolo = false;
    // CLA mode: both era keys lit. On only by shift-clicking the unlit era key (or opening the editor on a SECRET
    // preset); any plain era-key click leaves it and switches era as normal.
    bool hiddenMode = false;
    bool tipsOn = true;
    juce::TooltipWindow tooltipWindow{this, 600};
    float eqSig = 0;
    int eqViewShown = 1;
    int lastLimitEra = -1;
    int era = 0, fxBit = 1;
    juce::Slider* mixKnob = nullptr;
    yrev::ui::SkinKnob* echoDampKnob = nullptr; // shows HIGH DAMP 0-10 in era 7, the REV5's HIGH ratio in era 5
    void setEchoDampText ();

    // Meters
    std::array<float, 4> dispDb{-90, -90, -90, -90}; // in L, in R, out L, out R
    std::array<bool, 4> clip{false, false, false, false};

    friend class Canvas;
    friend class IntParamDrawer;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (YrevEditor)
};
