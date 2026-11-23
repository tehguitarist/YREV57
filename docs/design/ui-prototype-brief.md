# UI prototype brief: control types, screens and a prompt template

Companion to [ui-controls.md](ui-controls.md) (the full inventory). This file says **which widget to use for each control**, **how many screens to have**, and gives a **prompt template** for a UI prototyping tool. Recommendations are design judgement, not sourced facts.

## 1. Principles

1. **Hardware had one knob per job plus a tiny LCD; a plugin has a big screen.** Show values on the control itself (and in one shared readout), never only on a hidden page.
2. **Every control has a value readout.** A knob, a drag, a scroll or a keypad entry all update the same number, so "up/down buttons vs knob" is not either/or: the knob drives it, a text field shows it, double-click types it.
3. **Match the widget to the parameter's shape:**

| Parameter shape | Widget | Examples |
| --- | --- | --- |
| On/off (2 states, no in-between) | **Toggle button with lamp** | EQ on, Bypass, Mute, Out phase R, Dither, per-section ON |
| 2 states that name two *things* (not on/off) | **Segmented switch** | Wet input Mono/Stereo, Era 7/5 |
| 3–8 named choices, one at a time | **Segmented buttons** (≤4) or **button grid** (5–8) | E/R mode 1–8, REV mode 1–8, MOD type, HPF/LPF/crossover selectors |
| 9+ named choices / big lists | **Dropdown or browser** | Presets, memories 1–99 |
| Continuous, used while listening | **Rotary knob** | Mix, input level, RT, ratios, levels, EQ freq/gain |
| Continuous, needs precision or wide range | **Knob + numeric field** (drag/scroll/double-click) | Delay times (0.1–1,400 ms), pre-delay, LFO rate |
| Stepped number with few steps | **Knob with detents** or **stepper** | Diffusion 0–10, Liveness 0–10, Density 1–4, ER count |
| Two related values | **XY pad or paired knobs** | EQ freq + gain (use the curve, see below) |
| Shape of something | **Graph you can drag** | EQ curve, RT-vs-frequency curve, ER tap pattern |
| Level you watch | **Meter** (not interactive) | Input/output meters |
| Action (does something once) | **Push button** | Store, Recall, Init, Compare, Clear |

4. **Knob vs push-button rule of thumb:** if you would *hold or sweep* it, it is a knob; if you would *click once and leave it*, it is a button. Anything with up/down arrows on the hardware (because the hardware had no room for a knob) becomes a knob or slider.
5. **Visible state beats menus.** Prefer segmented buttons over dropdowns when there are ≤ 8 options.
6. **Keep the hardware vocabulary** (Room Size, Liveness, Hi/Lo ratio, Mixing) so users of the originals feel at home, but do not copy the hardware's *navigation* (PARAMETER, INT PARAM, UTILITY page-diving).

## 2. Recommended widget for each control

| Control | Hardware | Recommended plugin widget | Notes |
| --- | --- | --- | --- |
| Input level | Knob | **Knob** + dB readout + meter beside it | Show dial 0–10 and dB |
| Wet input Mono/Stereo | Switch | **Segmented switch** | |
| Mixing Direct↔Rev | Knob | **Knob** (big, prominent) | Label ends "DRY / WET" |
| Bypass | Key + LED | **Toggle button** | Always visible |
| Mute | Key + LED | **Toggle button** (small) | Optional |
| Out phase R | Parameter | **Toggle** in a settings/advanced strip | |
| Era | none | **Segmented switch**, top bar, prominent | Changes ranges and tints the UI |
| Character | none | **Knob** | |
| Dither | switch | **Toggle** in settings, shown only in Era 5 | |
| Meters | LEDs | **Meter** (vertical or horizontal) | Clip latch, click to reset |
| Memory/program | Keypad + up/down | **Preset bar**: ◀ name ▶ plus dropdown and number entry | Keep up/down arrows here; they suit presets |
| Store / Recall / Name | Keys | **Push buttons** in the preset bar | |
| Effect type (REV, E/R, DELAY, ECHO, MOD…) | Keys | **Segmented tabs** | Drives which page is shown |
| E/R mode (6–8) | Keypad / buttons | **Button grid** with tiny pattern icons | |
| REV mode (R1: 8) | Buttons | **Button grid** or dropdown | |
| Reverb time | Keypad value | **Large knob** + numeric field | Log/skewed taper |
| Hi / Lo ratio | Knob / keys | **Knobs** (or a draggable RT-vs-frequency curve) | See graph note below |
| R1 crossover 4K/2K/1K, 500/250/125 | Buttons | **Segmented 3-way**, ×2 | Only if we offer 4-band RT |
| Pre-delay / 1st ref delay | Keys | **Knobs with numeric field** | |
| 1st ref level | Keys | **Knob** | |
| Diffusion | Keys | **Knob with 0–10 steps** | |
| Room size | Keys / R1 knob | **Knob** (drag), numeric readout updates live | The user's example: knob, with the display reflecting it |
| E/R number | Keys / R1 knob | **Knob with stepped detents** | |
| Liveness | Keys / R1 knob | **Knob** | |
| Density 1–4 | Keys | **4-way segmented** | |
| Delay L / R | Keys | **Two knobs + numeric fields**, optional link toggle, tempo-sync toggle | |
| Feedback | Keys | **Knob** (bipolar for ±99 %) | |
| Echo High Damp | Keys | **Knob** | |
| LFO rate | Keys | **Knob + Hz field**, optional sync | |
| Mod depth / DM / AM | Keys | **Knobs** | |
| Mod type | Keys | **Segmented tabs** | |
| Analogue EQ on/off | Switch | **Toggle button** | |
| Analogue EQ freq and gain (×3) | 6 knobs | **Interactive EQ curve** with 3 draggable nodes, plus 6 small knobs underneath | Curve for feel, knobs for exact values |
| Digital EQ (Era 5) | Keys | **Same curve** with extra Q and shelf/peak toggles per band | One EQ widget, two modes |
| HPF / LPF selectors (R1) | Buttons | **Knob or dropdown** with "Off" | Plugin can use continuous or the 4 steps |
| Direct / E/R / Reverb / Master level + ON (R1) | Knobs + keys | **Four vertical faders** with mute/ON buttons | A "Mixer" panel |
| Gate threshold, hold, release | Keys | **Small knobs** or a draggable gate-envelope graphic | |
| Combination routing | Display | **Read-only block diagram**, order shown | |
| Pitch shift / fine | Keys | **Knobs** | |
| Pan direction | Keys | **3-way segmented** | |
| MIDI/footswitch/tables | Utility | **Settings dialog** | Hidden by default |
| Quality mode (native vs eco) | none | **Dropdown or toggle** in settings | |
| Knob "follow preset" (R1 AUTO) | AUTO buttons | **Ghost marker on knob**, no separate button | Optional; see ui-controls.md §19 |
| Show-the-value display | LCD | **One shared readout bar**, plus value on each control | Shows name and value of whatever is being touched |

## 3. How many screens?

**Recommendation: one main window with three zones, plus two optional drawers. No deep menus.**

```
┌───────────────────────────────────────────────────────────────┐
│ TOP BAR: preset ◀ name ▶ · store · era · in-level · bypass │
├───────────────┬───────────────────────────────┬───────────────┤
│ LEFT          │ CENTRE (tabbed by effect type)│ RIGHT         │
│ meters,       │ Reverb | Early Refl | Delay   │ EQ (curve),   │
│ input,        │ | Echo | Mod | (Pitch/Pan)    │ mix, output   │
│ mono/stereo   │ the 5–8 controls that matter  │               │
├───────────────┴───────────────────────────────┴───────────────┤
│ READOUT BAR: name = value of the control being touched        │
│ [ Advanced ▾ ]  density, LPF, space mod, secondary reverb,    │
│                 gate, out phase, dither, character            │
└───────────────────────────────────────────────────────────────┘
        Settings (cog): MIDI, quality mode, UI scale, about/legal
```

Why this works:
- **Hardware needed pages because it had one LCD. A plugin does not.** The five effect types each have ≤ 7 core parameters (REV7 rule), so a tab shows all of them at once.
- **Always visible:** preset bar, era, mix, input level, bypass, meters, EQ. These are touched constantly.
- **Tabbed by effect type** so unused controls are not on screen (a delay patch does not need Hi/Lo ratio).
- **"Advanced" drawer** hides the INT PARAM layer, so beginners get a clean surface and experts get the R5's depth.
- **Settings** hold anything set once.
- Optional second window or resize states: **Compact** (mix, RT, size, EQ on) and **Full**. Offering both helps with small hosts.
- **Era changes what the UI shows** (Era 5 reveals digital EQ, secondary reverb and longer ranges; Era 7 hides them). Do not show greyed-out controls if you can hide them.

Alternative to consider: a **"hardware" skin** that mimics the REV5 panel as a literal layout. Easy to recognise but cramped; consider it as a skin option over the same controls, not the default.

## 4. Do the knob values reflect in the display?

Yes: every control writes to the same parameter, and the readout bar and the control's own label show the value. That also means:
- Drag a knob, scroll over it, or double-click to type. All three update the readout.
- Arrow/up-down buttons are only for **discrete lists** (presets, modes).
- Show units (s, ms, Hz, dB, %) and sensible rounding (RT to 0.1 s, delay to 0.1 ms).

## 5. Prompt template for a UI prototyping tool

> **Superseded by [ui-prompt-v2.md](ui-prompt-v2.md)** (2.5:1 chassis, one prompt per screen state, based on the approved concept images). The text below is the first-round prompt, kept for reference.

**Attach with the prompt:** (1) the hardware reference photos (REV5 front panel, REV1 remote RCR-1, REV7 panel and any others), (2) optional moodboard images for the look and feel, (3) `ui-controls.md` and this file if the tool accepts documents. The prompt below is self-contained: it lists every control itself, so it still works if the documents are not read. Edit the bracketed parts.

```text
PROJECT
Design the user interface for YREV57, a free, open-source reverb plugin
(AU / VST3 / Standalone, resizable) inspired by two mid-1980s Yamaha digital
reverbs. It has an Era switch with two settings, 7 and 5 (named after the model numbers of two real units: 7 is the 1985 unit and the default; 5 is the later, 1987 unit). Label the switch "7 | 5" with the years as small captions, and do not show the Yamaha name or logo. I want a clean,
calm, modern interface that is easy to use, while still exposing every
control the originals had.

REFERENCE IMAGES (attached)
- Image set A, hardware references: [REV5 front panel], [REV1 remote
  RCR-1], [REV7 front panel], [others]. Use these ONLY to learn which
  controls exist, their ranges and the vocabulary hardware users expect.
  Do NOT copy their layout, button grids, typography, logos, names or
  artwork. Do not use the Yamaha name or logo anywhere in the design.
- Image set B, mood and style: [moodboard images]. Use these for colour,
  material, typography and overall feel.
- If a control in image set A is unclear, state your assumption instead of
  guessing silently.

GOAL
A first-time user gets a good sound in under 10 seconds using at most
4 controls. An expert finds every deeper control within one click. No
page-diving menus. Readable at 100% and 200% scale. Works at 900 x 560
(default), a compact size, and a large size.

LAYOUT (one main window, three zones)
1. TOP BAR: preset name with prev/next arrows and a dropdown; Store
   (save), Compare/revert, Init; Era switch (7 | 5); Input Level
   knob; Bypass; Settings cog.
2. LEFT: input level meter(s) with clip latch (click to reset);
   Mono | Stereo wet-input switch.
3. CENTRE: tabs by effect type: Reverb | Early Reflections | Delay |
   Echo | Modulation | (Pitch/Pan and Gate/Combination under "More").
   Each tab shows only its own 5-8 controls.
4. RIGHT: 3-band EQ with an interactive curve (draggable nodes) plus
   small knobs for exact values and an EQ On toggle; large Mix knob;
   output level.
5. BOTTOM: one shared readout bar ("Name = Value" for whatever control is
   being touched, replacing the old 2 x 16 LCD); an "Advanced" drawer; a
   status area (era, mono/stereo, latency).
Hide controls that don't apply to the current era or effect type; do not
show them greyed out.

CONTROL-TYPE RULES
- On/off = toggle button with a lamp.
- Two named options = segmented switch.
- 3-8 options = segmented buttons or a button grid. 9+ = dropdown.
- Anything swept while listening = rotary knob with value text under it;
  drag, scroll, or double-click to type an exact value.
- Stepped values (0-10, 1-4) = knob with detents or a stepper.
- Actions (store, recall, init) = push buttons.
- Up/down arrow buttons ONLY for presets and discrete lists. Where the
  hardware used up/down keys for a number, use a knob and keep the number
  visible.
- Every control shows its unit (s, ms, Hz, dB, %) and updates the shared
  readout bar when touched.
- Draggable graphs where it helps: EQ curve, reverb time vs frequency,
  early-reflection pattern, gate envelope, LFO shape.

COMPLETE CONTROL LIST (place every one; tell me if you merge or drop any)

GLOBAL: Input level (dial 0-10, 0 dB at 8, about +12 dB at 10); Wet input
Mono|Stereo; Mix Direct<->Rev; Bypass; Mute; Out Phase R (invert wet
right); Effect output level; Era 7|5; Character 0-100% (clean to
authentic converter sound); Dither on/off (Era 5 only).

METERS / INDICATORS: input level meter (CLIP, 0, -3, -6, -10, -15, -20,
-30 dB) with clip latch; optional stereo L/R meter with 16 segments (FULL
down to -60 dB) and an IN/OUT metering-point switch; MONO lamp; EQ ON lamp;
program number display; unsaved-edit marker; key/active lamps.

DISPLAY: program name; current parameter and value; one readout for each of
Reverb Time (sec), Delay 1 (ms), Delay 2 (ms); optional large display
with selectable views (waveform/pattern, frequency characteristic, reverb
time, ratio).

PRESETS: recall by number, name and list; up/down; direct-recall slots;
store, store-as, rename, copy, init, compare, revert; factory bank (1-30,
read-only), user bank (31-90), combination bank (91-99); search,
categories, favourites, import/export.

EFFECT TYPES: Reverb, Early Reflections (E/R), Delay, Echo, Modulation,
Pitch (A/B/C), Pan, Combination (reverb + effect + gate, fixed order).

REVERB: Reverb Time 0.3-10 s (Era 7) / 0.3-99 s (Era 5); High Ratio
0.1-1.0; Low Ratio 0.1-2.4; Initial (pre) Delay 0.1-100 ms; 1st
Reflection Delay 0.1-100 ms; 1st Reflection Level 0-100%; Diffusion 0-10;
Reverb mode selector (8 slots). Optional four-band reverb time: High,
Mid-High, Mid-Low (the reference time), Low, with crossover selectors 4k/2k/1k
and 500/250/125 Hz.

EARLY REFLECTIONS: Mode (Small Hall, Large Hall, Random, Reverse, Plate,
Spring, plus Pan A/B and Rehearsal-room patterns in Era 5); Room Size
0.1-20 (Era 7) / 0.1-25 (Era 5); Liveness 0-10; Number of reflections
1-34; Diffusion; Initial/1st-ref delay and level; ER/Reverb balance
0-100%; feedback (gain, delay, high ratio); Space Modulation 0-10.

DELAY: Delay L and R 0.1-900 ms (Era 7) / 0.1-1,400 ms (Era 5); level;
feedback -99..+99% (Era 5); low-pass; tempo-sync option.

ECHO: Echo L and R 0.1-450 ms; Feedback 0-99%; High Damp 0.1-1.0.

MODULATION: type (Flange, Reverb Flange, Chorus A, Chorus B, Phasing,
Tremolo, Symphonic); LFO rate 0.1-20 Hz (Era 7) / 0.1-40 Hz (Era 5); Depth
0-100%; Delay-Mod and Amp-Mod depth 0-100%; flange delay 0.1-100 ms (Era 5:
0.1-5 ms); flange feedback 0-99%; phasing delay 0.1-8 ms; reverb-flange
time 0.3-10 s; LPF 1-16 kHz/thru.

PITCH AND PAN (low priority): shift -12..+12 semitones; fine -100..+100
cents; delay 0.1-400 ms; feedback 0-99%; pan direction L>R, R>L, L<>R (back and forth);
pan rate 0.1-40 Hz; pan depth 0-100%.

GATE / COMBINATION: gate level, trigger delay 0.1-1,000 ms, hold 1-24,000
ms, release 3-24,000 ms; MIDI trigger on/off; routing display
(e.g. "Symphonic > Reverb > Gate").

EQ (analogue, wet only, not stored): On/Off; Low freq 50-700 Hz and gain
+/-15 dB; Mid freq 350 Hz-7 kHz and gain +/-15 dB; High freq 2-20 kHz and
gain +/-15 dB.
EQ (digital, Era 5, stored per preset): Low freq 32 Hz-2.2 kHz, gain,
peak/shelf, Q 0.1-5; Mid 250 Hz-5.6 kHz, gain, Q; High 500 Hz-16 kHz,
gain, peak/shelf, Q.

FILTERS: high-pass 50/100/200/400 Hz or off; low-pass 4k/6k/8k/10k or off
(the Era 5 low-pass goes 1-16 kHz/thru).

MIXER (optional panel): four faders with on/off - Direct, Early
Reflection, Reverberation, Master (-inf, -60 ... 0 dB).

ADVANCED DRAWER: density 1-4; low-pass; space modulation 0-10; secondary
reverb time (x0.1-10), delay (0-500 ms), level (0-100%); ER/reverb
balance; gate; out-phase; dither; character.

SETTINGS DIALOG: quality mode (native-rate vs eco), wet input default,
latency readout, MIDI program change / channel / tables (optional), UI
scale, tooltips on/off, theme, About (version, licence AGPLv3, trademark
disclaimer: "Independent project inspired by classic 1980s digital
reverbs; not affiliated with Yamaha Corporation").

"FOLLOW PRESET" BEHAVIOUR (optional): when a preset loads, each knob shows
the stored value; moving it overrides it; a ghost marker shows the stored
value. (This replaces the old AUTO / indicator-strip system.)

STYLE
[Describe: e.g. dark graphite with one warm accent, subtle vintage
character, flat controls, generous spacing, clear typography.] Avoid
skeuomorphic clutter and tiny text. High contrast. States for every
component: default, hover, active, disabled, focused (keyboard).

DELIVERABLES
1. Main window wireframe for each tab at default size.
2. Compact and large layouts.
3. Advanced drawer, Settings dialog, preset browser.
4. Era 7 vs Era 5 views side by side.
5. A component list with states, and a table of where each control from
   the list above lives.
6. Your assumptions about anything unclear in the reference images, and
   any controls you recommend removing, merging or renaming.
Give me 2 or 3 alternative layouts first, then I'll choose one to refine.
```

Tips:
- Keep **image set A (hardware)** and **image set B (mood)** clearly separated, so the tool does not copy the hardware.
- Name each hardware image in the prompt (as in the template) so the tool knows what it is looking at.
- Send the REV7 front panel too: the REV7's "five types, up to seven parameters each" model maps cleanly onto the tabs.
- Ranges above are from the repo's research; a few are still open (Q-02 room size, Q-03 hi ratio, Q-04 EQ gain, Q-07, Q-13). Treat them as placeholders in the UI.
