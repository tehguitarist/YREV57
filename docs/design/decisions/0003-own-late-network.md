# 0003 · Design our own late-reverb network

- Status: Proposed
- Date: 2026-11-17

## Context
The late-reverb topology is DSP microcode and is documented nowhere (Q-19). Only structural hints exist: "delay, all-pass and low-pass blocks", the DSP-2 → DSP-1 → DSP-2 route, the REV5's density, diffusion and secondary-reverb controls, and Yamaha patents (US 4,570,523: 3 parallel combs + 3 series all-passes + band-pass in the loop; JPS60237493: three units in series). Copying firmware is off the table ([legal.md](../../legal.md)).

## Decision
Start from the patent structure as a first model, then tune our own network to match measured behaviour (RT per band, echo-density growth, EDC, modulation). Do not extract or use ROM/microcode.

## Consequences
- Legally clean and flexible; success depends on good measurements.
- We may diverge from the patent structure if it cannot match the measured density build-up.
