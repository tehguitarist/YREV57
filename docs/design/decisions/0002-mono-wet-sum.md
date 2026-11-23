# 0002 · Default to a mono-summed wet path

- Status: Proposed
- Date: 2026-11-17

## Context
Both units sum L and R before the single A/D (REV7: two 20 kΩ summing resistors; REV5: stereo/mono switch into one buffer). The direct path stays stereo. Wet stereo width comes only from the engine's own L/R decorrelation.

## Decision
"Authentic mono wet" is the default. A true-stereo input mode is an option, not the default.

## Consequences
- Matches the originals and keeps CPU low (one engine input).
- Stereo-in mode needs its own tuning and must not change the authentic mode's sound.

## Update: three wet-input modes
Per the service manual, the hardware's **MONO** switch uses only the left input, and **STEREO** sums left and right into one buffer; neither unit has a true-stereo wet input. The plugin's `wet_input` therefore has three states:
- **Mono**: left input only (hardware MONO).
- **Stereo**: L+R summed into one mono wet input (hardware STEREO). This is the authentic default.
- **True stereo**: plugin only. The engine takes L and R separately. Needs its own tuning and must not change the authentic modes. Built as [ADR 0006](0006-true-stereo-wet-input.md) (2026-11-19).
