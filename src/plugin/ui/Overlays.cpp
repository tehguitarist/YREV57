#include "Overlays.h"

#include <array>

#include "../Parameters.h"

namespace yrev::ui
{
    namespace
    {
        constexpr int kKeyH = 64;
        constexpr int kTabW = 112; // five bank tabs share the browser's width
        // Window = pct x 10 by pct x 4; 250% is the 2500 x 1000 design size.
        constexpr std::array<int, 5> kUiScales = {75, 100, 150, 200, 250};
        // The one trademark disclaimer wording (docs/legal.md, README.md, installer/stage_docs.sh, ADR 0004).
        constexpr const char* kDisclaimer =
            "YREV57 is an independent product, inspired by the Yamaha REV7 and REV5. It is not affiliated with, "
            "endorsed by or sponsored by Yamaha Corporation. Yamaha, REV7 and REV5 are trademarks of Yamaha "
            "Corporation, used here only to describe the hardware that inspired this plug-in.";

        int keyWidthFor (const juce::String& label)
        {
            return juce::jmax (96, 16 + 11 * label.length ());
        }
    } // namespace

    // ---- Overlay ---------------------------------------------------------------------------------
    Overlay::Overlay ()
    {
        setWantsKeyboardFocus (true);
        setVisible (false);
        setAlwaysOnTop (true);
    }

    void Overlay::open ()
    {
        setBounds (0, 0, kDesignW, kDesignH);
        setVisible (true);
        toFront (true);
        opened ();
        grabKeyboardFocus ();
    }

    void Overlay::close ()
    {
        setVisible (false);
        if (onClose)
            onClose ();
    }

    void Overlay::paint (juce::Graphics& g)
    {
        g.fillAll (juce::Colours::black.withAlpha (.45f));
        paintPanel (g);
    }

    void Overlay::paintPanel (juce::Graphics& g) const
    {
        const auto r = panel.toFloat ();
        g.setColour (juce::Colours::black.withAlpha (.5f));
        g.fillRoundedRectangle (r.translated (0, 10).expanded (6), 10);
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff1b1b1c), 0, r.getY (), juce::Colour (0xff101011), 0,
                                                 r.getBottom (), false));
        g.fillRoundedRectangle (r, 4);
        g.setColour (juce::Colour (0xff3a3a3d));
        g.drawRoundedRectangle (r.reduced (1), 4, 2);
    }

    void Overlay::mouseDown (const juce::MouseEvent& e)
    {
        if (!panel.contains (e.getPosition ()))
            close ();
    }

    bool Overlay::keyPressed (const juce::KeyPress& k)
    {
        if (k == juce::KeyPress::escapeKey)
        {
            close ();
            return true;
        }
        return false;
    }

    // ---- PresetBrowser -------------------------------------------------------------------------------
    PresetBrowser::PresetBrowser (PresetManager& m, std::function<void (const juce::String&)> s)
        : mgr (m), say (std::move (s)), tabFactory ("FACTORY", kTabW, 60, true), tabUser ("USER", kTabW, 60, true),
          tabCombi ("COMBI", kTabW, 60, true), tabRev7 ("REV7", kTabW, 60, true), tabRev5 ("REV5", kTabW, 60, true),
          bStore ("Store as", 112, kKeyH, true), bRename ("Rename", 112, kKeyH, true),
          bDelete ("Delete", 112, kKeyH, true), bImport ("Import", 112, kKeyH, true),
          bExport ("Export", 112, kKeyH, true)
    {
        panel = {1130, 118, 640, 592};
        list.setModel (this);
        list.setRowHeight (40);
        list.setColour (juce::ListBox::backgroundColourId, juce::Colour (0xff0b0b0c));
        list.setColour (juce::ListBox::outlineColourId, col::rule);
        list.setOutlineThickness (1);
        addAndMakeVisible (list);

        for (auto* k :
             {&tabFactory, &tabCombi, &tabRev7, &tabRev5, &tabUser, &bStore, &bRename, &bDelete, &bImport, &bExport})
            addAndMakeVisible (*k);

        tabFactory.onClick = [this] { setBank ("FACTORY"); };
        tabUser.onClick = [this] { setBank ("USER"); };
        tabCombi.onClick = [this] { setBank ("COMBI"); };
        tabRev7.onClick = [this] { setBank ("REV7"); };
        tabRev5.onClick = [this] { setBank ("REV5"); };
        bStore.onClick = [this] { beginNaming (Naming::store); };
        bRename.onClick = [this] { beginNaming (Naming::rename); };
        bDelete.onClick = [this]
        {
            const int i = mgr.current ();
            if (i >= 0 && mgr.remove (i))
            {
                say ("DELETED");
                refresh ();
            }
        };
        bImport.onClick = [this]
        {
            chooser = std::make_unique<juce::FileChooser> ("Import a preset", juce::File (), "*.xml");
            chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                                  [this] (const juce::FileChooser& fc)
                                  {
                                      const auto f = fc.getResult ();
                                      if (f.existsAsFile ())
                                      {
                                          note = mgr.importFrom (f) ? "IMPORTED" : "NOT A YREV57 PRESET";
                                          setBank ("USER");
                                      }
                                  });
        };
        bExport.onClick = [this]
        {
            const int i = mgr.current ();
            if (i < 0)
                return;
            chooser =
                std::make_unique<juce::FileChooser> ("Export the preset",
                                                     juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
                                                         .getChildFile (mgr.at (i).name + ".xml"),
                                                     "*.xml");
            chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles |
                                      juce::FileBrowserComponent::warnAboutOverwriting,
                                  [this, i] (const juce::FileChooser& fc)
                                  {
                                      const auto f = fc.getResult ();
                                      if (f != juce::File ())
                                      {
                                          note = mgr.exportTo (i, f.withFileExtension ("xml")) ? "EXPORTED"
                                                                                               : "EXPORT FAILED";
                                          repaint ();
                                      }
                                  });
        };

        nameBox.setFont (Skin::get ().label (28, .04f));
        nameBox.setColour (juce::TextEditor::backgroundColourId, juce::Colour (0xff0d0506));
        nameBox.setColour (juce::TextEditor::textColourId, col::led);
        nameBox.setColour (juce::TextEditor::outlineColourId, juce::Colour (0xff6a2a26));
        nameBox.setColour (juce::TextEditor::focusedOutlineColourId, juce::Colour (0xff6a2a26));
        nameBox.setSelectAllWhenFocused (true);
        nameBox.setInputRestrictions (28);
        nameBox.onReturnKey = [this] { finishNaming (true); };
        nameBox.onEscapeKey = [this] { finishNaming (false); };
        addChildComponent (nameBox);
    }

    void PresetBrowser::resized ()
    {
        const auto p = panel.reduced (18);
        int x = p.getX ();
        for (auto* t : {&tabFactory, &tabCombi, &tabRev7, &tabRev5, &tabUser})
        {
            t->setBounds (x, p.getY (), kTabW, 60);
            x += kTabW + 8;
        }
        list.setBounds (p.getX (), p.getY () + 78, p.getWidth (), 400);
        const int fy = p.getY () + 78 + 400 + 14;
        x = p.getX ();
        for (auto* b : {&bStore, &bRename, &bDelete, &bImport, &bExport})
        {
            b->setBounds (x, fy, 112, kKeyH);
            x += 120;
        }
        nameBox.setBounds (p.getX (), fy + 8, p.getWidth (), 48);
    }

    void PresetBrowser::paint (juce::Graphics& g)
    {
        Overlay::paint (g);
        const auto p = panel.reduced (18);
        g.setColour (col::inkDim);
        g.setFont (Skin::get ().label (17, .08f));
        const auto msg = naming == Naming::none
                             ? note
                             : juce::String (naming == Naming::store ? "NAME FOR THE NEW PRESET, THEN ENTER"
                                                                     : "NEW NAME, THEN ENTER");
        g.drawText (msg, p.getX (), p.getY () + 60, p.getWidth (), 18, juce::Justification::centredRight);
    }

    void PresetBrowser::openHidden ()
    {
        hiddenNext = true;
        open ();
    }

    void PresetBrowser::opened ()
    {
        naming = Naming::none;
        note = {};
        if (hiddenNext)
            bank = "SECRET";
        else if (mgr.current () >= 0)
            bank = mgr.at (mgr.current ()).bank;
        hiddenNext = false;
        if (bank == "SECRET")
            note = "HIDDEN PRESETS";
        refresh ();
    }

    void PresetBrowser::setBank (const juce::String& b)
    {
        bank = b;
        refresh ();
    }

    void PresetBrowser::refresh ()
    {
        rows.clear ();
        for (int i = 0; i < mgr.size (); ++i)
            if (mgr.at (i).bank == bank)
                rows.push_back (i);
        list.updateContent ();
        list.repaint ();
        for (size_t r = 0; r < rows.size (); ++r)
            if (rows[r] == mgr.current ())
                list.scrollToEnsureRowIsOnscreen ((int) r);
        tabFactory.setToggleState (bank == "FACTORY", juce::dontSendNotification);
        tabUser.setToggleState (bank == "USER", juce::dontSendNotification);
        tabCombi.setToggleState (bank == "COMBI", juce::dontSendNotification);
        tabRev7.setToggleState (bank == "REV7", juce::dontSendNotification);
        tabRev5.setToggleState (bank == "REV5", juce::dontSendNotification);
        updateKeys ();
        repaint ();
    }

    void PresetBrowser::updateKeys ()
    {
        const int cur = mgr.current ();
        const bool userSel = cur >= 0 && mgr.at (cur).user;
        const bool busy = naming != Naming::none;
        bRename.setEnabled (userSel && !busy);
        bDelete.setEnabled (userSel && !busy);
        bStore.setEnabled (!busy);
        bImport.setEnabled (!busy);
        bExport.setEnabled (cur >= 0 && !busy);
        for (auto* b : {&bStore, &bRename, &bDelete, &bImport, &bExport})
            b->setVisible (!busy);
        nameBox.setVisible (busy);
    }

    void PresetBrowser::beginNaming (Naming n)
    {
        naming = n;
        const int cur = mgr.current ();
        juce::String initial = cur >= 0 ? mgr.at (cur).name : juce::String ("New preset");
        if (n == Naming::store && cur >= 0 && !mgr.at (cur).user)
            initial += " ed";
        nameBox.setText (initial, false);
        updateKeys ();
        nameBox.grabKeyboardFocus ();
        nameBox.selectAll ();
        repaint ();
    }

    void PresetBrowser::finishNaming (bool accept)
    {
        const auto n = naming;
        naming = Naming::none;
        if (accept)
        {
            const auto text = nameBox.getText ().trim ();
            const bool ok = n == Naming::store ? mgr.storeAs (text) : mgr.rename (mgr.current (), text);
            note = ok ? (n == Naming::store ? "STORED" : "RENAMED") : "NAME NOT ALLOWED";
            if (ok)
            {
                say (note);
                bank = "USER";
            }
        }
        refresh ();
        grabKeyboardFocus ();
    }

    void PresetBrowser::paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool)
    {
        if (row < 0 || row >= (int) rows.size ())
            return;
        const int idx = rows[(size_t) row];
        const bool sel = idx == mgr.current ();
        if (sel)
            g.fillAll (juce::Colour (0xff2a1210));
        g.setColour (juce::Colour (0xff1c1c1e));
        g.drawHorizontalLine (h - 1, 0.0f, (float) w);
        g.setColour (sel ? juce::Colour (0xffff7a6e) : col::ink);
        g.setFont (Skin::get ().label (26, .04f));
        g.drawText (mgr.at (idx).name.toUpperCase (), 14, 0, w - 160, h, juce::Justification::centredLeft);
        g.setColour (col::inkDim);
        g.setFont (Skin::get ().label (18, .08f));
        const auto& p = mgr.at (idx);
        const juce::String tag =
            !p.user ? "READ-ONLY" : (p.values.count (id::era) && p.values.at (id::era) > .5f ? "ERA 5" : "");
        g.drawText (tag, w - 160, 0, 146, h, juce::Justification::centredRight);
    }

    void PresetBrowser::listBoxItemClicked (int row, const juce::MouseEvent&)
    {
        if (row < 0 || row >= (int) rows.size ())
            return;
        mgr.load (rows[(size_t) row]);
        say ("RECALLED");
        close ();
    }

    // ---- SettingsPanel ----------------------------------------------------------------------------------
    SettingsPanel::SettingsPanel (juce::AudioProcessorValueTreeState& a, std::function<void (int)> s)
        : apvts (a), setScale (std::move (s)), closeKey ("Close", 112, kKeyH, true)
    {
        panel = {650, 105, 1200, 790}; // eight settings rows above the disclaimer
        build ();
        addAndMakeVisible (closeKey);
        closeKey.onClick = [this] { close (); };
    }

    juce::String SettingsPanel::prop (const char* name, const juce::String& def) const
    {
        const auto v = apvts.state.getProperty (name);
        return v.isVoid () ? def : v.toString ();
    }

    void SettingsPanel::setWet (int mode)
    {
        if (auto* p = apvts.getParameter (id::wetInput))
            p->setValueNotifyingHost (p->convertTo0to1 ((float) mode));
        syncKeys ();
    }

    void SettingsPanel::setProp (const char* name, const juce::var& v)
    {
        apvts.state.setProperty (name, v, nullptr);
    }

    SkinKey* SettingsPanel::addKey (const juce::String& label, int x, int y, int w,
                                    std::function<void (SkinKey&)> onClick)
    {
        auto* k = keys.add (new SkinKey (label, w, kKeyH, true));
        k->setBounds (x, y, w, kKeyH);
        k->onClick = [k, f = std::move (onClick)] { f (*k); };
        addAndMakeVisible (k);
        return k;
    }

    void SettingsPanel::build ()
    {
        const int keyX = panel.getX () + 400;
        int y = panel.getY () + 84;
        const int pitch = 74;

        auto group = [&] (const char* label, std::vector<std::pair<juce::String, std::function<void ()>>> items,
                          std::function<bool (int)> isOn)
        {
            rows.push_back ({label, y});
            int x = keyX;
            std::vector<SkinKey*> ks;
            for (size_t i = 0; i < items.size (); ++i)
            {
                const int w = keyWidthFor (items[i].first);
                auto act = items[i].second;
                ks.push_back (addKey (items[i].first, x, y, w, [act] (SkinKey&) { act (); }));
                x += w + 8;
            }
            syncers.push_back (
                [ks, isOn]
                {
                    for (size_t i = 0; i < ks.size (); ++i)
                        ks[i]->setToggleState (isOn ((int) i), juce::dontSendNotification);
                });
            y += pitch;
        };

        group ("Quality",
               {{"Native rate",
                 [this]
                 {
                     setProp ("quality", "native");
                     syncKeys ();
                 }},
                {"Eco (host rate)",
                 [this]
                 {
                     setProp ("quality", "eco");
                     syncKeys ();
                 }}},
               [this] (int i) { return (prop ("quality", "native") == "native") == (i == 0); });

        group ("Wet input default",
               {{"Mono", [this] { setWet (0); }},
                {"Stereo", [this] { setWet (1); }},
                {"True stereo", [this] { setWet (2); }}},
               [this] (int i) { return juce::roundToInt (apvts.getRawParameterValue (id::wetInput)->load ()) == i; });

        rows.push_back ({"Latency", y});
        y += pitch;

        group ("Noise seed",
               {{"Random",
                 [this]
                 {
                     setProp ("noiseSeed", "random");
                     syncKeys ();
                 }},
                {"Frozen",
                 [this]
                 {
                     setProp ("noiseSeed", "frozen");
                     syncKeys ();
                 }}},
               [this] (int i) { return (prop ("noiseSeed", "random") == "random") == (i == 0); });

        // "eraMemory": per era (default) keeps one set of settings per era; shared carries them across. A change
        // drops the stored sets, so per era starts again from the current settings.
        auto setEraMemory = [this] (const char* mode)
        {
            if (prop ("eraMemory", "perEra") != mode)
            {
                setProp ("eraMemory", mode);
                apvts.state.removeChild (apvts.state.getChildWithName ("ERA_MEMORY"), nullptr);
            }
            syncKeys ();
        };
        group ("Era settings",
               {{"Per era", [setEraMemory] { setEraMemory ("perEra"); }},
                {"Shared", [setEraMemory] { setEraMemory ("shared"); }}},
               [this] (int i) { return (prop ("eraMemory", "perEra") == "perEra") == (i == 0); });

        group ("Output safety limiter",
               {{"On",
                 [this]
                 {
                     setProp ("limiter", prop ("limiter", "1") == "1" ? 0 : 1);
                     syncKeys ();
                 }}},
               [this] (int) { return prop ("limiter", "1") == "1"; });

        // FX period A/B (ROADMAP M4b): each key toggles one period-style option in the FX slot so it can be heard
        // against the current build; stored as "1"/"0" state properties, read by the processor (readSettings).
        // Defaults (2026-11-22): Trem. DM on, the others off; keep in step with YrevProcessor::readSettings.
        static constexpr const char* kFxKeys[] = {"fxStepped", "fxLinear", "fxTriangle", "fxTremDm"};
        static constexpr const char* kFxDefaults[] = {"0", "0", "0", "1"};
        auto fxToggle = [this] (int i)
        {
            return [this, i]
            {
                setProp (kFxKeys[i], prop (kFxKeys[i], kFxDefaults[i]) == "1" ? "0" : "1");
                syncKeys ();
            };
        };
        group ("FX period A/B",
               {{"Stepped", fxToggle (0)},
                {"Linear", fxToggle (1)},
                {"Triangle", fxToggle (2)},
                {"Trem. DM", fxToggle (3)}},
               [this] (int i) { return prop (kFxKeys[i], kFxDefaults[i]) == "1"; });

        // Stored as the "uiScale" state property in percent (not a parameter); new choices are appended.
        std::vector<std::pair<juce::String, std::function<void ()>>> scaleKeys;
        for (const int pct : kUiScales)
            scaleKeys.push_back ({juce::String (pct) + "%", [this, pct]
                                  {
                                      setProp ("uiScale", pct);
                                      setScale (pct);
                                      syncKeys ();
                                  }});
        group ("UI scale", std::move (scaleKeys),
               [this] (int i) { return prop ("uiScale", "100").getIntValue () == kUiScales[(size_t) i]; });
        syncKeys ();
    }

    void SettingsPanel::syncKeys ()
    {
        for (auto& s : syncers)
            s ();
        // No latency is reported to the host in either quality: the converter's own delay is taken off the pre-delay
        // and the 1st-reflection delay inside the engine (Converter.h), and the dry path is not delayed.
        latencyText = "None reported (converter delay is taken off the pre-delay)";
        repaint ();
    }

    void SettingsPanel::resized ()
    {
        closeKey.setBounds (panel.getRight () - 18 - 112, panel.getBottom () - 18 - kKeyH, 112, kKeyH);
    }

    void SettingsPanel::paint (juce::Graphics& g)
    {
        Overlay::paint (g);
        const auto& sk = Skin::get ();
        g.setColour (col::ink);
        g.setFont (sk.label (34, .12f));
        g.drawText ("SETTINGS", panel.getX () + 36, panel.getY () + 22, 400, 44, juce::Justification::centredLeft);

        g.setFont (sk.label (24, .06f));
        for (const auto& r : rows)
        {
            g.setColour (col::ink);
            g.drawText (r.label.toUpperCase (), panel.getX () + 36, r.y, 340, kKeyH, juce::Justification::centredLeft);
            if (r.label == "Latency")
            {
                g.setColour (col::inkDim);
                g.drawText (latencyText.toUpperCase (), panel.getX () + 400, r.y, 700, kKeyH,
                            juce::Justification::centredLeft);
            }
        }

        g.setColour (col::inkDim);
        g.setFont (sk.label (18, .04f));
        g.drawFittedText ("YREV57 is free software under the GNU AGPL v3. " + juce::String (kDisclaimer),
                          panel.getX () + 36, panel.getBottom () - 108, 1000, 90, juce::Justification::centredLeft, 4);
    }
} // namespace yrev::ui
