#pragma once

#include <array>

#include "Biquad.h"
#include "DelayLine.h"

namespace yrev::dsp
{
    // Reverb engine controls. Ranges follow docs/design/parameters.md; the processor clamps them to the era first.
    struct EngineParams
    {
        float rt = 2.6f;            // s, 1 kHz decay to -60 dB
        float hiRatio = 0.7f;       // x rt above the high crossover
        float loRatio = 1.2f;       // x rt below the low crossover
        float preDelayMs = 30.0f;   // initial delay before the reflections and the late network
        float diffusion = 5.0f;     // 0..10, input diffuser
        float density = 3.0f;       // 1..4, all-pass diffusion inside each loop
        float ref1DelayMs = 10.0f;  // mono centre 1st reflection, from the input
        float ref1Level = 0.0f;     // % (centre)
        float ref1LDelayMs = 15.0f; // REV5 left 1st reflection (left channel only)
        float ref1LLevel = 0.0f;    // %
        float ref1RDelayMs = 20.0f; // REV5 right 1st reflection (right channel only)
        float ref1RLevel = 0.0f;    // %
        float erRevBalance = 60.0f; // %, 100 = reflections only, 0 = late reverb only (REV5 ER/REV BAL, OM p. 18)
        float wetLpfKHz = 17.0f;    // 1..16 kHz, 17 = thru
        int erMode = 1;             // index into the er_mode choice list
        float liveness = 5.0f;      // 0..10
        float roomSize = 2.0f;      // x, scales the reflection spacing
        int erCount = 12;           // reflections per channel
        float spaceMod = 0.0f;      // 0..10, slow modulation of the late loops (0 = static, exact)
        // REV5 (owner's manual pp. 17 and 21); the processor sets them to 0 in era 7
        float erFbGain = 0.0f;     // %, -99..99: the reflection pattern repeats itself (negative inverts each repeat)
        float erFbDelayMs = 20.0f; // 0.1..1400 ms from the start of the reflections to the first repeat
        float erFbHi = 0.5f;       // x0.1..1.0 high-frequency feedback
        float rev2Time = 1.0f;     // x0.1..10 of rt
        float rev2DelayMs = 80.0f; // 0..500 ms after the start of the primary reverberation
        float rev2Level = 0.0f;    // %, 0..100 relative to the primary reverberation
        // Not a user control: the era's late-network input bandwidth (one-pole low-pass), set by the converter. 0 =
        // off.
        float lateBandwidthHz = 0.0f;
        // Not a user control: the era's bandwidth on the reflection outputs (early reflections and 1st reflections;
        // one-pole low-pass), set by the converter (7.5 kHz era 7, off in era 5). 0 = off.
        float erBandwidthHz = 0.0f;
        // Not a user control: the era-5 tap-comb average on the reflection outputs (a second-order low-pass, Q 0.5),
        // set by the converter (20 kHz in era 5, off in era 7). 0 = off.
        float erTopHz = 0.0f;
        // Not a user control: which unit's E/R charts the modes use, set by the converter per era (0 REV7, 1 REV5).
        int erTableSet = 0;
        // Hidden per-preset voice (parameters.md; ours): scales of the late loops and the input diffusers, the loops'
        // low- and high-shelf corners and a gain on the reflection group. Defaults are the fixed values the engine was
        // tuned with.
        float loopScale = 1.0f;     // x0.7..1.4
        float diffuserScale = 1.0f; // x0.6..1.5
        float loXoverHz = 300.0f;   // 100..800 Hz
        float erGainDb = 0.0f;      // -6..+6 dB
        float hiXoverHz = 8500.0f;  // 4..14 kHz (hi_ratio stays on the 8 kHz octave at every corner)
        float lateOnsetMs = 0.0f;   // 0..60 ms: the late network hears its input this much later (pass 26)
        float erTiltDb = 0.0f;      // -12..+12 dB: high shelf on the audible reflections (pass 35)
        float lateTiltDb = 0.0f;    // -12..+12 dB: high shelf on the late tail (pass 35)
        float rev2HiRatio = 0.0f;   // secondary reverb HIGH x0.1..1.0; 0 = the primary's hi_ratio (REV5 99, 2 HIGH)
        float rev2GainDb = 0.0f;    // 0..+12 dB on the secondary reverb's level (REV5 99, REV1/2 BAL)
        // The initial delay as programmed, before the converter takes its latency off preDelayMs: the era-7 Rev taps
        // scale with it (kRevRefMs7). 0 = use preDelayMs (callers without a converter).
        float initialDelayMs = 0.0f;
        // Not a user control: true-stereo input (wet_input True stereo, ADR 0006), set by the chain.
        int trueStereo = 0; // 0 or 1; an int so EngineParams has no padding (compared with memcmp)
        // Hidden per-preset (parameters.md): the E/R program type, 0 = E/R1, 1 = E/R2 (both eras; ignored by the Rev
        // modes). E/R2 passes the reflections through a decaying diffuser (twenty-fifth pass; era 5 twenty-eighth).
        int erType = 0;
    };

    // Engine v0, era-agnostic and run at the host rate (the native-rate bridge of ADR 0001 comes next).
    //   input (mid, plus a side memory read in true stereo, ADR 0006) -> delay memory -> early-reflection multitap
    //   (per-channel tap sets from the REV7 E/R charts, spacing scaled by room size) + mono 1st-reflection tap
    //   pre-delayed input + reflections -> input diffuser (series all-passes) -> 8 parallel damped comb loops, each
    //   with a low shelf, a high shelf (Jot's rule) and two in-loop all-passes -> L/R read-outs with orthogonal signs.
    // The late network is our own (ADR 0003): Moorer-style combs, in the patent family of US 4,472,993 / 4,570,523.
    // No allocation after prepare(); coefficients change at block rate and every loop filter keeps its poles inside
    // the unit circle, so filter changes apply at once. Changes to the reflection, pre-delay and 1st-reflection
    // layout crossfade over 30 ms; a change during a fade waits for it to end.
    class Engine
    {
    public:
        static constexpr int kNumLoops = 8;
        static constexpr int kNumLoops2 = 4;  // secondary reverb
        static constexpr int kMaxErTaps = 34; // reflections per channel (REV5 charts; the REV7's have 18)

        // Allocates for max(sampleRate, maxSampleRate); setSampleRate() then switches between rates up to that
        // without allocating (the converter bridge runs the engine at 31.25 or 44.1 kHz).
        void prepare (double sampleRate, int maxBlockSize, double maxSampleRate = 0.0);
        void setSampleRate (double sampleRate);
        double getSampleRate () const { return fs; }
        void reset ();
        // inL/inR may alias outL/outR. In the mono wet-input modes inL == inR.
        void process (const float* inL, const float* inR, float* outL, float* outR, int numSamples,
                      const EngineParams& p);

        // Per-sample form for the rate bridge: call update() once per block, then processSample() per sample.
        void update (const EngineParams& p);
        void processSample (float inL, float inR, float& outL, float& outR) noexcept;
        // The 1st reflections (centre and side taps) inside the last processSample's output, before the reflection
        // filters: the converter keeps them at full level when REV MIX scales the reverb.
        float lastRef1L () const noexcept { return ref1Out[0]; }
        float lastRef1R () const noexcept { return ref1Out[1]; }

        // Seconds until the tail falls below -60 dB at every frequency, plus the reflection span.
        static double tailSeconds (const EngineParams& p);

        // Test hooks: the loop lengths in samples and the loop gain at 1 kHz.
        int loopLength (int i) const { return loopLen[(size_t) i]; }

    private:
        float ref1Out[2]{};
        double fs = 48000.0, maxFs = 48000.0;
        bool dirty = true;
        EngineParams last;

        void applyLengths (); // loop, tap and diffuser lengths for fs and the voice scales
        double loopScale = 1.0, diffuserScale = 1.0;
        float loXoverHz = 300.0f, hiXoverHz = 8500.0f, hiShelfHz = 8500.0f; // hiShelfHz: the corner as held for fs
        DelayLine<float> input;      // pre-delay, reflections and the 1st reflection read from one memory (the mid)
        DelayLine<float> sideMemory; // (L - R) / 2 beside it, read only in true stereo (ADR 0006)
        struct Tap
        {
            int delay = 0;
            float gain = 0.0f; // reflection output
            float feed = 0.0f; // into the late network (faded with rt, see kLateFeedFade)
            int feedDelay = 0; // where the late feed reads it: delay plus the voice's late onset
        };
        // Everything read from the input memory. Plain ints and floats, no padding, so it compares with memcmp.
        struct Layout
        {
            std::array<Tap, kMaxErTaps> tapsL{}, tapsR{};
            int numTapsL = 0, numTapsR = 0, preDelay = 1, ref1Delay = 1, ref1DelayL = 1, ref1DelayR = 1;
            float ref1Gain = 0.0f, ref1GainL = 0.0f, ref1GainR = 0.0f, erGain = 1.0f;
            float sideGain = 0.0f; // true stereo: left reflections read mid + k side, right ones mid - k side
            int erEarly = 0;       // era 5: the reflection output is read this many samples early (kErDiffMs)
            int lateDirect = 1;    // the late network's direct input: pre-delay plus the voice's late onset
        };
        void startFade (const Layout& next) noexcept;
        // Reads one layout: wet reflections per channel, the late network's input and the 1st reflection.
        void mixLoops (std::array<double, kNumLoops>& fb) noexcept;
        void readLayout (const Layout& lay, float& outL, float& outR, std::array<float, kNumLoops>& lateIn, float& refL,
                         float& refR) const noexcept;
        Layout cur, prev, pending;
        bool fresh = true, hasPending = false;
        int fadeLen = 1, fadeLeft = 0;
        float lateGain = 1.0f, loopInGain = 1.0f;

        // one four-stage input diffuser per loop (each loop hears its own sign pattern of the reflection feed)
        std::array<std::array<AllPass<float>, 4>, kNumLoops> diffuser;
        struct Loop
        {
            DelayLine<double> line;
            std::array<AllPass<double>, 2> ap;
            BasicBiquad<double> loShelf, hiShelf;
            double gain = 0.0;
            // space modulation: sine LFO as a rotating phasor, first-order all-pass interpolator state
            double lfoC = 1.0, lfoS = 0.0, rotC = 1.0, rotS = 0.0, apState = 0.0;
            double mixX = 0.0, mixY = 0.0; // loop-mixing crossover all-pass state
        };
        std::array<Loop, kNumLoops> loops;
        std::array<int, kNumLoops> loopLen{};               // loop lengths
        std::array<std::array<int, kNumLoops>, 2> tapLen{}; // mid-loop output taps per era
        size_t tapEra = 0;
        double mixApC = 0.0; // loop-mixing crossover all-pass coefficient (0 = mix at every frequency)
        double tapWeight = 0.4, tapNorm = 1.0;
        double modDepth = 0.0, modTarget = 0.0, modSmooth = 0.0; // samples of peak delay swing

        BasicBiquad<float> lpf[2];
        // Late-tail bass coherence: a third-order all-pass complementary split of mid and side at kTailCohHz (a
        // first- and a second-order all-pass, coefficients and states; [0] mid, [1] side) and the era's gains
        double cohC1 = 0.0, cohB1 = 0.0, cohB2 = 0.0, cohGain[2]{1.0, 1.0};
        struct CohState
        {
            double x1 = 0.0, y1 = 0.0, u1 = 0.0, u2 = 0.0, v1 = 0.0, v2 = 0.0;
        };
        CohState coh[2];
        int cohSet = -1;
        // Late-tail band coherence (thirty-fourth pass): a band-pass copy of each channel fed to the other at bandEps
        double bandB0 = 0.0, bandA1 = 0.0, bandA2 = 0.0, bandEps = 0.0, bandZ[2][2]{};
        float lateBwCoef = 1.0f; // late-network input low-pass (lateBandwidthHz)
        std::array<float, kNumLoops> lateBwState{};
        float erBwCoef = 1.0f; // reflection-output low-pass (erBandwidthHz)
        float erBwState[2]{};
        BasicBiquad<float> erTop[2]; // reflection-output top-octave low-pass (erTopHz)
        bool erTopOn = false;
        // voice tilts (thirty-fifth pass): high shelves on the audible reflections and on the late tail
        BasicBiquad<float> erTilt[2];
        BasicBiquad<double> lateTilt[2];
        bool erTiltOn = false, lateTiltOn = false;
        float erTiltSet = 0.0f, lateTiltSet = 0.0f;
        bool tiltFresh = true; // redesign the tilts (new rate)
        float erTopHzSet = -1.0f;
        double hiRefFrac = 1.0;  // share of the high shelf's plateau reached at the reference (per rate and corner)
        void applyEr2 (int set); // E/R2 diffuser lengths and gains for fs and the era (0 REV7, 1 REV5); no allocation
        void measureHiRef ();    // hiRefFrac for fs and hiXoverHz (a probe biquad; no allocation)
        double hiPlateau (double hiRatio) const; // hi_ratio (8 kHz octave) -> the shelf's plateau ratio
        bool lpfOn = false;

        // ER feedback: a comb on each channel's reflection output, with a one-pole low-pass in the loop (FB HIGH)
        DelayLine<float> erFbLine[2];
        float erFbLp[2]{}, erFbG = 0.0f, erFbHiGain = -1.0f, erFbCoef = 1.0f; // erFbHiGain: the HIGH erFbCoef is for
        // era-5 reflection diffusion (one short all-pass per channel on the reflection output, sixteenth pass)
        std::array<AllPass<float>, 2> erDiff;
        bool erDiffOn = false;
        int erDiffLen = 0;
        // E/R2 diffuser: four series all-passes per channel on the reflection output (twenty-fifth pass, era 7;
        // twenty-eighth pass, era 5 with its own decay and channel scale); er2Set is the era its lengths are set for
        std::array<std::array<AllPass<float>, 4>, 2> er2Diff;
        bool er2On = false;
        int er2Set = -1;
        int erFbDelay = 1;

        // Secondary reverb: a smaller network fed from the primary network's input, delayed by rev2_delay
        DelayLine<double> rev2Pre;
        struct Loop2
        {
            DelayLine<double> line;
            AllPass<double> ap;
            BasicBiquad<double> loShelf, hiShelf;
            double gain = 0.0;
            int len = 1;
        };
        std::array<Loop2, kNumLoops2> loops2;
        int rev2Delay = 0;
        double rev2InGain = 0.0, rev2OutGain = 0.0;
        bool rev2On = false;
    };
} // namespace yrev::dsp
