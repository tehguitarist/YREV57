#pragma once

#include <array>
#include <vector>

// FX slot: delay, echo, modulation (flange, reverb flange, chorus A/B, phasing, tremolo, symphonic), pitch shift
// and auto-pan, with the slot's own output low-pass. Stereo in place. Designed as a standalone block: the chain
// decides where it sits (before or after the reverb).
//
// Structures and the source/inference split are in docs/design/fx-slot.md. Everything here is our own design.
namespace yrev::dsp
{
    // Indices match the `fx_type`, `mod_type` and `pan_dir` choice lists in Parameters.cpp.
    namespace fxtype
    {
        inline constexpr int delay = 0, echo = 1, mod = 2, pitch = 3, pan = 4, off = 5;
    }
    namespace modtype
    {
        inline constexpr int flange = 0, revFlange = 1, chorusA = 2, chorusB = 3, phasing = 4, tremolo = 5,
                             symphonic = 6;
    }
    namespace pandir
    {
        inline constexpr int leftToRight = 0, rightToLeft = 1, backAndForth = 2;
    }

    // Plain values in the parameters' natural units (percent stays 0..100, times in ms, etc.).
    struct FxParams
    {
        int era = 0;          // 0 = era 7 (REV7), 1 = era 5 (REV5)
        double hostBpm = 0.0; // <= 0 means unknown; sync then has no effect

        int type = fxtype::off; // fx_type
        bool link = false;      // fx_link: R follows L (delay and echo times)
        bool sync = false;      // fx_sync: delay and echo times snap to the nearest note value at hostBpm
        float lpfKHz = 17.0f;   // fx_lpf, 1..16 kHz, >= 17 is thru

        float dlyLMs = 100.0f, dlyRMs = 100.0f; // dly_l, dly_r (0.1..900 era 7, ..1400 era 5)
        float dlyLevel = 50.0f;                 // dly_level, %
        float dlyFb = 0.0f;                     // dly_fb, Lch % (-99..99; era 5 only, the REV7 delay has none)
        float dlyFbR = 0.0f;                    // dly_fb_r, Rch % (-99..99; era 5)
        float dlyHigh = 1.0f;                   // dly_high, x0.1..1.0 high-frequency feedback (era 5)

        float echoLMs = 170.0f, echoRMs = 178.0f; // echo_l, echo_r (0.1..450)
        float echoFb = 50.0f;                     // echo_fb, Lch % (0..99 era 7, shared by both sides; -99..99 era 5)
        float echoFbR = 50.0f;                    // echo_fb_r, Rch % (era 5)
        float echoDamp =
            1.0f; // HIGH ratio x0.1..1.0 of the repeats' treble (1.0 = none); from echo_damp / 10 (echoDampToHigh)
        float echoLevel = 50.0f; // echo_level, %
        // Set by the converter: the bridge delay, ms. It comes off the first repeat only (the output read); the loop
        // keeps the panel time, so every later repeat is spaced as printed (M6, the hardware Stereo Echo capture).
        float repeatAdvanceMs = 0.0f;

        int modType = modtype::chorusA; // mod_type
        float lfoRateHz = 1.0f;         // lfo_rate (0.1..20 era 7, ..40 era 5)
        float modDepth = 50.0f;         // mod_depth, % (flange, reverb flange, phasing, tremolo, symphonic)
        float dmDepth = 50.0f;          // dm_depth, % (chorus A/B delay modulation; 100 % = +-4 ms)
        float amDepth = 40.0f;          // am_depth, % (chorus A/B amplitude modulation)
        float modDelayMs = 1.2f; // mod_delay, ms (flange 0.1..100, reverb flange 0.1..30; phasing 0.1..8, era 5 0.1..5)
        float modFb = 35.0f;     // mod_fb, % (0..99; flange only)
        float modRtS = 2.5f;     // mod_rt, s: unused since 2026-11-22 (the reverb flange's decay is the engine)
        float modLpfKHz = 17.0f; // mod_lpf, 1..16 kHz, >= 17 is thru (mod types only, after fx_lpf)

        int pitchSel = 0;          // pitch_sel: A = one voice with feedback, B = two centre voices, C = L and R voices
        float pitchShiftSt = 0.0f; // pitch_shift, semitones (-12..12): A's voice, B's 1st, C's left
        float pitchFineCents = 0.0f;  // pitch_fine, cents (-100..100)
        float pitchDelayMs = 10.0f;   // pitch_delay, ms (0.1..400; C 0.1..200)
        float pitchFb = 0.0f;         // pitch_fb, % (0..99; A only)
        float pitch2ShiftSt = 0.0f;   // pitch2_shift: B's 2nd voice, C's right
        float pitch2FineCents = 0.0f; // pitch2_fine
        float pitch2DelayMs = 20.0f;  // pitch2_delay

        int panDir = pandir::leftToRight; // pan_dir
        float panRateHz = 1.0f;           // pan_rate (0.1..40)
        float panDepth = 50.0f;           // pan_depth, %

        // Period A/B options (fx-slot.md "Period authenticity"): global settings, not parameters, so
        // each can be heard against the current build before a default is chosen. All off is the build as before.
        bool steppedSweep = false; // mod delay sweeps quantised to 8-bit MOD words (YM3807, inference)
        bool linearRead = false;   // linear fractional delay reads instead of cubic Hermite
        bool triangleLfo = false;  // triangle LFO for the modulation types instead of sine
        bool afterReverb = false;  // set by the chain for Rev > FX: the input is the reverb's uncorrelated L and R
        bool tremoloDm = false;    // tremolo built like the chorus: a small delay sweep beside the AM (REV7 p. 19)
    };

    namespace fxdetail
    {
        // Power-of-two ring buffer with cubic Hermite (or, with `linear`, linear) fractional reads. Read before write:
        // read (d) returns the input from d samples ago (so d >= 2 is required; smaller values are clamped).
        class Line
        {
        public:
            void allocate (int maxDelaySamples);
            void clear ();
            void write (float x) noexcept;
            float read (double delaySamples) const noexcept;
            bool linear = false;

        private:
            std::vector<float> buf;
            int mask = 0;
            int pos = 0;
            double maxDelay = 2.0;
        };

        // First-order low-pass (bilinear one-pole), as the reverb's wet_lpf: the REV5's delay, echo and modulation
        // programs use the same LPF FRQ control as its reverbs (owner's manual pp. 25-26, page images), and the
        // reverb's was fitted first order on the captures (twenty-first pass); no capture covers the FX path, so the FX
        // LPFs follow it (the maintainer's decision, 2026-11-20). Thru when inactive.
        struct Lowpass1
        {
            void reset () noexcept { z1 = 0.0f; }
            void set (double sampleRate, float cutoffKHz);
            float process (float x) noexcept;
            bool active = false;

        private:
            float b0 = 1.0f, a1 = 0.0f, z1 = 0.0f;
        };

        // One-pole parameter smoother that snaps to its first target (so the first block is exact).
        struct Slew
        {
            void reset () noexcept { primed = false; }
            double tick (double target, double a) noexcept
            {
                if (!primed)
                {
                    v = target;
                    primed = true;
                }
                else
                    v += a * (target - v);
                return v;
            }
            double v = 0.0;
            bool primed = false;
        };
    } // namespace fxdetail

    class FxSlot
    {
    public:
        void prepare (double sampleRate, int maxBlockSize);
        // Switches to another rate up to the prepared one without allocating (the converter runs the slot at the
        // era's native rate); clears the state.
        void setSampleRate (double sampleRate);
        void reset ();
        // Stereo in place. Off passes the audio through untouched.
        void process (float* l, float* r, int numSamples, const FxParams& p);
        // True when process() with these settings would leave the audio untouched and change nothing: Off, settled, no
        // fade running, so a caller may skip the call (CPU pass, 2026-11-21).
        bool idle (const FxParams& p) const noexcept
        {
            return primed && fadeOutLeft == 0 && fadeInLeft == 0 && p.type == fxtype::off && lastType == fxtype::off;
        }

        // Seconds for the effect's repeats to fall by 60 dB (0 for off and pan, short for the mod types).
        static double tailSeconds (const FxParams& p);

        // How much of the input the chain takes off the slot's output to get the effect's own part when the slot runs
        // before the reverb: delay and echo 1 (their wet part is the repeats alone); 0 for the rest, whose whole output
        // is the effect (the flange keeps its direct half, which makes the notches; chorus and symphonic have none).
        // Was 0.5 for chorus, symphonic and flange until 2026-11-22, which left the flange's wet part a lone swept copy
        // and put a negative dry into the symphonic's.
        static float dryCarried (const FxParams& p);

    private:
        void runType (float* l, float* r, int n, const FxParams& p);
        void processRepeats (float* l, float* r, int n, const FxParams& p, bool echo);
        void processModulation (float* l, float* r, int n, const FxParams& p);
        void processPitch (float* l, float* r, int n, const FxParams& p);
        void processPan (float* l, float* r, int n, const FxParams& p);
        void clearState ();
        double syncedMs (double ms, const FxParams& p) const;

        double sr = 48000.0, maxSr = 48000.0;
        double smoothA = 0.01; // per-sample smoothing coefficient (about 20 ms)
        int lastType = -1, lastModType = -1;

        std::array<fxdetail::Line, 2> lineA, lineB;
        std::array<fxdetail::Slew, 2> sTime;
        fxdetail::Slew sLevel, sFb, sA, sB, sRate;
        std::array<float, 2> damp{};     // echo damping filter states
        std::array<float, 2> dampPrev{}; // era-7 echo: the two-tap average's previous loop sample
        std::array<float, 2> panGain{0.5f, 0.5f};
        double phase = 0.0;                         // LFO phase in cycles
        double phaseSlow = 0.0;                     // symphonic's slow LFO phase in cycles
        double shiftPhase = 0.0, shiftPhase2 = 0.0; // pitch shifter window phases, cycles
        // Type changes fade the old effect out, clear it and fade the new one in (the REV7 mutes briefly on a
        // program change, owner's manual p. 23).
        FxParams oldParams;
        int fadeLen = 480, fadeOutLeft = 0, fadeInLeft = 0, primeLeft = 0;
        bool primed = false;
        std::array<fxdetail::Lowpass1, 2> fxLpf, modLpf;
        float lastLpf = -1.0f, lastModLpf = -1.0f;
        // coefficients kept between calls: per rate (setSampleRate) and per setting
        float highA = 1.0f, panA = 1.0f; // highA: HIGH low-pass coefficient for highFor
        double highFor = -1.0;
        struct PitchRatio
        {
            float st = 1.0e9f, ct = 1.0e9f; // the settings ratio was worked out for (none yet)
            double ratio = 1.0;
        };
        std::array<PitchRatio, 2> pitchRatio{};
    };
} // namespace yrev::dsp
