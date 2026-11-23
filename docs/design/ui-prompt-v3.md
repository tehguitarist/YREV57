# UI prompt v3: one main screen, no REV1-only features

> **Status:** the approved layout has been built. Where this prompt and the built UI differ, [ui-layout.md](ui-layout.md) and the prototype win. Changes since this was written: the top bar ends with BYPASS and a **?** key (tooltips on/off); the 7 and 5 keys use smaller numerals; the EQ row is ANALOG, ON, DIGITAL, ON; MIDI is dropped from settings; the display panel's legend and meters follow the prototype.

Supersedes [ui-prompt-v2.md](ui-prompt-v2.md). Inventory: [ui-controls.md](ui-controls.md). Approved look: [ui-concepts/](ui-concepts/).

## 1. Scope decisions

**Cut (REV1 remote only):** four-band reverb time (HIGH / MID-HIGH / MID-LOW / LOW) and crossover buttons; REV MODE 1–8; DISPLAY buttons (W/F, F/C, R/T, RATIO, E/R, REV, P/S, M); HPF 50/100/200/400 and LPF 4K/6K/8K/10K selector buttons; four-fader mixer (DIRECT / E.R. / REVERBERATION / MASTER); AUTO buttons and knob-position strips; D1/D2 readouts; FUNCTION and PANEL keys; PRESET 1–8 direct keys; IN/OUT meter switch.

**Kept (REV7, REV5 and plugin essentials):** everything else in [ui-controls.md](ui-controls.md), including the REV5's own **LPF 1–16 kHz / THRU**, early-reflection feedback, secondary reverb, density, space modulation, gate, digital EQ, dither, pitch and pan.

**Graph rule:** the **EQ graph is the only graph** in the UI. No reverb-time curve, tap-pattern display, delay or echo graphs, LFO scope or gate-envelope graph, and no room-size chart. The space they would have used goes to larger knobs.

**Program rule:** there is **no program-number display** (no 7-segment memory number); the LCD shows the preset name. Choosing, naming, storing, copying and every other memory-style function live in the **preset browser** (dropdown) and the PREV / NEXT / STORE / COMPARE / INIT buttons. There is no typing of a memory number, and no number is shown.

**Layout rule: avoid page flipping.** There are no tabs and no overlays for controls.
- Reverb, early reflections, tail/advanced controls, gate, EQ, mix and output are **always visible** on one screen.
- Exactly **two** in-place swaps exist:
  1. the **FX slot** (DELAY | ECHO | MOD | PITCH | PAN | OFF) in the third column;
  2. the **EQ ANALOG | DIGITAL** toggle (era 5 only).
- The only other windows are the **preset dropdown** (anchored under the top bar) and the **settings dialog** (rare).
- Controls that do not exist in the current era are **not removed and not moved**: their slot shows an engraved blank plate, so positions never shift between eras.

**Design decision this implies (record as an ADR before building the engine):** the original units run one effect type per program, but the REV5's combination programs chain effects. The plugin lets the reverb (with early reflections) run together with **one optional FX slot**, with an ORDER switch (FX → REV or REV → FX). Hardware-faithful presets just set the slot to OFF or to the single type they use. This also removes the need for separate REV / E/R "types".

## 2. Layout at 2500 × 1000 (2.5:1)

```
x:    0      240            800            1360          1880        2500
y:0   ┌──────────────────────────────────────────────────────────────┐
      │ TOP BAR (h 160): wordmark | 7 5 | DISPLAY PANEL | 8 keys     │
      │  display panel = OUT meter · MONO/STEREO · LCD (narrow)      │
y166  ├──────┬──────────────┬──────────────┬─────────────┬───────────┤
      │      │ REVERB       │ EARLY        │ FX SLOT     │ EQ        │
      │INPUT │ 8 knobs      │ REFLECTIONS  │ all buttons │ (analog / │
      │ w240 │ (centred)    │ keys on top, │ on top,     │  digital) │
      │ IN   │ w560         │ knobs centred│ knobs below │ graph     │
      │meter │              │ w560         │ (centred)   │ ───────── │
y750  │knob  ├──────────────┴──────────────┴─────────────┤ MIX OUT   │
      │(ctrd)│ TAIL · GATE · DUCKER strip (h 192)        │ WIDTH CHAR│
y942  ├──────┴───────────────────────────────────────────┴───────────┤
      │ BOTTOM BAR (h 44): version number at the far right           │
y1000 └──────────────────────────────────────────────────────────────┘
```

Compact version later: 1250 × 500, same proportions.

## 3. Contents of each zone

**TOP BAR (h 160), all on one row, left to right:** YREV57 wordmark and "digital reverberator" · **ERA keys**: two **square** keys in the same style as the other keys (raised black key, lamp window on top), carrying just a big "7" or "5" (no year captions); exactly one lit · **one connected DISPLAY PANEL** (a single recessed black glass window with a thin bezel, sections divided by fine engraved lines, like the display window on the real units) holding the **OUT stereo LED meter** (L, R, its own dB scale CLIP, 0, −3, −6, −10, −15, −20, −30; engraved OUT beneath; one clickable area over both CLIP lamps releases the L and R holds together), a **MONO / STEREO / TRUE STEREO backlit legend** (three stacked words, clickable, the active one lit red, STEREO by default; this one stays rectangular because it is a word legend) and the **2 × 16 green LCD** (name / "parameter = value"; an asterisk after the name means edited and not stored). The panel is only as wide as these need · then **seven square keys in one row**, same style, each with a lamp: PREV · NEXT · PRESET ▼ · STORE · COMPARE · INIT · SETUP (cog) · then, **all the way to the right after a clear extra gap and a thin vertical divider**, the **BYPASS key**, identical in style to the other square lamp keys (not a different switch).

**INPUT RAIL (left, 240 px):** a tall stereo **IN** LED meter (L, R, own dB scale CLIP, 0, −3, −6, −10, −15, −20, −30; engraved IN beneath; **one clickable area over both CLIP lamps releases the L and R clip holds together** (the IN and OUT meters reset independently)) with a **smaller** INPUT knob (dial 0–10) below it with generous space between, as on the real panels. The meter and knob are one assembly, **centred vertically** in the rail.

**REVERB (column 1):**
8 knobs, 2 rows of 4 with comfortable breathing room between the rows: REVERB TIME 0.3–10 s (era 5: to 99 s) · HIGH RATIO 0.1–1.0 · LOW RATIO 0.1–2.4 · PRE-DELAY 0.1–100 ms · 1ST REF DELAY 0.1–100 ms · 1ST REF LEVEL 0–100 % · DIFFUSION 0–10 · ER / REVERB 0–100 %.
No graph. The whole knob block is **centred vertically** in the column.

**EARLY REFLECTIONS (column 2):**
Mode buttons (**square keys** in two rows of six, at the top of the column): SMALL HALL · LARGE HALL · RANDOM · REVERSE · PLATE · SPRING (+ era 5: PAN A · PAN B and the rehearsal-room patterns PIANO · ORGAN · BRASS · GUITAR).
Knobs: ROOM SIZE 0.1–20 (era 5: 0.1–25) · LIVENESS 0–10 · E/R NUMBER 1–18 (era 5: 1–34, stepped).
Era 5 knobs (blank plates in era 7): FEEDBACK GAIN · FEEDBACK DELAY · FEEDBACK HF RATIO.
No graph. The knob block is **centred vertically** in the space under the keys.

**FX SLOT (column 3):**
**All buttons live at the top, as square keys.** Row 1: DELAY · ECHO · MOD · PITCH · PAN · OFF (one lit; PITCH and PAN era 5 only). Row 2: ORDER keys FX → REV | REV → FX (these two are slightly **rectangular**, wider than tall, so the text is not cramped), a thin divider, then the **contextual keys for the selected type**: LINK L/R and TEMPO SYNC (Delay), LINK L/R and TEMPO SYNC (Echo), LFO SYNC (Mod); none for Pitch, Pan or Off. A third row appears only where the type has its own selector (Mod types, Pitch A/B/C, Pan direction). Below the buttons, the knobs for the chosen type are **centred vertically** in the remaining space; the knob area swaps in place:
- **DELAY:** DELAY L and R 0.1–900 ms (era 5: 1,400 ms) · LEVEL; era 5: FEEDBACK −99…+99 % and FX LPF. (There is no second initial-delay knob: PRE-DELAY in the Reverb column is the only one.)
- **ECHO:** ECHO L and R 0.1–450 ms · FEEDBACK 0–99 % (era 5: ±99 %) · HIGH DAMP 0.1–1.0 · LEVEL.
- **MOD:** type keys FLANGE · REV FLANGE · CHORUS A · CHORUS B · PHASING · TREMOLO · SYMPHONIC; knobs LFO RATE 0.1–20 Hz (era 5: 40) · DEPTH · DM DEPTH · AM DEPTH · BASE DELAY (range follows the type: flange 0.1–100 ms, era 5 flange 0.1–5 ms, phasing 0.1–8 ms) · FEEDBACK · REVERB TIME (rev flange) · FX LPF 1–16 kHz/THRU (one knob shared by Delay and Mod), with only the knobs relevant to the chosen type lit.
- **REVERB:** a 4 × 2 grid of large knobs: reverb time, high, low, pre-delay; 1st ref delay and level (centre), diffusion, ER/reverb. **E/R:** mode keys and room size, liveness, number. The tail, gate and ducker strip, the side 1st reflections and E/R feedback moved into the **INT PARAM** drawer ([ui-layout.md](ui-layout.md)).
- **PITCH (era 5):** PITCH A · B · C keys; SHIFT −12…+12 st; FINE −100…+100 ct; DELAY 0.1–400 ms; FEEDBACK 0–99 % (A only); second row SHIFT 2, FINE 2, DELAY 2 (B's second voice, C's right voice; greyed in A). Mod and pitch use the smaller `s` knobs so two rows fit under their selector keys. Delay adds FEEDBACK L/R and HIGH (era 5); echo adds FEEDBACK R (era 5).
- **PAN (era 5):** DIRECTION keys L→R · R→L · L↔R (back and forth); RATE 0.1–40 Hz; DEPTH 0–100 %.
- **OFF:** blank plate.

**TAIL, GATE and DUCKER strip (under columns 1–3), one row of small knobs, three groups with fine dividers:**
TAIL: DENSITY 1–4 · SPACE MOD 0–10 · REVERB LPF 1–16 kHz/THRU · SECONDARY TIME ×0.1–10 · SECONDARY DELAY 0–500 ms · SECONDARY LEVEL 0–100 % (secondary reverb and space mod are blank plates in era 7) | GATE: LEVEL · TRIGGER DELAY 0.1–1,000 ms · HOLD 1–24,000 ms · RELEASE 3–24,000 ms | DUCKER: DUCK RANGE −60…0 dB · THRESHOLD −80…0 dB · ATTACK 0–1,000 ms · RELEASE 0–1,000 ms, then a square **DUCK ON** key at the right-hand end of the group. The ducker lowers the reverb while the dry input is over the threshold. **No MIDI trigger key** (DAW automation covers it). (SPACE MOD is a single parameter; it appears once, here, and is era 5 only.)

**EQ (column 4):**
A row of four square keys: ANALOG, ON, DIGITAL, ON. ANALOG and DIGITAL choose which EQ is shown; each ON key switches its own EQ and always shows its state, so there is no hidden EQ (in era 7 the DIGITAL pair is blank plates) · a tall 3-node green-phosphor EQ graph that fills the column's free height, leaving a little padding below the EQ knobs · LOW / MID / HIGH FREQ and GAIN knobs.
- Era 7: analogue only: LOW 50–700 Hz, MID 350 Hz–5 kHz (Q-26), HIGH 2–20 kHz, gain ±15 dB.
- Era 5: **ANALOG | DIGITAL** toggle. DIGITAL adds Q 0.1–5 per band and PEAK/SHELF buttons on LOW and HIGH; LOW 32 Hz–2.2 kHz, MID 250 Hz–5.6 kHz, HIGH 500 Hz–16 kHz.

**MIX, OUTPUT, WIDTH and CHARACTER (column 4, below the EQ, separated from it by a thin divider line sitting very close above the knob row):** MIX knob (DIRECT ↔ REV, centre = 1:1) · OUTPUT knob (−∞…+12 dB) · **WIDTH knob** 0–200 % (100 = unchanged, 0 = mono; wet stereo width) · CHARACTER knob 0–100 %.

**GLOBAL KEYS (column 4, bottom row, square keys):** DITHER (era 5) · WET R INVERT · WET SOLO (output 100 % wet, bypassing the mix control; lit amber) · MUTE.

**BOTTOM BAR:** only the **version number** ("v0.0.1") at the far right. No status text (era, wet input and latency are already shown elsewhere). There is no second readout; the top LCD's second line shows "PARAMETER = value" for whatever is touched.

**Preset dropdown (anchored under the PRESET button, not a page):** bank buttons FACTORY · USER · COMBI; scrolling numbered list; search field; category chips; RECALL · STORE AS · RENAME · COPY · IMPORT · EXPORT.

**Settings dialog (modal):** QUALITY (native-rate | eco) · WET INPUT default · LATENCY · RANDOM or FROZEN NOISE SEED · OUTPUT SAFETY LIMITER on/off · UNDO / REDO (also Cmd/Ctrl-Z) · UI SCALE · TOOLTIPS · THEME · ABOUT (version, AGPLv3 licence, disclaimer: "Independent project inspired by classic 1980s digital reverbs; not affiliated with Yamaha Corporation").

## 4. Coverage check (every inventory item against this layout)

Checked section by section against [ui-controls.md](ui-controls.md). Fixes made during the check are folded into §3 and the prompts above.

| Inventory section | Where it lives | Status |
| --- | --- | --- |
| §2 Global (input, wet input, mix, bypass, mute, out phase R, output level, era, character, dither) | Left rail, top bar, column 4 | Covered |
| §3 Meters and indicators (level meter, clip, MONO, EQ ON, key lamps; the memory number is dropped) | Left rail, top bar, EQ block | Covered |
| §4 Display (LCD, parameter readout) | Top bar LCD only (line 1 name, line 2 parameter = value) | Covered; the duplicate bottom readout was removed |
| §5 Programs (recall, up/down, store, name, banks, compare, init, copy, browser, import/export, edited marker) | Top bar and preset dropdown (all memory functions live in the browser) | Covered. Typing a program number is deliberately dropped (browser instead); the edited asterisk was added |
| §5 R5 direct-recall keys (REV1–4, E/R1–2, OTHERS, USER MEMORY) | Preset dropdown (USER bank, favourites) | **Merged, not a separate row of keys.** Say if you want a favourites strip in the top bar |
| Era (plugin-only) | Two square lamp keys, 7 and 5, beside the display panel in the top bar | Covered; replaces the 7-segment memory display |
| §6 Effect types | Reverb and E/R always on; FX slot for the rest | Covered; see the "design decision" in §1 |
| §6.2 E/R modes | Early reflections column | Covered after fix: era-5 rehearsal-room patterns were missing |
| §7 Reverb (RT, hi, lo, pre-delay, 1st ref delay and level, diffusion, mix) | Reverb column | Covered |
| §8 Early reflections (mode, size, liveness, number, balance, feedback ×3, space mod) | Column 2 and tail strip | Covered. Diffusion is one shared knob (Reverb column), not duplicated |
| §9 Delay and §10 Echo | FX slot | Covered after fix: removed a second initial-delay knob that duplicated pre-delay |
| §10 Modulation (types, rate, depth, DM, AM, base delay, feedback, rev-flange time, LPF) | FX slot | Covered after fix: two knobs were both called "LPF"; now REVERB LPF and FX LPF |
| §11 Pitch and pan | FX slot (era 5) | Covered |
| §12 Gate (level, trigger delay, hold, release) and routing order | Tail, gate and ducker strip; ORDER keys | Covered. MIDI trigger dropped (DAW automation covers it); pitch BASE KEY (also MIDI) dropped for the same reason |
| Ducker and stereo width (plugin-only, DSP ported from REV-246) | Ducker group in the strip; WIDTH knob beside MIX and OUTPUT | Added |
| §13 EQ analogue and digital (freq, gain, Q, peak/shelf, on) | Column 4 | Covered after fix: analogue and digital EQ each need their own on/off |
| §14 Internal parameters (density, LPF, space mod, secondary reverb, balance, gate level) | Tail strip, Reverb column | Covered |
| §17 Utility (quality, latency, UI scale, tooltips, undo; MIDI dropped) | Settings dialog | Covered after fix: noise seed, safety limiter and undo/redo added. Footswitch range is dropped (no plugin equivalent) |
| §18 About / legal | Settings dialog | Covered |
| §21 Plugin-only (automation, resize, A/B, text entry) | Whole UI | Covered (automation is automatic, resize via compact layout, A/B via COMPARE) |

**Deliberately left out:** typing a program number, every graph except the EQ (reverb-time curve, tap pattern, delay/echo/LFO/gate displays), the room-size chart, the four program-change tables, MIDI learn, and the REV1-only items listed in §1.

**Behaviour notes the UI must show:** ER / REVERB at 100 % gives early reflections only (the original "E/R" programs); at 0 % it is reverb only. Knob ranges change with the era (for example mid EQ top frequency, room size, delay time), so the printed min/max numbers should change with the era switch.

## 5. Prompts

### Prompt 1: master image (era 7, FX slot = DELAY)

```text
Design a plugin UI for YREV57, a free open-source reverb plugin, on a
2500 x 1000 px canvas (exactly 2.5:1, no cropping).

ATTACHED REFERENCES
- Visual direction: the attached concept images. I approve their look.
  They show only a reverb page; do NOT copy their layout or tabs.
- Hardware photos (REV5 panel, REV7 panel): used only to learn which
  controls exist. Do not copy their layout. Do not use the Yamaha name or
  logo anywhere.

VISUAL STYLE: FRONT PANEL OF A 1980s RACK-MOUNT STUDIO UNIT
It must look like the straight-on front panel of a physical rack unit, not
a flat web app or a modern plugin skin.
- Surface: one black brushed-metal panel (about #151515 to #1d1d1d, fine
  horizontal brushing), four corner screws, thin engraved divider lines,
  recessed section frames with the section title engraved at top-left.
- Lettering: cream engraved capitals (about #e8e2cf), clean condensed
  sans-serif; tick-mark scales with min/max numbers around every knob.
- Knobs: black cylindrical knobs with ribbed edge, white pointer line,
  tick marks, subtle top highlight.
- Value displays: under EVERY knob a little recessed black LED screen
  (small rectangle, thin dark bezel) showing the value in glowing red
  LED digits with the unit. Not white-outlined boxes.
- Buttons: ALL buttons are SQUARE raised black keys with a lamp window
  above the label (lit lamp = red for function on, amber for utility on;
  unlit = dark). Exceptions that stay rectangular: the MONO / STEREO / TRUE STEREO
  word legend and keys in the settings dialog with long labels.
- Displays: 2 x 16 character LCD, yellow-green backlight (about #b5e04a),
  dark green dot-matrix characters. There is only one LCD on the whole
  panel and no 7-segment display. Lit legends and keys glow red (about
  #ff2a1a).
- Meters: vertical LED bars, green then yellow then red, engraved dB.
- The EQ graph is the ONLY graph: a green phosphor window (dark green
  background, bright yellow-green trace, small square nodes) inset into
  the panel. Do not add any other graphs, curves, scopes or charts.
- Lighting: even, straight-on, soft inner shadows for recesses. No glare,
  no 3D perspective, no web cards, no drop-shadow modals, no emoji or web
  icons (the settings cog is an engraved cog on a key).
- Branding: only "YREV57 digital reverberator".

LAYOUT: ONE SCREEN, NO TABS, NO OVERLAYS FOR CONTROLS
Top bar (160 px), everything on ONE row, left to right: wordmark; two
SQUARE era keys "7" and "5" (same style as the other keys: raised black
square key, lamp window on top, a big numeral and NO year caption, one lit
red); then ONE CONNECTED DISPLAY PANEL: a single recessed
black glass window with a thin bezel, sections divided by fine engraved
lines, like the display window on the real units, holding only (left to
right): the OUT stereo LED meter (L R, its own dB scale CLIP / 0 / -3 /
-6 / -10 / -15 / -20 / -30, engraved OUT beneath), a MONO / STEREO / TRUE STEREO
backlit legend (three stacked words, the active one lit red), and the 2 x 16 green
LCD (name with an asterisk when edited, e.g. "LARGE HALL *"). Keep the LCD
section compact, not wide. To the right of the panel, seven SQUARE keys in
the same style in one row: PREV, NEXT, PRESET (dropdown), STORE, COMPARE,
INIT, SETUP (cog). Then, all the way at the far right of the bar after a
clear extra gap (no divider line), the BYPASS key, the same
square lamp key as all the others. No memory-number display.
Left rail (240 px) titled INPUT: a tall stereo IN LED meter (L R, its
own dB scale, engraved IN beneath; the CLIP lamps at the top are
clickable) with a SMALL INPUT knob (0-10) below it, with generous space
between, the whole meter-and-knob assembly centred vertically in the rail.
Four columns to the right of the left rail, about 585 px tall:
1) REVERB (560 px): 8 knobs in 2 rows of 4 with comfortable space
    between the rows, the whole block centred
    vertically in the column: REVERB TIME 0.3-10 s, HIGH RATIO 0.1-1.0,
    LOW RATIO 0.1-2.4, PRE-DELAY 0.1-100 ms, 1ST REF DELAY 0.1-100 ms,
    1ST REF LEVEL 0-100 %, DIFFUSION 0-10, ER / REVERB 0-100 %. No graph.
 2) EARLY REFLECTIONS (560 px): at the top, two rows of six SQUARE mode
    keys: SMALL HALL, LARGE HALL, RANDOM, REVERSE, PLATE, SPRING, then PAN
    A, PAN B, PIANO, ORGAN, BRASS, GUITAR (the second row as blank plates).
    Below, centred vertically in the remaining space: knobs ROOM SIZE
    0.1-20, LIVENESS 0-10, E/R NUMBER 1-18, and three more positions
    (FEEDBACK GAIN, FEEDBACK DELAY, FEEDBACK HF RATIO) as engraved blank
    plates. No graph.
 3) FX SLOT (520 px): ALL its buttons at the top as SQUARE keys. Row 1:
    DELAY, ECHO, MOD, PITCH, PAN, OFF (DELAY lit; PITCH and PAN as blank
    plates). Row 2: two slightly RECTANGULAR (wider than tall) ORDER keys "FX > REV"
    and "REV > FX", a thin divider, then LINK L/R and TEMPO SYNC. Below the buttons, centred
    vertically in the remaining space, the DELAY knobs: DELAY L and DELAY R
    0.1-900 ms, LEVEL, and FEEDBACK and FX LPF positions as blank plates.
    No graph.
 4) EQ (620 px): a row of four square keys (ANALOG, ON, DIGITAL, ON), a tall 3-node green EQ graph filling the
    free height of the column, LOW / MID / HIGH
    FREQ and GAIN knobs (analogue ranges: 50-700 Hz, 350 Hz-7 kHz, 2-20 kHz,
    +/-15 dB). Then a thin divider line, very close above a row of four
    knobs: MIX (DIRECT to REV), OUTPUT (-inf to +12 dB), WIDTH (0-200 %,
    centre 100) and CHARACTER. Bottom row of square keys: DITHER (blank
    plate), WET R INVERT, WET SOLO, MUTE.
Strip under columns 1-3 (190 px), one row of small knobs in three groups
with fine dividers, the whole row centred in the strip: TAIL: DENSITY 1-4, SPACE MOD 0-10 (blank plate),
REVERB LPF 1-16 kHz/THRU, SECONDARY TIME, SECONDARY DELAY, SECONDARY LEVEL
(these three as blank plates); GATE: LEVEL, TRIGGER DELAY 0.1-1,000 ms,
HOLD 1-24,000 ms, RELEASE 3-24,000 ms; DUCKER: DUCK RANGE -60..0 dB,
THRESHOLD -80..0 dB, ATTACK 0-1,000 ms, RELEASE 0-1,000 ms, then a square
DUCK ON key at the right-hand end of the group. No MIDI trigger key.
Bottom bar (44 px): only the version number "v0.0.1" at the far right. No
status text, no second LCD or readout.

CENTRING: every group of knobs or buttons is centred horizontally within
its section (including the last, shorter row of a wrapped knob block, the
strip's three groups as a whole, and the key rows).

INERT PLATES (prototype behaviour to specify in the UI): in era 7 the blank era-5 plates are not clickable, not focusable and cannot be changed from the keyboard.

BLANK PLATES: controls that exist only in era 5 stay in their slot as an
engraved blank metal plate with a faint outline and a tiny "5" caption,
so nothing moves when the era changes.

RULES
- Every control appears once. No duplicates, no hardware key clutter, no
  decorative controls.
- Every knob has tick marks, min/max numbers and a little red LED value
  screen with the unit. Minimum label height 18 px at this canvas size.
- On/off = square lamp key; 2-8 choices = square lamp keys in a row;
  knobs for anything swept.
- Realistic values (RT 2.6 s, Hi 0.7, Lo 1.2, Delay L 100 ms / R 200 ms,
  program LARGE HALL, era key 7 lit, EQ ON lit, MIX 35 %).
```

### Follow-up template

```text
Same chassis, same 1980s rack-unit front-panel look (black brushed metal,
engraved cream lettering, ribbed black knobs with pointer lines and tick
marks, little red LED value screens under every knob, square lamp keys, one connected display panel, green LCD, LED meters,
green-phosphor EQ graph only), same 2500 x 1000 canvas as the previous image.
Change ONLY what is stated below. Do not move, resize or restyle anything
else. Every control appears once, with an LED value screen and units.
[CHANGE FROM LIST BELOW]
```

### Changes to request, one image each

1. **FX = ECHO:** FX slot keys row 2 shows LINK L/R and TEMPO SYNC; knobs ECHO L and R 0.1–450 ms, FEEDBACK 0–99 %, HIGH DAMP 0.1–1.0, LEVEL.
2. **FX = MOD, CHORUS A:** type buttons FLANGE, REV FLANGE, CHORUS A (lit), CHORUS B, PHASING, TREMOLO, SYMPHONIC; lit knobs LFO RATE, DM DEPTH, AM DEPTH; the others as blank plates.
3. **FX = MOD, FLANGE:** lit knobs LFO RATE, DEPTH, BASE DELAY, FEEDBACK.
4. **Era 5 view (FX = DELAY):** ERA key 5 lit (7 unlit). Reveal PAN A/B and PIANO/ORGAN/BRASS/GUITAR modes, E/R NUMBER up to 34, the three ER feedback knobs, SPACE MOD, the three secondary-reverb knobs, DITHER, delay FEEDBACK and LPF, PITCH and PAN in the FX header. Show REVERB TIME up to 99 s (value 6.4 s).
5. **Era 5, FX = PITCH:** a third key row PITCH A/B/C; knobs SHIFT, FINE, DELAY, FEEDBACK. No LINK or SYNC keys.
6. **Era 5, FX = PAN:** DIRECTION (3 keys), RATE, DEPTH.
7. **Era 5, DIGITAL EQ:** ANALOG | DIGITAL toggle on DIGITAL; Q knobs and PEAK/SHELF keys on LOW and HIGH; taller EQ graph (the only graph in the UI).
8. **FX = OFF:** blank plate; ORDER switch dimmed to a blank plate.
9. **Preset dropdown open** under the PRESET button (§3).
10. **Settings dialog open** over a dimmed panel (§3).
11. **Compact** 1250 × 500: "Same design at 1250 x 500 keeping all controls, smaller knobs, labels at least 16 px."

### Tips

- Generate **image 1 first and approve it**. Attach the approved image to every follow-up.
- If a result drifts toward a flat web look, add: "It must look like a photographed hardware front panel, not a flat UI. Keep the knobs, keys, LCD and brushed metal exactly as in the attached approved image."
- If the 2.5:1 ratio is ignored, repeat: "2500 x 1000 px, exactly 2.5:1, no cropping."
- If it is too crowded, keep all controls but ask for "smaller knobs and tighter spacing, not fewer controls".
- Ranges are working values from the research; open conflicts (Q-02 room size, Q-03 Hi ratio, Q-04 EQ gain) may change.
