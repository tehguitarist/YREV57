# CLAUDE.md

Guidance for Claude Code working in this repository.

## What this project is

**YREV57** is an open-source reverb plugin (JUCE / C++20, AU + VST3 + Standalone, LV2 on Linux) *inspired by* two mid-1980s Yamaha digital reverbs, the REV7 (1985) and REV5 (1987). It is not a clone and is not affiliated with Yamaha. A modelled converter-and-EQ chain wraps **our own** delay/all-pass reverb engine, with an **era switch** (1985 / 1987) as the main character control. Status: beta (0.9.1); see [ROADMAP.md](ROADMAP.md).

## Read first

- [docs/design/architecture.md](docs/design/architecture.md): signal flow, era table
- [docs/design/engine.md](docs/design/engine.md): reverb engine and converter model
- [docs/design/fx-slot.md](docs/design/fx-slot.md): FX slot
- [docs/design/parameters.md](docs/design/parameters.md): parameter spec (names, ranges, sources)
- [docs/design/ui-controls.md](docs/design/ui-controls.md), [ui-layout.md](docs/design/ui-layout.md): every control and the frozen layout
- [docs/design/decisions/](docs/design/decisions/): ADRs (native-rate engine and reported latency, mono wet sum, own late network, "inspired by", JUCE, true-stereo wet input, era-7 time variance)

## Layout

```
CMakeLists.txt     JUCE fetched via FetchContent (tag in YREV_JUCE_TAG)
src/plugin/        Processor, parameter layout (Parameters.*), editor (YrevEditor, ui/ skin and generated layout)
src/dsp/           Chain (wet path, mix, bypass), Converter (rate bridge, era filters, A/D-D/A), Engine (ER + late network), generated ErTables.h / ConverterFilters.h, DelayLine, Biquad, Ducker.h, StereoWidth.h, FxSlot (delay, echo, mod, pitch, pan)
tests/             Catch2 tests; ReverbMeasure.h helpers
installer/         macOS .pkg, Windows NSIS, Linux .deb scripts
.github/workflows/ plugin-ci.yml, plugin-release.yml (manual only)
data/              CSVs: ER taps, presets, parameter ranges, ICs, components, patents
tools/             Python: generators, ER digitiser, emphasis and memory calculators
docs/              manual, design notes, ADRs
```

## Build

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release   # fetches JUCE on first run
cmake --build build --config Release -j 8
```

- Targets: `YREV57_Standalone`, `YREV57_VST3`, `YREV57_AU`. Artefacts land under `build/YREV57_artefacts/`.
- `-DYREV_COPY_AFTER_BUILD=ON` installs plugins into the user's plugin folders (off by default).
- `-DCMAKE_OSX_ARCHITECTURES=arm64` for a faster Apple-silicon-only build; default is universal.
- Set `FETCHCONTENT_BASE_DIR` to a shared folder to reuse one JUCE checkout across build dirs.
- Tests: `-DYREV_BUILD_TESTS=ON`, build target `YREV57Tests`, run `ctest --test-dir build --output-on-failure`. The frozen parameter-ID list in `tests/ParameterTests.cpp` guards the saved-state contract; update it deliberately when adding IDs.
- CI and releases are manual-only workflows; see [docs/release.md](docs/release.md). Do not enable push/PR triggers without asking.
- Python tools: `pip install -r tools/requirements.txt`.

## Hard rules

1. **No Yamaha firmware, ROM dumps or SysEx/bulk-dump data in the repository, and never present the plugin as Yamaha's.** Program names and parameter settings *printed in the owner's manuals* may ship as preset settings, cited to the page (ADR 0004). See [CONTRIBUTING.md](CONTRIBUTING.md).
2. **Never commit anything under `references/`** (copyrighted manuals and scans; git-ignored). Do not remove those ignore rules.
3. **No Yamaha logos or panel artwork** in the UI. Hardware names appear only descriptively, with the trademark disclaimer.
4. **Never copy firmware behaviour.** The late-reverb network is our own design (ADR 0003).
5. **Cite a source for every hardware fact.** Mark visual readings *(schematic read)*, OCR text *OCR*, own reasoning "inference". When a fact changes, update the doc **and** the matching CSV in `data/`.

## Conventions

- Australian English in docs and comments (colour, organise, analyse).
- Config constants at the top of each file; defensive handling of edge cases; concise code. Run `clang-format` (config in `.clang-format`).
- Significant design choices get an ADR (copy `docs/design/decisions/0000-template.md`).
- Small PRs.

## Plugin / audio-thread rules

- **Parameter IDs are a saved-state contract.** Never rename or renumber a released ID; add new ones. Version via `ParameterID{ id, version }`.
- All parameters live in the APVTS layout in `src/plugin/Parameters.cpp`; IDs in `yrev::id`. Add new parameters there and keep [parameters.md](docs/design/parameters.md) in step.
- `processBlock` must not allocate, lock, log or throw. Preallocate in `prepareToPlay`.
- Handle sample-rate changes, block size 1, and denormals (`ScopedNoDenormals`).
- The native-rate engine (31.25 kHz / 44.1 kHz) needs resampling at the edges; the bridge delay is reported to the host as latency and the dry is delayed to match; pre-delay, 1st reflections and FX delays play as printed (ADR 0001).
- Report the reverb tail honestly (`getTailLengthSeconds`); Era 5 allows RT to 99 s.
- Wet path is mono-summed by default (ADR 0002); dry stays stereo.

## Licence

AGPLv3 ([LICENSE](LICENSE)), compatible with JUCE's AGPLv3 option. Do not add dependencies with licences incompatible with AGPLv3 (no proprietary SDKs such as AAX or VST2 without discussion).
