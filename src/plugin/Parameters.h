#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

// Parameter IDs are part of the saved-state / automation contract. Never rename
// or renumber one after release; add new IDs instead.
// Names and ranges follow docs/design/parameters.md (global section).
namespace yrev::id
{
    inline constexpr auto era = "era"; // 0 = era 7 (REV7, 1985), 1 = era 5 (REV5, 1987)
    inline constexpr auto wetInput =
        "wet_input"; // 0 = mono (left input only, hardware MONO), 1 = stereo (L+R summed, hardware STEREO: the
                     // authentic default), 2 = true stereo (plugin only)
    inline constexpr auto inLevel = "in_level"; // dB
    inline constexpr auto mix = "mix";          // %
    inline constexpr auto outPhaseR = "out_phase_r";
    inline constexpr auto bypass = "bypass";
    inline constexpr auto character = "character"; // % (0 clean .. 100 authentic)
    inline constexpr auto dither = "dither";       // Era 5 only

    // Plugin-only wet-path tools ported from REV-246 (src/dsp/StereoWidth.h, Ducker.h)
    inline constexpr auto width = "width"; // 0 = mono, 100 = unchanged, 200 = extra wide
    inline constexpr auto wetSolo =
        "wet_solo"; // output 100 % wet, mix bypassed; a monitoring state, not stored in presets
    inline constexpr auto duckOn = "duck_on";
    inline constexpr auto duckRange = "duck_range";     // dB, attenuation while ducked
    inline constexpr auto duckThresh = "duck_thresh";   // dB, dry-input threshold
    inline constexpr auto duckAttack = "duck_attack";   // ms
    inline constexpr auto duckRelease = "duck_release"; // ms

    // ---- Panel controls (reverb, early reflections, FX slot): ranges follow docs/design/parameters.md ----
    // Ranges are the widest of the two eras; the era-7 limits (rt 10 s, room size 20, 18 reflections,
    // delay 900 ms, LFO 20 Hz) are applied by the engine and UI later.
    inline constexpr auto outLevel = "out_level";
    inline constexpr auto mute = "mute";

    inline constexpr auto rt = "rt";
    inline constexpr auto hiRatio = "hi_ratio";
    inline constexpr auto loRatio = "lo_ratio";
    inline constexpr auto preDelay = "pre_delay";
    inline constexpr auto diffusion = "diffusion";
    inline constexpr auto ref1Delay = "ref1_delay";
    inline constexpr auto ref1Level = "ref1_level";
    inline constexpr auto erRevBal = "er_rev_balance";
    inline constexpr auto density = "density";
    inline constexpr auto wetLpf = "wet_lpf"; // 1..16 kHz in kLpfStepsKHz, 17 = thru
    inline constexpr auto spaceMod = "space_mod";
    inline constexpr auto rev2Time = "rev2_time";
    inline constexpr auto rev2Delay = "rev2_delay";
    inline constexpr auto rev2Level = "rev2_level";
    inline constexpr auto gateLevel = "gate_level";
    inline constexpr auto gateTrigDelay = "gate_trig_delay";
    inline constexpr auto gateHold = "gate_hold";
    inline constexpr auto gateRelease = "gate_release";

    inline constexpr auto erMode = "er_mode"; // 15 choices (Pan A/B and the instrument modes are era 5)
    inline constexpr auto liveness = "liveness";
    inline constexpr auto roomSize = "room_size";
    inline constexpr auto erCount = "er_count";
    inline constexpr auto erFbGain = "er_fb_gain";
    inline constexpr auto erFbDelay = "er_fb_delay";
    inline constexpr auto erFbHi = "er_fb_hi";

    inline constexpr auto fxType = "fx_type";   // Delay, Echo, Mod, Pitch, Pan, Off
    inline constexpr auto fxOrder = "fx_order"; // FX then reverb, or reverb then FX
    inline constexpr auto fxLink = "fx_link";
    inline constexpr auto fxSync = "fx_sync";
    inline constexpr auto fxLpf = "fx_lpf"; // 1..16 kHz in kLpfStepsKHz, 17 = thru
    inline constexpr auto dlyL = "dly_l";
    inline constexpr auto dlyR = "dly_r";
    inline constexpr auto dlyLevel = "dly_level";
    inline constexpr auto dlyFb = "dly_fb";
    inline constexpr auto echoL = "echo_l";
    inline constexpr auto echoR = "echo_r";
    inline constexpr auto echoFb = "echo_fb";
    inline constexpr auto echoDamp = "echo_damp";
    inline constexpr auto echoLevel = "echo_level";
    inline constexpr auto modType = "mod_type";
    inline constexpr auto lfoRate = "lfo_rate";
    inline constexpr auto modDepth = "mod_depth";
    inline constexpr auto dmDepth = "dm_depth";
    inline constexpr auto amDepth = "am_depth";
    inline constexpr auto modDelay = "mod_delay";
    inline constexpr auto modFb = "mod_fb";
    inline constexpr auto modRt = "mod_rt";
    inline constexpr auto modLpf = "mod_lpf"; // 1..16 kHz in kLpfStepsKHz, 17 = thru
    // Digital EQ (era 5): ranges from the prototype's EQSETS (own design; see docs/design/parameters.md)
    inline constexpr auto eqView = "eq_view"; // 0 analogue, 1 digital (era 5)
    inline constexpr auto deqOn = "deq_on";
    inline constexpr auto deqLoF = "deq_lo_f";
    inline constexpr auto deqLoG = "deq_lo_g";
    inline constexpr auto deqLoQ = "deq_lo_q";
    inline constexpr auto deqLoShelf = "deq_lo_shelf";
    inline constexpr auto deqMidF = "deq_mid_f";
    inline constexpr auto deqMidG = "deq_mid_g";
    inline constexpr auto deqMidQ = "deq_mid_q";
    inline constexpr auto deqHiF = "deq_hi_f";
    inline constexpr auto deqHiG = "deq_hi_g";
    inline constexpr auto deqHiQ = "deq_hi_q";
    inline constexpr auto deqHiShelf = "deq_hi_shelf";
    inline constexpr auto pitchSel = "pitch_sel";     // Pitch A, B, C (era 5)
    inline constexpr auto pitchShift = "pitch_shift"; // semitones
    inline constexpr auto pitchFine = "pitch_fine";   // cents
    inline constexpr auto pitchDelay = "pitch_delay";
    inline constexpr auto pitchFb = "pitch_fb";
    inline constexpr auto panDir = "pan_dir"; // L to R, R to L, L <> R back and forth (era 5)
    inline constexpr auto panRate = "pan_rate";
    inline constexpr auto panDepth = "pan_depth";
    // Added after the first FX pass, from the REV5 owner's manual pp. 25-36: per-channel feedback and HIGH damping on
    // delay and echo (REV5), the second pitch voice of Pitch Change B and C.
    inline constexpr auto dlyFbR = "dly_fb_r";   // REV5 Rch F.B (dly_fb is Lch F.B)
    inline constexpr auto dlyHigh = "dly_high";  // REV5 delay HIGH (x0.1..1.0 high-frequency feedback)
    inline constexpr auto echoFbR = "echo_fb_r"; // REV5 Rch F.B (echo_fb is Lch F.B; one shared control on the REV7)
    inline constexpr auto pitch2Shift = "pitch2_shift"; // Pitch B: 2nd voice; Pitch C: right voice
    inline constexpr auto pitch2Fine = "pitch2_fine";
    inline constexpr auto pitch2Delay = "pitch2_delay";
    // REV5 left and right 1st reflections (owner's manual p. 14); ref1_delay / ref1_level are the centre one
    inline constexpr auto ref1LDelay = "ref1_l_delay";
    inline constexpr auto ref1LLevel = "ref1_l_level";
    inline constexpr auto ref1RDelay = "ref1_r_delay";
    inline constexpr auto ref1RLevel = "ref1_r_level";

    // Hidden per-preset voice (parameters.md "Hidden per-preset values"): not on the panel and not automatable; set by
    // presets so programs differ in more than their printed settings, as on the hardware. Defaults are the engine's
    // fixed values, so a preset that leaves them alone sounds as before. Ours throughout (inference).
    inline constexpr auto voiceLoopSize = "voice_loop_size"; // x late-network loop lengths
    inline constexpr auto voiceDiffSize = "voice_diff_size"; // x input-diffuser lengths
    inline constexpr auto voiceLoXover = "voice_lo_xover";   // Hz, the loops' low-shelf corner (lo_ratio band)
    inline constexpr auto voiceErGain = "voice_er_gain";     // dB on the early-reflection group
    // Hidden per-preset gate placement: 1 = the REV5 per-program noise gate (GATE LVL) on the program input, 0 = the
    // trigger gate on the effect output (Reverb & Gate, combinations). Set by the REV5 manual programs.
    inline constexpr auto gateInput = "gate_input";
    inline constexpr auto voiceHiXover = "voice_hi_xover"; // Hz, the loops' high-shelf corner (hi_ratio band)
    // Hidden per-preset REV7 E/R program type: >= 0.5 = E/R2 (the reflections through a decaying diffuser), else
    // E/R1. Era 7 only; set by the REV7 manual programs from the ROM chart's type column (twenty-fifth pass).
    inline constexpr auto erType = "er_type";
    // Hidden per-preset late onset: ms by which the late network hears its input after the early taps (twenty-sixth
    // pass; set on the REV5 hall and plate programs, fitted on the REV5 captures).
    inline constexpr auto voiceLateOnset = "voice_late_onset";
    // Hidden per-preset tilts: dB of high shelf (5 kHz) on the audible reflections and on the late tail (thirty-fifth
    // pass; set on CLA Drum Room, fitted on its capture).
    inline constexpr auto voiceErTilt = "voice_er_tilt";
    inline constexpr auto voiceLateTilt = "voice_late_tilt";
    // Hidden per-preset secondary reverb voice (REV5 99 Plate & Hall: the hall's own 2 HIGH and REV1/2 BAL):
    // its HIGH ratio (0 = follow hi_ratio) and a gain in dB on rev2_level (2026-11-22, manual audit).
    inline constexpr auto voiceRev2Hi = "voice_rev2_hi";
    inline constexpr auto voiceRev2Gain = "voice_rev2_gain";
    // FX routing (2026-11-22). The REV5 combination programs run the effect through the reverb AND around it (OM
    // block diagrams, printed pp. 37-39): FX > Rev = effect + REV MIX x reverb (effect); Rev > FX = effect (dry +
    // REV MIX x reverb). rev_mix is that REV MIX (0-100 %); fx_wet removes the bypass (fully wet, a plugin choice):
    // FX > Rev = reverb (effect) plus delay or echo repeats, Rev > FX = effect (reverb).
    inline constexpr auto revMix = "rev_mix";
    inline constexpr auto fxWet = "fx_wet";

    inline constexpr auto aeqOn = "aeq_on";
    inline constexpr auto aeqLoF = "aeq_lo_f";
    inline constexpr auto aeqLoG = "aeq_lo_g";
    inline constexpr auto aeqMidF = "aeq_mid_f";
    inline constexpr auto aeqMidG = "aeq_mid_g";
    inline constexpr auto aeqHiF = "aeq_hi_f";
    inline constexpr auto aeqHiG = "aeq_hi_g";
} // namespace yrev::id

namespace yrev
{
    // ER modes 8-11 (Piano, Organ, Brass, Guitar) are hidden in the UI until there is data for them (Q-01). The
    // choices stay in er_mode so saved states and host automation keep their indices; the processor plays a charted
    // stand-in instead and leaves the stored value alone. The stand-ins are the patterns the placeholders already
    // used: Small Hall, Large Hall, Plate, Spring.
    inline constexpr int kFirstHiddenErMode = 8, kLastHiddenErMode = 11;
    inline constexpr int erModeStandIn (int mode) noexcept
    {
        constexpr int standIn[] = {0, 1, 4, 5};
        return mode >= kFirstHiddenErMode && mode <= kLastHiddenErMode ? standIn[mode - kFirstHiddenErMode] : mode;
    }

    // Rev Hall, Rev Plate and Rev Room (12-14): the reverb programs' own early taps, in both eras (REV5: twenty-second
    // pass, Rev Room playing the sparse Rev Hall set; REV7: three per channel, twenty-third pass).
    inline constexpr int kRevHallErMode = 12, kRevPlateErMode = 13, kRevRoomErMode = 14;
    inline constexpr bool isEra5OnlyErMode (int mode) noexcept
    {
        return mode >= 6 && mode < kRevHallErMode;
    }
    // The panel shows the three as one REV E/R key: which set plays is a preset detail, as on the hardware, where the
    // reverb programs' reflections are fixed per program.
    inline constexpr bool isRevErMode (int mode) noexcept
    {
        return mode >= kRevHallErMode && mode <= kRevRoomErMode;
    }
    // The mode era 7 plays: its six charts and the Rev modes as they are, the era-5 modes (Pan A/B, instruments) as
    // Small Hall.
    inline constexpr int erModeEra7 (int mode) noexcept
    {
        return isEra5OnlyErMode (mode) ? 0 : mode;
    }

    // Per-era limits (docs/design/parameters.md). The parameter ranges are the widest of the two eras; the
    // UI narrows the knob travel and the processor clamps the stored value to these.
    struct EraLimit
    {
        const char* id;
        float min7, max7, min5, max5;
    };
    inline constexpr EraLimit kEraLimits[] = {
        {id::rt, 0.3f, 10.0f, 0.3f, 99.0f},
        {id::roomSize, 0.1f, 20.0f, 0.1f, 25.0f},
        {id::erCount, 1.0f, 18.0f, 1.0f, 34.0f},
        {id::dlyL, 0.1f, 900.0f, 0.1f, 2900.0f}, // REV5 manual p. 25: Lch/Rch DLY 0.1-2900 ms (Q-11)
        {id::dlyR, 0.1f, 900.0f, 0.1f, 2900.0f},
        {id::echoL, 0.1f, 450.0f, 0.1f, 1400.0f}, // REV7 p. 14: 0.1-450 ms; REV5 p. 26: 0.1-1400 ms
        {id::echoR, 0.1f, 450.0f, 0.1f, 1400.0f},
        {id::lfoRate, 0.1f, 20.0f, 0.1f, 40.0f},
        // the same in both eras: FxSlot clamps by type (flange 100, reverb flange 30, phasing 8 / era 5 5 ms; pass 32)
        {id::modDelay, 0.1f, 100.0f, 0.1f, 100.0f},
        {id::echoFb, 0.0f, 99.0f, -99.0f, 99.0f},
        {id::preDelay, 0.1f, 100.0f, 0.1f, 1000.0f}, // REV7 p. 12: 0.1-100 ms; REV5 p. 14: 0.1-1,000 ms
        {id::ref1Delay, 0.1f, 100.0f, 0.1f, 1000.0f},
        {id::echoFbR, 0.0f, 99.0f, -99.0f, 99.0f},
    };

    // Echo HIGH 0-10 (REV7 ROM chart "HIGH x0 ~ x10", printed p. 27, page image; Q-13) -> the treble ratio kept on
    // each repeat, value / 10: 10 keeps the treble, lower is darker. The factory Stereo Echo's 9 then matches the
    // REV5 twin's printed HIGH x0.9 (same 170/178 ms, 60 %). Was 1 - 0.09 x value ("higher = darker", inference),
    // which made the REV7 Stereo Echo very dark (manual audit, 2026-11-22). The REV5's echo HIGH is the ratio itself
    // (x0.1-1.0, pp. 25-26), so era 5 displays it directly; 0 is held at x0.1.
    inline float echoDampToHigh (float damp) noexcept
    {
        return juce::jmax (0.1f, 0.1f * juce::jlimit (0.0f, 10.0f, damp));
    }

    // Reverb Flange base delay: 0.1-30 ms on the REV7 ROM chart (preset 10, OM p. 28) against 0.1-100 ms for Flange
    // (Q-07, the chart taken as the more specific source). Era 5 already stops at 5 ms (kEraLimits).
    inline constexpr float kRevFlangeMaxDelayMs = 30.0f;

    // Reverb LPF steps, kHz. The REV5's LPF FRQ runs 1-16 kHz or THRU (OM), and its ROM chart prints 3.6, 6.3, 7.0,
    // 8.0, 9, 10 kHz and THRU (06-rev5-programs.md). The full step list is not printed; a rounded 1/6-octave series
    // through every printed value is inference (maintainer decision 2026-11-19). The last entry is THRU.
    inline constexpr float kLpfThruKHz = 17.0f;
    inline constexpr float kLpfStepsKHz[] = {1.0f, 1.1f, 1.25f, 1.4f,  1.6f,  1.8f,  2.0f,  2.2f,       2.5f,
                                             2.8f, 3.2f, 3.6f,  4.0f,  4.5f,  5.0f,  5.6f,  6.3f,       7.0f,
                                             8.0f, 9.0f, 10.0f, 11.0f, 12.5f, 14.0f, 16.0f, kLpfThruKHz};

    // Limits for a parameter in the given era (0 = era 7, 1 = era 5), or nullptr when it has none.
    const EraLimit* eraLimitFor (const juce::String& paramID);

    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout ();
} // namespace yrev
