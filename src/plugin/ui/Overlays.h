#pragma once

#include <functional>
#include <memory>

#include "../Presets.h"
#include "SkinKey.h"

namespace yrev::ui
{
    // Full-canvas overlay: dims the panel, shows a panel of its own, closes on a click outside it or Escape.
    class Overlay : public juce::Component
    {
    public:
        Overlay ();
        void open ();
        void close ();
        std::function<void ()> onClose;

        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;
        bool keyPressed (const juce::KeyPress&) override;

    protected:
        juce::Rectangle<int> panel;
        void paintPanel (juce::Graphics&) const;
        virtual void opened () {}
    };

    // The preset list: banks, scrolling list, store / rename / delete / import / export.
    class PresetBrowser final : public Overlay, private juce::ListBoxModel
    {
    public:
        PresetBrowser (PresetManager&, std::function<void (const juce::String&)> say);
        void refresh ();    // after the presets or the current one changed
        void openHidden (); // opens on the SECRET bank (no tab; shift-click the unlit era key)
        bool showingHidden () const { return isVisible () && bank == "SECRET"; }

        void resized () override;
        void paint (juce::Graphics&) override;

    private:
        enum class Naming
        {
            none,
            store,
            rename
        };
        PresetManager& mgr;
        std::function<void (const juce::String&)> say;
        juce::String bank = "FACTORY";
        std::vector<int> rows;
        juce::ListBox list;
        SkinKey tabFactory, tabUser, tabCombi, tabRev7, tabRev5, bStore, bRename, bDelete, bImport, bExport;
        juce::TextEditor nameBox;
        Naming naming = Naming::none;
        juce::String note;
        bool hiddenNext = false;
        std::unique_ptr<juce::FileChooser> chooser;

        int getNumRows () override { return (int) rows.size (); }
        void paintListBoxItem (int row, juce::Graphics&, int w, int h, bool selected) override;
        void listBoxItemClicked (int row, const juce::MouseEvent&) override;
        void opened () override;
        void setBank (const juce::String& b);
        void beginNaming (Naming n);
        void finishNaming (bool accept);
        void updateKeys ();
    };

    // Plugin-wide settings, stored in the plugin state (not automatable parameters).
    class SettingsPanel final : public Overlay
    {
    public:
        SettingsPanel (juce::AudioProcessorValueTreeState&, std::function<void (int)> setUiScalePercent);
        void resized () override;
        void paint (juce::Graphics&) override;

    private:
        juce::AudioProcessorValueTreeState& apvts;
        std::function<void (int)> setScale;
        juce::OwnedArray<SkinKey> keys;
        SkinKey closeKey;
        struct Row
        {
            juce::String label;
            int y;
        };
        std::vector<Row> rows;
        juce::String latencyText;

        juce::String prop (const char* name, const juce::String& def) const;
        void setWet (int mode);
        void setProp (const char* name, const juce::var& v);
        SkinKey* addKey (const juce::String& label, int x, int y, int w, std::function<void (SkinKey&)> onClick);
        void syncKeys ();
        void build ();
        std::vector<std::function<void ()>> syncers;
    };
} // namespace yrev::ui
