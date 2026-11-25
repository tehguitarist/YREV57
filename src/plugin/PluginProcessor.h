#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <array>
#include <atomic>

#include "Parameters.h"
#include "Presets.h"
#include "dsp/Chain.h"

class YrevProcessor final : public juce::AudioProcessor,
                            private juce::AudioProcessorValueTreeState::Listener,
                            private juce::ValueTree::Listener,
                            private juce::AsyncUpdater
{
public:
    YrevProcessor ();
    ~YrevProcessor () override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources () override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor () override;
    bool hasEditor () const override { return true; }

    const juce::String getName () const override { return JucePlugin_Name; }
    bool acceptsMidi () const override { return false; }
    bool producesMidi () const override { return false; }
    double getTailLengthSeconds () const override; // follows rt, lo_ratio, pre-delay and room size

    int getNumPrograms () override { return juce::jmax (1, presets.size ()); }
    int getCurrentProgram () override { return juce::jmax (0, presets.current ()); }
    void setCurrentProgram (int i) override { presets.load (i); }
    const juce::String getProgramName (int i) override
    {
        return i >= 0 && i < presets.size () ? presets.at (i).name : juce::String ();
    }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    juce::AudioProcessorValueTreeState apvts;

    // Keeps the stored settings inside what the current era allows. Called when the era or a limited control
    // changes, and after a state load: clamps controls to the era's range (kEraLimits) and, in era 7, puts the
    // era-5-only choices back to era-7 ones and turns the digital EQ off so nothing stays active unseen.
    // Switching back to era 5 therefore finds the digital EQ off. Message thread.
    void enforceEraLimits ();
    yrev::PresetManager presets{apvts};

    // Era memory setting ("eraMemory" state property): "perEra" (default) keeps a separate set of preset settings
    // for each era, swapped in when the era changes; "shared" carries the settings across. The stored sets live in
    // the state under kEraMemoryType, so they are saved with the session.
    static constexpr const char* kEraMemoryType = "ERA_MEMORY";
    bool perEraMemory () const { return setPerEra.load (); }
    // Runs what an era or limited-control change needs (per-era swap, then the era limits). Message thread; called
    // from the async update, and by tests that have no message loop.
    void handleEraChange ();

    // Peak levels since the editor last read them (linear): input L, R, output L, R. Written on the audio thread.
    std::array<std::atomic<float>, 4> meterPeak{};

private:
    yrev::dsp::Chain chain;
    struct Raw // raw parameter values, looked up once
    {
        std::atomic<float>*era, *wetInput, *inLevel, *outLevel, *mix, *wetSolo, *bypass, *mute, *outPhaseR, *width,
            *duckOn, *duckRange, *duckThresh, *duckAttack, *duckRelease, *aeqOn, *aeqLoF, *aeqLoG, *aeqMidF, *aeqMidG,
            *aeqHiF, *aeqHiG, *deqOn, *deqLoShelf, *deqHiShelf;
        std::atomic<float>* deqF[3];
        std::atomic<float>* deqG[3];
        std::atomic<float>* deqQ[3];
        std::atomic<float>*rt, *hiRatio, *loRatio, *preDelay, *diffusion, *density, *ref1Delay, *ref1Level, *erRevBal,
            *wetLpf, *erMode, *liveness, *roomSize, *erCount, *spaceMod, *character, *dither;
        std::atomic<float>*fxType, *fxOrder, *fxLink, *fxSync, *fxLpf, *dlyL, *dlyR, *dlyLevel, *dlyFb, *echoL, *echoR,
            *echoFb, *echoDamp, *echoLevel, *modType, *lfoRate, *modDepth, *dmDepth, *amDepth, *modDelay, *modFb,
            *modRt, *modLpf, *pitchShift, *pitchFine, *pitchDelay, *pitchFb, *panDir, *panRate, *panDepth, *pitchSel,
            *dlyFbR, *dlyHigh, *echoFbR, *pitch2Shift, *pitch2Fine, *pitch2Delay;
        std::atomic<float>*erFbGain, *erFbDelay, *erFbHi, *rev2Time, *rev2Delay, *rev2Level, *gateLevel, *gateTrigDelay,
            *gateHold, *gateRelease, *ref1LDelay, *ref1LLevel, *ref1RDelay, *ref1RLevel;
        std::atomic<float>*voiceLoopSize, *voiceDiffSize, *voiceLoXover, *voiceErGain, *gateInput, *voiceHiXover,
            *erType, *voiceLateOnset, *voiceErTilt, *voiceLateTilt, *revMix, *fxWet, *voiceRev2Hi, *voiceRev2Gain,
            *voiceTapRead, *voiceRevTaps, *voiceErFill, *voiceErFillSize, *voiceDiffGain, *voiceSubRatio, *voiceSubHz,
            *voiceErCloud, *voiceErCloudHold, *voiceErCloudFade, *voiceErCloudRise, *voiceErLoTilt, *voiceErLoHz,
            *voiceErPrinted;
    } raw{};
    yrev::dsp::ChainParams readParams () const;
    double hostBpm = 0.0;              // from the play head, audio thread only (fx_sync)
    yrev::dsp::ChainParams heldParams; // last complete settings, used while a preset load is in progress (audio thread)
    bool haveHeld = false;
    void snapSteppedParameters ();
    void parameterChanged (const juce::String&, float) override;
    // plugin settings live in the state as properties (SettingsPanel); mirrored into atomics for the audio thread
    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override { readSettings (); }
    void readSettings ();
    std::atomic<bool> setEco{false}, setFrozen{false}, setLimiter{true}, setPerEra{true};
    // FX period A/B options (Settings, "FX period A/B"; M4b): state properties fxStepped, fxLinear, fxTriangle,
    // fxTremDm
    std::atomic<bool> setFxStepped{false}, setFxLinear{false}, setFxTriangle{false}, setFxTremDm{true};

    // per-era memory: lastEra is the era whose settings are in the parameters (message thread); settledEra tells
    // the audio thread that the swap for an era change is done, so it holds its settings until then
    int lastEra = 0;
    std::atomic<int> settledEra{0};
    int64_t heldSamples = 0; // audio thread: how long it has held for a swap (bounded, see processBlock)
    void storeEra (int era);
    bool recallEra (int era);
    void swapEraIfNeeded ();
    void handleAsyncUpdate () override;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (YrevProcessor)
};
