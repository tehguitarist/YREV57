# tools/

Python helpers. Not part of the plugin build.

```bash
python -m venv .venv && source .venv/bin/activate
pip install -r tools/requirements.txt
```

| Path | Purpose |
| --- | --- |
| [`er-digitise/`](er-digitise/) | Digitise the early-reflection stem plots from the owner's-manual scans (the scans are not in the repository) |
| [`gen_er_tables.py`](gen_er_tables.py) | Generates `src/dsp/ErTables.h` from `data/er-taps/` |
| [`gen_factory_presets.py`](gen_factory_presets.py) | Generates `src/plugin/FactoryPresets.h` and `data/presets/manual_programs_mapping.csv` from the manual program charts |
| [`gen_ui_layout.py`](gen_ui_layout.py), [`export_ui_layout.py`](export_ui_layout.py) | UI layout generation and export |
| [`analysis/emphasis_response.py`](analysis/emphasis_response.py) | Compute pre-/de-emphasis responses from the schematic component values (`--csv` to write a table) |
| [`analysis/memory_budget.py`](analysis/memory_budget.py) | Delay-memory arithmetic for both units' DRAM banks |
