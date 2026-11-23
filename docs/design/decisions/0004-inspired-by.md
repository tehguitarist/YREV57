# 0004 · "Inspired by", not a clone

- Status: Accepted
- Date: 2026-11-17

## Context
Firmware, factory presets and manuals are copyright; "Yamaha", "REV5" and "REV7" are trademarks. The relevant patents appear expired, but that needs confirming ([legal.md](../../legal.md)).

## Decision
- No ROM, firmware or preset data extracted from the units (dumps, SysEx) ships with the plugin.
- The project and product name is **YREV57** (replaces the codename "era-reverb"), following the industry pattern of model-number nods (LX480, NV73). Hardware names appear only descriptively, with a trademark disclaimer ([legal.md](../../legal.md)); no Yamaha logos or panel artwork.
- Factory presets are our own, voiced by ear and measurement. *Amended 2026-11-19 (maintainer):* presets may also take the program names and parameter settings printed in the owner's manuals (the REV5 ROM chart, REV7 program pages), cited to the page. These are published settings, not extracted data; our engine does not reproduce the hardware bit for bit, and the plugin never claims to be Yamaha's (trademark disclaimer).
- Third-party manuals and scans stay out of any public repository (`references/third_party/` is git-ignored).

## Consequences
- Users looking for a REV5/REV7-style reverb can find it by name and description.
- The disclaimer must ship everywhere the hardware is named.
