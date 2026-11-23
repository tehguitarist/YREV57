#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <map>
#include <vector>

#include <juce_audio_processors/juce_audio_processors.h>

namespace yrev
{
    struct Preset
    {
        juce::String name, bank; // bank: FACTORY, COMBI, REV7, REV5, SECRET (read-only; SECRET has no tab) or USER
        std::map<juce::String, float>
            values; // real parameter values; factory presets list only what differs from the defaults
        bool user = false;
    };

    // Factory and user presets over the APVTS. Message-thread use only.
    // A preset covers every parameter except the monitoring and level controls listed in isPresetParam().
    class PresetManager
    {
    public:
        explicit PresetManager (juce::AudioProcessorValueTreeState& state);

        static bool isPresetParam (const juce::String& id);

        int size () const { return (int) presets.size (); }
        const Preset& at (int i) const { return presets[(size_t) i]; }
        int current () const { return currentIndex; } // -1 after Init or for an unknown state
        juce::String currentName () const;
        void restoreFromState (); // after the host restores the plugin state

        void load (int index);
        void init ();
        bool isEdited () const; // differs from the loaded preset

        // Compare: toggles between the stored preset and the edited settings.
        bool comparing () const { return compareOn; }
        void toggleCompare ();

        // Load sequence for the audio thread (a seqlock): odd while settings are being written by a preset load,
        // compare or state restore, even otherwise. The processor holds its last settings while it is odd or moves
        // during a read, and passes sequence / 2 on as the program generation (whole-engine crossfade).
        void beginLoad () { loadSeq.fetch_add (1); }
        void endLoad () { loadSeq.fetch_add (1); }
        uint32_t loadSequence () const { return loadSeq.load (); }

        // Per-era memory (YrevProcessor): called around every preset load, compare and Init, inside the load
        // sequence. Not called by applyValues.
        std::function<void ()> onBeforeApply, onAfterApply;
        std::map<juce::String, float> currentValues () const { return snapshot (); }
        // Writes the given preset parameters (inside the load sequence) without the hooks or the preset selection.
        void applyValues (const std::map<juce::String, float>& values) { apply (values, false); }
        juce::String currentKey () const;

        // User bank
        void setUserDirectory (const juce::File& dir);
        bool storeAs (const juce::String& name); // saves the current settings; selects the result
        bool storeCurrent ();                    // overwrite the loaded user preset
        bool rename (int index, const juce::String& newName);
        bool remove (int index);
        bool exportTo (int index, const juce::File& file) const;
        bool importFrom (const juce::File& file);

    private:
        juce::AudioProcessorValueTreeState& apvts;
        std::vector<Preset> presets;
        int currentIndex = -1;
        bool compareOn = false;
        std::atomic<uint32_t> loadSeq{0};
        std::map<juce::String, float> editedSnapshot; // kept while comparing
        juce::File userDir;

        std::map<juce::String, float> snapshot () const;
        std::map<juce::String, float> effectiveValues (const Preset&) const;
        void apply (const std::map<juce::String, float>& values, bool hooks = true);
        void setParam (const juce::String& id, float realValue);
        void rebuild ();
        juce::File fileFor (const juce::String& name) const;
        void remember (); // current preset key ("BANK/name") -> state
    };
} // namespace yrev
