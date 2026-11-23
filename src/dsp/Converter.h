#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include "Biquad.h"
#include "ConverterFilters.h"
#include "Engine.h"
#include "FxSlot.h"
#include "Gate.h"

namespace yrev::dsp
{
    // Analogue filter chain (ConverterFilters.h sections) turned into double-precision biquads at the host rate by a
    // prewarped bilinear transform. Stereo, allocation-free.
    class AnalogCascade
    {
    public:
        static constexpr int kMaxSections = 8;
        void design (const conv::AnalogSection* sections, int count, double warpHz, double sampleRate);
        void reset ();
        double process (int channel, double x) noexcept
        {
            for (int i = 0; i < count; ++i)
                x = f[i][channel].process (x);
            return x;
        }

    private:
        BasicBiquad<double> f[kMaxSections][2];
        int count = 0;
    };

    // Streaming band-limited rate converter, host rate <-> native rate, around a per-sample native process. Kaiser-
    // windowed sinc read from a table; one fixed latency (latencySamples) for the round trip. Equal rates bypass it.
    // When the two rates are in a small integer ratio p / q (every era at the usual host rates) the kernel offsets
    // repeat every q native (p host) samples, so the weights are worked out once per phase (preparePolyphase) and
    // each output is a plain dot product (CPU pass, 2026-11-21: the same weights, without the per-tap table reads).
    class RateBridge
    {
    public:
        void prepare (double hostRate, double lowestNativeRate);
        // Builds the per-phase weights for one native rate and hold setting (allocates: call after prepare(), from
        // prepare only). setNativeRate and setHoldDroop then use them when they match; any other setting falls back to
        // reading the kernel table per tap.
        void preparePolyphase (double nativeRate, bool holdOn);
        void setNativeRate (double nativeRate);
        // Era 5 (twenty-seventh pass): the up path plays each native sample as a held step (the D/A's sample-and-hold),
        // band-limited, so the droop is the hold's own sin(x)/x instead of an analogue shelf after the bridge.
        void setHoldDroop (bool on);
        void reset ();
        int latencySamples () const { return latency; }
        bool polyphaseActive () const { return poly != nullptr; } // test hook
        // Test hook: ignore the per-phase weights (the per-tap path, for comparison).
        void setPolyphaseEnabled (bool on)
        {
            polyEnabled = on;
            selectPolyphase ();
        }

        // Processes one host sample: returns the delayed host output after running every due native sample
        // through fn (float l, float r, float& outL, float& outR).
        template <typename Fn> void process (float inL, float inR, float& outL, float& outR, Fn&& fn) noexcept
        {
            if (bypass)
            {
                fn (inL, inR, outL, outR);
                return;
            }
            const auto j = hostIndex++;
            put (hostRing, hostMask, j, inL, inR);
            while ((double) nativeIndex * ratio + halfDown <= (double) j)
            {
                float l = 0.0f, r = 0.0f;
                if (poly != nullptr)
                    polyDown (l, r);
                else
                    convolve (hostRing, hostMask, (double) nativeIndex * ratio, halfDown, fcDown, l, r);
                float ol = 0.0f, orr = 0.0f;
                fn (l, r, ol, orr);
                put (nativeRing, nativeMask, nativeIndex, ol, orr);
                ++nativeIndex;
            }
            if (poly != nullptr)
                polyUp (j - latency, outL, outR);
            else
                convolve (nativeRing, nativeMask, (double) (j - latency) / ratio, hold ? halfUpHold : halfUp, fcUp,
                          outL, outR, hold);
        }

    private:
        float kernel (double x) const noexcept; // x in zero crossings, |x| < kZeroCrossings
        // a kernel table read at pos (in table cells), linearly interpolated; 0 past the end
        static float lerp (const std::vector<float>& tab, double pos) noexcept
        {
            const auto i = (size_t) pos;
            if (i + 1 >= tab.size ())
                return 0.0f;
            const float frac = (float) (pos - (double) i);
            return tab[i] + frac * (tab[i + 1] - tab[i]);
        }
        void convolve (const std::array<std::vector<double>, 2>& ring, int64_t mask, double centre, double half,
                       double fc, float& l, float& r, bool holdUp = false) const noexcept;
        void updateLatency ();
        void selectPolyphase () noexcept;
        void polyDown (float& l, float& r) const noexcept;
        void polyUp (int64_t hostOut, float& l, float& r) const noexcept;
        // Rings are twice their length (mask + 1): each sample is stored at its index and one length on, so a kernel's
        // span reads contiguously; stored as double, so the per-tap reads need no conversion (CPU pass, 2026-11-21).
        static void put (std::array<std::vector<double>, 2>& ring, int64_t mask, int64_t index, float l,
                         float r) noexcept
        {
            const auto k = (size_t) (index & mask), len = (size_t) (mask + 1);
            ring[0][k] = ring[0][k + len] = l;
            ring[1][k] = ring[1][k + len] = r;
        }

        // One direction's weights: phase m's taps start offset[m] input samples from the whole part of the centre
        struct PolyKernel
        {
            std::vector<double> w; // phases x taps, zero-padded
            std::vector<int> offset;
            int taps = 0;
        };
        // host / native = p / q in lowest terms: down phases repeat every q native samples, up phases every p host ones
        struct PolySet
        {
            double nativeRate = 0.0;
            bool hold = false;
            int64_t p = 1, q = 1;
            PolyKernel down, up;
        };
        void buildKernel (PolyKernel& k, int64_t phases, double half, double fc, bool holdUp);

        static constexpr int kZeroCrossings = 24, kTableRes = 256;
        std::vector<float> table, holdTable; // holdTable: the up kernel integrated over one native sample (hold droop)
        double holdWidth = 1.0;              // one native sample in zero crossings of the up kernel
        double halfUpHold = 1.0;
        bool hold = false;
        std::array<std::vector<double>, 2> hostRing, nativeRing;
        int64_t hostMask = 0, nativeMask = 0, hostIndex = 0, nativeIndex = 0;
        double hostFs = 48000.0, nativeFs = 48000.0, ratio = 1.0, fcDown = 0.5, fcUp = 0.5, halfDown = 1.0,
               halfUp = 1.0;
        int latency = 0;
        bool bypass = true;
        std::array<PolySet, 2> polySets; // one per era's native rate
        int numPolySets = 0;
        bool polyEnabled = true;
        const PolySet* poly = nullptr; // the set matching the current rate and hold, if any
    };

    // 2x half-band FIR (Kaiser-windowed sinc) for the converter's oversampled analogue stages. Stereo.
    // A half-band kernel is zero at every even offset from its centre but the centre itself, and symmetric, so only
    // the odd-offset taps are multiplied, in mirrored pairs (CPU pass, 2026-11-21; the skipped taps are about 1e-17,
    // rounding residue of sin(k pi)). Histories are written twice, a ring length apart, so every read is contiguous.
    class HalfBand
    {
    public:
        static constexpr int kTaps = 95; // odd; group delay (kTaps - 1) / 2 samples at the doubled rate
        void prepare ();
        void reset ();
        // One host sample per channel in, two doubled-rate samples per channel out.
        void up (const double (&x)[2], double (&y0)[2], double (&y1)[2]) noexcept;
        // Two doubled-rate samples per channel in, one host sample per channel out.
        void down (const double (&x0)[2], const double (&x1)[2], double (&y)[2]) noexcept;

    private:
        static constexpr int kCentre = (kTaps - 1) / 2;  // 47: the one even-offset tap that is not zero
        static constexpr int kPairs = (kCentre + 1) / 2; // odd-offset taps on one side of the centre
        static_assert (kPairs % 4 == 0, "the half-band sums run in fours");
        static constexpr int kHist = 128; // power of two >= kTaps
        std::array<double, kTaps> h{};
        std::array<double, kPairs> hPair{}; // the odd-offset taps h[2k], k < kPairs (h[2k] = h[kTaps - 1 - 2k])
        double hCentre = 0.5;
        // [2 kHist][channel]: each sample is stored at pos and pos + kHist
        std::array<std::array<double, 2>, 2 * kHist> upHist{}, downHist{};
        int upPos = 0, downPos = 0;
    };

    // REV5 per-program digital EQ (YM3608 in the DSP, so before the D/A; own design ranges, parameters.md)
    struct DigitalEqParams
    {
        bool on = false;
        float f[3] = {120.0f, 1000.0f, 8000.0f}, g[3] = {0, 0, 0}, q[3] = {1, 1, 1};
        bool loShelf = false, hiShelf = false;
    };

    struct ConverterParams
    {
        int era = 0;              // 0 = era 7 (REV7-like), 1 = era 5 (REV5-like)
        float character = 100.0f; // %, 0 = clean (24-bit, no hiss), 100 = authentic (16-bit, truncating)
        bool dither = true;       // era 5 only
        bool eco = false;         // run the DSP stage at the host rate (no rate bridge, no 2x stages, so no era-5 hold
                                  // droop; ADR 0001)
        bool frozenNoise = false; // hiss and dither restart from a fixed seed on every reset (repeatable renders)
        const FxParams* fx = nullptr; // FX slot settings (nullptr = no FX)
        int fxOrder = 0;              // 0 = FX > Rev, 1 = Rev > FX
        float revMix = 1.0f;          // REV MIX 0..1 (hardware routing: the reverb beside the effect's bypass)
        bool fxWet = false;           // no bypass: fully wet serial routing (plugin choice)
        GateParams gate;              // REV5 gate after the effects (levelPct 0 = off)
        DigitalEqParams deq;          // used in era 5 only
    };

    // The modelled converter chain around the engine (docs/design/architecture.md section 1, engine.md):
    //   host rate (analogue): era 7  anti-alias LPF -> pre-emphasis;  era 5  pre-emphasis -> anti-alias LPF
    //   native rate (31.25 kHz era 7, 44.1 kHz era 5): A/D clip, optional dither (era 5), 16-bit truncation toward
    //     zero -> FX slot and engine in the fx_order -> gate (keyed by the A/D input) -> digital EQ (era 5) -> D/A
    //     16-bit truncation, hiss (the FX, gate and EQ ran in the same DSP)
    //   host rate (analogue): reconstruction LPF -> sample-and-hold droop -> de-emphasis
    // Below 88.2 kHz host rates the analogue stages run at twice the host rate. Era 5 is fitted on that realised chain
    // (bilinear cramping near Nyquist, the bridge, the hold droop; twenty-seventh pass), so its top octave lands where
    // it was measured; 4x was tried and fitted only 0.06 dB rms better for about 2.5x the converter's cost. The
    // bridge's own delay is taken off the pre-delay and the 1st-reflection delay, so timing matches the panel and no
    // plug-in latency is reported (the dry path is not delayed). No allocation after prepare().
    class Converter
    {
    public:
        void prepare (double sampleRate, int maxBlockSize);
        void reset ();
        void process (float* left, float* right, int numSamples, const EngineParams& engineParams,
                      const ConverterParams& p);

        // Wet-path delay of the converter model in host samples (half-band filters plus the rate bridge).
        int latencySamples () const { return latency; }
        double nativeRate () const { return native; }
        // Test hook: the native stage passes A/D -> D/A without the engine (to measure the converter alone).
        void setEngineBypass (bool on) { engineBypass = on; }
        // Test hook: keep hiss and dither running in silence (normally gated off below -110 dBFS).
        void setNoiseGateEnabled (bool on) { noiseGate = on; }

    private:
        void configure (int era, bool eco);
        float quantise (float x, bool dither, float carryLsb = 0.0f) noexcept;
        float noise () noexcept; // uniform -1..1, deterministic

        double hostFs = 48000.0, innerFs = 96000.0, native = 31250.0;
        // INITIAL DELAY on the effect: "an offset between the direct sound and the beginning of the effect" (REV5 OM
        // p. 14; REV7 pp. 14-16). A ring per channel on the effect's own part, up to 1 s at the highest native rate.
        std::vector<float> fxOffset[2];
        size_t fxOffsetPos = 0;
        int era = -1, oversample = 2, latency = 0;
        bool engineBypass = false, noiseGate = true, eco = false, frozen = false;
        double maxDspFs = 44100.0; // the highest rate the engine, FX and gate were prepared for
        uint32_t randomSeed = 0x2545f491u;
        Engine engine;
        FxSlot fxSlot;
        Gate gate;
        Biquad deq[3][2]; // [band][channel], designed at the native rate
        DigitalEqParams lastDeq;
        bool deqDirty = true;
        void updateDeq (const DigitalEqParams& d);
        FxParams fxNative; // the block's FX settings with the bridge delay taken off the repeat times
        RateBridge bridge;
        AnalogCascade aaIn, recon, droop, emphasis, deemphasis;
        BasicBiquad<double> bassLift[2]; // era 7 wet-path low shelf (host rate, after de-emphasis)
        BasicBiquad<double> dcBlock[2];  // wet-path DC block, both eras (kDcBlockHz)
        HalfBand halfBand;
        float step = 1.0f / 32768.0f, hissGain = 0.0f, fade = 1.0f, fadeStep = 0.0f;
        uint32_t rng = 0x12345678u;
        // noise gates (native rate): dither follows the A/D input, hiss follows the engine output
        float envIn = 0.0f, envOut = 0.0f, gateIn = 0.0f, gateOut = 0.0f;
        float envDecay = 0.0f, gateAttack = 0.0f, gateRelease = 0.0f;
        void followGate (float x, float& env, float& gate) noexcept;
    };
} // namespace yrev::dsp
