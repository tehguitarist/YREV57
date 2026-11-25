# YREV57 user manual

YREV57 is a free, open-source reverb plugin (AU, VST3 and Standalone) inspired by the Yamaha REV7 (1985) and REV5 (1987). This page describes the panel, control by control. For an overview, installation and build instructions see the [README](../README.md).

> YREV57 is an independent product, inspired by the Yamaha REV7 and REV5. It is not affiliated with, endorsed by or sponsored by Yamaha Corporation. Yamaha, REV7 and REV5 are trademarks of Yamaha Corporation, used here only to describe the hardware that inspired this plug-in.

This is version 1.0: every feature is in and the voicing is tuned against reference recordings and by ear; a listening pass may still refine it in 1.x releases. Ranges and defaults below come from the parameter specification ([parameters.md](design/parameters.md)) and the built panel. The tooltips in the plugin are the quickest check of what each control does.

## Contents

1. [The panel](#the-panel)
2. [Working the controls](#working-the-controls)
3. [The era switch](#the-era-switch)
4. [Top bar and presets](#top-bar-and-presets)
5. [Input, wet input mode and meters](#input-wet-input-mode-and-meters)
6. [Reverb](#reverb)
7. [Early reflections](#early-reflections)
8. [FX slot](#fx-slot)
9. [EQ](#eq)
10. [Mix, output, width and character](#mix-output-width-and-character)
11. [Global keys](#global-keys)
12. [INT PARAM drawer](#int-param-drawer)
13. [Settings](#settings)
14. [Automation and host behaviour](#automation-and-host-behaviour)
15. [Legal](#legal)

## The panel

The plugin is one screen with no tabs. The window is resizable with a fixed 2.5:1 shape. The panel is laid out as:

- **Top bar:** the YREV57 name, the two era keys (7 and 5), the display panel (output meter, wet input legend and the LCD), the preset keys, Setup, **INT PARAM** and Bypass.
- **Input rail (left):** the input meter and the INPUT knob.
- **Reverb** (first column), **Early reflections** (second column) and the **FX slot** (third column).
- **EQ** (fourth column), with mix, output, width and character below it, and a row of global keys.

Controls that do not exist in the current era are not removed or moved. Their place shows a blank plate, so positions stay the same when you change era.

## Working the controls

- **Knobs:** drag to change a value; double-click to reset it to its default. The mouse wheel also works.
- **Value readouts:** click a value to type a number. An entry outside the control's range is not accepted.
- **Keys:** the square keys with a lamp switch a mode on or off, or choose one option from a group.
- **LCD:** the top-bar display shows the preset name on the first line and `parameter = value` for whatever you last touched on the second. An asterisk after the name means the settings have been edited and not stored.
- **Tooltips:** hover over a control for a short description. The **?** key beside Bypass turns tooltips on or off (saved with the plugin state).

## The era switch

The two keys at the top left, **7** and **5**, choose the era. The numbers are the hardware model numbers. Era 5 is the later machine (1987), and era 7 is the default.

| | Era 7 (REV7-like, 1985) | Era 5 (REV5-like, 1987) |
| --- | --- | --- |
| Native rate | 31.25 kHz | 44.1 kHz |
| Wet bandwidth | 12 kHz | 20 kHz |
| Dither | none | on (switchable) |
| Reverb time | 0.3 to 10 s | 0.3 to 99 s |
| Pre-delay and 1st reflection delay | up to 100 ms | up to 1,000 ms |
| Early reflections | up to 18 | up to 34, with E/R feedback |
| Secondary reverb | no | yes |
| Early-reflection modes | six plus REV E/R | six plus Pan A, Pan B, REV E/R and Piano |
| FX | delay, echo, mod | delay, echo, mod, pitch, pan |
| Delay time L and R | up to 900 ms | up to 2,900 ms |
| Echo time L and R | up to 450 ms | up to 1,400 ms |
| LFO rate | 0.1 to 20 Hz | 0.1 to 40 Hz |
| Analogue EQ | yes | yes |
| Digital EQ | no | yes |

The engine runs at the era's native rate inside a model of the hardware's converters (band limit, pre- and de-emphasis, 16-bit quantiser, hiss, and dither in era 5), so changing era changes the tone as well as the available controls.

When you switch era, the plugin crossfades the whole engine over about 30 ms, so a ringing tail is not cut off. By default each era remembers its own settings (see [Settings](#settings)). Going from era 5 to 7 turns the digital EQ off, returns the EQ view to analogue and moves era-5-only choices back (the Pan A and B modes to Small Hall, and the pitch and pan FX to Delay).

## Top bar and presets

The preset keys are **PREV**, **NEXT**, **PRESET** (opens the browser), **STORE**, **COMPARE**, **INIT**, **SETUP** (settings) and **INT PARAM**. **BYPASS** sits at the far right with the **?** key.

- **PREV / NEXT:** step through the presets in order, from one bank into the next.
- **PRESET:** opens the preset browser. It has five banks:
  - **FACTORY** and **COMBI:** our own generic presets (reverb alone, and reverb with an FX).
  - **REV7:** the 30 programs of the REV7 under the names and settings printed in its owner's manual.
  - **REV5:** the 39 programs of the REV5 (programs 1 to 30 and the combinations 91 to 99) the same way.
  - **USER:** your own presets, stored as XML files in your application-data folder under `YREV57/Presets`.
- In the browser, **Store as** saves the current settings as a new user preset, **Rename** and **Delete** act on a user preset, and **Import** and **Export** read and write a preset file. Factory and manual presets are read-only.
- **STORE:** saves the current settings as a preset.
- **COMPARE:** switches between the edited settings and the stored preset, to hear the difference.
- **INIT:** resets every control.
- A fresh instance shows INIT and does not load a preset until you choose one.

The presets from the manuals keep their printed names and settings; some controls of ours have no printed value, so those are our own choice. The details are in [presets.md](design/presets.md). Several names repeat across banks (for example there is a Large Hall in FACTORY, REV7 and REV5), so a preset is identified by its bank and name.

Presets store every control except bypass, mute, wet solo, input and output level, the wet input mode, wet R invert and the EQ view. The manual programs print nothing for the analogue EQ, mix, width, ducker, character and dither, so they load with those at their defaults (flat EQ, mix 50 %).

## Input, wet input mode and meters

- **INPUT:** the input level into the reverb, from minus infinity to +12 dB (0 dB is the dial's 8). The dry signal is not affected.
- **IN meter (left rail):** a stereo LED meter with a CLIP lamp. Click the CLIP lamps to clear the hold.
- **OUT meter (display panel):** the same, for the output. The IN and OUT clip lamps are cleared separately.
- **Wet input legend (display panel):** three words, **MONO**, **STEREO** and **TRUE STEREO**, one lit. Click to choose:
  - **MONO:** only the left input feeds the reverb.
  - **STEREO:** left and right are summed to one mono input and halved, so a centred signal keeps its level. This is what the hardware did, and it is the default.
  - **TRUE STEREO:** plugin only. The early reflections lean to the side the input comes from; a centred input sounds the same as STEREO.

The dry signal always stays stereo.

## Reverb

A grid of nine knobs. Ranges are the widest of the two eras; the knob travel narrows to the current era.

| Control | Range | What it does |
| --- | --- | --- |
| **REVERB TIME** | 0.3 to 10 s (era 5: to 99 s) | Decay time at 1 kHz |
| **HIGH RATIO** | x0.1 to 1.0 | Treble decay compared with the mid. Lower is darker |
| **LOW RATIO** | x0.1 to 2.4 | Bass decay compared with the mid |
| **PRE-DELAY** | 0.1 to 100 ms (era 5: 1,000 ms) | Delay before the reverb, and before the effect when one is on |
| **1ST REF DELAY** | 0.1 to 100 ms (era 5: 1,000 ms) | Delay of the centre 1st reflection |
| **1ST REF LEVEL** | 0 to 100 % | Level of the centre 1st reflection |
| **DIFFUSION** | 0 to 10 | How quickly the reverb thickens |
| **ER / REVERB** | 0 to 100 % | Balance of early reflections and reverb. 100 % is early reflections only, 0 % is reverb only |
| **REVERB LPF** | 1 to 16 kHz, or THRU | Low-pass on the reverb. THRU is off |

## Early reflections

A row of **mode keys** and three knobs.

- **Modes:** Small Hall, Large Hall, Random, Reverse, Plate and Spring. Era 5 adds **Pan A** and **Pan B**, where the reflections sweep across the stereo field (Pan A one way, Pan B across and back) and LIVENESS sets the direction, Both eras have **REV E/R**, the reverb program's own early reflections, the way the hardware's reverb programs play them: in era 7 the REV7's three per channel, in era 5 the REV5's multitap (denser at DENSITY 4). Each preset carries its own set (hall, plate or room), so the key just turns them on; from another mode it picks the hall set. ROOM SIZE, LIVENESS and E/R NUMBER do not apply; the reflections start at the pre-delay. Era 5 also has **PIANO** beside REV E/R, the REV5's piano reflection pattern (the *Rehearsal Room* program uses it); ROOM SIZE scales it.
- **ROOM SIZE:** 0.1 to 20 (era 5: 0.1 to 25). Spreads the reflection times.
- **LIVENESS:** 0 to 10. How reflective the room is.
- **E/R NUMBER:** 1 to 18 (era 5: 1 to 34). The number of reflections.
- **E/R FEEDBACK** (era 5, in the INT PARAM drawer): gain (plus or minus 99 %, negative inverts each repeat), delay (0.1 to 1,400 ms) and treble damping (x0.1 to 1.0).

For an early-reflection-only sound, set ER / REVERB to 100 %.

## FX slot

The third column holds one optional effect, which runs together with the reverb. The row of keys at the top chooses it:

**DELAY**, **ECHO**, **MOD**, **PITCH** (era 5), **PAN** (era 5) or **OFF**.

The **FX -> REV** and **REV -> FX** keys set the order. As on the hardware's combination programs, the effect is also heard directly beside the reverb: with FX -> REV you hear the effect plus the reverb of it, and with REV -> FX the dry signal and the reverb are fed into the effect together. **REV MIX** (in the INT PARAM drawer) sets how much reverb joins. Effect-only presets set it to 0 %, so they play the effect alone.

**FX WET** (plugin only, off by default) removes that direct path, so the wet signal is fully processed. FX -> REV then plays only the reverb of the effect (delay and echo keep their repeats), and REV -> FX plays only the effect of the reverb. With it off, the plugin works like the hardware.

The **PRE-DELAY** also delays the effect from the direct sound, as on the hardware. Some types add keys: **LINK L/R** and **TEMPO SYNC** (delay and echo) and **LFO SYNC** (mod).

| Type | Controls |
| --- | --- |
| **Delay** | DELAY L and R (0.1 to 900 ms; era 5: to 2,900 ms), LEVEL. Era 5 adds FEEDBACK L and R (plus or minus 99 %), HIGH (treble feedback) and FX LPF |
| **Echo** | ECHO L and R (0.1 to 450 ms; era 5: to 1,400 ms), FEEDBACK (0 to 99 %; era 5: plus or minus 99 % per side), HIGH (0 to 10: how much treble each repeat keeps; lower is darker; era 5 shows it as x0.1 to 1.0), LEVEL |
| **Mod** | Type keys: FLANGE, REV FLANGE, CHORUS A, CHORUS B, PHASING, TREMOLO, SYMPHONIC. Knobs: LFO RATE, DEPTH, DM DEPTH (delay modulation), AM DEPTH (amplitude modulation), BASE DELAY, FEEDBACK, FX LPF. Only the knobs that apply to the chosen type are lit. REV FLANGE is a flange at a fixed 30 % feedback; the reverb gives it its decay, so its REVERB TIME knob is unlit |
| **Pitch** (era 5) | PITCH A (one voice, with feedback), B (two voices) or C (two voices, one per side). SHIFT plus or minus 12 semitones, FINE plus or minus 100 cents, DELAY 0.1 to 400 ms (C: 200 ms), FEEDBACK 0 to 99 % (A only); a second row of SHIFT, FINE and DELAY for the second voice |
| **Pan** (era 5) | Direction keys (L to R, R to L, L to R and back), RATE 0.1 to 40 Hz, DEPTH 0 to 100 % |

The base delay range follows the mod type: flange 0.1 to 100 ms, reverb flange 0.1 to 30 ms, and phasing 0.1 to 8 ms (era 5: 0.1 to 5 ms). The knob reads to 100 ms; a longer setting than the type allows plays at its limit.

## EQ

The fourth column has four keys, **ANALOG**, **ON**, **DIGITAL**, **ON**, above a graph with three nodes and the EQ knobs.

- **ANALOG** and **DIGITAL** choose which EQ the graph and knobs show. Each **ON** key switches its own EQ and always shows its state.
- **Analogue EQ** (both eras, wet path only, before the converter, off by default): LOW frequency 50 to 700 Hz, MID frequency 350 Hz to 5 kHz, HIGH frequency 2 to 20 kHz, each with a gain of plus or minus 15 dB.
- **Digital EQ** (era 5 only, a plugin-side design; the printed REV5 programs all have it off and flat): LOW 32 Hz to 2.2 kHz, MID 250 Hz to 5.6 kHz and HIGH 500 Hz to 16 kHz, each with gain of plus or minus 15 dB and a Q of 0.1 to 5. **PEAK / SHELF** keys on the low and high bands choose the band shape.
- Drag a node in the graph to set a band's frequency and gain.

## Mix, output, width and character

- **MIX:** the dry/wet balance, from all dry to all wet. The default is 50 %.
- **OUTPUT:** the master level, from minus infinity to +12 dB.
- **WIDTH:** the stereo width of the wet signal, 0 to 200 %. 100 % leaves it unchanged and 0 % sums it to mono.
- **CHARACTER:** 0 to 100 %. 0 is a clean path (24-bit word length, no hiss, no dither) and 100 % is the full converter model (16-bit truncation, hiss and, in era 5, dither). The era's band limit and emphasis stay at every setting.

## Global keys

- **DITHER** (era 5): dither at the A/D input.
- **WET R INVERT:** inverts the polarity of the right wet channel.
- **WET SOLO:** output is 100 % wet and the mix control is ignored. This is a monitoring state and is not stored in presets.
- **MUTE:** silences the output.
- **BYPASS** (top bar): fades to the untouched dry signal.
- **DUCK ON:** see the ducker below.

## INT PARAM drawer

The REV5 kept its fine controls behind an INT PARAM key, and the plugin does the same. The **INT PARAM** key in the top bar slides a drawer over the middle panels; click outside it, press Esc or press the key again to close it. An amber dot on the key means one of its controls is away from its default and active in the current era, so a hidden control never changes the sound without a sign on the front panel. The drawer holds:

- **Tail:** DENSITY (1 to 4), SPACE MOD (0 to 10, era 5; the default is 0), the secondary reverb (TIME x0.1 to 10 of the main, DELAY 0 to 500 ms, LEVEL 0 to 100 %; era 5).
- **1st reflections, left and right** (era 5): delay and level for each side.
- **E/R feedback** (era 5): gain, delay and treble damping.
- **Gate** (era 5): LEVEL (the input level that opens it; 0 is off), TRIGGER DELAY (0.1 to 1,000 ms), HOLD (1 to 24,000 ms) and RELEASE (3 to 24,000 ms).
- **FX routing:** REV MIX (0 to 100 %, default 100 %): how much reverb joins the effect when an FX is on and FX WET is off.
- **Ducker** (plugin only): RANGE (0 to minus 60 dB), THRESHOLD (minus 80 to 0 dB), ATTACK (0 to 1,000 ms) and RELEASE (0 to 1,000 ms). The ducker lowers the reverb while the dry input is over the threshold, so the reverb comes up in the gaps. The **DUCK ON** key is on the front panel; the defaults are off, minus 10 dB, minus 20 dB, 10 ms and 200 ms.

## Settings

The **SETUP** key opens the settings dialog. These are plugin settings, saved with the plugin state, not automatable parameters.

| Setting | Options |
| --- | --- |
| **Quality** | **Native rate** runs the engine at the era's own sample rate (31.25 kHz or 44.1 kHz) and resamples at the edges. **Eco (host rate)** runs at the host rate without the resampling. Native rate costs about 1.3 to 1.8 % of one Apple M1 Pro core at 48 or 96 kHz (block 256); Eco about 0.9 % at 48 kHz and 1.8 % at 96 kHz |
| **Wet input default** | Mono, Stereo or True stereo, for new instances |
| **Latency** | A read-only display of the latency the plugin reports |
| **Noise seed** | **Random** gives different hiss and dither per instance; **Frozen** is repeatable |
| **Era settings** | **Per era:** each era keeps its own settings, and switching era restores them. **Shared:** settings carry over between eras |
| **Output safety limiter** | On by default. It stops the output going above the ceiling (runaway FX feedback, a wild EQ setting). It is not a mastering limiter |
| **FX period A/B** | Four toggles that try period-style choices in the FX slot, for comparison by ear: **Stepped** (modulation sweeps on 8-bit steps), **Linear** (linear delay reads), **Triangle** (triangle LFO) and **Trem. DM** (tremolo adds a small delay sweep). Trem. DM is on by default, the other three off |
| **UI scale** | 75, 100, 150, 200 or 250 % |

The settings dialog also shows the about box with the version, the licence and the disclaimer below.

## Automation and host behaviour

- Every continuous control on the panel is a host-automatable parameter. The hidden per-preset voice parameters that give the manual programs their identity (loop and diffuser sizes, crossovers, reflection gain and tilt, late onset, late tilt, the E/R type and the gate placement) are saved with presets but are not on the panel and are not automatable.
- The reverb tail is reported to the host (up to 99 s in era 5).
- The plugin reports its converter delay (about 2–3 ms) as latency and delays the dry to match, so the host keeps everything aligned and pre-delays, reflections and FX delays play as printed. Live monitoring through it adds that much delay.
- The plugin handles sample-rate changes and any block size, and the presets are also exposed to the host as programs. A fresh instance reports no current program until you choose a preset (it opens on INIT).
- Validated with Apple's `auval` (AU) and with pluginval at strictness 10 (AU and VST3).

## Legal

YREV57 is an independent product, inspired by the Yamaha REV7 and REV5. It is not affiliated with, endorsed by or sponsored by Yamaha Corporation. Yamaha, REV7 and REV5 are trademarks of Yamaha Corporation, used here only to describe the hardware that inspired this plug-in. SPX and REV1 are likewise trademarks of Yamaha Corporation, named only in the research notes. No firmware, ROM dump or SysEx data from the units is used; the REV7 and REV5 presets use only settings printed in the owners' manuals. See [legal.md](legal.md).

YREV57 is free software under the [GNU AGPLv3](../LICENSE).
