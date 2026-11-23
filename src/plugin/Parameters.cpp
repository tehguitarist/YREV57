#include "Parameters.h"

namespace
{
    // ---- Config (ranges from docs/design/parameters.md) --------------------
    constexpr int kVersion = 1;
    constexpr float kInLevelMinDb = -60.0f, kInLevelMaxDb = 12.0f;
    constexpr float kEqGainDb = 15.0f;
    constexpr float kLoFMin = 50.0f, kLoFMax = 700.0f, kLoFDef = 200.0f;
    // Mid band 350 Hz-5 kHz in both units: the REV7 and REV5 share the circuit (Wien-bridge band-pass, 100 k dual
    // rheostat + 7.5 k with 0.0027 / 0.0062 uF: 362 Hz-5.19 kHz; the REV7 spec tables' 7 kHz is not supported, Q-26).
    constexpr float kMidFMin = 350.0f, kMidFMax = 5000.0f, kMidFDef = 1000.0f;
    constexpr float kHiFMin = 2000.0f, kHiFMax = 20000.0f, kHiFDef = 8000.0f;

    using juce::AudioParameterBool;
    using juce::AudioParameterChoice;
    using juce::AudioParameterFloat;
    using juce::NormalisableRange;
    using juce::ParameterID;

    std::unique_ptr<AudioParameterFloat> freq (const char* pid, const char* name, float lo, float hi, float def)
    {
        NormalisableRange<float> r (lo, hi);
        r.setSkewForCentre (std::sqrt (lo * hi));
        return std::make_unique<AudioParameterFloat> (ParameterID{pid, kVersion}, name, r, def,
                                                      juce::AudioParameterFloatAttributes ().withLabel ("Hz"));
    }

    std::unique_ptr<AudioParameterFloat> gain (const char* pid, const char* name)
    {
        return std::make_unique<AudioParameterFloat> (ParameterID{pid, kVersion}, name,
                                                      NormalisableRange<float> (-kEqGainDb, kEqGainDb, 0.1f), 0.0f,
                                                      juce::AudioParameterFloatAttributes ().withLabel ("dB"));
    }

    // Continuous parameter. log = geometric centre skew (time, frequency-like controls).
    std::unique_ptr<AudioParameterFloat> knob (const char* pid, const char* name, float lo, float hi, float def,
                                               const char* unit, bool log = false, float step = 0.0f)
    {
        NormalisableRange<float> r (lo, hi, step);
        if (log)
            r.setSkewForCentre (std::sqrt (lo * hi));
        return std::make_unique<AudioParameterFloat> (ParameterID{pid, kVersion}, name, r, def,
                                                      juce::AudioParameterFloatAttributes ().withLabel (unit));
    }

    // Hidden per-preset voice value: saved with the state and presets, not shown and not automatable.
    std::unique_ptr<AudioParameterFloat> hidden (const char* pid, const char* name, float lo, float hi, float def,
                                                 const char* unit, bool log = false)
    {
        NormalisableRange<float> r (lo, hi);
        if (log)
            r.setSkewForCentre (std::sqrt (lo * hi));
        return std::make_unique<AudioParameterFloat> (
            ParameterID{pid, kVersion}, name, r, def,
            juce::AudioParameterFloatAttributes ().withLabel (unit).withAutomatable (false));
    }

    // Stepped low-pass (REV5 LPF FRQ, 1-16 kHz or THRU): value in kHz, 17 = thru. Snaps to kLpfStepsKHz.
    std::unique_ptr<AudioParameterFloat> lpfKnob (const char* pid, const char* name)
    {
        constexpr int last = (int) std::size (yrev::kLpfStepsKHz) - 1;
        auto nearest = [] (float v)
        {
            int best = 0;
            for (int i = 1; i <= last; ++i)
                if (std::abs (std::log (v / yrev::kLpfStepsKHz[i])) <
                    std::abs (std::log (v / yrev::kLpfStepsKHz[best])))
                    best = i;
            return best;
        };
        NormalisableRange<float> r (
            yrev::kLpfStepsKHz[0], yrev::kLpfStepsKHz[last], [] (float, float, float t)
            { return yrev::kLpfStepsKHz[juce::jlimit (0, last, juce::roundToInt (t * last))]; },
            [nearest] (float, float, float v) { return (float) nearest (std::max (v, 0.01f)) / (float) last; },
            [nearest] (float, float, float v) { return yrev::kLpfStepsKHz[nearest (std::max (v, 0.01f))]; });
        return std::make_unique<AudioParameterFloat> (ParameterID{pid, kVersion}, name, r, yrev::kLpfThruKHz,
                                                      juce::AudioParameterFloatAttributes ().withLabel ("kHz"));
    }

    std::unique_ptr<AudioParameterBool> toggle (const char* pid, const char* name, bool def)
    {
        return std::make_unique<AudioParameterBool> (ParameterID{pid, kVersion}, name, def);
    }

    std::unique_ptr<AudioParameterChoice> choice (const char* pid, const char* name, juce::StringArray items, int def)
    {
        return std::make_unique<AudioParameterChoice> (ParameterID{pid, kVersion}, name, items, def);
    }
} // namespace
juce::AudioProcessorValueTreeState::ParameterLayout yrev::createParameterLayout ()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add (std::make_unique<AudioParameterChoice> (ParameterID{id::era, kVersion}, "Era",
                                                        juce::StringArray{"7 (1985)", "5 (1987)"}, 0));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID{id::wetInput, kVersion}, "Wet Input",
                                                        juce::StringArray{"Mono", "Stereo", "True Stereo"}, 1));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID{id::inLevel, kVersion}, "Input Level",
                                                       NormalisableRange<float> (kInLevelMinDb, kInLevelMaxDb, 0.1f),
                                                       0.0f, juce::AudioParameterFloatAttributes ().withLabel ("dB")));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID{id::mix, kVersion}, "Mix",
                                                       NormalisableRange<float> (0.0f, 100.0f, 0.1f), 50.0f,
                                                       juce::AudioParameterFloatAttributes ().withLabel ("%")));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID{id::outPhaseR, kVersion}, "Out Phase R", false));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID{id::bypass, kVersion}, "Bypass", false));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID{id::character, kVersion}, "Character",
                                                       NormalisableRange<float> (0.0f, 100.0f, 0.1f), 100.0f,
                                                       juce::AudioParameterFloatAttributes ().withLabel ("%")));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID{id::dither, kVersion}, "Dither (Era 5)", true));

    layout.add (std::make_unique<AudioParameterFloat> (ParameterID{id::width, kVersion}, "Width",
                                                       NormalisableRange<float> (0.0f, 200.0f, 0.1f), 100.0f,
                                                       juce::AudioParameterFloatAttributes ().withLabel ("%")));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID{id::wetSolo, kVersion}, "Wet Solo", false));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID{id::duckOn, kVersion}, "Ducker", false));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID{id::duckRange, kVersion}, "Duck Range",
                                                       NormalisableRange<float> (-60.0f, 0.0f, 0.1f), -10.0f,
                                                       juce::AudioParameterFloatAttributes ().withLabel ("dB")));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID{id::duckThresh, kVersion}, "Duck Threshold",
                                                       NormalisableRange<float> (-80.0f, 0.0f, 0.1f), -20.0f,
                                                       juce::AudioParameterFloatAttributes ().withLabel ("dB")));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID{id::duckAttack, kVersion}, "Duck Attack",
                                                       NormalisableRange<float> (0.0f, 1000.0f, 0.1f), 10.0f,
                                                       juce::AudioParameterFloatAttributes ().withLabel ("ms")));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID{id::duckRelease, kVersion}, "Duck Release",
                                                       NormalisableRange<float> (0.0f, 1000.0f, 0.1f), 200.0f,
                                                       juce::AudioParameterFloatAttributes ().withLabel ("ms")));

    layout.add (std::make_unique<AudioParameterBool> (ParameterID{id::aeqOn, kVersion}, "EQ On", false));
    layout.add (freq (id::aeqLoF, "EQ Low Freq", kLoFMin, kLoFMax, kLoFDef));
    layout.add (gain (id::aeqLoG, "EQ Low Gain"));
    layout.add (freq (id::aeqMidF, "EQ Mid Freq", kMidFMin, kMidFMax, kMidFDef));
    layout.add (gain (id::aeqMidG, "EQ Mid Gain"));
    layout.add (freq (id::aeqHiF, "EQ High Freq", kHiFMin, kHiFMax, kHiFDef));
    layout.add (gain (id::aeqHiG, "EQ High Gain"));

    // ---- Panel controls (ranges: docs/design/parameters.md; widest of the two eras) ----
    layout.add (knob (id::outLevel, "Output Level", -60.0f, 12.0f, 0.0f, "dB", false, 0.1f));
    layout.add (toggle (id::mute, "Mute", false));

    layout.add (knob (id::rt, "Reverb Time", 0.3f, 99.0f, 2.6f, "s", true));
    layout.add (knob (id::hiRatio, "High Ratio", 0.1f, 1.0f, 0.7f, "x"));
    layout.add (knob (id::loRatio, "Low Ratio", 0.1f, 2.4f, 1.2f, "x"));
    layout.add (knob (id::preDelay, "Pre-delay", 0.1f, 1000.0f, 30.0f, "ms", true));
    layout.add (knob (id::diffusion, "Diffusion", 0.0f, 10.0f, 5.0f, "", false, 1.0f));
    layout.add (knob (id::ref1Delay, "1st Reflection Delay", 0.1f, 1000.0f, 10.0f, "ms", true));
    layout.add (knob (id::ref1Level, "1st Reflection Level", 0.0f, 100.0f, 0.0f, "%"));
    layout.add (knob (id::erRevBal, "ER / Reverb Balance", 0.0f, 100.0f, 60.0f, "%")); // 100 % = ER only (REV5)
    layout.add (knob (id::density, "Density", 1.0f, 4.0f, 3.0f, "", false, 1.0f));
    layout.add (lpfKnob (id::wetLpf, "Reverb LPF"));
    layout.add (knob (id::spaceMod, "Space Modulation", 0.0f, 10.0f, 0.0f, "", false,
                      1.0f)); // factory 0 (REV5 ROM chart; the REV7 has none)
    layout.add (knob (id::rev2Time, "Secondary Time", 0.1f, 10.0f, 1.0f, "x", true));
    layout.add (knob (id::rev2Delay, "Secondary Delay", 0.0f, 500.0f, 80.0f, "ms"));
    layout.add (knob (id::rev2Level, "Secondary Level", 0.0f, 100.0f, 20.0f, "%"));
    layout.add (knob (id::gateLevel, "Gate Level", 0.0f, 100.0f, 0.0f, "%"));
    layout.add (knob (id::gateTrigDelay, "Gate Trigger Delay", 0.1f, 1000.0f, 10.0f, "ms", true));
    layout.add (knob (id::gateHold, "Gate Hold", 1.0f, 24000.0f, 200.0f, "ms", true));
    layout.add (knob (id::gateRelease, "Gate Release", 3.0f, 24000.0f, 400.0f, "ms", true));

    layout.add (choice (id::erMode, "ER Mode",
                        {"Small Hall", "Large Hall", "Random", "Reverse", "Plate", "Spring", "Pan A", "Pan B", "Piano",
                         "Organ", "Brass", "Guitar", "Rev Hall", "Rev Plate", "Rev Room"},
                        1));
    layout.add (knob (id::liveness, "Liveness", 0.0f, 10.0f, 5.0f, "", false, 1.0f));
    layout.add (knob (id::roomSize, "Room Size", 0.1f, 25.0f, 2.0f, "x", true));
    layout.add (knob (id::erCount, "Reflections", 1.0f, 34.0f, 12.0f, "", false, 1.0f));
    layout.add (knob (id::erFbGain, "ER Feedback Gain", -99.0f, 99.0f, 0.0f, "%"));           // REV5 p. 21
    layout.add (knob (id::erFbDelay, "ER Feedback Delay", 0.1f, 1400.0f, 20.0f, "ms", true)); // REV5 p. 21
    layout.add (knob (id::erFbHi, "ER Feedback HF", 0.1f, 1.0f, 0.5f, "x"));

    layout.add (choice (id::fxType, "FX Type", {"Delay", "Echo", "Mod", "Pitch", "Pan", "Off"}, 0));
    layout.add (choice (id::fxOrder, "FX Order", {"FX > Rev", "Rev > FX"}, 0));
    layout.add (toggle (id::fxLink, "FX Link L/R", false));
    layout.add (toggle (id::fxSync, "FX Tempo Sync", false));
    layout.add (lpfKnob (id::fxLpf, "FX LPF"));
    layout.add (knob (id::dlyL, "Delay L", 0.1f, 2900.0f, 100.0f, "ms", true));
    layout.add (knob (id::dlyR, "Delay R", 0.1f, 2900.0f, 200.0f, "ms", true));
    layout.add (knob (id::dlyLevel, "Delay Level", 0.0f, 100.0f, 50.0f, "%"));
    layout.add (knob (id::dlyFb, "Delay Feedback L", -99.0f, 99.0f, 0.0f, "%"));
    layout.add (knob (id::echoL, "Echo L", 0.1f, 1400.0f, 170.0f, "ms", true));
    layout.add (knob (id::echoR, "Echo R", 0.1f, 1400.0f, 178.0f, "ms", true));
    layout.add (knob (id::echoFb, "Echo Feedback L", -99.0f, 99.0f, 60.0f, "%"));
    // REV7 echo HIGH 0-10 in whole steps (ROM chart "HIGH x0 ~ x10", p. 27; Q-13): value / 10 is the treble ratio,
    // lower is darker. Era 5 shows the same control as the REV5 echo's HIGH ratio (x0.1-1.0), see echoDampToHigh.
    layout.add (knob (id::echoDamp, "Echo High", 0.0f, 10.0f, 5.0f, "", false, 1.0f));
    layout.add (knob (id::echoLevel, "Echo Level", 0.0f, 100.0f, 50.0f, "%"));
    layout.add (choice (id::modType, "Mod Type",
                        {"Flange", "Rev Flange", "Chorus A", "Chorus B", "Phasing", "Tremolo", "Symphonic"}, 2));
    layout.add (knob (id::lfoRate, "LFO Rate", 0.1f, 40.0f, 1.0f, "Hz", true));
    layout.add (knob (id::modDepth, "Mod Depth", 0.0f, 100.0f, 50.0f, "%"));
    layout.add (knob (id::dmDepth, "Delay Mod Depth", 0.0f, 100.0f, 50.0f, "%"));
    layout.add (knob (id::amDepth, "Amplitude Mod Depth", 0.0f, 100.0f, 40.0f, "%"));
    layout.add (knob (id::modDelay, "Mod Base Delay", 0.1f, 100.0f, 1.2f, "ms", true));
    layout.add (knob (id::modFb, "Mod Feedback", 0.0f, 99.0f, 35.0f, "%"));
    layout.add (knob (id::modRt, "Mod Reverb Time", 0.3f, 10.0f, 2.5f, "s", true));
    layout.add (lpfKnob (id::modLpf, "Mod FX LPF"));
    layout.add (choice (id::eqView, "EQ View", {"Analog", "Digital"}, 0));
    layout.add (toggle (id::deqOn, "Digital EQ On", false));
    layout.add (freq (id::deqLoF, "Digital EQ Low Freq", 32.0f, 2200.0f, 120.0f));
    layout.add (gain (id::deqLoG, "Digital EQ Low Gain"));
    layout.add (knob (id::deqLoQ, "Digital EQ Low Q", 0.1f, 5.0f, 1.0f, ""));
    layout.add (toggle (id::deqLoShelf, "Digital EQ Low Shelf", false));
    layout.add (freq (id::deqMidF, "Digital EQ Mid Freq", 250.0f, 5600.0f, 1000.0f));
    layout.add (gain (id::deqMidG, "Digital EQ Mid Gain"));
    layout.add (knob (id::deqMidQ, "Digital EQ Mid Q", 0.1f, 5.0f, 1.0f, ""));
    layout.add (freq (id::deqHiF, "Digital EQ High Freq", 500.0f, 16000.0f, 8000.0f));
    layout.add (gain (id::deqHiG, "Digital EQ High Gain"));
    layout.add (knob (id::deqHiQ, "Digital EQ High Q", 0.1f, 5.0f, 1.0f, ""));
    layout.add (toggle (id::deqHiShelf, "Digital EQ High Shelf", false));
    layout.add (choice (id::pitchSel, "Pitch Type", {"Pitch A", "Pitch B", "Pitch C"}, 0));
    layout.add (knob (id::pitchShift, "Pitch Shift", -12.0f, 12.0f, 0.0f, "st", false, 1.0f));
    layout.add (knob (id::pitchFine, "Pitch Fine", -100.0f, 100.0f, 0.0f, "ct", false, 1.0f));
    layout.add (knob (id::pitchDelay, "Pitch Delay", 0.1f, 400.0f, 10.0f, "ms", true));
    layout.add (knob (id::pitchFb, "Pitch Feedback", 0.0f, 99.0f, 0.0f, "%"));
    layout.add (choice (id::panDir, "Pan Direction", {"L > R", "R > L", "L <> R"}, 0));
    layout.add (knob (id::panRate, "Pan Rate", 0.1f, 40.0f, 1.0f, "Hz", true));
    layout.add (knob (id::panDepth, "Pan Depth", 0.0f, 100.0f, 50.0f, "%"));
    // added at the end so earlier parameters keep their positions
    layout.add (knob (id::dlyFbR, "Delay Feedback R", -99.0f, 99.0f, 0.0f, "%"));
    layout.add (knob (id::dlyHigh, "Delay High", 0.1f, 1.0f, 1.0f, "x"));
    layout.add (knob (id::echoFbR, "Echo Feedback R", -99.0f, 99.0f, 60.0f, "%"));
    layout.add (knob (id::pitch2Shift, "Pitch 2 Shift", -12.0f, 12.0f, 0.0f, "st", false, 1.0f));
    layout.add (knob (id::pitch2Fine, "Pitch 2 Fine", -100.0f, 100.0f, 0.0f, "ct", false, 1.0f));
    layout.add (knob (id::pitch2Delay, "Pitch 2 Delay", 0.1f, 400.0f, 20.0f, "ms", true));
    layout.add (knob (id::ref1LDelay, "1st Reflection L Delay", 0.1f, 1000.0f, 15.0f, "ms", true));
    layout.add (knob (id::ref1LLevel, "1st Reflection L Level", 0.0f, 100.0f, 0.0f, "%"));
    layout.add (knob (id::ref1RDelay, "1st Reflection R Delay", 0.1f, 1000.0f, 20.0f, "ms", true));
    layout.add (knob (id::ref1RLevel, "1st Reflection R Level", 0.0f, 100.0f, 0.0f, "%"));
    // hidden per-preset voice (ours); ranges must match the engine's capacity (Engine.cpp kVoice*)
    layout.add (hidden (id::voiceLoopSize, "Voice Loop Size", 0.7f, 1.4f, 1.0f, "x"));
    layout.add (hidden (id::voiceDiffSize, "Voice Diffuser Size", 0.6f, 1.5f, 1.0f, "x"));
    layout.add (hidden (id::voiceLoXover, "Voice Low Crossover", 100.0f, 800.0f, 300.0f, "Hz", true));
    layout.add (hidden (id::voiceErGain, "Voice ER Gain", -6.0f, 6.0f, 0.0f, "dB"));
    layout.add (hidden (id::gateInput, "Gate On Input", 0.0f, 1.0f, 0.0f, "")); // >= 0.5 on
    layout.add (hidden (id::voiceHiXover, "Voice High Crossover", 4000.0f, 14000.0f, 8500.0f, "Hz", true));
    layout.add (hidden (id::erType, "E/R Type 2", 0.0f, 1.0f, 0.0f, "")); // >= 0.5 E/R2 (both eras)
    layout.add (hidden (id::voiceLateOnset, "Voice Late Onset", 0.0f, 60.0f, 0.0f, "ms"));
    layout.add (hidden (id::voiceErTilt, "Voice ER Tilt", -12.0f, 12.0f, 0.0f, "dB"));
    layout.add (hidden (id::voiceLateTilt, "Voice Late Tilt", -12.0f, 12.0f, 0.0f, "dB"));
    layout.add (knob (id::revMix, "Rev Mix", 0.0f, 100.0f, 100.0f, "%"));
    layout.add (toggle (id::fxWet, "FX Wet", false));
    layout.add (hidden (id::voiceRev2Hi, "Voice Secondary High", 0.0f, 1.0f, 0.0f, "x")); // 0 = follow hi_ratio
    layout.add (hidden (id::voiceRev2Gain, "Voice Secondary Gain", 0.0f, 12.0f, 0.0f, "dB"));

    return layout;
}

namespace yrev
{
    const EraLimit* eraLimitFor (const juce::String& paramID)
    {
        for (const auto& l : kEraLimits)
            if (paramID == l.id)
                return &l;
        return nullptr;
    }
} // namespace yrev
