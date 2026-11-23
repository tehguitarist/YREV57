# Contributing

## Ground rules

- **No Yamaha firmware, ROM dumps or SysEx/bulk-dump data** in the repository, ever. REV5 SysEx dumps are for private analysis only. Program names and parameter settings printed in the owner's manuals may be used for presets, cited to the manual page (ADR 0004, amended 2026-11-19). Scanned manual pages themselves stay out of the repository (`references/third_party/`).
- **No third-party manuals or scans** outside `references/third_party/` (git-ignored).
- Cite a source for every hardware fact. Mark visual readings *(schematic read)*, OCR text *OCR*, and your own reasoning "inference".
- When you change a fact, update the doc **and** the matching CSV in `data/`.
- Australian English spelling in docs (colour, organise, analyse).

## Workflow

1. Open or pick an issue (bug report or feature request).
2. Branch: `dsp/converter-model`, `ui/preset-browser`, `fix/windows-close-hang`.
3. Keep PRs small; fill in the PR template's source checklist.
4. Significant design choices get an ADR in `docs/design/decisions/` (copy `0000-template.md`).

## Code style

Config constants at the top of each file, defensive error handling, concise code. Python tools run with the standard library plus `tools/requirements.txt`.
