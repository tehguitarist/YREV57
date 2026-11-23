#pragma once

// REV5-style gate (REV5 owner's manual, REVERB & GATE p. 18, combination programs p. 36). Keyed from the program
// input, applied to the effect output. Header-only, no allocation after prepare(); run it per native-rate sample.
//
// Behaviour (manual facts first, the rest is inference):
//  - TRG LEVEL: input level needed to open the gate; 1 % triggers on the tiniest signal, 99 % only on very loud ones
//    (manual). Our levelPct 0 means gate off (design choice: gain is exactly 1).
//  - Threshold mapping: linear in dB from kThresholdMinDb at 1 % to kThresholdMaxDb at 100 % (host dBFS), compared
//    against the key's instantaneous absolute value (a sample peak detector, no smoothing). The end points are those of
//    a third-party SPX90 model's Reverb & Gate (2026-11-22: a 1 kHz tone opens it from -53,
//    -29 and -5 dBFS peak at 1, 50 and 100); was -60 to 0 dBFS (inference).
//  - TRG DLY: the gate opens that long after the key crosses the threshold (manual). Done with a ring buffer of the
//    key's above-threshold flag, sized for kMaxTrigDelayMs at the highest prepared rate.
//  - HOLD / RELEASE: stay open for the hold, then close over the release (manual). Retrigger rule (inference): every
//    delayed above-threshold sample restarts the hold, so hold counts from the last time the key was above the
//    threshold plus the trigger delay. A retrigger during release re-opens from the current gain.
//  - Attack (inference): a linear ramp of kAttackMs so opening does not click.
//  - Release shape: exponential, falling 60 dB over the release time, then cut to exactly 0 below kReleaseFloorDb.
//    Measured on the same SPX90 model (a straight line in dB: 500 ms falls 6 dB per 50 ms, 2,000 ms 1.5 dB). Adopted
//    by the maintainer's decision (2026-11-22), replacing a linear-in-amplitude release (inference, 2026-11-20).

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace yrev::dsp
{
    struct GateParams
    {
        float levelPct = 0.0f;     // 0 = off; 1..100 = trigger level (%)
        float trigDelayMs = 10.0f; // 0.1..1000
        float holdMs = 200.0f;     // 1..24000
        float releaseMs = 400.0f;  // 3..24000
        // REV5 per-program noise gate (GATE LVL on INT PARAM, owner's manual pp. 22, 27-29, 34, 37, OCR): "the
        // program's internal noise gate ... noise is eliminated when no source signal is present, and the attack and
        // decay portions of the source signal are passed as naturally as possible". Taken as a gate on the program
        // input (inference: on the output it would cut every echo and reverb tail), opening with no trigger delay.
        // Off: the REV5 trigger gate of Reverb & Gate and the combinations, on the effect output (pp. 18, 36).
        bool onInput = false;
    };

    class Gate
    {
    public:
        static constexpr float kThresholdMinDb = -53.0f;  // threshold at 1 % (third-party SPX90 model)
        static constexpr float kThresholdMaxDb = -5.0f;   // threshold at 100 % (the same)
        static constexpr float kReleaseDb = -60.0f;       // fall over the release time
        static constexpr float kReleaseFloorDb = -120.0f; // then exactly 0
        static constexpr float kAttackMs = 1.0f;          // opening ramp (inference)
        static constexpr float kMaxTrigDelayMs = 1000.0f;

        // Allocates the delay ring; call outside the audio thread. maxSampleRate is the highest rate setSampleRate may
        // use.
        void prepare (double maxSampleRate)
        {
            maxRate = std::max (maxSampleRate, 1.0);
            ring.assign (static_cast<size_t> (std::ceil (maxRate * kMaxTrigDelayMs * 0.001)) + 2, 0);
            sampleRate = maxRate;
            updateTimes ();
            reset ();
        }

        // Never above the prepared rate (clamped). Keeps the gate state; no allocation.
        void setSampleRate (double newRate)
        {
            sampleRate = std::clamp (newRate, 1.0, maxRate);
            updateTimes ();
        }

        void reset ()
        {
            std::fill (ring.begin (), ring.end (), static_cast<uint8_t> (0));
            writePos = 0;
            gain = 0.0f;
            holdRemaining = 0;
        }

        void setParams (const GateParams& p)
        {
            params = p;
            enabledFlag = p.levelPct > 0.0f;
            const float pct = std::clamp (p.levelPct, 1.0f, 100.0f);
            const float db = kThresholdMinDb + (kThresholdMaxDb - kThresholdMinDb) * (pct - 1.0f) / 99.0f;
            threshold = std::pow (10.0f, db / 20.0f);
            updateTimes ();
        }

        bool enabled () const { return enabledFlag; }

        // `key` is the program input sample; returns the gain for this sample (exactly 1 when the gate is off).
        float process (float key) noexcept
        {
            if (!enabledFlag || ring.empty ())
                return 1.0f;

            const size_t n = ring.size ();
            ring[writePos] = std::fabs (key) >= threshold ? 1 : 0;
            const bool triggered = ring[(writePos + n - delaySamples) % n] != 0;
            writePos = (writePos + 1) % n;

            if (triggered)
            {
                holdRemaining = holdSamples;
                gain = std::min (1.0f, gain + attackStep);
            }
            else if (holdRemaining > 0)
            {
                --holdRemaining;
                gain = std::min (1.0f, gain + attackStep); // finish any attack ramp, then sit at 1
            }
            else if (gain > 0.0f)
            {
                // Exponential fall: kReleaseDb over the release time, then exactly 0 below the floor.
                gain *= releaseMul;
                if (gain < releaseFloor)
                    gain = 0.0f;
            }
            return gain;
        }

    private:
        void updateTimes ()
        {
            const auto toSamples = [this] (float ms)
            { return static_cast<int> (std::lround (ms * 0.001 * sampleRate)); };
            const int maxDelay = ring.empty () ? 0 : static_cast<int> (ring.size ()) - 1;
            delaySamples = static_cast<size_t> (
                std::clamp (toSamples (std::clamp (params.trigDelayMs, 0.1f, kMaxTrigDelayMs)), 0, maxDelay));
            holdSamples = std::max (1, toSamples (std::clamp (params.holdMs, 1.0f, 24000.0f)));
            const int releaseSamples = std::max (1, toSamples (std::clamp (params.releaseMs, 3.0f, 24000.0f)));
            releaseMul = std::pow (10.0f, kReleaseDb / 20.0f / static_cast<float> (releaseSamples));
            attackStep = 1.0f / static_cast<float> (std::max (1, toSamples (kAttackMs)));
        }

        GateParams params;
        std::vector<uint8_t> ring;
        double maxRate = 48000.0, sampleRate = 48000.0;
        size_t writePos = 0, delaySamples = 0;
        int holdSamples = 1, holdRemaining = 0;
        float threshold = 1.0f, gain = 0.0f, releaseMul = 0.0f, attackStep = 1.0f;
        const float releaseFloor = std::pow (10.0f, kReleaseFloorDb / 20.0f);
        bool enabledFlag = false;
    };
} // namespace yrev::dsp
