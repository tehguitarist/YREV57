# 0005 · Use JUCE (CMake, FetchContent) as the plugin framework

- Status: Accepted (framework and licence: AGPLv3)
- Date: 2026-11-17

## Context
Q-25 left the framework, formats and licence undecided. The project owner asked for a JUCE plugin.

## Decision
- Framework: **JUCE**, pinned by git tag (`YREV_JUCE_TAG`, currently 9.0.5), fetched with CMake `FetchContent` (shallow clone). No submodule, no Projucer.
- Formats for now: **AU, VST3, Standalone**. CLAP and AAX are not included.
- C++20, universal macOS binaries by default, macOS 11 deployment target.
- Placeholder UI is `GenericAudioProcessorEditor`; the real UI follows [ui-controls.md](../ui-controls.md). *(Superseded: the skinned editor, `YrevEditor`, is built; [ui-layout.md](../ui-layout.md).)*

## Consequences
- The project is licensed **AGPLv3**, free and open source, which satisfies JUCE's AGPLv3 option. Anyone distributing or hosting modified builds must offer the source.
- Proprietary formats (AAX, VST2) are out; VST3 is fine under JUCE's AGPL terms.
- First configure needs network access. Setting `FETCHCONTENT_BASE_DIR` avoids repeat downloads.
- Bumping JUCE is a one-line tag change; build and test afterwards.
- Windows and Linux are not yet set up or tested.
