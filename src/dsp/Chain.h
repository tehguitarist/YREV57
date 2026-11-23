#pragma once

#include <cstdint>
#include <vector>

#include <juce_audio_basics/juce_audio_basics.h>

#include "Biquad.h"
#include "Converter.h"
#include "Ducker.h"
#include "Engine.h"
#include "FxSlot.h"
#include "StereoWidth.h"

namespace yrev::dsp
{
    // Everything around the reverb engine that is not the reverb: wet input mode, input and output level, wet-only
    // analogue EQ, the reverb engine, digital EQ (era 5), wet phase, width, ducker, mix, wet
    // solo, bypass and mute. Signal flow follows docs/design/architecture.md. No allocation after prepare().
    struct ChainParams
    {
        int era = 0;      // 0 = era 7, 1 = era 5
        int wetInput = 1; // 0 mono (left only), 1 stereo (L+R summed), 2 true stereo
        float inLevelDb = 0.0f, outLevelDb = 0.0f, mix = 50.0f; // mix in %
        bool wetSolo = false, bypass = false, mute = false, outPhaseR = false;
        float width = 100.0f; // %
        bool duckOn = false;
        float duckRangeDb = -10.0f, duckThreshDb = -20.0f, duckAttackMs = 10.0f, duckReleaseMs = 200.0f;
        bool aeqOn = false;
        float aeqLoF = 200.0f, aeqLoG = 0.0f, aeqMidF = 1000.0f, aeqMidG = 0.0f, aeqHiF = 8000.0f, aeqHiG = 0.0f;
        bool deqOn = false; // used in era 5 only
        float deqF[3] = {120.0f, 1000.0f, 8000.0f}, deqG[3] = {0, 0, 0}, deqQ[3] = {1, 1, 1};
        bool deqLoShelf = false, deqHiShelf = false;
        EngineParams engine;
        float character = 100.0f; // % converter character (0 clean, 100 authentic)
        bool dither = true;       // era 5 converter dither
        FxParams fx;
        int fxOrder = 0;     // 0 = FX > Rev, 1 = Rev > FX
        float revMix = 1.0f; // REV MIX 0..1: the reverb beside the effect's bypass (hardware routing)
        bool fxWet = false;  // no bypass: fully wet serial routing
        GateParams gate;     // REV5 gate (levelPct 0 = off)
        // plugin settings (not parameters): quality, noise seed, output safety limiter
        bool eco = false, frozenNoise = false, limiter = true;
        // Program generation: changes when a preset (or a saved state) has loaded. A change, like a change of era or
        // eco, starts a whole-engine crossfade (Chain).
        uint32_t program = 0;
    };

    class Chain
    {
    public:
        void prepare (double sampleRate, int maxBlockSize);
        void reset ();
        // Processes one stereo block in place.
        void process (float* left, float* right, int numSamples, const ChainParams& p);

        // Test hook: with the engine off the wet path (converter and reverb) passes through unchanged, so the chain
        // around it can be tested on its own.
        void setEngineEnabled (bool on) { engineOn = on; }

        static double tailSeconds (const ChainParams& p)
        {
            return Engine::tailSeconds (p.engine) + FxSlot::tailSeconds (p.fx);
        }

        // Test hooks: whether an old engine is still ringing out after a switch, and how many switches have started.
        bool ringingOut () const { return oldLive; }
        int switchCount () const { return switches; }

    private:
        void processChunk (float* left, float* right, int n, const ChainParams& p);
        void runWetPath (float* wl, float* wr, int n, const ChainParams& p);
        void processSlot (int slot, float* l, float* r, int n);
        static bool needsSwitch (const ChainParams& a, const ChainParams& b)
        {
            return a.era != b.era || a.eco != b.eco || a.program != b.program;
        }
        void updateEq (const ChainParams& p);
        static void applyBands (Biquad (&bands)[3][2], float* l, float* r, int n);

        double sampleRate = 48000.0;
        int maxBlock = 0;
        std::vector<float> dryL, dryR, wetL, wetR, key;

        juce::SmoothedValue<float> inGain, outGain, wetAmount, soloAmount, bypassAmount, muteGain, phaseR;
        Biquad aeq[3][2]; // [band][channel]; the digital EQ is in the converter's DSP stage
        StereoWidener widener;
        Ducker ducker;
        // Converter model around the FX slot and the reverb engine, at the era's native rate. Two slots for the
        // whole-engine crossfade: on an era, eco or preset change the idle slot is reset
        // and takes the new settings, its input fades in while the old slot's input fades out (equal power), and the
        // old slot keeps its own settings and rings out until it falls silent. Each slot keeps its settings in
        // slotParams.
        Converter converter[2];
        ChainParams slotParams[2];
        std::vector<float> oldL, oldR;
        int active = 0, switches = 0;
        bool primed = false;   // the active slot has taken its first settings since reset
        bool oldLive = false;  // the other slot is ringing out
        bool dropping = false; // the old slot's output is fading out (a new switch is waiting, or it rang too long)
        float xfade = 1.0f, xfadeStep = 0.0f; // crossfade position, 0..1 (new slot's input gain sin(x pi/2))
        float oldIn0 = 1.0f;                  // the old slot's input gain when the switch started
        float oldOut = 1.0f, dropStep = 0.0f; // old slot's output gain while dropping
        int64_t oldAge = 0, oldQuiet = 0;     // samples since the switch, samples below the silence floor
        int64_t maxRing = 0, quietLen = 0, longDrop = 0, shortDrop = 0;
        bool engineOn = true;
        float limGain = 1.0f, limRelease = 0.0f; // output safety limiter
        float lastAeq[6] = {};
        bool eqDirty = true;
        ChainParams lastEqParams;
    };
} // namespace yrev::dsp
