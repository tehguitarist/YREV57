# Plugin UI layout (frozen reference)

Status: **approved and built** (prototype v13). The JUCE editor follows this layout; the prototype remains the visual source of truth.

- Visual reference: [prototype/layout-v13-skinned.png](prototype/layout-v13-skinned.png) (era 7, FX = Delay) and the interactive [prototype/index.html](prototype/index.html).
- Positions: [ui-layout.json](ui-layout.json), generated from the prototype. Design units are pixels in the **2500 × 1000** panel (the 250 % size); `rect` is `[x, y, w, h]`. Snapshots cover era 7 with each FX slot type (delay, echo, mod, off) and era 5 (every era-5 control enabled). Controls that only exist in some states differ between snapshots; match them by label.
- Images and key-state rules: [skin-assets.md](skin-assets.md), assets in `assets/ui/`.
- Behaviour and wording: [ui-prompt-v3.md](ui-prompt-v3.md), [ui-controls.md](ui-controls.md).

Build notes:

- Lay out in design units and scale the whole editor uniformly (aspect locked 2.5:1, resizable).
- Draw legends, LED value screens, LCD, meters and the EQ graph in code with embedded OFL fonts (Barlow Condensed, VT323); images are only the background, keys and knob cap.
- Value screens are click-to-type with validation; clip lights reset per meter pair; era-5-only controls are inert plates in era 7.
- If the prototype changes, regenerate the JSON rather than editing it by hand.

## Implementation (JUCE)

- `src/plugin/YrevEditor.*`: the editor. A 2500 × 1000 canvas scaled to the window (resizable, 2.5:1, default 1250 × 500). Draws frames, LCD, display panel, meters, EQ graph and labels in code; hosts the keys and knobs.
- `src/plugin/ui/UiLayout.h`: **generated** by `python3 tools/gen_ui_layout.py` from `ui-layout.json` and the knob specs in the prototype. After any prototype layout or tooltip change run `python3 tools/export_ui_layout.py` (measures the prototype in headless Edge and rewrites `ui-layout.json`), then `python3 tools/gen_ui_layout.py` (rewrites `UiLayout.h`, `UiTooltips.h` and the prototype's tooltip table).
- Tooltip text lives in `docs/design/ui-tooltips.json`, one source for the plugin and the prototype. The `?` key next to Bypass turns tooltips on or off (saved with the plugin state).
- `src/plugin/ui/Skin*.{h,cpp}`: images and fonts (compiled in with `juce_add_binary_data`), the pushed-key button, the knob and LED value screen.
- Everything on the panel is live: every control is an APVTS parameter (saved, automatable), including the Pitch and Pan FX panels and the Digital EQ view. The non-reverb DSP is live (see below); the reverb engine is still a pass-through slot, so the reverb, early-reflection and FX controls change nothing audible yet.
- Presets (`Presets.*`): 15 generic factory presets (FACTORY and COMBI banks, generated from the prototype by `tools/gen_factory_presets.py`) and a USER bank stored as XML in the user's application-data folder (`YREV57/Presets`). Prev, Next, the browser (store as, rename, delete, import, export), Store, Compare and Init all work, and the presets are also exposed to the host as programs.
- Settings overlay: quality, wet input default, noise seed, era settings (per era or shared), limiter and UI scale are stored in the plugin state.
- A fresh instance shows INIT; it does not load a preset until one is chosen (a deliberate choice for now).
- Era limits: knob travel narrows to the current era (`kEraLimits` in `Parameters.h`: reverb time, room size, reflection count, delay L/R, LFO rate, analogue mid frequency, mod base delay, echo feedback), and the processor clamps the stored values to the same limits.
- Development aid: `YREV_SNAPSHOT_DIR=/tmp/shots build/YREV57Tests_artefacts/Release/YREV57Tests "[.snapshot]"` writes PNGs of the editor (era 7, era 5, Pitch, Pan, Digital EQ, preset browser and settings).

- EQ: the row reads [Analog] [On]   [Digital] [On]. Analog and Digital choose which EQ is shown; each On key switches its own EQ (`aeq_on`, `deq_on`) and always shows its state, so there is no hidden EQ. In era 7 only the analogue pair is active (the digital pair is blank plates).
- Era 5 to 7: when the era becomes 7 the processor (`enforceEraLimits`) turns the digital EQ off, returns the EQ view to analogue, and moves era-5-only choices back (ER modes 7 to 12 to Small Hall, Pitch and Pan FX to Delay), also after a state load. Going back to era 5 finds the digital EQ off. The engine should still ignore the era-5-only controls in era 7 as a second guard.

## DSP behind the panel (non-reverb)

`src/dsp/Chain.*` runs the wet path around the engine slot, following the signal flow in [architecture.md](architecture.md):

- Wet input: mono (left only), stereo (L+R summed and halved, so a centred signal keeps its level), or true stereo. Input level feeds the wet path only; the dry signal stays stereo and untouched.
- Wet-only analogue EQ (three fixed-width bells) before the engine; digital EQ (peak or shelf, with Q) after it, era 5 only.
- Wet R phase invert, width (the ported LR4 widener), then the ducker keyed from the dry mono level.
- Mix (linear), wet solo (forces 100 % wet), bypass (fades to the untouched dry signal, ignoring output level), output level, mute. Gains ramp over 20 ms.
- At the time of this note the converter model, `character`, dither, the output safety limiter, latency and tail reporting and the reverb engine were still to come; all are in now ([engine.md](engine.md)).

## INT PARAM drawer

The REV5 kept its fine controls behind an INT PARAM key; the plugin does the same. The **INT PARAM** key in the top bar (beside Setup) slides a drawer over the middle panels (`#intp` in the prototype, `kDrawer` and `kDrawerGroups` in `UiLayout.h`, knobs and keys with `view` 3). It holds the reverb tail (density, space mod, reverb LPF, secondary reverb), the side 1st reflections, E/R feedback, the gate and the ducker's four controls; its Duck on key sits on the front panel with Dither, Wet R invert, Wet solo and Mute. Click outside it, press Esc or press the key again to close it. An amber dot on the key means one of its controls is away from its default and active in the current era, so a hidden control never changes the sound without a sign on the front panel. Era-5-only drawer controls are blank plates in era 7, as on the front panel. Everything in the drawer stays automatable and is saved with presets. `tools/export_ui_layout.py` measures the drawer in its own snapshot (`era5_intparam`). The drawer's first row ends with an **FX routing** group holding Rev mix (`rev_mix`); the **FX WET** key (`fx_wet`, plugin only) sits beside the FX → Rev / Rev → FX keys (2026-11-22).

## Mode and type keys

E/R modes use large square `esq` keys (116 × 116 design px, 19 px legends), four to a row over three rows. The FX-side keys (FX types, mod types, pitch and pan selectors) are square `msq` keys, 72 × 72 with 17 px legends: FX types on one row, mod types four then three. `SkinKey` picks the legend size from the key's shape and size.

## Front panel grid

Reverb is a 3 × 3 grid of large knobs: reverb time, high, low; pre-delay, 1st ref delay, 1st ref level; diffusion, ER/reverb, reverb LPF (moved from the drawer: it works in both eras). FX knob rows are 30 px apart. Top bar: 16 px gaps, the name box 318 px wide so the italic name clears the era keys, the LCD 508 px.
