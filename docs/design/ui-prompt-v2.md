# UI prompt v2: 2.5:1 chassis with button-driven screens

> **Superseded by [ui-prompt-v3.md](ui-prompt-v3.md)** (one screen, no tabs, REV1-only features cut). Kept for reference; §1 (what round 1 showed) is still useful.

Supersedes the single-prompt template in [ui-prototype-brief.md §5](ui-prototype-brief.md). Use [ui-controls.md](ui-controls.md) as the full inventory and the brief for the widget rules.

## 1. What round 1 showed

Reference images: [concept A](ui-concepts/concept-a-reverb-tab.png) and [concept B](ui-concepts/concept-b-reverb-tab.png). The **visual direction is approved**: black brushed-metal rack panel, engraved cream labels, knobs with tick marks and white pointers, boxed numeric readouts under every knob, green backlit LCD, red 7-segment memory number, square buttons with red/amber lamps, LED level meter, green-phosphor EQ graph.

Problems to fix:

| Problem | Seen in | Fix |
| --- | --- | --- |
| Only the Reverb tab was designed. Early Ref, Delay, Echo, Modulation and More were never shown, so controls "went missing" | A, B | One prompt per screen state (§5) |
| Missing controls: Early Ref set (room size, liveness, E/R number, modes, feedback), Delay, Echo, Modulation, pitch/pan/gate/combination, digital EQ (Q, shelf/peak), tempo sync, preset browser, settings, tap/curve displays | A, B | Listed per screen (§4) |
| Cramming: Advanced stuff shown permanently beside Reverb | A | Advanced, Filters and Mixer become **overlays** opened by buttons |
| Hardware key clutter copied over (INITIAL DELAY / 1ST REF / PARAM / LEVEL keys, memory lamp, duplicate MONO lamp) | B | Dropped; these are knobs now |
| Era switch labelled with years | A, B | Label **7 \| 5** (years as tiny captions only) |
| "Mode 1–8" with no names | A | Named buttons per era |
| Duplicated controls (1st ref delay in two places, mono shown twice) | A, B | Each control appears once |
| Tiny text | B | Minimum label height 16 px at 2000 px wide |

## 2. Canvas and chassis (identical on every screen)

**Canvas: 2000 × 800 px (2.5:1).** Compact version later: 1250 × 500, same proportions.

```
x:   0        230                         1380                   2000
y:0  ┌──────────────────────────────────────────────────────────────┐
     │ TOP BAR (h 110)                                              │
y110 ├──────────────────────────────────────────────────────────────┤
     │ TAB BAR (h 60)                                               │
y170 ├────────┬─────────────────────────────────┬───────────────────┤
     │ LEFT   │ CENTRE PAGE                     │ RIGHT RAIL        │
     │ RAIL   │ (changes with the tab and       │ (EQ, Mix, Output) │
     │ w 230  │  overlay buttons; w 1150)       │ w 620             │
y700 ├────────┴─────────────────────────────────┴───────────────────┤
     │ BOTTOM BAR (h 100)                                           │
y800 └──────────────────────────────────────────────────────────────┘
```

**TOP BAR (left to right):** YREV57 wordmark + "digital reverberator" · 7-segment MEMORY number (2 digits) · 2 × 16 green LCD (program name line, parameter = value line) · PREV · NEXT · PRESET ▼ · STORE · COMPARE · INIT · BYPASS (lamp) · ERA switch **7 | 5** · Settings cog.

**TAB BAR:** REVERB · EARLY REF · DELAY · ECHO · MODULATION · MORE (each a lamp button; exactly one lit).

**LEFT RAIL:** INPUT knob (dial 0–10, boxed dB value) · stereo L/R LED meters (CLIP, 0, −3, −6, −10, −15, −20, −30 dB) · CLIP LATCH button (click clears) · MONO | STEREO switch with lamps.

**RIGHT RAIL:** EQ block (EQ ON button, EQ graph with 3 draggable nodes, Low/Mid/High FREQ and GAIN knobs) above a MIX knob (DIRECT ↔ REV) and an OUTPUT knob.

**BOTTOM BAR:** shared readout LCD ("PARAMETER = value" for whatever is being touched) · three overlay buttons **ADVANCED · FILTERS · MIXER** (lamp when open) · status text "7 · STEREO · LATENCY 0 ms".

**Style (full wording is in the VISUAL STYLE block of Prompt 1, §5):** black brushed metal, 4 corner screws, engraved cream lettering, round knobs with pointer line, tick marks and min/max numbers, white-outlined numeric box under each knob, square buttons with a lamp above the label (red = on, amber = on for utility), green LCD with dark green characters, red 7-segment, green-yellow-red LED meter, green phosphor EQ graph. High contrast, even straight-on lighting with slight recess and knob depth, no glare, no photographic clutter.

## 3. How screens change (state map)

| Control pressed | What changes | What stays |
| --- | --- | --- |
| **Tab** (REVERB, EARLY REF, DELAY, ECHO, MODULATION, MORE) | Centre page swaps to that tab's controls | Chassis, rails, top, bottom |
| **MORE** sub-buttons (PITCH · PAN · GATE · COMBI) | Centre page swaps between the four More pages | Chassis |
| **MODULATION** type buttons (FLANGE · REV FLANGE · CHORUS A · CHORUS B · PHASING · TREMOLO · SYMPHONIC) | Centre page keeps the same layout but only the relevant knobs are lit; others hidden | Chassis |
| **REVERB "4-BAND RT"** toggle | The Hi/Lo knobs become HIGH · MID-HIGH · MID-LOW · LOW knobs and show crossover buttons | Rest of the page |
| **ADVANCED / FILTERS / MIXER** (bottom bar) | A panel covers the centre page (centre only); pressing again or the tab closes it | Chassis |
| **PRESET ▼** | Preset browser overlay covers the centre page | Chassis |
| **Cog** | Settings dialog (modal) | Dimmed chassis |
| **ERA 7 \| 5** | Controls appear/disappear (see §4.9); never greyed | Chassis |
| **EQ ANALOG \| DIGITAL** (era 5 only, in right rail) | Right-rail EQ swaps between 3-band analogue and 3-band digital parametric (Q + shelf/peak) | Mix/Output |

## 4. Screen contents (centre page unless stated)

All knobs: tick marks, min/max numbers, boxed value with unit. Ranges are the repo's working values.

**4.1 REVERB** (8 knobs in 2 rows of 4 + mode row)
REVERB TIME 0.3–10 s (era 5: to 99 s) · HIGH RATIO 0.1–1.0 · LOW RATIO 0.1–2.4 · PRE-DELAY 0.1–100 ms · 1ST REF DELAY 0.1–100 ms · 1ST REF LEVEL 0–100 % · DIFFUSION 0–10 · ER / REVERB 0–100 %.
REVERB MODE: 8 named buttons (Large Hall, Small Hall, Vocal Plate, Percussion Plate, Spring, Echo Room, Strings, Snare as placeholders).
4-BAND RT toggle (see §3): HIGH · MID-HIGH · MID-LOW · LOW knobs, plus CROSSOVER buttons 4K · 2K · 1K and 500 · 250 · 125.

**4.2 EARLY REF**
Mode buttons: SMALL HALL · LARGE HALL · RANDOM · REVERSE · PLATE · SPRING (+ PAN A · PAN B in era 5).
Knobs: ROOM SIZE 0.1–20 (era 5: 0.1–25, log taper) · LIVENESS 0–10 · E/R NUMBER 1–34 (stepped; era 7 max 18) · DIFFUSION 0–10 · INITIAL DELAY · 1ST REF DELAY · 1ST REF LEVEL · ER / REVERB.
Era 5 only: FEEDBACK GAIN · FEEDBACK DELAY · FEEDBACK HF RATIO · SPACE MOD 0–10.
Display: tap pattern graph (reflections over time) that reacts to mode and room size.

**4.3 DELAY**
DELAY L and R 0.1–900 ms (era 5: 1,400 ms) with LINK button · TEMPO SYNC toggle (shows note values) · LEVEL 0–100 % · INITIAL DELAY · era 5: FEEDBACK −99…+99 % (bipolar) and LPF 1–16 kHz/THRU. Display: two-tap delay graph.

**4.4 ECHO**
ECHO L and R 0.1–450 ms with LINK · FEEDBACK 0–99 % (era 5: ±99 %) · HIGH DAMP 0.1–1.0 · TEMPO SYNC · LEVEL. Display: repeats decaying and darkening.

**4.5 MODULATION**
Type buttons (§3). Knobs: LFO RATE 0.1–20 Hz (era 5: 0.1–40) with SYNC · DEPTH 0–100 % · DM DEPTH · AM DEPTH · BASE DELAY (flange 0.1–100 ms, era 5: 0.1–5 ms; phasing 0.1–8 ms) · FEEDBACK 0–99 % · REVERB TIME 0.3–10 s (reverb flange only) · LPF 1–16 kHz/THRU. LFO scope shows the waveform. Per type: flange = rate, depth, delay, feedback; reverb flange = rate, depth, delay, reverb time; chorus = rate, DM, AM; phasing = rate, depth, delay; tremolo = rate, depth; symphonic = rate, depth.

**4.6 MORE** with four sub-buttons:
- **PITCH:** PITCH A · B · C buttons; SHIFT −12…+12 st; FINE −100…+100 ct; DELAY 0.1–400 ms; FEEDBACK 0–99 %; BASE KEY (MIDI) toggle.
- **PAN:** DIRECTION L→R · R→L · MIDDLE-OUT; RATE 0.1–40 Hz; DEPTH 0–100 %.
- **GATE:** LEVEL; TRIGGER DELAY 0.1–1,000 ms; HOLD 1–24,000 ms; RELEASE 3–24,000 ms; MIDI TRIGGER toggle; gate envelope graph.
- **COMBI:** read-only routing diagram with arrow order (e.g. Symphonic → Reverb → Gate) and a selector for combination programs 91–99.

**4.7 Overlays (centre only)**
- **ADVANCED:** DENSITY 1–4 · LPF 1–16 kHz/THRU · SPACE MOD 0–10 · SECONDARY TIME ×0.1–10 · SECONDARY DELAY 0–500 ms · SECONDARY LEVEL 0–100 % · ER/REVERB 0–100 % · GATE LEVEL · CHARACTER 0–100 % · DITHER (era 5) · WET R INVERT · MUTE.
- **FILTERS:** HPF OFF · 50 · 100 · 200 · 400 Hz; LPF OFF · 4 · 6 · 8 · 10 kHz; plus a small response graph.
- **MIXER:** four vertical faders with ON buttons: DIRECT · EARLY REFLECTION · REVERBERATION · MASTER (−∞…+6 dB).
- **PRESET BROWSER:** bank buttons FACTORY · USER · COMBI; scrolling list with number and name; search field; category chips (Hall, Plate, Room, Spring, ER, Delay, Echo, Mod, Combi); favourite star; RECALL · STORE AS · RENAME · COPY · IMPORT · EXPORT buttons.
- **SETTINGS (modal):** QUALITY (native-rate | eco) · WET INPUT default · LATENCY readout · MIDI program change, channel, tables (optional) · UI SCALE · TOOLTIPS · THEME · ABOUT (version, AGPLv3 licence, disclaimer: "Independent project inspired by classic 1980s digital reverbs; not affiliated with Yamaha Corporation").

**4.8 Right rail EQ in detail**
- ANALOG (both eras, wet only, not stored): EQ ON · LOW FREQ 50–700 Hz / GAIN ±15 dB · MID FREQ 350 Hz–7 kHz (era 5: to 5 kHz) / GAIN · HIGH FREQ 2–20 kHz / GAIN.
- DIGITAL (era 5, stored per preset): EQ ON · LOW FREQ 32 Hz–2.2 kHz, GAIN, Q 0.1–5, PEAK/SHELF · MID FREQ 250 Hz–5.6 kHz, GAIN, Q · HIGH FREQ 500 Hz–16 kHz, GAIN, Q, PEAK/SHELF.
- MIX (DIRECT ↔ REV, centre = 1:1) and OUTPUT (−∞…+12 dB) knobs stay below the EQ.

**4.9 Era 7 vs era 5**
- Era 7 shows: analogue EQ only, RT to 10 s, no secondary reverb, no dither, no digital EQ, delay without feedback.
- Era 5 adds: DIGITAL EQ switch, RT to 99 s, secondary reverb, dither, ER feedback and space mod, delay feedback and LPF, PAN A/B modes, Pitch/Pan/Gate/Combi pages.
- Hide, never grey out. Switch caption: **7** (small: 1985) and **5** (small: 1987).

## 5. Prompts (paste in this order)

### Prompt 1: master chassis and Reverb tab

```text
Design a plugin UI for YREV57, a free open-source reverb plugin, on a
2000 x 800 px canvas (2.5:1).

ATTACHED REFERENCES
- Visual direction: the two attached concept images (A and B). I approve
  their look: black brushed-metal rack panel, engraved cream labels, round
  knobs with tick marks and white pointer lines, boxed numeric values under
  knobs, green backlit LCD, red 7-segment memory number, square lamp
  buttons, LED level meter, green-phosphor EQ graph. Keep this style
  exactly. Do not copy any hardware layout. Do not use the Yamaha name or
  logo anywhere.
- Hardware photos (REV5 panel, REV1 remote RCR-1, REV7 panel): used only
  to learn which controls exist. Do not copy their layout.

THE CHASSIS (identical on every screen I will ask for later)
Top bar (110 px): YREV57 wordmark; 2-digit red 7-segment memory number;
2 x 16 green LCD (program name over "parameter = value"); PREV, NEXT,
PRESET dropdown; STORE, COMPARE, INIT; BYPASS lamp button; ERA switch
labelled "7 | 5" (tiny captions 1985 / 1987); settings cog.
Tab bar (60 px): REVERB, EARLY REF, DELAY, ECHO, MODULATION, MORE. One lit.
Left rail (230 px wide): INPUT knob (dial 0-10), stereo L/R LED meters
(CLIP, 0, -3, -6, -10, -15, -20, -30), CLIP LATCH button, MONO | STEREO
switch with lamps.
Right rail (620 px wide): EQ block (EQ ON button, 3-node EQ graph, LOW /
MID / HIGH FREQ and GAIN knobs), then MIX knob (DIRECT to REV) and OUTPUT
knob.
Bottom bar (100 px): shared readout LCD ("PARAMETER = value"), three
overlay buttons ADVANCED, FILTERS, MIXER, and status text
"7 | STEREO | LATENCY 0 ms".
Centre area (1150 x 530 px): changes with the tab and buttons.

VISUAL STYLE: FRONT PANEL OF A 1980s RACK-MOUNT STUDIO UNIT
The whole interface must look like the front panel of a physical rack
unit, photographed straight on. It is NOT a flat web app or a modern
plugin skin.
- Surface: one black brushed-metal panel (near-black, about #151515 to
  #1d1d1d, fine horizontal brushing), four corner screws/rack-ear holes,
  thin engraved divider lines and recessed section frames with the
  section title engraved in the top-left of each frame (e.g. "REVERB",
  "WET EQ").
- Lettering: cream/off-white engraved capitals (about #e8e2cf), a clean
  condensed sans-serif, evenly spaced; small tick-mark scales and min/max
  numbers printed around every knob.
- Knobs: black cylindrical knobs with a ribbed or knurled edge, a white
  pointer line, tick marks around them, subtle highlight on top. No flat
  circles, no arcs or sliders drawn as web widgets.
- Value boxes: small dark rectangles with a thin white outline directly
  under each knob, cream digits.
- Buttons: square, slightly raised black keys with a small lamp window
  above the label. Lit lamp = red (function on) or amber (utility on);
  unlit = dark red/dark amber. Pressed key looks slightly recessed.
- Displays: a 2 x 16 character LCD with a yellow-green backlit background
  (about #b5e04a) and dark green characters, in monospaced dot-matrix
  style; a separate 2-digit red 7-segment memory display (about #ff2a1a).
  The shared readout bar is a second smaller LCD of the same type.
- Meters: vertical LED bars, green then yellow then red at the top, with
  engraved dB labels (CLIP, 0, -3, -6, -10, -15, -20, -30).
- Graphs (EQ, tap pattern, LFO, gate): green phosphor screens (dark green
  background, bright yellow-green trace, small square control nodes)
  inset into the panel like a small CRT window.
- Overlays and dialogs: look like a second recessed engraved panel or a
  sub-plate laid over the centre area, with the same lettering, knobs,
  keys and LCD style. Never white pop-up cards, drop-shadow web modals,
  rounded gradient buttons, emoji or web icons (the settings cog is an
  engraved cog on a key).
- Lighting: even, straight-on, a little depth (soft inner shadows for
  recesses, tiny highlights on knob tops). No glare, no photographic
  background, no 3D perspective.
- Branding: only the wordmark "YREV57 digital reverberator". No Yamaha
  name, logo or artwork.

RULES
- Every control appears once on the whole screen. No duplicates, no
  decorative controls, no hardware key clutter (no INITIAL DELAY / PARAM /
  LEVEL keys).
- Every knob has tick marks, min and max numbers, and a boxed value with
  its unit. Minimum label height 16 px.
- On/off = lamp button; 2-8 choices = lamp buttons in a row; knobs for
  anything swept.
- Hide controls that do not apply; never grey them out.

THIS IMAGE: the REVERB tab, era 7.
Centre area: 8 knobs in two rows of four: REVERB TIME 0.3-10 s, HIGH RATIO
0.1-1.0, LOW RATIO 0.1-2.4, PRE-DELAY 0.1-100 ms, 1ST REF DELAY 0.1-100 ms,
1ST REF LEVEL 0-100 %, DIFFUSION 0-10, ER / REVERB 0-100 %. Below them a
row of 8 named REVERB MODE buttons (Large Hall, Small Hall, Vocal Plate,
Percussion Plate, Spring, Echo Room, Strings, Snare) and a 4-BAND RT
toggle button at the right end.
Show realistic values (RT 2.6 s, Hi 0.7, Lo 1.2, etc.), program "LARGE
HALL" memory 01, EQ ON lit, analogue EQ node curve, MIX 35 %.
```

### Follow-up template (use for every later image, in the same conversation)

```text
Same chassis, same 1980s rack-unit front-panel look (black brushed metal,
engraved cream lettering, ribbed black knobs with pointer lines and tick
marks, boxed values, lamp keys, green LCD, red 7-segment, LED meters,
green-phosphor graphs), same 2000 x 800 canvas as the previous image.
Change ONLY the centre area [and anything stated below]. Do not move,
resize, restyle or remove anything in the top bar, tab bar, left rail,
right rail or bottom bar, except the lit tab/lamp states. Every control
appears once, with boxed value and units. [SCREEN SPEC FROM LIST BELOW]
```

Screen specs to substitute (each is one image):

1. **Reverb, 4-band:** "REVERB tab, 4-BAND RT toggle lit. The HIGH and LOW RATIO knobs become four knobs: HIGH, MID-HIGH, MID-LOW (reverb time, 0.3-10 s), LOW. Add two crossover button groups: 4K 2K 1K and 500 250 125. Remaining knobs unchanged."
2. **Early Ref, era 7:** §4.2 minus the era-5-only items.
3. **Early Ref, era 5:** "Era switch on 5. EARLY REF with era-5 items: PAN A/PAN B modes, E/R NUMBER up to 34, feedback gain/delay/HF ratio, space mod. Right rail shows ANALOG | DIGITAL EQ switch."
4. **Delay:** §4.3. 5. **Echo:** §4.4.
6. **Modulation, Chorus A selected:** §4.5; show only chorus controls and the LFO scope.
7. **Modulation, Flange selected:** same layout, flange controls.
8. **More > Pitch**, 9. **More > Pan**, 10. **More > Gate**, 11. **More > Combi:** §4.6, all with the four sub-buttons shown, one lit.
12. **ADVANCED overlay** (button lamp lit, over the REVERB tab): §4.7.
13. **FILTERS overlay**, 14. **MIXER overlay:** §4.7.
15. **Digital EQ (era 5):** right rail with ANALOG | DIGITAL switch on DIGITAL, Q and PEAK/SHELF controls, taller EQ graph; centre shows the REVERB tab with era-5 values (RT 6.4 s).
16. **Preset browser overlay**, 17. **Settings dialog**, 18. **About panel:** §4.7.
19. **Compact layout** (1250 x 500): "Same design scaled to 1250 x 500 with only the controls a user touches most (RT, size, mix, EQ on, preset, bypass), everything else behind tabs."

### Tips

- Generate **image 1 first and approve it**; if it drifts, fix it before asking for more. Attach the approved image to every follow-up.
- Keep the two concept images attached as "visual direction", and say once that they show only the Reverb tab.
- If a tool ignores the 2.5:1 ratio, repeat "2000 x 800 px, exactly 2.5:1, no cropping" in the prompt.
- If a result drifts toward a flat "web app" look, add: "It must look like a photographed hardware front panel, not a flat UI. Keep the knobs, keys, LCD and brushed metal exactly as in the attached approved image."
- Ask for a **component sheet** at the end: knob, button (off/on/hover/disabled), segmented row, boxed value, meter, graph, overlay frame.
- Ranges are working values from the research; open conflicts (room size, Hi ratio, EQ gain) may change.
