# JUCE skin: button and knob image states

Supplied artwork (user-rendered): knob cap, and square and rectangular keys in **off**, **on** (lit lamp) and **pushed** variants.

## Key states

| State | Image | When |
|---|---|---|
| Off | off | idle, parameter off |
| On | on | idle, parameter on |
| Pushed (off) | pushed-off | mouse down on a key that is currently off |
| Pushed (on) | pushed-on | mouse down on a key that is currently on |

Rules:

- The pushed image is shown **only while the mouse button is held**. It keeps the key's current lamp state while held (on stays lit, off stays dark).
- The parameter changes on **release** (inside the key). After release the normal image for the new state is shown. Dragging off the key and releasing cancels the change.
- While pushed, the printed legend is drawn about 7 % smaller and slightly darker (about 80 % opacity) to suggest the cap moving down. Keep it subtle.
- Radio groups (era, wet input, EQ type) follow the same rule per key.
- Legends, LEDs, LCD and graphs are drawn in code; only key bodies, the knob cap and the background are images.

The prototype (`prototype/index.html`) simulates this with `.key:active`.

## Files and sizes (`assets/ui/`)

Resampled (Lanczos) from the high-res originals and palette-optimised with pngquant. Transparent corners are preserved. Sizes are in pixels at the **250 % design size** (the 2500 × 1000 panel; the nominal 100 % UI is 1000 × 400).

| File | Size |
|---|---|
| `knob-cap.png` | 144 × 144 (largest knob in the layout is 134) |
| `key-sq-{off,on}.png`, `key-sq-{off,on}-pushed.png` | 84 × 84 (largest square key) |
| `key-rect-{off,on}.png`, `key-rect-{off,on}-pushed.png` | 156 × 84 |

The editor must be resizable: lay everything out in 2500 × 1000 design units and scale the whole component (aspect locked at 2.5:1, `setResizeLimits` plus `AffineTransform::scale`, or per-component scaled bounds). Draw images with high-quality resampling (`Graphics::setImageResamplingQuality(highResamplingQuality)`). Shrinking from 250 % stays sharp; enlarging beyond it softens, so regenerate at a larger size if a bigger maximum is wanted.

## Background

`panel-bg.jpg` (2500 × 1000, ~190 KB) is the supplied brushed plate, cropped to the plate and rebuilt as a 9-slice: the four screw corners keep their proportions and the middle is stretched, so the 3.5:1 source fits the 2.5:1 panel without squashing the screws. The prototype's CSS screws are hidden because the plate has its own.
