#include "Chain.h"

#include <algorithm>
#include <cmath>

namespace yrev::dsp
{
    namespace
    {
        constexpr double kRampSeconds = 0.02;
        constexpr float kAnalogQ = 0.9f; // the analogue bands are fixed-width bells
        constexpr float kMonoSum = 0.5f; // L+R summed to the mono wet input, scaled so a centred signal keeps its level
        constexpr float kShelfQ = 0.707f;
        // Output safety limiter (a plugin setting, on by default): instant gain reduction to the ceiling, so
        // nothing leaves above it (runaway FX feedback, wild EQ), then a smooth recovery. Not a mastering limiter.
        constexpr float kLimitCeiling = 0.989f; // -0.1 dBFS
        constexpr double kLimitReleaseSeconds = 0.15;
        // Whole-engine crossfade on an era, eco or preset change: the new engine's input
        // fades in while the old one's fades out, equal power; the old engine rings out on its own settings.
        constexpr double kSwitchSeconds = 0.03;
        constexpr float kSilenceFloor = 1.0e-5f; // -100 dBFS: the old engine has rung out below this...
        constexpr double kQuietSeconds = 0.25;   // ...held this long
        // Ring-out limit (design choice): at the longest RTs (era 5 allows 99 s) the old engine would run, at double
        // the CPU, for minutes; after this long its output fades out instead.
        constexpr double kMaxRingSeconds = 30.0;
        constexpr double kRingFadeSeconds = 2.0;
        // A switch that arrives while the old engine still rings waits for it to fade out over this time.
        constexpr double kQueueFadeSeconds = 0.03;
        constexpr float kHalfPi = 1.57079632679f;
    } // namespace

    void Chain::prepare (double newSampleRate, int maxBlockSize)
    {
        sampleRate = newSampleRate;
        maxBlock = std::max (1, maxBlockSize);
        for (auto* v : {&dryL, &dryR, &wetL, &wetR, &key, &oldL, &oldR})
            v->assign ((size_t) maxBlock, 0.0f);

        for (auto* s : {&inGain, &outGain, &wetAmount, &soloAmount, &bypassAmount, &muteGain, &phaseR})
            s->reset (sampleRate, kRampSeconds);
        inGain.setCurrentAndTargetValue (1.0f);
        outGain.setCurrentAndTargetValue (1.0f);
        wetAmount.setCurrentAndTargetValue (0.5f);
        soloAmount.setCurrentAndTargetValue (0.0f);
        bypassAmount.setCurrentAndTargetValue (0.0f);
        muteGain.setCurrentAndTargetValue (1.0f);
        phaseR.setCurrentAndTargetValue (1.0f);

        widener.prepare (sampleRate);
        ducker.prepare (sampleRate);
        for (auto& c : converter)
            c.prepare (sampleRate, maxBlock);
        for (auto& r : dryRing)
            r.assign ((size_t) converter[0].reportedLatency () + 1, 0.0f);
        dryPos = 0;
        xfadeStep = (float) (1.0 / (kSwitchSeconds * sampleRate));
        quietLen = (int64_t) (kQuietSeconds * sampleRate);
        maxRing = (int64_t) (kMaxRingSeconds * sampleRate);
        longDrop = std::max<int64_t> (1, (int64_t) (kRingFadeSeconds * sampleRate));
        shortDrop = std::max<int64_t> (1, (int64_t) (kQueueFadeSeconds * sampleRate));
        limRelease = (float) (1.0 - std::exp (-1.0 / (kLimitReleaseSeconds * sampleRate)));
        limGain = 1.0f;
        eqDirty = true;
        reset ();
    }

    void Chain::reset ()
    {
        for (auto& band : aeq)
            for (auto& f : band)
                f.reset ();
        ducker.reset ();
        for (auto& c : converter)
            c.reset ();
        for (auto& r : dryRing)
            std::fill (r.begin (), r.end (), 0.0f);
        dryPos = 0;
        primed = false;
        oldLive = dropping = false;
        xfade = oldOut = 1.0f;
        oldAge = oldQuiet = 0;
    }

    void Chain::runWetPath (float* wl, float* wr, int n, const ChainParams& p)
    {
        // decide at the chunk start: take the new settings, start a switch, or wait for the old engine to fade out
        if (!primed)
        {
            slotParams[active] = p; // first settings after a reset: nothing is ringing, so no crossfade
            primed = true;
        }
        else if (needsSwitch (p, slotParams[active]))
        {
            if (!oldLive)
            {
                // the active slot becomes the old one and keeps its settings; the other starts empty
                oldIn0 = std::sin (xfade * kHalfPi); // its input share now, so a switch mid-fade has no jump
                active = 1 - active;
                converter[active].reset ();
                slotParams[active] = p;
                xfade = 0.0f;
                oldLive = true;
                dropping = false;
                oldOut = 1.0f;
                oldAge = oldQuiet = 0;
                ++switches;
            }
            else if (!dropping || dropStep < 1.0f / (float) shortDrop)
            {
                dropping = true; // the newest settings win when the old engine has gone
                dropStep = 1.0f / (float) shortDrop;
            }
        }
        else
        {
            slotParams[active] = p;
        }
        // the engine reads its reflections in true stereo when the wet input is (ADR 0006); in place, no switch
        for (auto& sp : slotParams)
            sp.engine.trueStereo = sp.wetInput == 2 ? 1 : 0;

        float* ol = oldL.data ();
        float* orr = oldR.data ();
        if (oldLive)
        {
            if (xfade < 1.0f)
            {
                std::copy (wl, wl + n, ol);
                std::copy (wr, wr + n, orr);
            }
            else
            {
                std::fill (ol, ol + n, 0.0f);
                std::fill (orr, orr + n, 0.0f);
            }
        }
        if (xfade < 1.0f)
            for (int i = 0; i < n; ++i)
            {
                xfade = std::min (1.0f, xfade + xfadeStep);
                const float a = xfade * kHalfPi;
                const float gNew = std::sin (a);
                wl[i] *= gNew;
                wr[i] *= gNew;
                if (oldLive)
                {
                    const float gOld = oldIn0 * std::cos (a);
                    ol[i] *= gOld;
                    orr[i] *= gOld;
                }
            }

        processSlot (active, wl, wr, n);
        if (!oldLive)
            return;
        processSlot (1 - active, ol, orr, n);

        for (int i = 0; i < n; ++i)
        {
            if (dropping)
                oldOut = std::max (0.0f, oldOut - dropStep);
            const float pk = std::max (std::abs (ol[i]), std::abs (orr[i]));
            oldQuiet = pk < kSilenceFloor ? oldQuiet + 1 : 0;
            wl[i] += ol[i] * oldOut;
            wr[i] += orr[i] * oldOut;
        }
        oldAge += n;
        if ((dropping && oldOut <= 0.0f) || (xfade >= 1.0f && oldQuiet >= quietLen))
        {
            oldLive = dropping = false;
        }
        else if (!dropping && oldAge >= maxRing)
        {
            dropping = true;
            dropStep = 1.0f / (float) longDrop;
        }
    }

    void Chain::processSlot (int s, float* l, float* r, int n)
    {
        const auto& p = slotParams[s];
        ConverterParams cp{p.era, p.character, p.dither};
        cp.eco = p.eco;
        cp.frozenNoise = p.frozenNoise;
        cp.fx = &p.fx;
        cp.fxOrder = p.fxOrder;
        cp.revMix = p.revMix;
        cp.fxWet = p.fxWet;
        cp.gate = p.gate;
        cp.deq.on = p.deqOn;
        cp.deq.loShelf = p.deqLoShelf;
        cp.deq.hiShelf = p.deqHiShelf;
        for (int b = 0; b < 3; ++b)
        {
            cp.deq.f[b] = p.deqF[b];
            cp.deq.g[b] = p.deqG[b];
            cp.deq.q[b] = p.deqQ[b];
        }
        converter[s].process (l, r, n, p.engine, cp);
    }

    void Chain::updateEq (const ChainParams& p)
    {
        const float a[6] = {p.aeqLoF, p.aeqLoG, p.aeqMidF, p.aeqMidG, p.aeqHiF, p.aeqHiG};
        if (eqDirty || !std::equal (a, a + 6, lastAeq))
        {
            std::copy (a, a + 6, lastAeq);
            const float f[3] = {a[0], a[2], a[4]}, g[3] = {a[1], a[3], a[5]};
            for (int b = 0; b < 3; ++b)
                for (int c = 0; c < 2; ++c)
                    aeq[b][c].setPeak (sampleRate, f[b], kAnalogQ, g[b]);
        }

        lastEqParams = p;
        eqDirty = false;
    }

    void Chain::applyBands (Biquad (&bands)[3][2], float* l, float* r, int n)
    {
        for (int b = 0; b < 3; ++b)
            for (int i = 0; i < n; ++i)
            {
                l[i] = bands[b][0].process (l[i]);
                r[i] = bands[b][1].process (r[i]);
            }
    }

    void Chain::process (float* left, float* right, int numSamples, const ChainParams& p)
    {
        for (int done = 0; done < numSamples; done += maxBlock)
            processChunk (left + done, right + done, std::min (maxBlock, numSamples - done), p);
    }

    void Chain::processChunk (float* left, float* right, int n, const ChainParams& p)
    {
        updateEq (p);

        inGain.setTargetValue (juce::Decibels::decibelsToGain (p.inLevelDb, -90.0f));
        outGain.setTargetValue (juce::Decibels::decibelsToGain (p.outLevelDb, -90.0f));
        wetAmount.setTargetValue (juce::jlimit (0.0f, 1.0f, p.mix * 0.01f));
        soloAmount.setTargetValue (p.wetSolo ? 1.0f : 0.0f);
        bypassAmount.setTargetValue (p.bypass ? 1.0f : 0.0f);
        muteGain.setTargetValue (p.mute ? 0.0f : 1.0f);
        phaseR.setTargetValue (p.outPhaseR ? -1.0f : 1.0f);

        float* dl = dryL.data ();
        float* dr = dryR.data ();
        float* wl = wetL.data ();
        float* wr = wetR.data ();

        // dry copy and the ducker's key (the dry mono level)
        // (delayed by the reported latency, so the wet path's padded converter delay lines up with it)
        const int dryDelay = latencySamples ();
        for (int i = 0; i < n; ++i)
        {
            if (dryDelay > 0)
            {
                const size_t len = dryRing[0].size (), rd = (dryPos + len - (size_t) dryDelay) % len;
                dl[i] = dryRing[0][rd];
                dr[i] = dryRing[1][rd];
                dryRing[0][dryPos] = left[i];
                dryRing[1][dryPos] = right[i];
                dryPos = (dryPos + 1) % len;
            }
            else
            {
                dl[i] = left[i];
                dr[i] = right[i];
            }
            key[(size_t) i] = (dl[i] + dr[i]) * kMonoSum;
        }

        // wet input: mono (left only), stereo (summed) or true stereo; input level applies to the wet path only
        for (int i = 0; i < n; ++i)
        {
            const float g = inGain.getNextValue ();
            if (p.wetInput == 0)
            {
                wl[i] = wr[i] = left[i] * g;
            }
            else if (p.wetInput == 1)
            {
                wl[i] = wr[i] = (left[i] + right[i]) * kMonoSum * g;
            }
            else
            {
                wl[i] = left[i] * g;
                wr[i] = right[i] * g;
            }
        }

        if (p.aeqOn)
            applyBands (aeq, wl, wr, n); // wet-only analogue EQ, before the engine
        if (engineOn) // converter model with the FX slot and the engine at the native rate, crossfaded on a switch
            runWetPath (wl, wr, n, p);

        widener.setWidth (p.width);
        widener.process (wl, wr, n);

        ducker.setEnabled (p.duckOn);
        ducker.setRangeDb (p.duckRangeDb);
        ducker.setThresholdDb (p.duckThreshDb);
        ducker.setAttackMs (p.duckAttackMs);
        ducker.setReleaseMs (p.duckReleaseMs);
        ducker.process (key.data (), wl, wr, n);

        // mix, wet solo, bypass, output level, mute
        for (int i = 0; i < n; ++i)
        {
            const float wet = juce::jmin (1.0f, wetAmount.getNextValue () + soloAmount.getNextValue ());
            const float dry = 1.0f - wet;
            const float byp = bypassAmount.getNextValue ();
            const float out = outGain.getNextValue ();
            const float mute = muteGain.getNextValue ();
            const float rSign = phaseR.getNextValue ();

            const float mixedL = dl[i] * dry + wl[i] * wet;
            const float mixedR = dr[i] * dry + wr[i] * rSign * wet;
            float ol = ((1.0f - byp) * mixedL * out + byp * dl[i]) * mute;
            float orr = ((1.0f - byp) * mixedR * out + byp * dr[i]) * mute;
            if (p.limiter)
            {
                const float pk = std::max (std::abs (ol), std::abs (orr));
                const float target = pk > kLimitCeiling ? kLimitCeiling / pk : 1.0f;
                limGain = std::min (target, limGain + limRelease * (1.0f - limGain));
                ol *= limGain;
                orr *= limGain;
            }
            left[i] = ol;
            right[i] = orr;
        }
    }
} // namespace yrev::dsp
