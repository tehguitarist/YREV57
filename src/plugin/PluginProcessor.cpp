#include "PluginProcessor.h"
#include "YrevEditor.h"

namespace
{
    constexpr double kMaxSwapWaitSeconds = 0.25; // longest the audio thread waits for a per-era settings swap
}

YrevProcessor::YrevProcessor ()
    : AudioProcessor (BusesProperties ()
                          .withInput ("Input", juce::AudioChannelSet::stereo (), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo (), true)),
      apvts (*this, nullptr, "YREV57", yrev::createParameterLayout ())
{
    auto r = [this] (const char* id) { return apvts.getRawParameterValue (id); };
    using namespace yrev;
    raw = {r (id::era),
           r (id::wetInput),
           r (id::inLevel),
           r (id::outLevel),
           r (id::mix),
           r (id::wetSolo),
           r (id::bypass),
           r (id::mute),
           r (id::outPhaseR),
           r (id::width),
           r (id::duckOn),
           r (id::duckRange),
           r (id::duckThresh),
           r (id::duckAttack),
           r (id::duckRelease),
           r (id::aeqOn),
           r (id::aeqLoF),
           r (id::aeqLoG),
           r (id::aeqMidF),
           r (id::aeqMidG),
           r (id::aeqHiF),
           r (id::aeqHiG),
           r (id::deqOn),
           r (id::deqLoShelf),
           r (id::deqHiShelf),
           {r (id::deqLoF), r (id::deqMidF), r (id::deqHiF)},
           {r (id::deqLoG), r (id::deqMidG), r (id::deqHiG)},
           {r (id::deqLoQ), r (id::deqMidQ), r (id::deqHiQ)},
           r (id::rt),
           r (id::hiRatio),
           r (id::loRatio),
           r (id::preDelay),
           r (id::diffusion),
           r (id::density),
           r (id::ref1Delay),
           r (id::ref1Level),
           r (id::erRevBal),
           r (id::wetLpf),
           r (id::erMode),
           r (id::liveness),
           r (id::roomSize),
           r (id::erCount),
           r (id::spaceMod),
           r (id::character),
           r (id::dither),
           r (id::fxType),
           r (id::fxOrder),
           r (id::fxLink),
           r (id::fxSync),
           r (id::fxLpf),
           r (id::dlyL),
           r (id::dlyR),
           r (id::dlyLevel),
           r (id::dlyFb),
           r (id::echoL),
           r (id::echoR),
           r (id::echoFb),
           r (id::echoDamp),
           r (id::echoLevel),
           r (id::modType),
           r (id::lfoRate),
           r (id::modDepth),
           r (id::dmDepth),
           r (id::amDepth),
           r (id::modDelay),
           r (id::modFb),
           r (id::modRt),
           r (id::modLpf),
           r (id::pitchShift),
           r (id::pitchFine),
           r (id::pitchDelay),
           r (id::pitchFb),
           r (id::panDir),
           r (id::panRate),
           r (id::panDepth),
           r (id::pitchSel),
           r (id::dlyFbR),
           r (id::dlyHigh),
           r (id::echoFbR),
           r (id::pitch2Shift),
           r (id::pitch2Fine),
           r (id::pitch2Delay),
           r (id::erFbGain),
           r (id::erFbDelay),
           r (id::erFbHi),
           r (id::rev2Time),
           r (id::rev2Delay),
           r (id::rev2Level),
           r (id::gateLevel),
           r (id::gateTrigDelay),
           r (id::gateHold),
           r (id::gateRelease),
           r (id::ref1LDelay),
           r (id::ref1LLevel),
           r (id::ref1RDelay),
           r (id::ref1RLevel),
           r (id::voiceLoopSize),
           r (id::voiceDiffSize),
           r (id::voiceLoXover),
           r (id::voiceErGain),
           r (id::gateInput),
           r (id::voiceHiXover),
           r (id::erType),
           r (id::voiceLateOnset),
           r (id::voiceErTilt),
           r (id::voiceLateTilt),
           r (id::revMix),
           r (id::fxWet),
           r (id::voiceRev2Hi),
           r (id::voiceRev2Gain)};
    apvts.addParameterListener (yrev::id::era, this);
    apvts.addParameterListener (yrev::id::modType, this);
    for (const auto& l : yrev::kEraLimits)
        apvts.addParameterListener (l.id, this);
    apvts.state.addListener (this);
    readSettings ();
    lastEra = juce::roundToInt (apvts.getRawParameterValue (yrev::id::era)->load ());
    settledEra = lastEra;
    // a preset load sets the era itself: keep the settings being left, and take the loaded ones as they are
    presets.onBeforeApply = [this]
    {
        if (setPerEra.load ())
            storeEra (lastEra);
    };
    presets.onAfterApply = [this]
    {
        lastEra = juce::roundToInt (apvts.getRawParameterValue (yrev::id::era)->load ());
        settledEra = lastEra;
    };
}

void YrevProcessor::readSettings ()
{
    const auto& st = apvts.state;
    setEco = st.getProperty ("quality", "native").toString () == "eco";
    setFrozen = st.getProperty ("noiseSeed", "random").toString () == "frozen";
    setLimiter = st.getProperty ("limiter", "1").toString () != "0";
    setPerEra = st.getProperty ("eraMemory", "perEra").toString () != "shared";
    const auto on = [&st] (const char* key, const char* byDefault = "0")
    { return st.getProperty (key, byDefault).toString () == "1"; };
    // FX period options (fx-slot.md): decided 2026-11-22 (maintainer, on the evidence): Stepped, Linear and Triangle
    // off (the third-party SPX90 models sweep smooth sines; nothing argues for linear reads), Trem. DM on (REV7 OM p. 19:
    // tremolo is made as the chorus is)
    setFxStepped = on ("fxStepped");
    setFxLinear = on ("fxLinear");
    setFxTriangle = on ("fxTriangle");
    setFxTremDm = on ("fxTremDm", "1");
}

void YrevProcessor::storeEra (int era)
{
    auto mem = apvts.state.getOrCreateChildWithName (kEraMemoryType, nullptr);
    auto slot = mem.getChildWithProperty ("era", era);
    if (!slot.isValid ())
    {
        slot = juce::ValueTree ("ERA");
        slot.setProperty ("era", era, nullptr);
        mem.appendChild (slot, nullptr);
    }
    slot.removeAllProperties (nullptr);
    slot.setProperty ("era", era, nullptr);
    slot.setProperty ("preset", presets.currentKey (), nullptr);
    for (const auto& [pid, v] : presets.currentValues ())
        if (pid != yrev::id::era)
            slot.setProperty (pid, v, nullptr);
}

bool YrevProcessor::recallEra (int era)
{
    const auto slot = apvts.state.getChildWithName (kEraMemoryType).getChildWithProperty ("era", era);
    if (!slot.isValid ())
        return false; // first visit: the settings carry over
    std::map<juce::String, float> v;
    for (const auto& [pid, cur] : presets.currentValues ())
        if (pid != yrev::id::era)
            v[pid] = slot.hasProperty (pid) ? (float) slot.getProperty (pid) : cur;
    presets.applyValues (v);
    apvts.state.setProperty ("preset", slot.getProperty ("preset"), nullptr);
    presets.restoreFromState ();
    return true;
}

void YrevProcessor::swapEraIfNeeded ()
{
    // Message thread. The era has changed (panel, host automation): with per-era memory, keep the settings being
    // left under their era and bring back the new era's, before the era limits are applied.
    const int era = juce::roundToInt (apvts.getRawParameterValue (yrev::id::era)->load ());
    if (era != lastEra && setPerEra.load ())
    {
        storeEra (lastEra);
        recallEra (era);
    }
    lastEra = era;
    settledEra = era;
}

YrevProcessor::~YrevProcessor ()
{
    cancelPendingUpdate ();
    apvts.state.removeListener (this);
    apvts.removeParameterListener (yrev::id::era, this);
    apvts.removeParameterListener (yrev::id::modType, this);
    for (const auto& l : yrev::kEraLimits)
        apvts.removeParameterListener (l.id, this);
}

void YrevProcessor::parameterChanged (const juce::String&, float)
{
    triggerAsyncUpdate (); // may arrive on any thread; the clean-up runs on the message thread
}

void YrevProcessor::handleAsyncUpdate ()
{
    handleEraChange ();
}

void YrevProcessor::handleEraChange ()
{
    swapEraIfNeeded ();
    enforceEraLimits ();
}

void YrevProcessor::enforceEraLimits ()
{
    using namespace yrev;
    const int eraIndex = juce::roundToInt (apvts.getRawParameterValue (id::era)->load ());

    // clamp the limited controls to the current era's range
    for (const auto& l : kEraLimits)
        if (auto* p = apvts.getParameter (l.id))
        {
            const float v = p->convertFrom0to1 (p->getValue ());
            const float lo = eraIndex == 0 ? l.min7 : l.min5, hi = eraIndex == 0 ? l.max7 : l.max5;
            const float c = juce::jlimit (lo, hi, v);
            if (std::abs (c - v) > 1.0e-4f)
                p->setValueNotifyingHost (p->convertTo0to1 (c));
        }

    // Reverb Flange's base delay stops at 30 ms (Q-07)
    if (juce::roundToInt (apvts.getRawParameterValue (id::modType)->load ()) == dsp::modtype::revFlange)
        if (auto* p = apvts.getParameter (id::modDelay);
            p != nullptr && p->convertFrom0to1 (p->getValue ()) > kRevFlangeMaxDelayMs)
            p->setValueNotifyingHost (p->convertTo0to1 (kRevFlangeMaxDelayMs));

    if (eraIndex != 0)
        return;

    auto setIndex = [this] (const char* pid, int index)
    {
        if (auto* p = apvts.getParameter (pid))
            p->setValueNotifyingHost (p->convertTo0to1 ((float) index));
    };
    if (yrev::isEra5OnlyErMode (
            juce::roundToInt (apvts.getRawParameterValue (id::erMode)->load ()))) // Pan A/B, instruments
        setIndex (id::erMode, yrev::erModeEra7 (juce::roundToInt (apvts.getRawParameterValue (id::erMode)->load ())));
    if (apvts.getRawParameterValue (id::fxType)->load () > 2.5f &&
        apvts.getRawParameterValue (id::fxType)->load () < 4.5f)
        setIndex (id::fxType, 0); // pitch, pan
    if (apvts.getRawParameterValue (id::deqOn)->load () > .5f)
        setIndex (id::deqOn, 0); // the digital EQ is era 5 only
    if (apvts.getRawParameterValue (id::eqView)->load () > .5f)
        setIndex (id::eqView, 0);
}

void YrevProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    chain.prepare (sampleRate, juce::jmax (samplesPerBlock, 1));
}

yrev::dsp::ChainParams YrevProcessor::readParams () const
{
    yrev::dsp::ChainParams p;
    p.era = juce::roundToInt (raw.era->load ());
    p.wetInput = juce::roundToInt (raw.wetInput->load ());
    p.inLevelDb = raw.inLevel->load ();
    p.outLevelDb = raw.outLevel->load ();
    p.mix = raw.mix->load ();
    p.wetSolo = raw.wetSolo->load () > .5f;
    p.bypass = raw.bypass->load () > .5f;
    p.mute = raw.mute->load () > .5f;
    p.outPhaseR = raw.outPhaseR->load () > .5f;
    p.width = raw.width->load ();
    p.duckOn = raw.duckOn->load () > .5f;
    p.duckRangeDb = raw.duckRange->load ();
    p.duckThreshDb = raw.duckThresh->load ();
    p.duckAttackMs = raw.duckAttack->load ();
    p.duckReleaseMs = raw.duckRelease->load ();
    p.aeqOn = raw.aeqOn->load () > .5f;
    p.aeqLoF = raw.aeqLoF->load ();
    p.aeqLoG = raw.aeqLoG->load ();
    p.aeqMidF = raw.aeqMidF->load ();
    p.aeqMidG = raw.aeqMidG->load ();
    p.aeqHiF = raw.aeqHiF->load ();
    p.aeqHiG = raw.aeqHiG->load ();
    p.deqOn = raw.deqOn->load () > .5f;
    p.deqLoShelf = raw.deqLoShelf->load () > .5f;
    p.deqHiShelf = raw.deqHiShelf->load () > .5f;
    for (int i = 0; i < 3; ++i)
    {
        p.deqF[i] = raw.deqF[i]->load ();
        p.deqG[i] = raw.deqG[i]->load ();
        p.deqQ[i] = raw.deqQ[i]->load ();
    }

    // reverb engine; the era limits are applied here too, so a host automating past them cannot exceed the era
    auto& e = p.engine;
    auto limited = [&p] (const char* pid, float v)
    {
        if (const auto* l = yrev::eraLimitFor (pid))
            return juce::jlimit (p.era == 0 ? l->min7 : l->min5, p.era == 0 ? l->max7 : l->max5, v);
        return v;
    };
    e.rt = limited (yrev::id::rt, raw.rt->load ());
    e.hiRatio = raw.hiRatio->load ();
    e.loRatio = raw.loRatio->load ();
    e.preDelayMs = limited (yrev::id::preDelay, raw.preDelay->load ());
    e.diffusion = raw.diffusion->load ();
    e.density = raw.density->load ();
    e.ref1DelayMs = limited (yrev::id::ref1Delay, raw.ref1Delay->load ());
    e.ref1Level = raw.ref1Level->load ();
    e.erRevBalance = raw.erRevBal->load ();
    e.wetLpfKHz = raw.wetLpf->load ();
    e.spaceMod = raw.spaceMod->load ();
    if (p.era == 1) // ER feedback and the secondary reverb are REV5 controls
    {
        e.erFbGain = raw.erFbGain->load ();
        e.erFbDelayMs = raw.erFbDelay->load ();
        e.erFbHi = raw.erFbHi->load ();
        e.rev2Time = raw.rev2Time->load ();
        e.rev2DelayMs = raw.rev2Delay->load ();
        e.rev2Level = raw.rev2Level->load ();
        e.ref1LDelayMs = raw.ref1LDelay->load (); // left and right 1st reflections (p. 14)
        e.ref1LLevel = raw.ref1LLevel->load ();
        e.ref1RDelayMs = raw.ref1RDelay->load ();
        e.ref1RLevel = raw.ref1RLevel->load ();
        // the gate: REV5 reverb-and-gate and combination programs (pp. 18, 36); the REV7 gates through E/R modes
        p.gate.levelPct = raw.gateLevel->load ();
        p.gate.trigDelayMs = raw.gateTrigDelay->load ();
        p.gate.holdMs = raw.gateHold->load ();
        p.gate.releaseMs = raw.gateRelease->load ();
        p.gate.onInput = raw.gateInput->load () >= 0.5f; // hidden: GATE LVL programs gate the input
    }
    e.erMode = yrev::erModeStandIn (juce::roundToInt (raw.erMode->load ())); // hidden instrument modes (Q-01)
    if (p.era == 0)
        e.erMode = yrev::erModeEra7 (e.erMode); // Pan A/B and the instruments are era 5
    e.liveness = raw.liveness->load ();
    e.loopScale = raw.voiceLoopSize->load (); // hidden per-preset voice
    e.diffuserScale = raw.voiceDiffSize->load ();
    e.loXoverHz = raw.voiceLoXover->load ();
    e.erGainDb = raw.voiceErGain->load ();
    e.hiXoverHz = raw.voiceHiXover->load ();
    e.lateOnsetMs = raw.voiceLateOnset->load ();
    e.erTiltDb = raw.voiceErTilt->load ();
    e.lateTiltDb = raw.voiceLateTilt->load ();
    e.rev2HiRatio = raw.voiceRev2Hi->load ();
    e.rev2GainDb = raw.voiceRev2Gain->load ();
    e.erType = raw.erType->load () >= 0.5f ? 1 : 0; // hidden: E/R2 programs (REV7 pass 25, REV5 pass 28)
    e.roomSize = limited (yrev::id::roomSize, raw.roomSize->load ());
    e.erCount = juce::roundToInt (limited (yrev::id::erCount, raw.erCount->load ()));
    p.character = raw.character->load ();
    p.dither = raw.dither->load () > .5f;
    p.eco = setEco.load ();
    p.frozenNoise = setFrozen.load ();
    p.limiter = setLimiter.load ();

    // FX slot; the era limits apply as for the engine, and pitch and pan are era 5 (enforceEraLimits picks Delay)
    auto& f = p.fx;
    f.era = p.era;
    f.hostBpm = hostBpm;
    f.steppedSweep = setFxStepped.load ();
    f.linearRead = setFxLinear.load ();
    f.triangleLfo = setFxTriangle.load ();
    f.tremoloDm = setFxTremDm.load ();
    f.type = juce::roundToInt (raw.fxType->load ());
    if (p.era == 0 && (f.type == yrev::dsp::fxtype::pitch || f.type == yrev::dsp::fxtype::pan))
        f.type = yrev::dsp::fxtype::delay;
    p.fxOrder = juce::roundToInt (raw.fxOrder->load ());
    p.revMix = raw.revMix->load () * 0.01f;
    p.fxWet = raw.fxWet->load () > .5f;
    f.link = raw.fxLink->load () > .5f;
    f.sync = raw.fxSync->load () > .5f;
    f.lpfKHz = raw.fxLpf->load ();
    f.dlyLMs = limited (yrev::id::dlyL, raw.dlyL->load ());
    f.dlyRMs = limited (yrev::id::dlyR, raw.dlyR->load ());
    f.dlyLevel = raw.dlyLevel->load ();
    f.dlyFb = raw.dlyFb->load ();
    f.dlyFbR = raw.dlyFbR->load ();
    f.dlyHigh = raw.dlyHigh->load ();
    f.echoLMs = limited (yrev::id::echoL, raw.echoL->load ());
    f.echoRMs = limited (yrev::id::echoR, raw.echoR->load ());
    f.echoFb = limited (yrev::id::echoFb, raw.echoFb->load ());
    f.echoFbR = limited (yrev::id::echoFbR, raw.echoFbR->load ());
    f.echoDamp = yrev::echoDampToHigh (raw.echoDamp->load ());
    f.echoLevel = raw.echoLevel->load ();
    f.modType = juce::roundToInt (raw.modType->load ());
    f.lfoRateHz = limited (yrev::id::lfoRate, raw.lfoRate->load ());
    f.modDepth = raw.modDepth->load ();
    f.dmDepth = raw.dmDepth->load ();
    f.amDepth = raw.amDepth->load ();
    f.modDelayMs = limited (yrev::id::modDelay, raw.modDelay->load ());
    if (f.modType == yrev::dsp::modtype::revFlange)
        f.modDelayMs = juce::jmin (f.modDelayMs, yrev::kRevFlangeMaxDelayMs);
    f.modFb = raw.modFb->load ();
    f.modRtS = raw.modRt->load ();
    f.modLpfKHz = raw.modLpf->load ();
    f.pitchShiftSt = raw.pitchShift->load ();
    f.pitchFineCents = raw.pitchFine->load ();
    f.pitchDelayMs = raw.pitchDelay->load ();
    f.pitchFb = raw.pitchFb->load ();
    f.pitchSel = juce::roundToInt (raw.pitchSel->load ());
    f.pitch2ShiftSt = raw.pitch2Shift->load ();
    f.pitch2FineCents = raw.pitch2Fine->load ();
    f.pitch2DelayMs = raw.pitch2Delay->load ();
    f.panDir = juce::roundToInt (raw.panDir->load ());
    f.panRateHz = raw.panRate->load ();
    f.panDepth = raw.panDepth->load ();
    return p;
}

double YrevProcessor::getTailLengthSeconds () const
{
    return yrev::dsp::Chain::tailSeconds (readParams ());
}

bool YrevProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet ();
    return out == juce::AudioChannelSet::stereo () && layouts.getMainInputChannelSet () == out;
}

void YrevProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    auto publishPeaks = [this] (const juce::AudioBuffer<float>& b, int first)
    {
        for (int ch = 0; ch < 2 && ch < b.getNumChannels (); ++ch)
        {
            const float peak = b.getMagnitude (ch, 0, b.getNumSamples ());
            auto& slot = meterPeak[(size_t) (first + ch)];
            float old = slot.load (std::memory_order_relaxed);
            while (peak > old && !slot.compare_exchange_weak (old, peak, std::memory_order_relaxed))
            {
            }
        }
    };
    publishPeaks (buffer, 0);
    for (int ch = getTotalNumInputChannels (); ch < getTotalNumOutputChannels (); ++ch)
        buffer.clear (ch, 0, buffer.getNumSamples ());

    if (auto* ph = getPlayHead ())
        if (const auto pos = ph->getPosition ())
            if (const auto bpm = pos->getBpm ())
                hostBpm = *bpm;

    // A preset load or state restore writes many parameters from another thread; read them only when no load is in
    // progress (seqlock) and otherwise keep the last complete settings, so the engine never runs half a preset.
    // With per-era memory an era change also waits (briefly) for the message thread to swap in that era's settings,
    // so the new engine starts with them; the wait is bounded in case no message thread runs (offline tools).
    const auto seqBefore = presets.loadSequence ();
    auto params = readParams ();
    const bool awaitingSwap = setPerEra.load () && params.era != settledEra.load () &&
                              heldSamples < (int64_t) (kMaxSwapWaitSeconds * getSampleRate ());
    heldSamples = awaitingSwap ? heldSamples + buffer.getNumSamples () : 0;
    if (!awaitingSwap && (seqBefore & 1u) == 0 && presets.loadSequence () == seqBefore)
    {
        params.program = seqBefore >> 1;
        heldParams = params;
        haveHeld = true;
    }
    else if (haveHeld)
    {
        params = heldParams;
    }

    if (buffer.getNumChannels () >= 2)
        chain.process (buffer.getWritePointer (0), buffer.getWritePointer (1), buffer.getNumSamples (), params);

    publishPeaks (buffer, 2);
}

juce::AudioProcessorEditor* YrevProcessor::createEditor ()
{
    return new YrevEditor (*this);
}

void YrevProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState ().createXml ())
        copyXmlToBinary (*xml, destData);
}

void YrevProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    presets.beginLoad (); // the audio thread holds its settings until the whole state is in, then crossfades
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType ()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
    snapSteppedParameters ();
    readSettings ();
    presets.restoreFromState ();
    lastEra = juce::roundToInt (apvts.getRawParameterValue (yrev::id::era)->load ());
    settledEra = lastEra; // the restored settings belong to the restored era: no swap
    enforceEraLimits ();
    presets.endLoad ();
}

void YrevProcessor::snapSteppedParameters ()
{
    // A host may leave a switch or choice at an in-between normalised value (pluginval sets 0.21 on a toggle).
    // replaceState skips parameters whose stepped value already matches the state, so that stray value would
    // survive a restore; put every stepped parameter on an exact step instead.
    for (auto* prm : getParameters ())
        if (auto* r = dynamic_cast<juce::RangedAudioParameter*> (prm))
            if (r->isDiscrete () || r->isBoolean ())
            {
                const float v = r->getValue ();
                const float snapped = r->convertTo0to1 (r->convertFrom0to1 (v));
                if (!juce::approximatelyEqual (v, snapped))
                    r->setValueNotifyingHost (snapped);
            }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter ()
{
    return new YrevProcessor ();
}
