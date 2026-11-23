"""Pre-/de-emphasis responses from the component values read off the schematics.

Usage:  python tools/analysis/emphasis_response.py [--csv out.csv]

Both stages are inverting op-amp stages with a series R-C network in parallel
with one resistor. Values are schematic readings;
change them here if a full-resolution re-check disagrees.
"""
import argparse, csv, math

# --- Config: component values (ohms, farads) -------------------------------
EMPH = dict(r_in=10e3, r_fb=10e3, r_series=4.3e3, c_series=0.0036e-6)      # REV7 R213/R214/R212/C251; REV5 R104/C103 network
DEEMPH = dict(r_in=20e3, r_fb=15e3, r_series=6.2e3, c_series=0.0024e-6)    # REV5 R133/R134/C136, 20k input
FREQS = [20, 100, 500, 1000, 2000, 3150, 5000, 7000, 10000, 12200, 15000, 20000]
# ---------------------------------------------------------------------------

def z_rc(r, c, f):
    return r + 1 / (1j * 2 * math.pi * f * c)

def par(a, b):
    return a * b / (a + b)

def emphasis(f, r_in, r_fb, r_series, c_series):
    """Gain magnitude of Rf / (Rin || (Rs + 1/sC))."""
    return abs(r_fb / par(r_in, z_rc(r_series, c_series, f)))

def deemphasis(f, r_in, r_fb, r_series, c_series):
    """Gain magnitude of (Rf || (Rs + 1/sC)) / Rin."""
    return abs(par(r_fb, z_rc(r_series, c_series, f)) / r_in)

def db(x):
    return 20 * math.log10(x)

def corners():
    e, d = EMPH, DEEMPH
    tau = lambda r, c: 1 / (2 * math.pi * r * c)
    return {
        "emph_zero_hz": tau(e["r_series"] + e["r_in"], e["c_series"]),
        "emph_pole_hz": tau(e["r_series"], e["c_series"]),
        "emph_hf_asymptote_db": db(e["r_fb"] / par(e["r_in"], e["r_series"])),
        "deemph_pole_hz": tau(d["r_series"] + d["r_fb"], d["c_series"]),
        "deemph_zero_hz": tau(d["r_series"], d["c_series"]),
        "deemph_dc_gain_db": db(d["r_fb"] / d["r_in"]),
        "deemph_hf_asymptote_db": db(par(d["r_fb"], d["r_series"]) / d["r_in"]),
    }

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--csv", help="write the table to this CSV path")
    args = ap.parse_args()

    for k, v in corners().items():
        print(f"{k:24s} {v:9.1f}")
    rows = []
    dc = db(deemphasis(1.0, **DEEMPH))
    print(f"\n{'f (Hz)':>8} {'emph dB':>9} {'deemph dB':>10} {'deemph rel dB':>14} {'net rel dB':>11}")
    for f in FREQS:
        e = db(emphasis(f, **EMPH)); d = db(deemphasis(f, **DEEMPH))
        rows.append((f, round(e, 2), round(d, 2), round(d - dc, 2), round(e + d - dc, 2)))
        print(f"{f:8d} {e:9.2f} {d:10.2f} {d - dc:14.2f} {e + d - dc:11.2f}")
    if args.csv:
        with open(args.csv, "w", newline="") as fh:
            w = csv.writer(fh)
            w.writerow(["freq_hz", "emphasis_db", "deemphasis_abs_db", "deemphasis_rel_db", "net_rel_db"])
            w.writerows(rows)

if __name__ == "__main__":
    main()
