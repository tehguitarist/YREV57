# UI controls, screens and meters (inventory for prototyping)

> Widget choices (knob vs button vs segmented), screen layout and a prototyping prompt are in [ui-prototype-brief.md](ui-prototype-brief.md).

> **Status:** this is the full hardware inventory. What the plugin actually ships is described in [ui-layout.md](ui-layout.md). MIDI features (program change, base key, trigger, bulk dump) are dropped; DAW automation and the preset browser replace them.

> **Scope (v3):** REV1-remote-only features are **cut** from the plugin: four-band reverb time and crossovers (§7.1), REV MODE 1–8 (§6.3), DISPLAY buttons (§6.4), HPF/LPF selector buttons (§15; the REV5 LPF 1–16 kHz/THRU in §14 stays), the four-fader mixer (§16), AUTO / knob-indicator system (§19), D1/D2 readouts, PRESET 1–8 direct keys, FUNCTION/PANEL keys and the IN/OUT meter switch. They remain listed here for reference. Added: **Wet solo** (a key beside Wet R invert and Mute: output 100 % wet, mix bypassed, the mix screen shows "WET"; not stored in presets). Also dropped: the gate's MIDI trigger and the pitch programs' MIDI base key (DAW automation covers them). Added (plugin-only, DSP ported from REV-246): a **ducker** (on, range, threshold, attack, release) and a **stereo width** knob (0–200 %). Also dropped from the plugin: the 7-segment memory number display (the ERA legend keys take its place) and typing a memory number (use the preset browser; all memory functions fold into it), every graph except the EQ graph, and the room-size chart. The current layout is in [ui-prompt-v3.md](ui-prompt-v3.md).

> Status: **Draft v0.** A complete list of everything the hardware exposes, plus what a plugin needs on top. It is meant to be the checklist for a UI prototype. DSP parameter ranges live in [parameters.md](parameters.md); this document adds panel controls, screens, meters, indicators and plugin-only needs.

## 0. Sources and tags

| Tag | Source |
| --- | --- |
| **R1** | Yamaha REV1 remote unit **RCR-1**, read from a photo of the panel. Ranges printed on the knobs are read by eye; the REV1 manual is not in the repo (Q-17), so treat R1 values as *(photo read)*. |
| **R5** | REV5 front panel, read from a render of the panel. Ranges printed beside the knobs. |
| **R7** | REV7, from the REV7 owner's and service manuals; no panel image supplied. |
| **R5-INT** | REV5 INT PARAM layer (REV5 owner's manual). |
| **PLG** | Plugin-only; no hardware equivalent. |

Priority: **P0** needed for a usable v1, **P1** wanted for the "authentic" experience, **P2** later or optional.

"Stored" = whether the hardware saves it with the program/memory (this decides whether it belongs in the preset or in global settings).

---

## 1. Screens (suggested map)

The hardware reached everything through a few buttons and a 2 × 16 LCD. In a plugin most of this can be visible at once, but the structure below is what a UI has to cover. A prototype could show Main always, with the rest as tabs or panels.

| # | Screen / panel | Contents | Section |
| --- | --- | --- | --- |
| S1 | **Main / LCD display** | Program number, name, type, headline parameter ("REV TIME = 2.6s"), era, meters | §3, §4 |
| S2 | **Global strip** | Input level, wet input mode, mix, bypass, mute, out phase, era, character | §2 |
| S3 | **Program browser** | Recall, store, names, banks, compare, init | §5 |
| S4 | **Reverb page** | RT, Hi/Lo ratio (R1: 4-band), pre-delay, 1st ref, diffusion | §7 |
| S5 | **Early reflection page** | Mode, liveness, room size, count, delays, feedback | §8 |
| S6 | **Delay / Echo page** | L/R delay, feedback, damp | §9 |
| S7 | **Modulation page** | Flange, reverb flange, chorus, phasing, tremolo, symphonic | §10 |
| S8 | **Pitch / Pan page** (REV5 only, P2) | Pitch A/B/C, pan | §11 |
| S9 | **Gate / combination page** | Gate, order of chained effects | §12 |
| S10 | **Analogue EQ** | 3-band, wet only, not stored | §13.1 |
| S11 | **Digital EQ** (Era 5) | 3-band parametric, stored per program | §13.2 |
| S12 | **Internal parameters** ("INT PARAM") | Density, LPF, space modulation, secondary reverb, ER/rev balance | §14 |
| S13 | **Filters** (R1 style) | HPF and LPF selectors | §15 |
| S14 | **Mixer / levels** (R1 style) | Direct, E/R, reverb, master, each with on/auto | §16 |
| S15 | **Utility / settings** | MIDI, footswitch, tables, quality, UI scale, about | §17 |
| S16 | **About / legal** | Trademark disclaimer, version, licence | §18 |

---

## 2. Global strip (always visible)

| Control | Type | Range / values | Default | Source | Stored | Param ID | Pri |
| --- | --- | --- | --- | --- | --- | --- | --- |
| **Input level** | Knob | 0–10 dial (8 = 0 dB unity, 10 ≈ +12 dB). Show as dial and as dB | 8 (0 dB) | R5, R7 | No | `in_level` | P0 |
| **Wet input: MONO / STEREO / TRUE STEREO** | Backlit legend, three stacked words in the display panel | Mono (left input only) · Stereo (L+R summed, authentic) · True stereo (plugin only) | Stereo | R5, R7, PLG | No | `wet_input` | P0 |
| **Mixing: DIRECT ↔ REV** | Knob | Direct to Reverb; 1:1 direct/wet at the centre-to-full wet range | 50 % | R5, R7 | No | `mix` | P0 |
| **Bypass** | Button + LED | On / off (click-free, VCA on hardware) | Off | R5, R7 | No | `bypass` | P0 |
| **Mute** | Button + LED | Silences dry and wet (REV7 service manual); output also mutes briefly on program change | Off | R5, R7 | No | (not a param; PLG optional) | P1 |
| **Out phase R** | Button / menu | Invert wet right channel | Off | R7 (param), R5 | Yes | `out_phase_r` | P1 |
| **Effect output level** | Knob | Common to all programs (R5). R1 equivalent is MASTER, §16 | 0 dB | R5, R1 | Yes | to add | P1 |
| **Era** | Two large backlit legend keys (7, 5) | 7 (REV7, 1985) / 5 (REV5, 1987) | 7 | PLG | Preset | `era` | P0 |
| **Character** | Knob | 0–100 % (clean ↔ authentic converter model) | 100 % | PLG | Preset | `character` | P1 |
| **Dither** | Switch | On / off (Era 5 only) | On | R5 (SW103) | Preset | `dither` | P2 |
| **Power** | (none) | The R5 has a POWER switch; not needed in a plugin | — | R5 | — | — | — |

---

## 3. Meters and indicators

| Item | What the hardware has | Plugin note | Pri |
| --- | --- | --- | --- |
| **R5 level meter** | 8 segments: CLIP, 0, −3, −6, −10, −15, −20, −30 dB. Two top segments grey/red, then red, then green. Single column. Per the REV7 service manual the meter is fed from the pre-emphasis stage and all lit = clipping (assumed the same on the REV5) | Input meter, peak with hold/decay. Show clip latch | P0 |
| **R1 level meter** | **Two columns, L and R**, 16 segments each: FULL, −3, −6, −9, −12, −15, −18, −21, −24, −27, −30, −36, −42, −48, −54, −60 dB | Stereo bar meter with dB scale | P1 |
| **R1 LEVEL METER selector** | Button toggling meter point: **IN** or **OUT**, with an IN lamp and an OUT lamp | Input / output meter select | P1 |
| **MONO lamp** | Lit when wet input is mono | Part of the three-way wet-input legend | P1 |
| **EQ ON lamp** | Lit when analogue EQ is on | | P1 |
| **Memory number display** | 7-segment, memory 1–99 (render shows one digit; 2 are needed). R1: small 3-segment-style window under MEMORY | Program number | P0 |
| **Key LEDs** | Every R5 key has an LED (REV1–4, E/R1–2, OTHERS, USER MEMORY, PARAMETER, LEVEL, INITIAL DELAY, 1ST REF, EQ, EQ ON, MEMORY, STORE, RECALL/ENTER, MUTE, INT PARAM, UTILITY, BYPASS) | Active-state lamps | P1 |
| **R1 knob-position indicators** | A small strip above **every** R1 knob (room size, E/R number, liveness, high, mid-hi, low, direct, early reflection, reverberation, master) | Likely shows stored value vs knob position so the knob can "catch up" before taking over (inference). In a plugin: a ghost marker showing the preset's stored value while the knob is edited | P1 |
| **R1 AUTO lamps** | AUTO buttons on E/R, D1, D2, early reflection, reverberation, panel | See §19 | P1 |
| **Clip / overload** | Clip = all level segments lit; A/D clip only | Sticky clip indicator, click to reset | P0 |
| **Activity** | Mute lamp, MIDI activity (implied) | Optional | P2 |

---

## 4. Display (LCD)

| Item | Detail | Pri |
| --- | --- | --- |
| **R5 2 × 16 LCD** (yellow-green, backlit) | Line 1: program name ("LARGE HALL"). Line 2: current parameter and value ("REV TIME = 2.6s"). Shows names, parameter pages, store/name editing, utility menus | P0 |
| **R1 large graphic/alphanumeric LCD** | Very large, spans the panel top. Selected by the DISPLAY buttons (§6.4): likely curves and values for reverb time, ratios, E/R pattern. Exact content unknown (Q-17) | P1 |
| **R1 value readouts** | Three small dedicated displays: **REV. TIME (R/T)** in *sec*, **E/R DELAY 1 (D1)** in *ms*, **REV DELAY 2 (D2)** in *ms* | P1 |
| **R1 MEMORY readout** | Small window next to M / STR / RCL | P1 |
| **Parameter name + value + unit** | Plugin: a contextual readout that shows the name/value of whichever control is being touched | P0 |

---

## 5. Program / memory management

| Control | Source | Detail | Pri |
| --- | --- | --- | --- |
| **Memory number entry** | R5, R7, R1 | Numeric keypad 0–9 · `.` · `–` · CLEAR · ENTER | P1 |
| **UP / DOWN** | R5 (▲▼ keys), R7, R1 (UP, DWN) | Step through memories / parameter values | P0 |
| **Direct recall keys** | R5: **REV1 (31) · REV2 (32) · REV3 (33) · REV4 (34) · E/R1 (35) · E/R2 (36) · OTHERS (37)** plus **USER MEMORY**. R1: **PRESET 1–8** (two rows of 4). R7: direct-recall keys | Key numbers printed on the R5 keys are *(panel read)*; how they map onto the 1–99 map is not confirmed in the repo | P1 |
| **Memory map** | R5 99 locations: 1–30 ROM presets, 31–90 user RAM, 91–99 ROM combination programs. R7: 1–30 ROM, 31–90 user | Factory preset bank + user bank + combos | P0 |
| **MEMORY key / RECALL-ENTER / STORE** | R5: MEMORY, STORE, RECALL ENTER. R1: M, STR, RCL | Recall, store (overwrite), store-as. Factory 1–30 are read-only | P0 |
| **Name editing** | R5 (UTILITY) | Rename user memories (R5: named memories) | P0 |
| **Edited / unsaved indicator** | R7 "edits are temporary until stored" | Dirty marker | P0 |
| **Compare / revert** | PLG | A/B, revert to stored | P1 |
| **Initialise** | PLG | Reset to default state | P1 |
| **Copy static params between memories** | Review complaint (R5 can't) | Copy/paste preset sections | P2 |
| **Program-change recall** | R7: PC 0–127 → memories 1–90; R5: four PC tables | Host program list; MIDI PC optional | P2 |
| **Preset browser** | PLG | Search, categories, favourites, factory/user split, import/export | P1 |
| **Hidden presets** | PLG | Shift-click the era key that is not lit: the browser opens on the SECRET bank (no tab; Prev / Next skip it); the era does not change; both era keys light while the hidden bank is open or one of its presets is loaded. Holds *CLA Drum Room* ([presets.md](presets.md#cla-drum-room)) | P2 |
| **Hidden values** | §14 | Not shown; each preset has its own | — |

### Preset families to categorise (from the hardware)

REV (hall, plate, spring, echo room, strings, snare, handclaps) · Early reflection (bass, kick, gate, reverse, rehearsal room, piano, organ, brass, guitar, live reference) · Delay · Echo · Modulation (flange, reverb flange, chorus A/B, phasing, tremolo, symphonic) · Pitch · Pan · Combination (91–99).

---

## 6. Mode / type selection

### 6.1 Effect type (R7 five types; R5 more)

| Type | Notes | Pri |
| --- | --- | --- |
| REV | Reverb with 3 early reflections per channel (R7) | P0 |
| E/R | Early reflections only (18 per channel R7, up to 34 R5) | P0 |
| DELAY | Independent L/R delay | P1 |
| ECHO | Feedback repeats with High Damp | P1 |
| MOD | Flange, reverb flange, chorus A/B, phasing, tremolo, symphonic | P1 |
| PITCH A/B/C | R5 only | P2 |
| PAN | R5 only | P2 |
| COMBINATION | R5 91–99: two effects plus gate, fixed order | P2 |

### 6.2 E/R MODE selector (R1: buttons 1–8, two rows of 4)

R7 modes: 1 Small Hall · 2 Large Hall · 3 Random · 4 Reverse · 5 Plate · 6 Spring. R5 adds PAN-A, PAN-B, and REHEARSAL ROOM patterns (PIANO, ORGAN, BRASS, GUITAR; PIANO has a key in the plugin since 2026-11-25 (pass 52 pattern, Rehearsal Room); ORGAN, BRASS and GUITAR stay hidden until data turns up, Q-01). The plugin adds one key for both eras, REV E/R, in their place: the reverb program's own early reflections (era 5 the REV5's, twenty-second pass; era 7 the REV7's three per channel, twenty-third pass). It stands for `er_mode` 12–14 (Rev Hall, Rev Plate, Rev Room): which set plays is stored with the preset and not shown, as on the hardware, where the reverb programs' reflections are fixed per program; pressing the key from another mode picks Rev Hall (ours as a control, neither unit has an ER mode on its reverb programs). R1 has **eight** mode buttons; their names are not readable from the photo (Q-17). Plugin: a single 8-slot selector, relabelled per era.

### 6.3 REV MODE selector (R1: buttons 1–8, two rows of 4)

Eight reverb algorithm slots on the R1. Names not readable. Plugin: reverb type selector (hall, plate, spring, chamber, room…). Our own voicings ([ADR 0003](decisions/0003-own-late-network.md)).

### 6.4 DISPLAY buttons (R1, two rows of 4)

Printed labels: **W/F · F/C · R/T · RATIO** and **E/R · REV · P/S · M**. Meanings are not documented here (inference only): probably which graph/readout the large LCD shows (waveform/pattern, frequency characteristic, reverb time, ratio, E/R, reverb, parameter set, memory). Prototype as a row of 8 selector buttons that switches the main display view.

---

## 7. Reverb controls

| Control | Range | Default | Source | Stored | Param ID | Pri |
| --- | --- | --- | --- | --- | --- | --- |
| **Reverb time (R/T)** at 1 kHz | 0.3–10 s (Era 7); 0.3–99 s (Era 5). R1 shows it in a dedicated *sec* readout | 2.6 s | R7, R5, R1 | Yes | `rt` | P0 |
| **High ratio** | ×0.1–1.0 (Q-03 notes one review says 2.0) | 0.3 | R7, R5 | Yes | `hi_ratio` | P0 |
| **Low ratio** | ×0.1–2.4 | 1.0 | R7, R5 | Yes | `lo_ratio` | P0 |
| **Initial (pre) delay** | 0.1–100 ms | 20 ms | R7, R5 | Yes | `pre_delay` | P0 |
| **1st reflection delay** | 0.1–100 ms (mono centre tap) | 10 ms | R7, R5 | Yes | `ref1_delay` | P1 |
| **1st reflection level** | 0–100 % of direct | 0 % | R7, R5 | Yes | `ref1_level` | P1 |
| **Diffusion** | 0–10 (0–5 on R5 SMALL HALL) | 5 | R7, R5 | Yes | `diffusion` | P1 |
| **Reverb mix / level** | Effect/direct mix, common | — | R5 | Yes | `mix` | P0 |
| **D2: reverb delay 2** | R1 only: separate **REV DELAY 2 (D2)** readout in ms with AUTO. Probably the pre-delay before the reverb (inference) | — | R1 | Yes | to confirm | P2 |

### 7.1 R1-style four-band reverb time (if we want it)

The R1 remote exposes a richer reverb-time shape than the R5/R7:

| Control | Printed scale (photo read) | Notes |
| --- | --- | --- |
| **HIGH** knob | −5 … −1, then +2 … +5 style steps (scale marked −5 to 5 with dots) | Reverb-time adjustment of the high band. Different scale from the R5 Hi ratio |
| **MID-HI** knob | 0.2, 0.3, 0.4, 0.5, 0.6, 0.8, 0.9, 1.0, 1.1, 1.2 | Ratio |
| **MID-LOW** | Reference; the **R/T readout** is labelled MID-LOW | The "1 kHz" RT equivalent |
| **LOW** knob | 0.4, 0.6, 0.8, 1.0, 1.2, 1.6, 1.8, 2.0, 2.2, 2.4 | Ratio, same 0.1–2.4 limit as R5/R7 |
| **Crossover buttons, upper** (bracket marked X) | **4K · 2K · 1K** | Radio selector: where MID-HI meets HIGH |
| **Crossover buttons, lower** (bracket marked X) | **500 · 250 · 125** | Radio selector: where LOW meets MID-LOW |

Decision for the prototype: draw the R5/R7 two-ratio version (Hi, Lo) as the default and show the R1 four-band version as an "advanced" layout, or the other way round if you want the richer design. Our engine can support either.

---

## 8. Early reflection controls

| Control | Range / values | Source | Stored | Param ID | Pri |
| --- | --- | --- | --- | --- | --- |
| **E/R mode** | See §6.2 | R7, R5, R1 | Yes | `er_mode` | P0 |
| **Room size** | R7: 0.1–20 (Q-02: or 0.1–10). R5: 0.1–25. R1 knob scale: 1/8, 1/4, 1/2, 1, 2, 4, 6, 8 (log-ish) | R7, R5, R1 | Yes | `room_size` | P0 |
| **Liveness** | 0–10 (R1 knob 1–8) | R7, R5, R1 | Yes | `liveness` | P0 |
| **E/R number** (reflections) | R5 1–34. R1 knob: 2, 4, 8, 16, 24, 32, 40 (photo read; may be 1–40) | R5, R1 | Yes | `er_count` | P1 |
| **Diffusion** | 0–10 | R7, R5 | Yes | `diffusion` | P1 |
| **Initial delay / 1st ref delay / 1st ref level** | As §7 | R7, R5 | Yes | | P1 |
| **E/R delay 1 (D1)** | R1 separate readout in ms, with AUTO | R1 | Yes | to confirm | P2 |
| **ER feedback: gain · delay · HF ratio** | R5 ranges "not recorded" in the repo (to measure) | R5 | Yes | `er_fb_*` | P2 |
| **ER / reverb balance** | 0–100 % | R5-INT | Yes | `er_rev_balance` | P1 |
| **Space modulation** | 0–10, E/R programs 6, 20, 24, 30 only | R5 | Yes | `space_mod` | P2 |
| **Room-size chart** | Reference graphic mapping each mode's room size to real room size (box → Astrodome, log scale) | R7 manual | — | — | P2 (help overlay) |
| **E/R tap display** | PLG: visual of the reflection pattern (taps over time) | PLG | — | — | P1 |

---

## 9. Delay and echo

| Control | Range | Source | Stored | Param ID | Pri |
| --- | --- | --- | --- | --- | --- |
| **Delay L** | 0.1–900 ms (Era 7), 0.1–1,400 ms (Era 5; a review says up to 2.9 s, Q-11) | R7, R5 | Yes | `dly_l` | P1 |
| **Delay R** | same | R7, R5 | Yes | `dly_r` | P1 |
| **Delay level** | 0–100 % | R7 | Yes | `dly_level` | P1 |
| **Delay feedback** | −99 … +99 % (Era 5); R7 delay has none | R5 | Yes | `dly_fb` | P2 |
| **Delay LPF** | R5 low-pass | R5 | Yes | | P2 |
| **Initial delay** | 0.1–100 ms (adds to both channels) | R7 | Yes | `pre_delay` | P1 |
| **Echo L / R time** | 0.1–450 ms each | R7 | Yes | `echo_l`, `echo_r` | P1 |
| **Echo feedback** | 0–99 % (±99 % Era 5) | R7, R5 | Yes | `echo_fb` | P1 |
| **Echo High Damp** | 0–10, whole steps (Q-13); era 5 shows it as the REV5's HIGH ratio ×1.0–0.1; low-pass inside the feedback loop | R7, R5 | Yes | `echo_damp` | P1 |
| **Tempo sync** | Note-value delay times | PLG | — | to add | P1 |

---

## 10. Modulation

| Control | Range | Source | Applies to | Param ID | Pri |
| --- | --- | --- | --- | --- | --- |
| **LFO rate** | 0.1–20 Hz (Era 7), 0.1–40 Hz (Era 5) | R7, R5 | All MOD | `lfo_rate` | P1 |
| **Depth** | 0–100 % (100 % = ±4 ms delay swing) | R7 | Flange, reverb flange, phasing, tremolo, symphonic | `mod_depth` | P1 |
| **DM depth** (delay modulation) | 0–100 % | R7, R5 | Chorus A/B | `dm_depth` | P1 |
| **AM depth** (amplitude modulation) | 0–100 % | R7, R5 | Chorus A/B, tremolo | `am_depth` | P1 |
| **Flange delay** | 0.1–100 ms (Era 7), 0.1–5 ms (Era 5) | R7, R5 | Flange | `mod_delay` | P1 |
| **Flange feedback** | 0–99 % | R7 | Flange (fixed 30 % on reverb flange) | `mod_fb` | P1 |
| **Reverb flange: RT and delay** | RT 0.3–10 s; delay 0.1–100 ms (Q-07: 30) | R7 | Reverb flange | `mod_rt` unused since 2026-11-22: the reverb engine gives the decay; `mod_delay` held to 30 ms | P2 |
| **Phasing delay** | 0.1–8 ms | R7 | Phasing | | P2 |
| **MOD LPF** | 1–16 kHz or THRU | R5-INT | MOD | `wet_lpf` | P2 |
| **MOD gate level** | 0–100 % | R5-INT | MOD | | P2 |
| **Type selector** | Flange · Reverb flange · Chorus A · Chorus B · Phasing · Tremolo · Symphonic | R7, R5 | | | P1 |
| **LFO scope** | PLG: moving LFO shape indicator | PLG | | | P2 |

---

## 11. Pitch and pan (REV5 only, lowest priority)

| Control | Range | Pri |
| --- | --- | --- |
| Pitch A/B/C selector | 3 programs | P2 |
| Shift | −12 … +12 semitones | P2 |
| Fine | −100 … +100 cents | P2 |
| Pitch delay | 0.1–400 ms | P2 |
| Pitch feedback | 0–99 % | P2 |
| MIDI base key | Select pitch by key (needs MIDI input) | P2 |
| Pan direction | L→R · R→L · L↔R (back and forth) | P2 |
| Pan rate | 0.1–40 Hz | P2 |
| Pan depth | 0–100 % | P2 |

---

## 12. Gate and combination programs (REV5)

| Control | Range | Pri |
| --- | --- | --- |
| **Gate level / trigger threshold** | level (0–100 %) | P1 |
| **Trigger delay** | 0.1–1,000 ms | P2 |
| **Hold** | 1–24,000 ms | P2 |
| **Release** | 3–24,000 ms | P2 |
| **MIDI trigger** | On/off, note-on, no note selection (needs MIDI input) | P2 |
| **Combination routing** | Display fixed order, e.g. "Symphonic → Reverb → Gate" (93), "Reverb → Symphonic → Gate" (95); 98/99 parallel. Built as `fx_order` with the hardware bypass around the reverb (fx-slot.md, Routing) | P2 |
| **REV MIX** | 0–100 % (R5 91–97: reverb mixed into the effect signal; 45 % printed) | P1 | `rev_mix`, INT PARAM drawer; plugin key FX WET (`fx_wet`) beside the order keys removes the bypass |
| **Gate / reverse-gate shape display** | PLG: envelope graphic | P2 |

---

## 13. Equalisers

### 13.1 Analogue 3-band (wet path only, before the converter, **not stored**)

| Control | Range | Default | Param ID | Pri |
| --- | --- | --- | --- | --- |
| **EQ ON/OFF** | Switch + EQ ON lamp (R5 has both a front-panel switch and an EQ ON key) | Off | `aeq_on` | P0 |
| **Low freq** | 50–700 Hz | 200 | `aeq_lo_f` | P0 |
| **Low level** | ±15 dB (Q-04: service manual may read ±18) | 0 | `aeq_lo_g` | P0 |
| **Mid freq** | 350 Hz–5 kHz, both eras (shared circuit, Q-26) | 1 kHz | `aeq_mid_f` | P0 |
| **Mid level** | ±15 dB | 0 | `aeq_mid_g` | P0 |
| **High freq** | 2–20 kHz | 8 kHz | `aeq_hi_f` | P0 |
| **High level** | ±15 dB | 0 | `aeq_hi_g` | P0 |
| **EQ response curve** | PLG: live curve | | | P1 |

### 13.2 Digital programmable 3-band parametric (Era 5, **stored per program**, YM3608)

| Control | Range | Pri |
| --- | --- | --- |
| **EQ key / EQ ON key** | Edit and enable (R5) | P1 |
| **Low freq** | 32 Hz–2.2 kHz | P1 |
| **Low gain** | ±15 dB | P1 |
| **Low type** | Peak / shelf | P1 |
| **Low Q** | 0.1–5.0 (peak mode only) | P1 |
| **Mid freq** | 250 Hz–5.6 kHz | P1 |
| **Mid gain** | ±15 dB | P1 |
| **Mid Q** | 0.1–5.0 | P1 |
| **High freq** | 500 Hz–16 kHz | P1 |
| **High gain** | ±15 dB | P1 |
| **High type** | Peak / shelf | P1 |
| **High Q** | 0.1–5.0 (peak mode only) | P1 |
| **Digital EQ curve** | PLG: live response | P1 |

Param IDs to add: `deq_*`. Show only when the era is 5.

---

## 14. Internal parameters (R5 INT PARAM layer; "advanced")

| Control | Range | Applies to | Pri |
| --- | --- | --- | --- |
| **ER/reverb balance** | 0–100 % | Reverb programs | P1 |
| **Density** | 1–4 | Reverb | P1 |
| **Low-pass filter** | 1–16 kHz or THRU | Reverb, MOD | P1 |
| **Space modulation** | 0–10 (0 off) | Reverb, E/R 6/20/24/30 | P2 |
| **Secondary reverb time** | ×0.1–10 of primary | Reverb | P2 |
| **Secondary reverb delay** | 0–500 ms | Reverb | P2 |
| **Secondary reverb level** | 0–100 % | Reverb | P2 |
| **Gate level** | threshold | Reverb, MOD | P2 |

**Hidden per-preset values** (late-network delays, all-pass coefficients, damping corners, tail modulation depth, ER group gains) are deliberately not shown. If you want an "expert" panel, they would live here.

---

## 15. Filters (R1 remote)

| Control | Values | Notes | Pri |
| --- | --- | --- | --- |
| **HPF** (4 buttons) | **400 · 200 · 100 · 50 Hz** | Radio group on the R1 (select one, or none). High-pass on the effect | P1 |
| **LPF** (4 buttons) | **10K · 8K · 6K · 4K Hz** | Radio group. Low-pass on the effect. The R5 has a 1–16 kHz LPF in the INT layer | P1 |

---

## 16. Mixer / levels (R1 remote)

Four sections, each with a level knob (−∞, −60, −40, −30, −20, −10, 0 dB) and a position-indicator strip above it:

| Section | Controls | Notes |
| --- | --- | --- |
| **DIRECT** | ON button + level knob | Dry level |
| **EARLY REFLECTION** | AUTO · ON buttons + level knob | E/R level |
| **REVERBERATION** | AUTO · ON buttons + level knob | Late reverb level |
| **MASTER** | ON button + level knob | Overall output |

Each ON is a mute/enable for that section. For the plugin: either the R5 style (one Mixing knob plus effect output level) or this four-fader mixer; the mixer gives independent ER and reverb levels, which our engine can provide. Suggested IDs: `lvl_direct`, `lvl_er`, `lvl_rev`, `lvl_master`, `on_direct`, `on_er`, `on_rev`, `on_master`.

---

## 17. Utility / settings

| Item | Source | Pri |
| --- | --- | --- |
| MIDI program-change on/off, omni, channel | R7, R5 | P2 |
| Four program-change tables | R5 | P2 |
| MIDI bulk dump / load | R5 | — (replace with preset import/export) |
| Footswitch function / range | R5 | P2 |
| Name memory | R5 | P0 (§5) |
| **Quality mode**: native-rate engine vs host-rate "eco" ([ADR 0001](decisions/0001-native-rate-engine.md)) | PLG | P1 |
| **Wet input default** (mono / stereo / true stereo) | PLG | P1 |
| **Latency report** (from native-rate resampling) | PLG | P1 |
| Randomise noise seed / freeze noise | PLG | P2 |
| UI scale / resize | PLG | P1 |
| Tooltips and value-entry (double-click to type) | PLG | P0 |
| Undo/redo | PLG | P1 |
| Reset all | PLG | P1 |

---

## 18. About / legal

Name, version, build, licence, **trademark disclaimer** ([ADR 0004](decisions/0004-inspired-by.md), [legal.md](../legal.md)), credits. Required by the project's rules wherever the hardware is named.

---

## 19. AUTO, ON and pickup behaviour (R1 remote)

The R1 remote has an **AUTO** concept in several places: PANEL (P · EDIT · AUTO), E/R (AUTO), E/R DELAY 1 (AUTO), REV DELAY 2 (AUTO), EARLY REFLECTION (AUTO + ON), REVERBERATION (AUTO + ON). Together with the knob-position indicator strips, the likeliest reading (inference, unverified) is:

- **AUTO lit**: the parameter follows the stored memory (and updates on recall).
- **AUTO off**: the physical knob/value takes over.
- **PANEL P / EDIT / AUTO**: whether the panel shows *preset* values, *edit* values, or tracks automatically.

Plugin interpretation: each knob has a **"follow preset"** state. When a preset loads, the knob shows the stored value; moving a knob overrides it. A ghost marker shows the stored value so you can see how far you have moved. This also maps neatly to host automation. Decide whether to include this at all; it is the one area where the R1 remote is conceptually different from the R5 and R7.

---

## 20. Physical inventory (cross-check list)

Use this to tick off that the prototype does not miss any item from the supplied photos.

### 20.1 REV5 front panel (render)

- [ ] Power switch (ON/OFF)
- [ ] MONO / STEREO switch
- [ ] INPUT LEVEL knob (0–10)
- [ ] EQ ON/OFF switch
- [ ] LO: FREQ knob (50–700 Hz), LEVEL knob (−15…+15)
- [ ] MID: FREQ knob (0.35–5 kHz), LEVEL knob (−15…+15)
- [ ] HI: FREQ knob (2–20 kHz), LEVEL knob (−15…+15)
- [ ] MIXING knob (DIRECT ↔ REV)
- [ ] 2 × 16 LCD
- [ ] Level meter: CLIP, 0, −3, −6, −10, −15, −20, −30
- [ ] MONO lamp, EQ ON lamp
- [ ] MEMORY number display (7-segment)
- [ ] Keys: REV1 (31), REV2 (32), REV3 (33), REV4 (34), E/R1 (35), E/R2 (36), OTHERS (37), USER MEMORY
- [ ] Keys: PARAMETER, LEVEL, INITIAL DELAY, 1ST REF, EQ, EQ ON
- [ ] Keys: ▼ and ▲
- [ ] Keypad: 0–9, `.`, `–`, CLEAR
- [ ] Keys: MEMORY, STORE, RECALL ENTER
- [ ] Keys: MUTE, INT PARAM, UTILITY, BYPASS
- [ ] Rack ears / mounting holes (cosmetic only)

### 20.2 REV1 remote RCR-1

- [ ] Large LCD
- [ ] DISPLAY: W/F, F/C, R/T, RATIO, E/R, REV, P/S, M (8 buttons)
- [ ] L/R level meters, 16 segments each (FULL … −60)
- [ ] LEVEL METER: IN/OUT button + IN lamp + OUT lamp
- [ ] HPF: 400, 200, 100, 50 (4 buttons)
- [ ] LPF: 10K, 8K, 6K, 4K (4 buttons)
- [ ] REV. TIME (R/T) readout in sec; MID-LOW label
- [ ] E/R MODE 1–8 (8 buttons)
- [ ] ROOM SIZE knob + indicator strip
- [ ] E/R NUMBER knob + indicator strip
- [ ] LIVENESS knob + indicator strip
- [ ] E/R AUTO button
- [ ] E/R DELAY 1 (D1) readout in ms + AUTO button
- [ ] REV MODE 1–8 (8 buttons)
- [ ] HIGH knob + indicator strip
- [ ] MID-HI knob + indicator strip
- [ ] LOW knob + indicator strip
- [ ] Upper crossover buttons 4K · 2K · 1K
- [ ] Lower crossover buttons 500 · 250 · 125
- [ ] REV DELAY 2 (D2) readout in ms + AUTO button
- [ ] DIRECT: ON + level knob + indicator
- [ ] EARLY REFLECTION: AUTO, ON + level knob + indicator
- [ ] REVERBERATION: AUTO, ON + level knob + indicator
- [ ] MASTER: ON + level knob + indicator
- [ ] PRESET 1–8 (8 buttons)
- [ ] PANEL: P, EDIT, AUTO
- [ ] MEMORY readout + M, STR, RCL
- [ ] FUNCTION: R/T, D1, D2, M (selects what the keypad / UP-DWN edits)
- [ ] Keypad: 7 8 9 / 4 5 6 / 1 2 3 / 0 `.` CLR / UP DWN ENT

### 20.3 REV7 front panel (two photos supplied)

Read from the photos (the second is a straight-on view). Printed ranges are *(photo read)*.

- [ ] POWER switch (ON/OFF)
- [ ] MONO / STEREO switch
- [ ] INPUT LEVEL knob (0–10)
- [ ] EQ ON/OFF switch
- [ ] LO: FREQ knob (50–700 Hz, ticks 100 and 300), LEVEL knob (−15…+15)
- [ ] MID: FREQ knob (printed 0.35–5 kHz, ticks 0.8 and 2), LEVEL knob (−15…+15). Matches the circuit (Q-26 resolved: 350 Hz–5 kHz)
- [ ] HI: FREQ knob (2–20 kHz, ticks 4 and 10), LEVEL knob (−15…+15)
- [ ] MIXING knob (DIRECT ↔ REV)
- [ ] 2 × 16 LCD (shows e.g. "LARGE HALL" over "DLY = 70.0ms", with a cursor on the edited parameter)
- [ ] Level meter: CLIP, 0, −3, −6, −10, −15, −20, −30 dB
- [ ] Backlit legends: MONO, EQ ON
- [ ] MEMORY number display (2-digit 7-segment)
- [ ] Direct-recall keys: REV1 (31), REV2 (32), REV3 (33), REV4 (34), E/R1 (35), E/R2 (36), OTHERS (37), USER MEMORY (the REV5 render has the same eight)
- [ ] Parameter keys: REV TIME, INITIAL DELAY, 1ST REF, F1, F2, F3, `–`, `+`. REV TIME, INITIAL DELAY and 1ST REF are fixed; F1–F3 select the other parameters of the current program type, which matches the "at most seven parameters per program" rule (inference); `–` / `+` step the value
- [ ] Keypad: 0–9, `.`, CLEAR, ENTER
- [ ] Keys: MEMORY, STORE, RECALL
- [ ] Keys: MUTE, OUT PHASE, MIDI CTRL, BYPASS
- [ ] Two rack ears (cosmetic)

REV7 versus the REV5 panel: the REV7 has **F1–F3, MIDI CTRL, OUT PHASE and REV TIME** keys; the REV5 has **PARAMETER, LEVEL, EQ, EQ ON, INT PARAM and UTILITY** keys and ▲ ▼ arrows. Both share the knob row, meter, direct-recall keys, keypad, MEMORY/STORE/RECALL, MUTE and BYPASS.

---

## 21. Plugin-only needs (not on any hardware)

| Item | Why | Pri |
| --- | --- | --- |
| Host automation of every continuous parameter | Hardware had no real-time control (the R5's main complaint) | P0 |
| Preset save/load in host state | Required by hosts | P0 |
| Resizable UI and HiDPI | Standard | P1 |
| Oversized-value text entry (double-click) | Precision | P0 |
| MIDI learn (optional) | | P2 |
| A/B snapshots | | P1 |
| Tail length reporting (up to 99 s) | Host bounce/tail handling | P1 |
| Sample-rate change handling, block size 1, denormals | Robustness | P0 (DSP) |
| Output headroom/limiter guard | Safety on long RT | P2 |
