# er-digitise

The scripts used to digitise the early-reflection charts, kept **as they were run** on 17 Nov 2026. They are research code: input paths are hard-coded to the original workspace (`/home/claude/rev7_work/er/p-23.png`, `/home/claude/rev5_om/er/p-23.png` etc.) and outputs are written to the current directory. Point them at `references/third_party/images/er_chart_300dpi/` (or re-render the PDF pages at 300 dpi) before re-running.

| Script | Status | What it does |
| --- | --- | --- |
| `digitise_er.py` | Library | Helpers: load a page as a binary mask, refine a baseline, find the vertical axis and ticks, extract stems as height runs (gap ≤ 2 px, height ≥ 8 px), weight-centre each stem |
| `run_er7.py` | Superseded | First REV7 pass |
| `run_er7b.py` | Superseded | Second REV7 pass |
| `run_er7c.py` | **Most likely the version used** (latest, and the only one matching the documented calibration) | Fixed calibration 1.612 px/ms and 6.2 px/dB; per-panel baselines and origins (`XL` table); writes `rev7_er_taps_raw.csv` |
| `run_er5.py`, `run_er5b.py` | Failed | First REV5 attempts; they traced lines up from the baseline and stopped at the first break. Output not kept |
| `digitise_er5.py` | **Used for the REV5 tables** (2026-11-19) | Paths configurable (`--pages`, `--out`). Column-density line finding, breaks bridged, per-panel axis calibration, completion to 34 per channel (split, insert, curve, cross-channel), by-eye review choices in `GUIDE`; writes `data/er-taps/rev5/` tables and numbered overlay PNGs |
| `digitise_er5.py --unit rev7` | **Used for both REV7 tables** (2026-11-19) | 600 dpi renders of the REV7 chart pages; per-panel deskew; levels from the y-axis ticks, times piecewise through the 100–500 ms axis ticks. Size 4.0: separated lines, centre from the ink, level from the highest ink (R tops under the L panel's time labels re-read below them); writes `rev7_er_taps_size4.csv`. Size 1.0: per-column top profile split into visible line tops (lines are 2 scan px wide). Large Hall and Reverse follow the size-4.0 chart scaled in time (×0.25 from about 20 ms), so the size-4.0 lines guide the reading there (order-preserving match; lines hidden under taller neighbours keep the scaled time). Other modes are read unguided. Writes `rev7_er_taps_size1_lowconf.csv` (with `kind` and `confidence`) and one numbered overlay PNG per mode |

`rev7_er_taps_raw.csv` is the 300 dpi output of `run_er7c.py`. The `size4` and `size1_lowconf` tables were replaced on 2026-11-19 by the 600 dpi reading (the old size-4.0 R levels had misreads from broken lines, up to 19 dB low on Large Hall R):

```bash
pdftoppm -r 600 -gray -png -f 23 -l 24 references/third_party/manuals/REV7_owners_manual.pdf <scratch>/p
# rename to REV7_manual_p23_ER_charts_600dpi.png / ..._p24_... (keep them out of the repo: copyrighted scans)
python3 -I tools/er-digitise/digitise_er5.py --unit rev7 --pages <scratch> --out <scratch>/out
```

The PDF's chart pages are 1-bit 300 ppi scans, so 600 dpi adds no detail; it only avoids the 300 dpi render's resampling, which merged one-pixel gaps between lines.

## To do

- Parameterise input/output paths (config at the top of each script).
- REV7 size 1.0: done at 600 dpi (Small Hall, Random, Plate, Spring are still low confidence: their lines merge and they do not follow the scaled size-4.0 chart).
- REV5: done with `digitise_er5.py` and a by-eye review of its overlays instead of a manual-assist mode.
