# Legal notes

> Not legal advice. A summary of the project's working position, drawn from [ADR 0004](design/decisions/0004-inspired-by.md).

## Trademarks

Yamaha, REV1, REV5, REV7 and SPX are trademarks of Yamaha Corporation, which is in no way associated or affiliated with YREV57. The names are used only to describe the hardware this project is inspired by.

Disclaimer wording (one text, used in the about box, README, installer readme and product page):

> YREV57 is an independent product, inspired by the Yamaha REV7 and REV5. It is not affiliated with, endorsed by or sponsored by Yamaha Corporation. Yamaha, REV7 and REV5 are trademarks of Yamaha Corporation, used here only to describe the hardware that inspired this plug-in.

## Rules

- No firmware, ROM, microcode, ROM-dump or SysEx data from the units ships with the plugin. Program names and settings printed in the owner's manuals may ship as presets, cited to the page (ADR 0004, amended 2026-11-19).
- No Yamaha logos or panel artwork in the UI.
- Third-party manuals and scans stay in `references/third_party/`, which is git-ignored and never distributed.
- Factory presets are our own designs, plus the REV7 and REV5 manual programs from their printed settings ([presets.md](design/presets.md)). The plugin never presents itself as Yamaha's; the trademark disclaimer is in the about box, README and installers.

## Open items

- **Patents:** the 1981–85 Yamaha patents that inform the first engine model appear expired. The patent list is in [`data/patents/patents.csv`](../data/patents/patents.csv).
- **Licence:** AGPLv3 for the project (decided; see [LICENSE](../LICENSE)). Compatible with JUCE's AGPLv3 option. Docs and data are covered by the same licence unless stated otherwise.
- **Manual-program data:** `data/presets/` holds values read from the REV7 and REV5 manuals' printed program charts; allowed since the 2026-11-19 amendment (CONTRIBUTING.md, ADR 0004).
