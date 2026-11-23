# src/

`plugin/` holds the working scaffold (pass-through processor, parameter layout, generic editor). The rest is the planned layout, matching [docs/design/architecture.md](../docs/design/architecture.md). Framework: JUCE ([ADR 0005](../docs/design/decisions/0005-juce-framework.md)).

```
src/
  plugin/          PluginProcessor, PluginEditor, parameter layout, preset I/O
  dsp/
    io/            mono sum, analogue EQ model, mix/bypass/out-phase
    converter/     AA/recon filters, emphasis pair, SRC (host <-> native), quantiser, dither, DC offset
    engine/        ER multitap, diffusion, late network, secondary reverb, gate
    modulation/    LFO (8-bit option), 3-voice chorus/symphonic, flange, phasing, tremolo
    eq/            digital per-program EQ (Era 5)
  presets/         factory presets (our own voicing) + hidden per-preset values
tests/             DSP unit tests; golden-file comparisons against measured features
```

Conventions: config constants at the top of each file, defensive handling of host edge cases (sample-rate changes, block size 1, denormals), keep code concise.
