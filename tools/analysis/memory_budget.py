"""Delay-memory arithmetic for the REV7 and REV5 DRAM banks.

Usage:  python tools/analysis/memory_budget.py

Chip counts and organisation come from the service manuals. Six x4 chips side by side form one
24-bit-wide bank per DSP.
"""
# --- Config ----------------------------------------------------------------
UNITS = {
    "REV7": dict(fs=31_250, words_per_chip=16_384, chips_per_bank=6, banks=2, quoted_max_s=1.0),   # 900 ms + 100 ms initial
    "REV5": dict(fs=44_100, words_per_chip=65_536, chips_per_bank=6, banks=3, quoted_max_s=2.9),   # review figure
}
BITS_PER_CHIP = 4
# ---------------------------------------------------------------------------

for name, u in UNITS.items():
    width = u["chips_per_bank"] * BITS_PER_CHIP
    bank_s = u["words_per_chip"] / u["fs"]
    print(f"{name}: {u['banks']} banks x {u['words_per_chip']} words x {width} bit")
    print(f"  one bank  = {bank_s * 1000:7.1f} ms")
    for n in range(2, u["banks"] + 1):
        print(f"  {n} chained = {n * bank_s * 1000:7.1f} ms")
    print(f"  quoted max delay {u['quoted_max_s'] * 1000:.0f} ms -> needs {u['quoted_max_s'] / bank_s:.2f} banks")
