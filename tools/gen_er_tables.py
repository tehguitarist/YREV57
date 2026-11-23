#!/usr/bin/env python3
"""Generate src/dsp/ErTables.h from the digitised E/R chart taps (room size 4.0) of both units.

Sources: data/er-taps/rev7/rev7_er_taps_size4.csv (REV7 owner's manual, 18 reflections per channel, 0-500 ms) and
data/er-taps/rev5/rev5_er_taps_size4.csv (REV5 owner's manual, 34 per channel, 0-600 ms), read from the manuals'
stem plots (published charts, not firmware or preset data; see the owner's and service manuals). Times are
stored relative to each chart set's first-tap offset so the engine can add its own initial delay and scale the
spacing by room size / 4: REV7 relative to 20 ms (the charts' first tap, an inference; earlier taps clamp to 0),
REV5 relative to each channel's own first tap, so both channels' first reflections coincide at the initial delay:
the captures of the REV5 E/R programs (Early Ref. 1 settings, room size 2.0) put both channels' first tap at
the same time where the charts print them 0.5-2.7 ms apart (twentieth pass, docs/design/engine.md; inference). The
Pan A/B tables start both channels together too and keep the lagging set's offset after that (pan_lines).

Usage: python3 tools/gen_er_tables.py [out.h]
"""
import csv
import sys
from collections import defaultdict
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
OUT_DEFAULT = ROOT / "src/dsp/ErTables.h"
REV7_CSV = ROOT / "data/er-taps/rev7/rev7_er_taps_size4.csv"
REV5_CSV = ROOT / "data/er-taps/rev5/rev5_er_taps_size4.csv"
REV7_FIRST_TAP_MS = 20.0  # t_0 of the scaling model in 08-early-reflections.md (inference)
REV5_PAN_CSV = ROOT / "data/er-taps/rev5/rev5_er_taps_size1_lowconf.csv"
PAN_MODES = ((7, "Pan A"), (8, "Pan B"))  # the CSV's mode numbers
PAN_LIVENESS = (0, 10)                    # the two charted liveness settings
PAN_SIZE_FACTOR = 4.0 / 1.0               # the charts are at room size 1.0; table times are for 4.0 (inference, below)
# Pan A's lagging set starts this long after the leading one at room size 1.0: the REV5 Pan-A captures put it 11.3 ms
# later at room size 2.0 (pan_lines); the charts print 4.2 (liveness 0) and 5.8 ms (10). Pan B keeps its printed lag.
PAN_LAG_MS = {"Pan A": 5.65}
MAX_TAPS = 34             # the REV5 charts' count; the REV7 charts have 18
REV7_SMALL_CSV = ROOT / "data/er-taps/rev7/rev7_er_taps_size1_lowconf.csv"
# REV5 reverb programs' own early taps (not charted): read from the REV5 captures (twenty-second pass,
# "The REV5 reverb programs' early taps"); times from the first arrival
# (the initial delay), no room-size scaling (the captures show none). Order is the engine's kRevPattern* indices.
REV5_REV_CSV = ROOT / "data/er-taps/rev5/rev5_reverb_program_taps.csv"
REV_PATTERNS = ("Rev Hall A", "Rev Hall B", "Rev Plate")
# REV7 reverb programs' early reflections: "3 early reflections are programmed for each channel" (REV7 OM p. 14, OCR),
# the first at the initial delay on both channels. Times and levels read from the REV7 captures whose first
# tap sits on a factory initial delay (twenty-third pass, "The REV7
# reverb programs' early reflections"); times from the initial delay, not scaled. Order is the engine's kRevPatterns7
# indices (era-7 Rev Hall, Rev Plate, Rev Room).
REV7_REV_CSV = ROOT / "data/er-taps/rev7/rev7_reverb_program_taps.csv"
REV7_PATTERNS = ("Rev Hall", "Rev Plate", "Rev Room")
# REV7 modes whose size-1.0 chart is a pattern of its own rather than the size-4.0 chart scaled by 1/4 (CSV mode
# numbers): Random (span 122 ms against 113 scaled, taps ~2 ms / 1-2 dB off) and Spring (34 ms against 48, levels
# ~9 dB off). Large Hall, Reverse and Small Hall scale (within ~1 ms / 1 dB); Plate's size-1.0 chart is very low
# confidence (a solid block) and is left out. Every REV5 mode scales. Check: docs/design/engine.md fourteenth pass.
REV7_SMALL_MODES = (3, 6)


def read(path: Path) -> tuple[dict, dict]:
    groups: dict[tuple[int, str], list[tuple[float, float]]] = defaultdict(list)
    names: dict[int, str] = {}
    with path.open(newline="") as f:
        for row in csv.DictReader(f):
            mode = int(row["mode"])
            names[mode] = row["mode_name"]
            groups[(mode, row["channel"])].append((float(row["time_ms"]), float(row["gain_db"])))
    return groups, names


def pattern_lines(groups: dict, names: dict, origin) -> list[str]:
    """origin(mode, channel) -> the time each channel's taps are stored relative to."""
    out = []
    for mode in sorted(names):
        parts = []
        for ch in ("L", "R"):
            t0 = origin(mode, ch)
            t = sorted(groups[(mode, ch)])[:MAX_TAPS]
            parts.append((len(t), "{" + ", ".join(f"{{{max(0.0, ms - t0):.1f}f, {db:.1f}f}}" for ms, db in t) + "}"))
        name = names[mode].title()
        out += [f'        {{"{name}", {parts[0][0]}, {parts[1][0]},', f"         {parts[0][1]},", f"         {parts[1][1]}}},"]
    return out


def read_pan(path: Path) -> dict:
    """(mode, liveness, channel) -> sorted [(time_ms, gain_db)] for the Pan A/B rows of the size-1.0 CSV."""
    groups: dict[tuple[int, int, str], list[tuple[float, float]]] = defaultdict(list)
    with path.open(newline="") as f:
        for row in csv.DictReader(f):
            mode = int(row["mode"])
            if mode in dict(PAN_MODES) and row["liveness"] != "":
                groups[(mode, int(row["liveness"]), row["channel"])].append((float(row["time_ms"]), float(row["gain_db"])))
    return groups


def pan_lines(groups: dict) -> list[str]:
    """Pan A and B at liveness 0 and 10. Each chart holds a leading set (the earlier first tap) and a lagging set on the
    other channel. Times: the leading set from its own first tap; the lagging set opens with a tap at that same time
    (its own first level) and then plays its chart from PAN_LAG_MS (Pan A, capture-fitted) or its printed lag (Pan B),
    keeping MAX_TAPS. Then x4 to put the size-1.0 charts on the size-4.0 time scale of the other tables (the scaling
    model in 08-early-reflections.md: t = t0 + (t4 - t0) x size / 4; inference, verified for Pan A at size 2.0 by the
    captures). Levels are the chart's own. Source for the shared first tap and the lag: the REV5 *Pan-A* and
    *Pan-A 2* captures (liveness 5, room size 2.0): L on the liveness-0 L chart from 16.4 ms (0.21 ms median error),
    R on the liveness-0 R chart from 27.7 ms (0.16 ms) after a tap at 16.4 ms (2026-11-21, E/R leftovers; inference)."""
    out = []
    for mode, name in PAN_MODES:
        for lv in PAN_LIVENESS:
            t = {ch: sorted(groups[(mode, lv, ch)]) for ch in ("L", "R")}
            lead = min(("L", "R"), key=lambda ch: t[ch][0][0])
            lag_ch = "R" if lead == "L" else "L"
            t0 = t[lead][0][0]
            lag = PAN_LAG_MS.get(name, t[lag_ch][0][0] - t0)
            rows = {lead: [(ms - t0, db) for ms, db in t[lead]][:MAX_TAPS],
                    lag_ch: ([(0.0, t[lag_ch][0][1])] + [(ms - t[lag_ch][0][0] + lag, db) for ms, db in t[lag_ch]])[:MAX_TAPS]}
            parts = []
            for ch in ("L", "R"):
                body = ", ".join(f"{{{ms * PAN_SIZE_FACTOR:.1f}f, {db:.1f}f}}" for ms, db in rows[ch])
                parts.append((len(rows[ch]), "{" + body + "}"))
            out += [f'        {{"{name} L{lv}", {parts[0][0]}, {parts[1][0]},', f"         {parts[0][1]},", f"         {parts[1][1]}}},"]
    return out


def small_lines(groups: dict, names: dict) -> list[str]:
    """REV7 size-1.0 charts of REV7_SMALL_MODES, times relative to REV7_FIRST_TAP_MS and x4 onto the size-4.0 scale
    (so the engine's room-size scaling treats them like the other tables; inference, as for Pan)."""
    out = []
    for mode in REV7_SMALL_MODES:
        parts = []
        for ch in ("L", "R"):
            t = sorted(groups[(mode, ch)])[:MAX_TAPS]
            body = ", ".join(f"{{{max(0.0, ms - REV7_FIRST_TAP_MS) * 4.0:.1f}f, {db:.1f}f}}" for ms, db in t)
            parts.append((len(t), "{" + body + "}"))
        out += [f'        {{"{names[mode].title()} 1.0", {parts[0][0]}, {parts[1][0]},', f"         {parts[0][1]},", f"         {parts[1][1]}}},"]
    return out


def rev_lines(path: Path = REV5_REV_CSV, names: tuple = REV_PATTERNS, digits: int = 1) -> list[str]:
    groups: dict[tuple[str, str], list[tuple[float, float]]] = defaultdict(list)
    with path.open(newline="") as f:
        for row in csv.DictReader(f):
            groups[(row["pattern"], row["channel"])].append((float(row["time_ms"]), float(row["gain_db"])))
    out = []
    for name in names:
        parts = []
        for ch in ("L", "R"):
            t = sorted(groups[(name, ch)])[:MAX_TAPS]
            parts.append((len(t), "{" + ", ".join(f"{{{ms:.{digits}f}f, {db:.1f}f}}" for ms, db in t) + "}"))
        out += [f'        {{"{name}", {parts[0][0]}, {parts[1][0]},', f"         {parts[0][1]},", f"         {parts[1][1]}}},"]
    return out


def main() -> None:
    out = Path(sys.argv[1]) if len(sys.argv) > 1 else OUT_DEFAULT
    g7, n7 = read(REV7_CSV)
    g5, n5 = read(REV5_CSV)
    gpan = read_pan(REV5_PAN_CSV)
    g7s, n7s = read(REV7_SMALL_CSV)
    first5 = {(m, ch): min(ms for ms, _ in g5[(m, ch)]) for m in n5 for ch in ("L", "R")}
    lines = [
        "#pragma once",
        "",
        "// Generated by tools/gen_er_tables.py from data/er-taps/rev7/rev7_er_taps_size4.csv and",
        "// data/er-taps/rev5/rev5_er_taps_size4.csv (plus the Pan A/B rows of rev5_er_taps_size1_lowconf.csv, rev5_reverb_program_taps.csv and rev7_reverb_program_taps.csv). Do not edit by hand.",
        "// E/R chart taps at room size 4.0, digitised from the owner's manuals' stem plots (REV7: times +-0.5 ms, gains",
        "// +-0.3 dB, re-read 2026-11-19; REV5: see the kind/confidence columns of its CSV). Times are relative to the chart set's first-tap",
        f"// offset (REV7 {REV7_FIRST_TAP_MS:g} ms; REV5 each channel's first tap), so the engine adds its own initial",
        "// delay and scales the spacing by room size / 4.",
        "",
        "namespace yrev::dsp::er",
        "{",
        "    struct Tap",
        "    {",
        "        float ms, db;",
        "    };",
        "    struct Pattern",
        "    {",
        "        const char* name;",
        "        int countL, countR;",
        f"        Tap left[{MAX_TAPS}], right[{MAX_TAPS}];",
        "    };",
        "",
        f"    inline constexpr float kChartFirstTapMs = {REV7_FIRST_TAP_MS:.1f}f;",
        "    inline constexpr float kChartSpanMs = 600.0f; // longest chart axis (REV5; the REV7's is 500 ms)",
        f"    inline constexpr int kMaxTaps = {MAX_TAPS};",
        f"    inline constexpr int kNumPatterns = {len(n7)};",
        f"    inline constexpr int kNumPatterns5 = {len(n5)};",
        "",
        "    // REV7 E/R charts (owner's manual, PDF pp. 23-24): Small Hall, Large Hall, Random, Reverse, Plate, Spring",
        "    inline constexpr Pattern kPatterns[kNumPatterns] = {",
        *pattern_lines(g7, n7, lambda m, ch: REV7_FIRST_TAP_MS),
        "    };",
        "",
        "    // REV5 E/R charts (owner's manual, printed pp. 22-23): S-Hall, L-Hall, Random, Reverse, Plate, Spring",
        "    inline constexpr Pattern kPatterns5[kNumPatterns5] = {",
        *pattern_lines(g5, n5, lambda m, ch: first5[(m, ch)]),
        "    };",
        "",
        "    // REV5 Pan A and Pan B (era 5 modes 6 and 7) at liveness 0 and 10, in the order A/0, A/10, B/0, B/10. Source:",
        "    // data/er-taps/rev5/rev5_er_taps_size1_lowconf.csv (LOW confidence: room size 1.0, lines merge, many estimates).",
        "    // The charts are at room size 1.0; times are scaled x4 about the mode's earliest tap onto the size-4.0 scale",
        "    // of the tables above (inference: the scaling model of 08-early-reflections.md, verified for two REV7 modes).",
        f"    inline constexpr int kNumPanPatterns = {len(PAN_MODES) * len(PAN_LIVENESS)};",
        "    inline constexpr Pattern kPanPatterns5[kNumPanPatterns] = {",
        *pan_lines(gpan),
        "    };",
        "",
        "    // REV7 room size 1.0 charts for the modes whose small-room pattern is their own (Random, Spring; the engine",
        "    // blends from these at room size <= 1 to kPatterns at >= 4). Source: data/er-taps/rev7/rev7_er_taps_size1_lowconf.csv",
        "    // (low confidence: lines merge at size 1.0). Times x4 onto the size-4.0 scale about the 20 ms first tap.",
        f"    inline constexpr int kNumSmallPatterns7 = {len(REV7_SMALL_MODES)};",
        "    inline constexpr int kSmallPatternMode7[kNumSmallPatterns7] = {" + ", ".join(str(m - 1) for m in REV7_SMALL_MODES) + "}; // kPatterns index",
        "    inline constexpr Pattern kSmallPatterns7[kNumSmallPatterns7] = {",
        *small_lines(g7s, n7s),
        "    };",
        "",
        "    // REV5 reverb programs' own early taps (era 5 modes Rev Hall and Rev Plate; Rev Hall plays A below density 4,",
        "    // B at 4). Source: data/er-taps/rev5/rev5_reverb_program_taps.csv, read from the REV5 captures (a",
        "    // third-party library: measurement, not a hardware spec). Times from the initial delay, not scaled by room size.",
        f"    inline constexpr int kNumRevPatterns5 = {len(REV_PATTERNS)};",
        "    inline constexpr Pattern kRevPatterns5[kNumRevPatterns5] = {",
        *rev_lines(),
        "    };",
        "",
        "    // REV7 reverb programs' early reflections, 3 per channel (REV7 OM p. 14, OCR; era 7 modes Rev Hall, Rev Plate,",
        "    // Rev Room). Source: data/er-taps/rev7/rev7_reverb_program_taps.csv, read from the REV7 captures (a",
        "    // third-party library: measurement, not a hardware spec). Times from the initial delay, not scaled by room size.",
        f"    inline constexpr int kNumRevPatterns7 = {len(REV7_PATTERNS)};",
        "    inline constexpr Pattern kRevPatterns7[kNumRevPatterns7] = {",
        *rev_lines(REV7_REV_CSV, REV7_PATTERNS, 2),
        "    };",
        "} // namespace yrev::dsp::er",
        "",
    ]
    out.write_text("\n".join(lines))
    print(f"wrote {out} ({len(n7)} REV7 + {len(n5)} REV5 patterns)")


if __name__ == "__main__":
    main()
