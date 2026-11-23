#!/usr/bin/env python3
"""Digitise the REV5 owner's manual "Early Reflection Mode Chart" (pp. 22-23 printed, PDF pages 23-24).

Each panel is a stem plot: one vertical line per reflection, its top at the reflection's level (0 to -40 dB), its
position its time (0-600 ms). The lines are printed thin and broken in places; the breaks are print/scan artefacts
and carry no information, so each stem is found from its column ink density and its level from the highest ink in
its columns (breaks bridged). Calibration per panel from its own axes: baseline row, y-axis column, 600 ms axis
length (1.506 px/ms at 300 dpi), 4.38 px/dB with 0 dB 214.5 px above the baseline (from the y-axis ticks).

The time-axis labels of the L panel sit just above the R panel's 0 dB line at 100..600 ms, so R stems under a label
are cut at the label band and, if they reach it, take the level of the matching L stem (flag `label`).

Inputs are the private 300 dpi page renders (references/third_party/, git-ignored). Outputs: a raw CSV of every
stem, and overlay PNGs (markers numbered per panel) for checking by eye. Published charts, not firmware or preset
data (the owner's and service manuals).

With --unit rev7 it reads the REV7 owner's manual's room size 1.0 panels instead (PDF pages 23-24 rendered at
600 dpi; see the REV7 section below): writes rev7_er_taps_size1_lowconf.csv and one overlay PNG per mode.

Usage: python3 -I tools/er-digitise/digitise_er5.py [--unit rev5|rev7] [--pages DIR] [--out DIR]
"""
from __future__ import annotations

import argparse
import csv
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw, ImageFont

# ---- Config ----
ROOT = Path(__file__).resolve().parents[2]
PAGES_DEFAULT = ROOT / "references/third_party/images/er_chart_300dpi"
PAGE_FILE = "REV5_manual_p{}_ER_charts.png"
PXMS = 1.506          # px per ms (axis 0-600 ms measured at 901-905 px on every panel)
PXDB = 4.38           # px per dB (y-axis ticks)
TOP0 = 214.5          # 0 dB this many px above the baseline
INK = 200             # grey level below which a pixel is ink
MINCOL = 18           # ink px a column needs to belong to a stem
LABEL_HALF_MS = 18    # half-width of a time-axis label box
LABEL_BAND = (-6, 14) # label band rows relative to the R panel's 0 dB row
TAPS = 34             # reflections per channel on every REV5 chart panel
# Panels where one channel was read better than the other (by-eye review of the overlays, 2026-11-19): the other
# channel follows it wherever the two disagree by more than the threshold (after the panel's typical R-L offset).
# (channel, threshold dB); Plate 4.0 follows R throughout (both channels there are estimates from merged lines)
GUIDE = {("SPRING", "1.0"): ("L", 2.0), ("REVERSE", "4.0"): ("L", 2.0), ("PLATE", "4.0"): ("R", 0.0)}
OUTLIER_DB = 3.5      # a level this far off the curve and off its neighbours' mean is a misread
ROBUST_DB = 3.0       # points this far off the curve are left out of the refit
DISPLAY_SCALE = 1.64  # the panel seeds below are in a 2000 px-high view of the page
# (page, mode, setting, channel, baseline y, y-axis x) in the 2000 px view; refined on the 300 dpi page
PANELS = []
for _mode, _yl, _yr in [("S-HALL", 496, 640), ("L-HALL", 917, 1061), ("RANDOM", 1336, 1478), ("REVERSE", 1756, 1900)]:
    for _lab, _xa in (("1.0", 152), ("4.0", 866)):
        PANELS += [(23, _mode, _lab, "L", _yl, _xa), (23, _mode, _lab, "R", _yr, _xa)]
for _mode, _yl, _yr, _labs in [("PLATE", 473, 619, ("1.0", "4.0")), ("SPRING", 897, 1042, ("1.0", "4.0")),
                               ("PAN-A", 1316, 1461, ("1.0 live0", "1.0 live10")),
                               ("PAN-B", 1735, 1880, ("1.0 live0", "1.0 live10"))]:
    for _lab, _xa in zip(_labs, (168, 875)):
        PANELS += [(24, _mode, _lab, "L", _yl, _xa), (24, _mode, _lab, "R", _yr, _xa)]


def load(path: Path) -> np.ndarray:
    return np.array(Image.open(path).convert("L")).astype(np.int16)


def calibrate(g: np.ndarray, yb0: float, xa0: float) -> tuple[int, int]:
    m = g < 170
    yb0, xa0 = int(yb0 * DISPLAY_SCALE), int(xa0 * DISPLAY_SCALE)
    yb = max(range(yb0 - 25, yb0 + 26), key=lambda y: m[y, xa0 + 60:xa0 + 850].sum())
    xa = max(range(xa0 - 30, xa0 + 31), key=lambda x: m[yb - 230:yb - 4, x].sum())
    return yb, xa


def columns(g: np.ndarray, yb: int, xa: int, labelled: bool) -> dict:
    """Per-column ink count and top level (dB) for one panel; label-band columns of R panels marked."""
    m = g < INK
    y0, y1 = int(yb - TOP0 - 6), yb - 4
    x0, x1 = xa + 3, int(xa + PXMS * 600)
    cnt = m[y0:y1, x0:x1].sum(axis=0)
    band_bot = int(yb - TOP0 + LABEL_BAND[1]) - y0
    top_db = np.full(x1 - x0, np.nan)
    in_label = np.zeros(x1 - x0, bool)
    for i in range(x1 - x0):
        if cnt[i] < MINCOL:
            continue
        t = (x0 + i - xa) / PXMS
        inbox = labelled and min(abs(t - k * 100) for k in range(1, 7)) < LABEL_HALF_MS
        col = m[y0:y1, x0 + i]
        ink = np.where(col[band_bot if inbox else 0:])[0]
        if len(ink) == 0:
            continue
        top = ink[0] + (band_bot if inbox else 0)
        if inbox and top <= band_bot + 3:
            in_label[i] = True
            top = band_bot
        ytop = max(y0 + top, int(yb - TOP0 - 3))
        top_db[i] = min(0.0, -(ytop - (yb - TOP0)) / PXDB)
    return {"cnt": cnt, "top_db": top_db, "in_label": in_label, "x0": x0, "xa": xa}


def line_from(cols: list[int], c: dict, kind: str) -> dict:
    w = c["cnt"][cols].astype(float)
    xc = (np.array(cols) * w).sum() / w.sum() + c["x0"]
    tops = c["top_db"][cols]
    db = float(np.nanmax(tops)) if np.any(np.isfinite(tops)) else np.nan
    flag = "label" if c["in_label"][cols].any() else ""
    return {"cols": cols, "time_ms": (xc - c["xa"]) / PXMS, "gain_db": db, "width_px": len(cols), "kind": kind,
            "flag": flag}


def stems(c: dict) -> list[dict]:
    """Lines as measured: contiguous ink columns, split at density dips (lines are 1-3 px wide)."""
    cnt = c["cnt"]
    groups: list[list[int]] = []
    for col in np.where(cnt >= MINCOL)[0]:
        if groups and col - groups[-1][-1] <= 1:
            groups[-1].append(int(col))
        else:
            groups.append([int(col)])
    lines = []
    for gc in groups:
        parts = [gc]
        if len(gc) > 3:
            v, cut = cnt[gc], [0]
            for i in range(1, len(v) - 1):
                if v[i] < 0.7 * max(v[i - 1], v[i + 1]) and v[i] <= v[i - 1] and v[i] <= v[i + 1] and i - cut[-1] >= 2:
                    cut.append(i)
            cut.append(len(gc))
            parts = [gc[a:b] for a, b in zip(cut[:-1], cut[1:]) if b > a]
        for part in parts:
            ln = line_from(part, c, "measured")
            if 5 <= ln["time_ms"] <= 598 and np.isfinite(ln["gain_db"]):
                lines.append(ln)
    return lines


def smooth_curve(t: np.ndarray, db: np.ndarray, at: np.ndarray, span: int, degree: int) -> np.ndarray:
    """Local polynomial fit (the `span` nearest points in time) evaluated at `at`."""
    out = np.empty(len(at))
    for k, x in enumerate(at):
        idx = np.argsort(np.abs(t - x))[:max(span, degree + 2)]
        cf = np.polyfit(t[idx] - x, db[idx], degree)
        out[k] = cf[-1]
    return np.minimum(out, 0.0)


def complete(lines: list[dict], c: dict, mode: str) -> list[dict]:
    """Complete a panel to TAPS lines (every REV5 chart panel has 34): split thick lines (merged pairs hide there),
    then insert into the widest gaps against the local spacing; levels of inserted and label-cut lines, and of
    isolated outliers, from a smooth curve through the measured neighbours (Random: its broad trend)."""
    lines = sorted(lines, key=lambda q: q["time_ms"])
    # 1. split thick lines, widest first, reading each half's own top
    while len(lines) < TAPS:
        widths = np.array([q["width_px"] for q in lines])
        k = int(np.argmax(widths))
        if widths[k] < 4:  # a printed line is 1-3 px wide; 4 px or more holds at least two
            break
        cols = lines[k]["cols"]
        h = len(cols) // 2
        a, b = line_from(cols[:h], c, "split"), line_from(cols[h:], c, "split")
        lines[k:k + 1] = [a, b]
    # 2. insert into the widest gaps relative to the local spacing
    while len(lines) < TAPS:
        t = np.array([q["time_ms"] for q in lines])
        g = np.diff(t)
        if len(g) == 0:
            break
        local = np.array([np.median(g[max(0, i - 3):i + 4]) for i in range(len(g))])
        i = int(np.argmax(g / np.maximum(local, 0.3)))
        lines.insert(i + 1, {"cols": [], "time_ms": (t[i] + t[i + 1]) / 2, "gain_db": np.nan, "width_px": 0,
                             "kind": "inserted", "flag": ""})
    # 3. levels from a robust curve: inserted, label-cut, and isolated outliers (not in Random)
    rnd = mode == "RANDOM"
    good = [q for q in lines if q["kind"] != "inserted" and not q["flag"] and np.isfinite(q["gain_db"])]
    if len(good) >= 4:
        gt = np.array([q["time_ms"] for q in good])
        gd = np.array([q["gain_db"] for q in good])
        keep = np.ones(len(good), bool)
        span, deg = (12, 1) if rnd else (7, 2)
        for _ in range(3):  # robust: set aside points far off the curve and refit
            fit = smooth_curve(gt[keep], gd[keep], gt, span, deg)
            keep = np.abs(gd - fit) < ROBUST_DB
            if keep.sum() < 4:
                keep[:] = True
                break
        at = np.array([q["time_ms"] for q in lines])
        curve = smooth_curve(gt[keep], gd[keep], at, span, deg)
        for k, q in enumerate(lines):
            if q["kind"] == "inserted" or q["flag"] or not np.isfinite(q["gain_db"]):
                q["gain_db"] = float(curve[k])
                q["kind"] = "curve (label)" if q["flag"] else q["kind"]
        if not rnd:  # isolated misreads: a spike beyond both neighbours the same way, and off the curve
            for k in range(1, len(lines) - 1):
                q = lines[k]
                if q["kind"] not in ("measured", "split"):
                    continue
                d1 = q["gain_db"] - lines[k - 1]["gain_db"]
                d2 = q["gain_db"] - lines[k + 1]["gain_db"]
                spike = (d1 < -OUTLIER_DB and d2 < -OUTLIER_DB) or (d1 > OUTLIER_DB and d2 > OUTLIER_DB)
                if spike and abs(q["gain_db"] - curve[k]) > OUTLIER_DB:
                    q["gain_db"], q["kind"] = float(curve[k]), "curve (outlier)"
    for q in lines:
        q["time_ms"] = round(float(q["time_ms"]), 1)
        q["gain_db"] = round(float(q["gain_db"]), 1)
    return lines


def cross_channel(completed: dict) -> None:
    """Outside the Pan modes, L and R carry the same level sequence (R slightly earlier): with 34 lines each, line k
    of one channel is line k of the other. Label-cut and inserted lines take their partner's level (plus the panel's
    median R-L offset over lines read in both), and GUIDE panels follow their better-read channel."""
    for (p, mode, lab, ch) in list(completed):
        if ch != "L" or mode.startswith("PAN"):
            continue
        L = sorted(completed[(p, mode, lab, "L")], key=lambda z: z["time_ms"])
        R = sorted(completed[(p, mode, lab, "R")], key=lambda z: z["time_ms"])
        if len(L) != len(R):
            continue
        read = ("measured", "split")
        pairs = [r["gain_db"] - l["gain_db"] for l, r in zip(L, R) if l["kind"] in read and r["kind"] in read]
        off = float(np.median(pairs)) if pairs else 0.0
        guide, guide_db = GUIDE.get((mode, lab), (None, 0.0))
        for l, r in zip(L, R):
            for me, other, sign in ((r, l, 1.0), (l, r, -1.0)):
                if guide and me is (l if guide == "L" else r):
                    continue  # the better-read channel keeps its own values, estimates included
                if me["kind"] in ("inserted", "curve (label)") and other["kind"] in read:
                    me["gain_db"] = round(min(0.0, other["gain_db"] + sign * off), 1)
                    me["kind"] = "from other channel"
            if guide:
                ref, fol, sign = (l, r, 1.0) if guide == "L" else (r, l, -1.0)
                want = min(0.0, ref["gain_db"] + sign * off)
                if abs(fol["gain_db"] - want) > guide_db:
                    fol["gain_db"], fol["kind"] = round(want, 1), "from other channel"


MODE_NUMBERS = {"S-HALL": 1, "L-HALL": 2, "RANDOM": 3, "REVERSE": 4, "PLATE": 5, "SPRING": 6, "PAN-A": 7, "PAN-B": 8}


def confidence(mode: str, lab: str) -> str:
    if lab == "4.0" and mode != "PLATE":
        return "good (34 lines/ch; read, with a few estimates marked in kind)"
    if lab == "4.0":
        return "low (lines merge; split, inserted and curve estimates; L follows R)"
    return "low (room size 1.0: lines merge; many estimates)"


def write_data_tables(completed: dict, out: Path) -> None:
    """data/er-taps/rev5 format: one file for room size 4.0 (modes 1-6), one low-confidence file for size 1.0
    (modes 1-6, and Pan A/B at liveness 0 and 10)."""
    head = ["mode", "mode_name", "room_size", "liveness", "channel", "tap", "time_ms", "gain_db", "kind", "confidence"]
    files = {"rev5_er_taps_size4.csv": [], "rev5_er_taps_size1_lowconf.csv": []}
    for (p, mode, lab, ch), st in completed.items():
        size, _, live = lab.partition(" ")
        rows = files["rev5_er_taps_size4.csv" if size == "4.0" else "rev5_er_taps_size1_lowconf.csv"]
        for k, q in enumerate(sorted(st, key=lambda z: z["time_ms"]), 1):
            rows.append([MODE_NUMBERS[mode], mode, size, live.replace("live", ""), ch, k, f"{q['time_ms']:.1f}",
                         f"{q['gain_db']:.1f}", q["kind"], confidence(mode, lab)])
    for name, rows in files.items():
        rows.sort(key=lambda r: (r[0], r[3], r[4], r[5]))
        with (out / name).open("w", newline="") as f:
            w = csv.writer(f)
            w.writerow(head)
            w.writerows(rows)


COLOURS = {"from other channel": (0, 160, 160), "measured": (230, 0, 0), "split": (255, 140, 0), "inserted": (0, 170, 0), "curve (label)": (0, 110, 255),
           "curve (outlier)": (170, 0, 200)}


# ---- REV7 room size 1.0 at 600 dpi (--unit rev7) ----
# The REV7 owner's manual's chart pages are 1-bit 300 ppi scans (CCITT); rendering at 600 dpi adds no detail but
# keeps every scan pixel as an exact 2x2 block (the 300 dpi renders resample by about 0.6% and merge 1-px gaps).
# Printed lines are 2 scan px (4 px here) wide; at room size 1.0 neighbouring lines are 0-2 scan px apart and merge.
# Each panel is deskewed (rows shifted so its y-axis is vertical), calibrated from its own axes, and read as a
# per-column top profile: a run of columns with one top level is a visible line top. Where room size 1.0 follows
# the size-4.0 chart scaled in time (t1 = T0 + (t4 - 20 ms) * s; Large Hall and Reverse), the 18 size-4.0 lines guide
# the reading: each takes the nearest visible top, and lines hidden under taller neighbours keep the scaled time.
REV7_PAGE_FILE = "REV7_manual_p{}_ER_charts_600dpi.png"
R7_PXMS = 3.232       # nominal px per ms (search windows; times come from each panel's own axis ticks)
R7_PXDB = 12.3        # px per dB (y-axis ticks 0..-40 dB)
R7_ZERO = 609.5       # default 0 dB height above the baseline (used when a panel's ticks are not found)
R7_LINE_PX = 4        # printed line width
R7_TAPS = 18          # lines per channel on the size-4.0 charts (Plate: 14 L / 12 R)
R7_CONF4 = "good (600 dpi, tick calibration; lines separate; overlay-checked)"
R7_GUIDED = {"LARGE HALL", "REVERSE"}  # size 1.0 = size 4.0 scaled (checked by overlay, 2026-11-19)
R7_LABEL_PX = 15    # R panels: the L panel's time labels sit at 0 dB; under them read tops from this far below 0 dB
R7_MATCH_MS = 1.3     # a guided line takes a visible top within this distance of its scaled time
# (page, mode, baseline y L, baseline y R, 0 ms x at size 1.0, 0 ms x at size 4.0), 300 dpi seeds from run_er7c.py
R7_PANELS = [(23, "SMALL HALL", 1009, 1371, 257.5, 1371), (23, "LARGE HALL", 1942, 2308, 272, 1373),
             (23, "RANDOM", 2883, 3233, 276, 1375), (24, "REVERSE", 1004, 1366, 295.5, 1394.5),
             (24, "PLATE", 1944, 2301, 306, 1398), (24, "SPRING", 2869, 3222, 309, 1403)]
R7_NUMBERS = {"SMALL HALL": 1, "LARGE HALL": 2, "RANDOM": 3, "REVERSE": 4, "PLATE": 5, "SPRING": 6}


def r7_groups(v, gap: int = 1) -> list[list[int]]:
    g: list[list[int]] = []
    for x in v:
        if g and x - g[-1][-1] <= gap:
            g[-1].append(int(x))
        else:
            g.append([int(x)])
    return g


def r7_panel(m: np.ndarray, y0: int, x0: int) -> dict:
    """Deskewed panel (rows shifted so the y-axis is vertical), baseline row, axis column, 0 dB row."""
    pts = []
    for y in range(y0 - 600, y0 - 30, 5):
        idx = np.where(m[y, x0 - 30:x0 + 20])[0]
        if 1 <= len(idx) <= 6 and idx.max() - idx.min() <= 5:
            pts.append((y, x0 - 30 + idx.mean()))
    pts = np.array(pts)
    a, b = np.polyfit(pts[:, 0], pts[:, 1], 1)
    keep = np.abs(pts[:, 1] - (a * pts[:, 0] + b)) < 2.5
    a, b = np.polyfit(pts[keep, 0], pts[keep, 1], 1)
    Y0, X0 = y0 - 720, x0 - 80
    out = np.zeros((760, 1780), bool)
    for i in range(760):
        s = int(round(a * i + a * (Y0 - y0)))
        out[i] = m[Y0 + i, X0 + s:X0 + s + 1780]
    xa = a * y0 + b - X0
    xi = int(round(xa))
    yb = max(range(660, 740), key=lambda y: out[y, xi + 300:xi + 1600].sum())
    est = []
    for q in r7_groups([y for y in range(yb - 700, yb - 60) if out[y, xi + 3:xi + 14].sum() >= 5], 2):
        yc = float(np.mean(q))
        k = round((yc - (yb - R7_ZERO)) / (10 * R7_PXDB))
        if 0 <= k <= 4 and abs(yc - (yb - R7_ZERO) - k * 10 * R7_PXDB) < 8:
            est.append(yc - k * 10 * R7_PXDB)
    # time axis: 0 ms at the y-axis, 100..500 ms at the axis ticks (short marks up from the baseline with no ink
    # above them); the scan stretches unevenly (up to 4 ms by 500 ms at a fixed px/ms), so map piecewise between them
    bt = min(y for y in range(yb - 10, yb + 10) if out[y, xi + 400:xi + 1500].mean() > 0.3)
    kx, kt = [xa], [0.0]
    for k in range(1, 6):
        lo, hi = int(xa + (100 * k - 12) * R7_PXMS), min(int(xa + (100 * k + 12) * R7_PXMS), out.shape[1])
        xs = [x for x in range(lo, hi) if out[bt - 12:bt - 2, x].sum() >= 6 and out[bt - 60:bt - 14, x].sum() < 5]
        if xs and max(xs) - min(xs) <= 8:
            kx.append(float(np.mean(xs)))
            kt.append(100.0 * k)
    if len(kx) == 1:
        kx, kt = [xa, xa + 500 * R7_PXMS], [0.0, 500.0]
    return {"m": out, "yb": yb, "xa": xa, "skew": a, "y0db": float(np.median(est)) if est else yb - R7_ZERO,
            "origin": (X0, Y0, y0), "kx": np.array(kx), "kt": np.array(kt)}


def r7_t(P: dict, x: float) -> float:
    """Time (ms) at panel column x: piecewise linear through the axis ticks, extrapolated from the last segment."""
    kx, kt = P["kx"], P["kt"]
    if x <= kx[-1] or len(kx) < 2:
        return float(np.interp(x, kx, kt)) if len(kx) > 1 else (x - P["xa"]) / R7_PXMS
    return float(kt[-1] + (x - kx[-1]) * (kt[-1] - kt[-2]) / (kx[-1] - kx[-2]))


def r7_x(P: dict, t: float) -> float:
    """Inverse of r7_t."""
    kx, kt = P["kx"], P["kt"]
    if t <= kt[-1]:
        return float(np.interp(t, kt, kx))
    return float(kx[-1] + (t - kt[-1]) * (kx[-1] - kx[-2]) / (kt[-1] - kt[-2]))


def r7_db(P: dict, y: float) -> float:
    return -(y - P["y0db"]) / R7_PXDB


def r7_top(P: dict, cols: list[int], start: int = -30) -> float:
    """Level of the highest ink in these columns at or below 0 dB + `start` px (specks ignored)."""
    ytop = int(P["y0db"] + start)
    sub = P["m"][ytop:P["yb"] - 3, cols].any(axis=1)
    ink = np.where(sub)[0]
    t = next((j for j in ink if sub[j:j + 12].sum() >= 4), None)
    return np.nan if t is None else r7_db(P, ytop + t)


def r7_lines4(P: dict, labelled: bool) -> list[dict]:
    """Size-4.0 lines (separated): centre from the lower-band ink, level from the highest ink in the line's columns.
    On R panels a top under one of the L panel's time labels (100..500 ms) that is a label stroke, not a solid line
    top, is read again from below the label band."""
    xi = int(round(P["xa"])) + 6
    cnt = P["m"][P["yb"] - 203:P["yb"] - 3, xi:int(r7_x(P, 505))].sum(axis=0)
    out = []
    for q in r7_groups(np.where(cnt >= 60)[0]):
        w = cnt[q].astype(float)
        xc = (np.array(q) * w).sum() / w.sum() + xi
        cols = [c + xi for c in q]
        t, db, kind = r7_t(P, xc), r7_top(P, cols), "measured"
        if labelled and min(abs(t - 100 * k) for k in range(1, 6)) < 12:
            y = int(P["y0db"] - db * R7_PXDB)
            if P["m"][y:y + 40, cols].any(axis=1).mean() < 0.5:
                db, kind = r7_top(P, cols, R7_LABEL_PX), "measured (under label)"
        out.append({"time_ms": t, "gain_db": db, "width_px": len(q), "kind": kind})
    return [q for q in out if q["time_ms"] > 5]  # the y-axis itself


def r7_visible(P: dict, labelled: bool, t1: float = 200.0) -> list[dict]:
    """Visible line tops at room size 1.0: runs of present columns with one top level (within 0.25 dB). A run as
    wide as a line or wider is a line (flat runs of 7+ px hold two equal lines); a narrower run next to a taller one
    is a line partly hidden by it, centred half a line further under the neighbour."""
    xi = int(round(P["xa"]))
    n = int(t1 * R7_PXMS)
    top = np.full(n, np.nan)
    for i in range(6, n):
        if P["m"][P["yb"] - 103:P["yb"] - 3, xi + i].mean() >= 0.5:
            top[i] = r7_top(P, [xi + i])
            t = r7_t(P, xi + i)
            if labelled and min(abs(t - 100 * k) for k in range(1, 6)) < 12:
                y = int(P["y0db"] - top[i] * R7_PXDB)
                if P["m"][y:y + 40, xi + i].mean() < 0.5:  # a label stroke, not a line top
                    top[i] = r7_top(P, [xi + i], R7_LABEL_PX)
    runs, i = [], 0
    while i < n:
        if np.isnan(top[i]):
            i += 1
            continue
        j = i
        while j + 1 < n and not np.isnan(top[j + 1]) and abs(top[j + 1] - top[i]) <= 0.25:
            j += 1
        runs.append([i, j, float(np.nanmax(top[i:j + 1]))])
        i = j + 1
    lines = []
    for k, (a, b, db) in enumerate(runs):
        w = b - a + 1
        left = runs[k - 1] if k and runs[k - 1][1] == a - 1 else None
        right = runs[k + 1] if k + 1 < len(runs) and runs[k + 1][0] == b + 1 else None
        hi = [r for r in (left, right) if r and r[2] > db]
        if w == 1 and (hi or db < max([r[2] for r in (left, right) if r] or [db]) - 3):
            continue  # an edge or broken column, not a top
        c = (a + b) / 2
        if w < R7_LINE_PX and hi:
            r = max(hi, key=lambda r: r[2])
            c += (R7_LINE_PX - w) / 2 * (1 if r is right else -1)
        nl = max(1, round(w / R7_LINE_PX)) if w >= 7 else 1
        for s in range(nl):
            cs = a + (s + 0.5) * w / nl if nl > 1 else c
            lines.append({"time_ms": r7_t(P, xi + cs), "gain_db": db, "width_px": w,
                          "kind": "split" if nl > 1 else ("measured" if w >= R7_LINE_PX - 1 or not hi else "partly hidden")})
    return lines


def r7_fit(vis: list[dict], ref: list[tuple[float, float]]) -> tuple[float, float]:
    """T0, s of t1 = T0 + (t4 - 20) * s that best fit the visible tops (grid, then least squares on matches)."""
    tv = np.array([v["time_ms"] for v in vis])
    best = None
    for T0 in np.arange(17.0, 23.0, 0.1):
        for s in np.arange(0.22, 0.28, 0.0025):
            d = np.array([np.min(np.abs(tv - (T0 + (t - 20) * s))) for t, _ in ref])
            sc = np.minimum(d, R7_MATCH_MS).sum()
            if best is None or sc < best[0]:
                best = (sc, T0, s)
    return best[1], best[2]


def r7_guided(vis: list[dict], ref: list[tuple[float, float]]) -> tuple[list[dict], tuple[float, float]]:
    """Order-preserving alignment of the scaled size-4.0 lines with the visible tops (dynamic programming). A
    matched line takes the measured time and level; a visible top of two line widths or more may take two lines
    (split at its centre); a size-4.0 line with no visible top is hidden under a neighbour and keeps its scaled
    time and size-4.0 level; a visible top with no size-4.0 line is kept as an extra."""
    T0, s = r7_fit(vis, ref)
    A = [(T0 + (t - 20) * s, g) for t, g in ref]
    nA, nV = len(A), len(vis)

    def mc(a, v):
        dt, dd = abs(v["time_ms"] - a[0]), abs(v["gain_db"] - a[1])
        return dt + 0.15 * dd if dt <= R7_MATCH_MS + 1.2 and dd <= 3 else None

    INF = 1e9
    D = np.full((nA + 1, nV + 1), INF)
    B: dict = {}
    D[0, 0] = 0
    for i in range(nA + 1):
        for j in range(nV + 1):
            if D[i, j] >= INF:
                continue
            steps = [(i + 1, j, R7_MATCH_MS, "hide"), (i, j + 1, R7_MATCH_MS, "extra")]
            if i < nA and j < nV and (c := mc(A[i], vis[j])) is not None:
                steps.append((i + 1, j + 1, c, "match"))
                if i + 1 < nA and vis[j]["width_px"] >= 6 and (c2 := mc(A[i + 1], vis[j])) is not None:
                    steps.append((i + 2, j + 1, c + c2, "match2"))
            for ni, nj, c, op in steps:
                if ni <= nA and nj <= nV and D[i, j] + c < D[ni, nj]:
                    D[ni, nj] = D[i, j] + c
                    B[(ni, nj)] = (i, j, op)
    out, i, j = [], nA, nV
    while (i, j) != (0, 0):
        pi, pj, op = B[(i, j)]
        if op == "hide":
            out.append({"time_ms": A[pi][0], "gain_db": A[pi][1], "width_px": 0, "kind": "hidden (scaled 4.0)"})
        elif op == "extra":
            out.append(dict(vis[pj], kind="extra (not in 4.0)"))
        elif op == "match":
            out.append(dict(vis[pj]))
        else:  # two lines under one wide top: half a line either side of its centre
            v, h = vis[pj], R7_LINE_PX / 2 / R7_PXMS
            out += [dict(v, time_ms=v["time_ms"] - h, kind="split"), dict(v, time_ms=v["time_ms"] + h, kind="split")]
        i, j = pi, pj
    out.sort(key=lambda q: q["time_ms"])
    # an extra top within a line width of a matched line and close in level is that line's ragged top edge
    read = [q for q in out if q["kind"] in ("measured", "partly hidden", "split")]
    out = [q for q in out if q["kind"] != "extra (not in 4.0)" or not any(
        abs(q["time_ms"] - r["time_ms"]) <= R7_LINE_PX / R7_PXMS and abs(q["gain_db"] - r["gain_db"]) <= 1.5 for r in read)]
    return out, (T0, s)


def r7_confidence(mode: str) -> str:
    return {"LARGE HALL": "medium (follows size 4.0 x0.25; 600 dpi, guided by the 4.0 lines)",
            "REVERSE": "medium (follows size 4.0 x0.25; 600 dpi, guided by the 4.0 lines)",
            "PLATE": "very low (solid block: envelope steps only, not individual lines)"}.get(
        mode, "low (600 dpi; lines 0-2 scan px apart merge; not size 4.0 scaled, read unguided)")


def run_rev7(pages_dir: Path, out: Path) -> None:
    pages = {p: np.array(Image.open(pages_dir / REV7_PAGE_FILE.format(p)).convert("L")) < 128 for p in (23, 24)}
    rows, imgs, rows4, imgs4 = [], [], [], []
    for p, mode, yl, yr, x1, x4 in R7_PANELS:
        for ch, y0 in (("L", yl), ("R", yr)):
            P1 = r7_panel(pages[p], 2 * y0, int(2 * x1))
            P4 = r7_panel(pages[p], 2 * y0, int(2 * x4))
            l4 = r7_lines4(P4, ch == "R")
            ref = [(q["time_ms"], q["gain_db"]) for q in l4]
            for k, q in enumerate(l4, 1):
                rows4.append([R7_NUMBERS[mode], mode, "4.0", ch, k, f"{q['time_ms']:.1f}", f"{round(min(q['gain_db'], 0.0), 1) + 0.0:.1f}",
                              q["kind"], R7_CONF4])
            imgs4.append(r7_overlay(P4, f"{mode} {ch} size 4.0", l4, [], None, fx=1))
            vis = r7_visible(P1, ch == "R")
            fit = None
            if mode in R7_GUIDED:
                lines, fit = r7_guided(vis, ref)
            else:
                lines = vis
            for k, q in enumerate(lines, 1):
                rows.append([R7_NUMBERS[mode], mode, "1.0", ch, k, f"{q['time_ms']:.1f}", f"{round(min(q['gain_db'], 0.0), 1) + 0.0:.1f}",
                             q["kind"], r7_confidence(mode)])
            print(f"{mode} {ch}: {len(lines)} lines ({len(vis)} visible tops)"
                  + (f", fit T0 {fit[0]:.1f} ms, s {fit[1]:.4f}" if fit else ""))
            imgs.append(r7_overlay(P1, f"{mode} {ch} size 1.0", lines, ref, fit))
    for name, rr in (("rev7_er_taps_size1_lowconf.csv", rows), ("rev7_er_taps_size4.csv", rows4)):
        with (out / name).open("w", newline="") as f:
            w = csv.writer(f, lineterminator="\n")
            w.writerow(["mode", "mode_name", "room_size", "channel", "tap", "time_ms", "gain_db", "kind", "confidence"])
            w.writerows(rr)
    for tag, im in (("size1", imgs), ("size4", imgs4)):
        for i in range(0, len(im), 2):
            a, b = im[i], im[i + 1]
            big = Image.new("RGB", (max(a.size[0], b.size[0]), a.size[1] + b.size[1] + 10), "white")
            big.paste(a, (0, 0))
            big.paste(b, (0, a.size[1] + 10))
            big.save(out / f"rev7_er_{tag}_{R7_PANELS[i // 2][1].lower().replace(' ', '_')}.png")


R7_COLOURS = {"measured": (230, 0, 0), "measured (under label)": (0, 110, 255), "partly hidden": (255, 140, 0), "split": (200, 0, 200),
              "hidden (scaled 4.0)": (0, 170, 0), "extra (not in 4.0)": (0, 110, 255)}


def r7_overlay(P: dict, title: str, lines: list[dict], ref: list, fit, fx: int = 4) -> Image.Image:
    """Numbered markers on the deskewed panel, fx times wide; grey ticks: size 4.0 scaled by the fit."""
    t1 = max(q["time_ms"] for q in lines) + 4
    lo = min(q["gain_db"] for q in lines)
    x0, x1 = int(r7_x(P, 15)), int(r7_x(P, t1))
    y0 = int(P["y0db"] - 25)
    y1 = int(min(P["yb"] + 5, P["y0db"] - (lo - 6) * R7_PXDB))
    c = P["m"][y0:y1, x0:x1]
    im = Image.fromarray(np.where(c, 0, 255).astype(np.uint8)).convert("RGB").resize((c.shape[1] * fx, c.shape[0]), Image.NEAREST)
    pad = Image.new("RGB", (max(im.size[0], 500), im.size[1] + 34), "white")
    pad.paste(im, (0, 34))
    d = ImageDraw.Draw(pad)
    d.text((2, 0), title, fill=(0, 0, 0))
    step = 1 if fx > 1 else 5
    for t in range(20, int(t1) + 1, step):
        X = (r7_x(P, t) - x0) * fx
        d.line((X, 22, X, 32 if t % (5 * step) == 0 else 27), fill=(255, 0, 0))
        if t % (5 * step * (1 if fx > 1 else 2)) == 0:
            d.text((X + 2, 12), str(t), fill=(255, 0, 0))
    if fit:
        for t4, g4 in ref:
            X = (r7_x(P, fit[0] + (t4 - 20) * fit[1]) - x0) * fx
            Y = P["y0db"] - g4 * R7_PXDB - y0 + 34
            d.line((X - 6, Y - 4, X + 6, Y - 4), fill=(150, 150, 150), width=1)
    for k, q in enumerate(lines, 1):
        X = (r7_x(P, q["time_ms"]) - x0) * fx
        Y = P["y0db"] - q["gain_db"] * R7_PXDB - y0 + 34
        col = R7_COLOURS.get(q["kind"], (0, 0, 0))
        d.line((X - 5, Y, X + 5, Y), fill=col, width=3)
        d.text((X - 4, Y - 13 - 11 * (k % 2)), str(k), fill=col)
    return pad


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--pages", type=Path, default=PAGES_DEFAULT)
    ap.add_argument("--out", type=Path, default=Path("."))
    ap.add_argument("--unit", choices=("rev5", "rev7"), default="rev5",
                    help="rev7: REV7 room size 1.0 from 600 dpi renders (--pages must hold them)")
    a = ap.parse_args()
    a.out.mkdir(parents=True, exist_ok=True)
    if a.unit == "rev7":
        run_rev7(a.pages, a.out)
        return
    pages = {p: load(a.pages / PAGE_FILE.format(p)) for p in (23, 24)}
    measured: dict[tuple, list[dict]] = {}
    completed: dict[tuple, list[dict]] = {}
    geom: dict[tuple, tuple[int, int]] = {}
    for p, mode, lab, ch, yb0, xa0 in PANELS:
        yb, xa = calibrate(pages[p], yb0, xa0)
        geom[(p, mode, lab, ch)] = (yb, xa)
        c = columns(pages[p], yb, xa, labelled=(ch == "R"))
        st = stems(c)
        measured[(p, mode, lab, ch)] = [dict(q) for q in st]
        completed[(p, mode, lab, ch)] = complete([dict(q) for q in st], c, mode)
    cross_channel(completed)
    for name, data in (("rev5_er_taps_raw.csv", measured), ("rev5_er_taps_completed.csv", completed)):
        rows = []
        for (p, mode, lab, ch), st in data.items():
            for k, q in enumerate(sorted(st, key=lambda z: z["time_ms"]), 1):
                rows.append({"page": p, "mode": mode, "setting": lab, "channel": ch, "tap": k,
                             "time_ms": round(float(q["time_ms"]), 1), "gain_db": round(float(q["gain_db"]), 1),
                             "width_px": q["width_px"], "kind": q["kind"], "label_cut": "yes" if q["flag"] else ""})
        with (a.out / name).open("w", newline="") as f:
            w = csv.DictWriter(f, fieldnames=list(rows[0]))
            w.writeheader()
            w.writerows(rows)
    write_data_tables(completed, a.out)
    try:
        font = ImageFont.truetype("/System/Library/Fonts/Helvetica.ttc", 15)
    except OSError:
        font = ImageFont.load_default()
    for p in (23, 24):
        keys = [k for k in completed if k[0] == p]
        for mode in dict.fromkeys(k[1] for k in keys):
            for lab in dict.fromkeys(k[2] for k in keys if k[1] == mode):
                im = Image.fromarray(pages[p].astype(np.uint8)).convert("RGB")
                d = ImageDraw.Draw(im)
                boxes = []
                for ch in ("L", "R"):
                    yb, xa = geom[(p, mode, lab, ch)]
                    st = sorted(completed[(p, mode, lab, ch)], key=lambda z: z["time_ms"])
                    for k, q in enumerate(st, 1):
                        X = xa + q["time_ms"] * PXMS
                        Y = yb - TOP0 - q["gain_db"] * PXDB
                        col = COLOURS.get(q["kind"], (0, 0, 0))
                        d.line((X - 6, Y, X + 6, Y), fill=col, width=3)
                        d.text((X - 6, Y - 17 - 13 * (k % 2)), str(k), fill=col, font=font)
                    last = st[-1]["time_ms"] if st else 100
                    boxes.append((xa - 60, int(yb - TOP0 - 50), int(xa + PXMS * min(600, last + 40)), yb + 25))
                x0 = min(b[0] for b in boxes); y0 = min(b[1] for b in boxes)
                x1 = max(b[2] for b in boxes); y1 = max(b[3] for b in boxes)
                crop = im.crop((x0, y0, x1, y1))
                if crop.size[0] < 900:  # crowded short panels: enlarge so numbers are readable
                    f = max(1, int(900 / crop.size[0]))
                    crop = crop.resize((crop.size[0] * f, crop.size[1] * f), Image.NEAREST)
                crop.save(a.out / f"rev5_er_{mode.lower()}_{lab.replace(' ', '_').replace('.', 'p')}.png")
    n = sum(len(v) for v in completed.values())
    print(f"{sum(len(v) for v in measured.values())} lines measured, {n} after completion, {len(completed)} panels")


if __name__ == "__main__":
    main()
