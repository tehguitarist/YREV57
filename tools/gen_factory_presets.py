#!/usr/bin/env python3
"""Generate src/plugin/FactoryPresets.h and data/presets/manual_programs_mapping.csv. Run from the repo root.

Two sources:
  1. PRESETS in the prototype (docs/design/prototype/index.html): our own generic presets (banks FACTORY, COMBI).
     Knob ids map to parameter IDs using the table in src/plugin/YrevEditor.cpp.
  2. The owner's-manual programs (banks REV7, REV5): the program names and settings PRINTED in the REV7 and REV5
     owner's manuals (ROM charts), read from data/presets/rev7_factory_presets.csv and rev5_factory_presets.csv and
     mapped onto our parameters. Policy: ADR 0004 as amended (printed manual data only; never firmware, ROM dumps or
     SysEx). Every value is tagged in the mapping CSV as
       printed  - a value the manual prints for that program, mapped one to one;
       derived  - computed from printed values (unit conversion or a split across our controls);
       ours     - our choice where the manual prints nothing for our control (marked "ours" in the docs).
     Analogue EQ, mix, width, ducker, character and dither stay at their defaults (not stored in the hardware
     programs). Output level, wet input, BALANCE, OUT LVL and OUT PHASE are not stored in presets."""
import csv, json, re

PROTO = 'docs/design/prototype/index.html'
EDITOR = 'src/plugin/YrevEditor.cpp'
PARAMS_H = 'src/plugin/Parameters.h'
OUT = 'src/plugin/FactoryPresets.h'
CSV5 = 'data/presets/rev5_factory_presets.csv'
CSV7 = 'data/presets/rev7_factory_presets.csv'
MAPPING = 'data/presets/manual_programs_mapping.csv'

# ---- config for the manual programs -----------------------------------------------------------------------------
THRU = 17.0               # wet_lpf / fx_lpf value for THRU (kLpfThruKHz)
SRC7 = 'REV7 owner\'s manual'
SRC5 = 'REV5 owner\'s manual'
# ER mode index (er_mode choice list): Small Hall, Large Hall, Random, Reverse, Plate, Spring, Pan A, Pan B, Piano,
# Organ, Brass, Guitar, Rev Hall, Rev Plate, Rev Room. REV7 prints 1-6 (index = mode - 1); REV5 prints names.
ER5_MODES = {'S-HALL': 0, 'L-HALL': 1, 'RANDOM': 2, 'REVERSE': 3, 'PLATE': 4, 'SPRING': 5, 'PAN-A': 6, 'PAN-B': 7,
             'PIANO': 8, 'ORGAN': 9, 'BRASS': 10, 'GUITAR': 11}
# The instrument ER modes (Piano 8, Organ 9, Brass 10, Guitar 11) are hidden until there is data for them (Q-01).
# A program that prints one (only Rehearsal Room: PIANO) gets a charted stand-in: Small Hall, the nearest room
# (inference; the same pattern the placeholder already used).
INSTRUMENT_STAND_IN = {9: 1, 10: 4, 11: 5}  # Piano (8) plays its own pattern (pass 52) and has a key (2026-11-25)
# The REV7 reverb programs print no ER mode: "3 early reflections are programmed for each channel" (REV7 OM p. 14,
# OCR). Each plays the set read from its own hardware capture (pass 40, docs/design/engine.md): Rev Hall 12, Rev Plate 13
# and Rev Room 14 are shared (Strings plays the plate set; Small Hall, Handclaps and Reverb Flange the room set), and
# Percussion Plate, Snare, Spring and Echo Room have their own (15-18).
REVERB_ER_MODE = {'Large Hall': 12, 'Small Hall': 14, 'Vocal Plate': 13, 'Percussion Plate': 15, 'Spring': 17,
                  'Echo Room': 18, 'Strings': 19, 'Snare': 16, 'Handclaps': 14}
# The REV5 reverb programs print no ER mode: they play their own early taps (Rev Hall 12, Rev Plate 13; read from the
# REV5 captures, twenty-second pass, docs/design/engine.md). The plates take Rev Plate, everything else Rev Hall.
REV5_REVERB_ER_MODE = {'Large Hall': 12, 'Small Hall': 14, 'Vocal Plate': 13, 'Percussion Plate': 15, 'Spring': 17,
                       'Echo Room': 18, 'Strings': 19, 'Snare': 16, 'Reverb & Gate': 12}
REV_NOTE = "the reverb program's own early taps (no ER mode on the REV5's reverb programs; read from its hardware capture, pass 41)"
REV_NOTE_COMBI = "the combination programs' shared early taps (read from the hardware captures of 95 and 96, pass 41)"
FX = ['delay', 'echo', 'mod', 'pitch', 'pan', 'off']
MOD = ['flange', 'revflange', 'chorusA', 'chorusB', 'phasing', 'tremolo', 'symph']
REV5_REVERB = {1, 2, 3, 4, 16, 17, 18, 22}
REV5_ER = {5, 6, 19, 20, 21, 24, 25, 30}
REV5_MOD = {9: 'flange', 10: 'revflange', 11: 'chorusA', 12: 'chorusB', 13: 'phasing', 14: 'tremolo', 15: 'symph'}
REV7_MOD = dict(REV5_MOD)  # the REV7 has the same 9-15 line-up
REV5_COMBO_MOD = {92: 'chorusA', 93: 'symph', 95: 'symph'}
REV5_COMBO_REVFIRST = {95, 96, 97}  # reverb then effect; 91-94 are effect then reverb
# Engine settings that make our always-on reverb engine as quiet as it goes, for programs that have no reverb
# (ours: the engine has no off switch; see docs/design/presets.md, mapping gaps)
NO_REVERB = {'rt': 0.3, 'er_rev_balance': 100.0, 'er_count': 1.0, 'liveness': 0.0, 'room_size': 0.1}

# ---- the prototype presets (unchanged behaviour) ------------------------------------------------------------------
src = open(PROTO).read()
a = src.index('var PRESETS = ['); b = src.index('];', a)
presets = []
for line in src[a + len('var PRESETS = ['):b].strip().split('\n'):
    line = line.strip().rstrip(',')
    if not line.startswith('{'): continue
    t = re.sub(r"(\b[A-Za-z_][A-Za-z0-9_]*)\s*:", r'"\1":', line).replace("'", '"')
    t = re.sub(r'(?<![\d"])\.(\d)', r'0.\1', t)
    presets.append(json.loads(t))

# id::name -> "string"
ids = dict(re.findall(r'inline constexpr auto (\w+)\s*=\s*"([\w]+)"', open(PARAMS_H).read()))
VALID_IDS = set(ids.values())
# tolerant of clang-format's spacing ({"rt", id::rt, ...} or { "rt", id::rt, ...})
kmap = {k: ids[v] for k, v in re.findall(r'\{\s*"(\w+)",\s*id::(\w+),', open(EDITOR).read())}
assert len(kmap) > 40, 'knob table not found in ' + EDITOR

# list of (name, bank, {id: value}) in output order
entries = []
for p in presets:
    if p['bank'] == 'USER': continue  # the prototype's sample user presets are not shipped
    vals = {kmap[k]: v for k, v in p['v'].items() if k in kmap}
    vals['era'] = 1 if p.get('era') == 5 else 0
    vals['er_mode'] = p.get('er', 0)
    vals['fx_type'] = FX.index(p.get('fx', 'off'))
    vals['fx_order'] = ['fxrev', 'revfx'].index(p.get('order', 'fxrev'))
    vals['mod_type'] = MOD.index(p.get('mod', 'chorusA'))
    for k, v in p.get('x', {}).items():  # raw parameter IDs with no prototype knob (e.g. aeq_on)
        assert k in VALID_IDS, k
        vals[k] = v
    entries.append((p['name'], p['bank'], vals))


# ---- the manual programs ------------------------------------------------------------------------------------------
class Prog:
    def __init__(self, unit, number, name, era, source):
        self.unit, self.number, self.name, self.source = unit, number, name, source
        self.v, self.basis, self.note = {}, {}, {}
        self.put('era', 1 if era == 5 else 0, 'derived', 'unit')

    def put(self, pid, val, basis, note=''):
        assert pid in VALID_IDS, pid
        self.v[pid] = float(val)
        self.basis[pid], self.note[pid] = basis, note

    def pr(self, pid, val, note=''):  # printed (skipped when the chart prints nothing)
        if val is not None: self.put(pid, val, 'printed', note)

    def ours(self, pid, val, note=''): self.put(pid, val, 'ours', note)
    def derived(self, pid, val, note=''): self.put(pid, val, 'derived', note)

    @property
    def title(self): return self.name  # the bank tab says which unit; presets are keyed by bank + name


def num(s):
    if s is None or s == '': return None
    if s.strip().upper() == 'THRU': return THRU
    return float(s)


def base_choices(p, er_mode, fx, order=0, mod=2):
    p.ours('er_mode', er_mode, 'our ER pattern; the chart prints none for this program')
    p.ours('fx_type', FX.index(fx), 'FX slot use')
    p.ours('fx_order', order, 'effect then reverb' if order == 0 else 'reverb then effect')
    p.ours('mod_type', mod if isinstance(mod, int) else MOD.index(mod), 'FX slot use')


def no_reverb(p):
    for k, v in NO_REVERB.items(): p.ours(k, v, 'reverb engine at its minimum: the program has no reverb')
    p.ours('rev2_level', 0, 'no secondary reverb')
    p.ours('rev_mix', 0, 'effect-only program: no reverb beside the effect (hardware routing, REV MIX 0)')


def damp_from_high(high):  # REV5 HIGH x0.1-1.0 -> echo_damp 1-10 (ratio = damp / 10)
    return round(high * 10.0)


def build5(r):
    n = int(r['number']); nm = r['name']
    p = Prog('REV5', n, nm, 5, SRC5 + ' ' + r['source'].replace('REV5 OM ', ''))
    g = lambda k: num(r.get(k))
    # common: initial delay, 1st reflections (C, L, R), digital EQ, gate level
    p.pr('pre_delay', g('initial_delay_ms'))
    p.pr('ref1_delay', g('ref_c_delay_ms')); p.pr('ref1_level', g('ref_c_level_pct'))
    p.pr('ref1_l_delay', g('ref_l_delay_ms')); p.pr('ref1_l_level', g('ref_l_level_pct'))
    p.pr('ref1_r_delay', g('ref_r_delay_ms')); p.pr('ref1_r_level', g('ref_r_level_pct'))
    p.pr('deq_on', 1 if r['eq_on'] == 'ON' else 0)
    p.pr('deq_lo_shelf', 0 if r['eq_low_type'] == 'PEAK' else 1)
    p.pr('deq_lo_f', g('eq_low_freq_hz')); p.pr('deq_lo_g', g('eq_low_gain_db')); p.pr('deq_lo_q', g('eq_low_q'))
    p.pr('deq_mid_f', g('eq_mid_freq_hz')); p.pr('deq_mid_g', g('eq_mid_gain_db')); p.pr('deq_mid_q', g('eq_mid_q'))
    p.pr('deq_hi_shelf', 0 if r['eq_high_type'] == 'PEAK' else 1)
    p.pr('deq_hi_f', g('eq_high_freq_hz')); p.pr('deq_hi_g', g('eq_high_gain_db')); p.pr('deq_hi_q', g('eq_high_q'))
    p.pr('gate_level', g('gate_level_pct'))
    # GATE LVL: the program's own noise gate, taken to sit on the input. A program with a trigger gate (TRG LEVEL,
    # program 23 and the combinations) keeps its gate on the effect output: the trigger block below owns the gate.
    if g('gate_level_pct') is not None and g('trg_level') is None:
        p.ours('gate_input', 1, 'GATE LVL is the program noise gate (pp. 22, 27-29, 34, 37); on the input (inference)')
    # gate block (program 23 and the combinations)
    if g('trg_level') is not None:
        p.pr('gate_level', g('trg_level'), 'TRG LEVEL (hardware MIDI TRG not mapped)')
        p.pr('gate_trig_delay', g('trg_delay_ms')); p.pr('gate_hold', g('hold_ms')); p.pr('gate_release', g('release_ms'))

    def reverb_block(with_bal=True):
        p.pr('rt', g('reverb_time_s')); p.pr('hi_ratio', g('hi_ratio')); p.pr('lo_ratio', g('lo_ratio'))
        p.pr('diffusion', g('diffusion')); p.pr('density', g('density')); p.pr('wet_lpf', g('lpf_khz'))
        if g('space_mod') is not None: p.pr('space_mod', g('space_mod'))
        else: p.ours('space_mod', 0, 'not printed on this program; modulation off like the others')
        if g('rev2_time_x') is not None or r.get('rev2_level_pct'):
            p.pr('rev2_time', g('rev2_time_x')); p.pr('rev2_delay', g('rev2_delay_ms')); p.pr('rev2_level', g('rev2_level_pct'))
        if with_bal:
            if g('er_rev_balance_pct') is not None:
                p.pr('er_rev_balance', g('er_rev_balance_pct'))
            else:
                p.ours('er_rev_balance', 54, 'same as Large Hall (54 % printed): this program prints none')

    if n in REV5_REVERB or n == 23:
        base_choices(p, REV5_REVERB_ER_MODE.get(nm, 12), 'off')
        p.ours('er_mode', REV5_REVERB_ER_MODE.get(nm, 12), REV_NOTE)
        reverb_block()
    elif n in REV5_ER:
        mode = ER5_MODES[r['er_mode']]
        base_choices(p, mode, 'off')
        if mode in INSTRUMENT_STAND_IN:  # instrument modes are hidden (Q-01); keep the printed name, play a stand-in
            p.ours('er_mode', INSTRUMENT_STAND_IN[mode], 'stand-in: the chart prints %s, hidden until there is data (Q-01)' % r['er_mode'])
        else:
            p.pr('er_mode', mode, r['er_mode'])
        p.pr('liveness', g('liveness')); p.pr('room_size', g('room_size')); p.pr('diffusion', g('diffusion'))
        p.pr('er_count', g('er_number')); p.pr('wet_lpf', g('lpf_khz')); p.pr('density', g('density'))
        # E/R1 or E/R2 as the ROM chart's type column prints it (pass 28: era-5 E/R2 diffuses the reflections, as pass 25 in era 7)
        if nm in KICK_ER2:  # pass 57: the capture plays a tail the chart's E/R1 has not
            p.derived('er_type', 1, KICK_ER2_NOTE % r['type'])
        else:
            p.pr('er_type', 1 if r['type'] == 'E/R2' else 0, 'TYPE ' + r['type'])
        p.pr('er_fb_delay', g('fb_delay_ms')); p.pr('er_fb_gain', g('fb_gain_pct')); p.pr('er_fb_hi', g('fb_high_x'))
        if g('space_mod') is not None: p.pr('space_mod', g('space_mod'))
        else: p.ours('space_mod', 0, 'not printed on this program; modulation off like the others')
        p.ours('rt', 0.3, 'reflections only: no late tail'); p.ours('er_rev_balance', 100, 'reflections only')
        p.ours('rev2_level', 0, 'no secondary reverb')
    elif n == 7:
        base_choices(p, 0, 'delay'); no_reverb(p)
        p.pr('dly_l', g('delay_l_ms')); p.pr('dly_fb', g('feedback_l_pct'))
        p.pr('dly_r', g('delay_r_ms')); p.pr('dly_fb_r', g('feedback_r_pct')); p.pr('dly_high', g('delay_high_x'))
        p.pr('fx_lpf', g('lpf_khz')); p.ours('dly_level', 100, 'no level control on the REV5 delay; BALANCE is not stored')
    elif n == 8:
        base_choices(p, 0, 'echo'); no_reverb(p)
        p.pr('echo_l', g('delay_l_ms')); p.pr('echo_r', g('delay_r_ms'))
        # The chart prints feedback L 60 % / R 58 % (p. 63). With the left repeat also arriving first, that pulled the
        # image hard left; both sides take the mean (maintainer's decision, 2026-11-22). Times stay as printed.
        fb = (g('feedback_l_pct') + g('feedback_r_pct')) / 2.0
        note = 'printed L %s / R %s %%; both at the mean so the repeats do not drift left (maintainer, 2026-11-22)' % (
            r['feedback_l_pct'], r['feedback_r_pct'])
        p.ours('echo_fb', fb, note); p.ours('echo_fb_r', fb, note)
        p.derived('echo_damp', damp_from_high(g('delay_high_x')), 'HIGH x%s -> echo_damp %d (ratio = value / 10)' % (r['delay_high_x'], damp_from_high(g('delay_high_x'))))
        p.pr('fx_lpf', g('lpf_khz')); p.ours('echo_level', 100, 'no level control on the REV5 echo; BALANCE is not stored')
    elif n == 10:
        # REV5 Reverb Flange: "the same REV TIME, HIGH, LOW and DIFFUSION parameters as the reverb type effects" (OM
        # p. 29, OCR), and the ROM chart prints the reverb block (p. 65): a reverb program with a flange on it, unlike
        # the REV7's (a flange whose feedback becomes a decay). The engine at the printed settings on the Rev Room
        # taps (its hardware capture shares Small Hall's, pass 41); the slot's
        # plain flange before it, its feedback at the REV7 program's fixed 30 % (the REV5 prints none; inference).
        base_choices(p, 14, 'mod', mod='flange')
        p.ours('er_mode', 14, REV_NOTE)
        reverb_block()
        p.pr('lfo_rate', g('mod_freq_hz')); p.pr('mod_depth', g('mod_depth_pct')); p.pr('mod_delay', g('mod_delay_ms'))
        p.ours('mod_fb', 30, 'feedback fixed at 30 % as in the REV7 Reverb Flange (REV7 OM p. 16); the REV5 prints none')
        p.ours('fx_lpf', 17, 'LPF FRQ is the reverb\'s (wet_lpf), not applied twice')
        p.ours('fx_wet', 1, 'flange into the reverb with no bypass: the REV5 Reverb Flange capture shows no flanged direct signal')
    elif n in REV5_MOD:
        base_choices(p, 0, 'mod', mod=REV5_MOD[n]); no_reverb(p)
        p.pr('lfo_rate', g('mod_freq_hz')); p.pr('fx_lpf', g('lpf_khz'))
        p.pr('mod_depth', g('mod_depth_pct')); p.pr('dm_depth', g('dm_depth_pct')); p.pr('am_depth', g('am_depth_pct'))
        p.pr('mod_delay', g('mod_delay_ms')); p.pr('mod_fb', g('mod_fb_gain_pct'))
    elif n in (26, 27, 28):
        base_choices(p, 0, 'pitch'); no_reverb(p)
        p.pr('pitch_sel', n - 26); p.pr('fx_lpf', g('lpf_khz'))
        p.pr('pitch_shift', g('pitch1_semitones')); p.pr('pitch_fine', g('pitch1_fine_cents')); p.pr('pitch_delay', g('pitch1_delay_ms'))
        if n == 26: p.pr('pitch_fb', g('pitch_fb_pct'))
        else:
            p.pr('pitch2_shift', g('pitch2_semitones')); p.pr('pitch2_fine', g('pitch2_fine_cents')); p.pr('pitch2_delay', g('pitch2_delay_ms'))
    elif n == 29:
        base_choices(p, 0, 'pan'); no_reverb(p)
        p.pr('pan_rate', g('pan_speed_hz')); p.pr('pan_depth', g('pan_depth_pct')); p.pr('fx_lpf', g('lpf_khz'))
        p.pr('pan_dir', {'L->R': 0, 'L<-R': 1, 'L<->R': 2}[r['pan_direction']])
    elif 91 <= n <= 97:
        order = 1 if n in REV5_COMBO_REVFIRST else 0
        fx = {91: 'echo', 92: 'mod', 93: 'mod', 94: 'pitch', 95: 'mod', 96: 'pan', 97: 'pitch'}[n]
        base_choices(p, 20, fx, order, REV5_COMBO_MOD.get(n, 2))
        p.ours('er_mode', 20, REV_NOTE_COMBI)
        reverb_block(); p.ours('rev2_level', 0, 'combination programs print no secondary reverb')
        p.pr('rev_mix', g('rev_mix_pct'), 'REV MIX: the reverb beside the effect\'s bypass (block diagram, OM pp. 37-39)')
        if n == 91:
            p.pr('echo_l', g('delay_l_ms'), 'one DELAY on this program'); p.derived('echo_r', g('delay_l_ms'), 'same single delay')
            p.pr('echo_fb', g('feedback_l_pct')); p.derived('echo_fb_r', g('feedback_l_pct'), 'same single feedback')
            p.derived('echo_damp', damp_from_high(g('delay_high_x')), 'F.B. HIGH x%s -> damp %d' % (r['delay_high_x'], damp_from_high(g('delay_high_x'))))
            p.ours('echo_level', 100, 'no level control printed')
        elif n in (92, 93, 95):
            p.pr('lfo_rate', g('mod_freq_hz')); p.pr('mod_depth', g('mod_depth_pct')); p.pr('dm_depth', g('dm_depth_pct')); p.pr('am_depth', g('am_depth_pct'))
        elif n in (94, 97):
            p.pr('pitch_sel', 0 if n == 94 else 2)
            p.pr('pitch_shift', g('pitch1_semitones')); p.pr('pitch_fine', g('pitch1_fine_cents')); p.pr('pitch_delay', g('pitch1_delay_ms'))
            if n == 94: p.pr('pitch_fb', g('pitch_fb_pct'))
            else:
                p.pr('pitch2_shift', g('pitch2_semitones')); p.pr('pitch2_fine', g('pitch2_fine_cents')); p.pr('pitch2_delay', g('pitch2_delay_ms'))
        elif n == 96:
            p.pr('pan_rate', g('pan_speed_hz')); p.pr('pan_depth', g('pan_depth_pct')); p.pr('pan_dir', 0)
    elif n == 98:
        base_choices(p, ER5_MODES[r['er_mode']], 'off')
        p.pr('er_mode', ER5_MODES[r['er_mode']], r['er_mode'])
        reverb_block()
        p.pr('liveness', g('liveness')); p.pr('room_size', g('room_size')); p.pr('er_count', g('er_number'))
        p.ours('rev2_level', 0, 'no secondary reverb')
        # ER DLY and REV DLY (p. 41: each from the input to the start of its sound) sit after the common initial
        # delay (inference); the reflections keep the initial delay (ER DLY 0.1 ms), the late network hears its input
        # REV DLY - ER DLY later (19.9 ms) through the hidden late onset
        # The REV5 capture of 98: its reverb is the combinations' network (91's, r 0.88) 21.8 ms after the
        # reflections, early taps and all, so the field is continuous from 10 ms; our late network beside a chart
        # carries no taps of its own, and REV DLY as a late onset left a hole at 30-60 ms (-38 against -15 dB).
        # Pass 67: the reverb plays the combinations' own tap set (voice_rev_taps 9, er_mode 20's) from REV DLY - ER
        # DLY (19.9 ms) as part of the reverb, and only those taps feed the late network; reflections +6 dB.
        # 0-200 ms per-channel envelope 3.48 -> 2.35 dB rms, C50 -2.4 -> -1.4 (capture -1.2)
        p.derived('voice_rev_taps', 9, 'the combinations\' tap set beside the chart: the REV5 capture of 98 is the combinations\' network (pass 55); pass 67')
        p.derived('voice_late_onset', round(g('rev_delay_ms') - g('er_delay_ms'), 3), 'REV DLY %s less ER DLY %s ms: where the reverb (its taps and late network) starts (pass 67)' % (r['rev_delay_ms'], r['er_delay_ms']))
        p.derived('voice_er_gain', 6.0, 'fitted on the REV5 capture of 98 with its reverb taps (pass 67; pass 55 +4)')
        p.derived('voice_diff_size', 0.6, 'the combinations\' diffusers (pass 55)')
        # Pass 86: the S-Hall times were re-measured on the E/R programs' captures (05 / 06); 98's own capture matches the
        # printed chart better at 10-40 ms (early-field match 0.26 against 0.19 at 40 ms), so 98 keeps the printed one
        p.derived('voice_er_printed', 1, 'S-Hall as printed: the REV5 capture of 98 fits the printed chart, 05 / 06 the re-measured times (pass 86)')
    elif n == 99:
        base_choices(p, 4, 'off')
        p.pr('rt', g('reverb_time_s'), 'REV1 TIME'); p.pr('hi_ratio', g('hi_ratio'), '1 HIGH'); p.pr('lo_ratio', g('lo_ratio'), '1 LOW')
        p.pr('diffusion', g('diffusion'), '1 DIFFUSION'); p.pr('density', g('density')); p.pr('wet_lpf', g('lpf_khz')); p.pr('space_mod', g('space_mod'))
        p.derived('rev2_time', round(g('rev2_time_s') / g('reverb_time_s'), 3), 'REV2 TIME 2.6 s / REV1 TIME 2.0 s')
        p.derived('rev2_delay', g('rev2_delay_ms') - g('rev1_delay_ms'), 'REV2 DLY 30 ms less REV1 DLY 10 ms (ours is relative to the primary)')
        p.ours('rev2_level', 100, 'REV1/2 BAL 50 %: the secondary at its full level')
        p.derived('voice_rev2_gain', 12.0, 'REV1/2 BAL 50 % read as equal levels (inference): +12 dB lifts the secondary\'s quarter share (pass 32) to the primary\'s')
        p.pr('voice_rev2_hi', g('rev2_hi_ratio'), '2 HIGH (the hall\'s own; 2 LOW 1.2 and 2 DIFFUSION 5 equal the plate\'s, so they share)')
        p.ours('er_rev_balance', 0, 'no ER on this program: late reverb only')
    else:
        raise ValueError('unmapped REV5 program %d' % n)
    return p


def build7(r):
    n = int(r['number']); nm = r['name']; typ = r['type']
    p = Prog('REV7', n, nm, 7, SRC7 + ' ' + r['source'].replace('REV7 OM ', ''))
    g = lambda k: num(r.get(k))
    p.pr('pre_delay', g('initial_delay_ms')); p.pr('ref1_delay', g('ref1_delay_ms')); p.pr('ref1_level', g('ref1_level_pct'))
    if typ == 'REV':
        base_choices(p, REVERB_ER_MODE[nm], 'off')
        p.ours('er_mode', REVERB_ER_MODE[nm], "the reverb program's early reflections, three per channel (REV7 OM p. 14; "
               "times and levels read from its hardware capture, pass 40)")
        p.pr('rt', g('reverb_time_s')); p.pr('hi_ratio', g('hi_ratio')); p.pr('lo_ratio', g('lo_ratio')); p.pr('diffusion', g('diffusion'))
        p.ours('rev2_level', 0, 'the REV7 has no secondary reverb')
    elif typ.startswith('E/R'):
        base_choices(p, int(g('er_mode')) - 1, 'off')
        p.pr('er_mode', int(g('er_mode')) - 1, 'E/R MODE %d' % int(g('er_mode')))
        p.pr('liveness', g('liveness')); p.pr('room_size', g('room_size')); p.pr('diffusion', g('diffusion'))
        p.ours('er_count', 18, 'the REV7 prints no count; 18 reflections per channel (era-7 maximum)')
        # E/R1 or E/R2 as the ROM chart's type column prints it (pass 25: E/R2 diffuses the reflections). Source
        # conflict: the manual's program notes call Gate Reverb an E/R1 program (OM printed p. 18, page image); the chart wins.
        if nm in KICK_ER2:  # pass 57: the capture plays a tail the chart's E/R1 has not
            p.derived('er_type', 1, KICK_ER2_NOTE % typ)
        else:
            p.pr('er_type', 1 if typ == 'E/R2' else 0, 'TYPE ' + typ + (' (the program notes say E/R1; ROM chart used)' if nm == 'Gate Reverb' else ''))
        p.ours('rt', 0.3, 'reflections only: no late tail'); p.ours('er_rev_balance', 100, 'reflections only')
        p.ours('rev2_level', 0, 'the REV7 has no secondary reverb')
    elif n == 7:
        base_choices(p, 0, 'delay'); no_reverb(p)
        p.pr('dly_l', g('delay_l_ms')); p.pr('dly_r', g('delay_r_ms')); p.pr('dly_level', g('level_pct'))
    elif n == 8:
        base_choices(p, 0, 'echo'); no_reverb(p)
        p.pr('echo_l', g('delay_l_ms')); p.pr('echo_r', g('delay_r_ms')); p.pr('echo_fb', g('feedback_pct'))
        p.pr('echo_damp', g('high_damp')); p.ours('echo_level', 100, 'no level control printed')
    elif n == 10:
        # REV7 Reverb Flange: the flange with its feedback fixed at 30 % and REV TIME in its place (OM p. 16). Built
        # like the REV5 program (pass 32): the engine at the printed REV TIME on the Rev Room taps (pass 40), the slot's plain
        # flange before it. The REV5 Reverb Flange capture is a dense reverb with no comb (2026-11-20), so a reverb, not
        # a decaying comb (our earlier guess, which rang like a spring). The REV7 prints no HIGH, LOW or DIFFUSION for
        # this program: the REV5 Reverb Flange's printed values stand in (REV5 OM p. 65; inference).
        base_choices(p, 14, 'mod', mod='flange')
        p.ours('er_mode', 14, "Rev Room taps, read from its hardware capture (pass 40; the REV7 prints no reflections for this program)")
        p.pr('rt', g('reverb_time_s'), 'REV TIME')
        p.ours('hi_ratio', 0.4, 'not printed on the REV7; the REV5 Reverb Flange prints 0.4 (OM p. 65)')
        p.ours('lo_ratio', 1.0, 'not printed on the REV7; the REV5 Reverb Flange prints 1.0 (OM p. 65)')
        p.ours('diffusion', 5, 'not printed on the REV7; the REV5 Reverb Flange prints 5 (OM p. 65)')
        p.ours('rev2_level', 0, 'the REV7 has no secondary reverb')
        p.pr('lfo_rate', g('lfo_hz')); p.pr('mod_depth', g('depth_pct')); p.pr('mod_delay', g('mod_delay_ms'))
        p.pr('mod_fb', g('feedback_pct'), 'FEEDBACK fixed at 30 % in the unit (OM p. 16)')
        p.ours('fx_wet', 1, 'flange into the reverb with no bypass, as the REV5 program (its capture shows no flanged direct signal)')
    elif n in REV7_MOD:
        base_choices(p, 0, 'mod', mod=REV7_MOD[n]); no_reverb(p)
        p.pr('lfo_rate', g('lfo_hz')); p.pr('mod_depth', g('depth_pct')); p.pr('dm_depth', g('dm_depth_pct'))
        p.pr('am_depth', g('am_depth_pct')); p.pr('mod_delay', g('mod_delay_ms'))
        if n == 9: p.pr('mod_fb', g('feedback_pct'))
    else:
        raise ValueError('unmapped REV7 program %d' % n)
    return p


progs = []
# Hidden per-preset voice (parameters.md): best guesses by program family, all ours (inference). Only programs with
# a late tail get one; reflection-only and FX programs keep the defaults, where the late network is not heard.
# Kept modest (the engine was fitted to the captures at the defaults): halls a little larger and smoother with a
# lower bass corner; plates shorter and denser with a thinner bass and quieter reflections; spring short loops with
# long, smeared diffusion and little bass; echo room plain (less diffusion) with prominent reflections; strings large
# and smooth; snare and handclaps small and bright with forward reflections.
# (loop size x, diffuser size x, low crossover Hz, ER gain dB)
VOICE = {
    'Large Hall': (1.1, 1.1, 260, 0.0, 'large hall: longer loops, smoother onset, lower bass corner'),
    'Small Hall': (0.9, 0.95, 320, 1.0, 'small hall: shorter loops, reflections a little forward'),
    'Vocal Plate': (0.8, 0.8, 380, -2.0, 'plate: short dense loops, thinner bass, few distinct reflections'),
    'Percussion Plate': (0.8, 0.75, 400, -1.0, 'plate: short dense loops, fast build-up, thinner bass'),
    'Spring': (0.85, 1.2, 450, -2.0, 'spring: short loops, long smeared diffusion, little bass'),
    'Echo Room': (1.0, 0.85, 280, 1.5, 'echo chamber: hard walls, less diffusion, prominent reflections'),
    'Strings': (1.15, 1.2, 240, -1.0, 'large smooth hall for strings: long loops, slow diffusion, warm'),
    'Snare': (0.9, 0.85, 350, 1.0, 'small bright room: short loops, forward reflections'),
    'Handclaps': (0.9, 0.85, 350, 1.0, 'small bright room: short loops, forward reflections'),
    'Plate & Hall': (0.85, 0.85, 350, 0.0, 'plate primary (the hall is the secondary network)'),
}
# REV5 programs that are a voiced family under another name: Reverb & Gate and the combinations 91-97 are Large Hall
# based (rt 2.6 s, L-HALL); 99 is plate-led.
VOICE_FAMILY5 = {23: 'Large Hall', 91: 'Combination', 92: 'Combination', 93: 'Combination', 94: 'Combination',
                 95: 'Combination', 96: 'Combination', 97: 'Combination', 99: 'Plate & Hall'}

# REV7 voices fitted on the REV7 captures of the factory programs (forty-fifth pass, docs/design/engine.md),
# replacing pass 24's diffuser sizes (REV7 captures): the same score as VOICE5 (500 Hz-4 kHz energy per window
# re the first 5 ms, C50, C80; 1 s sweeps), grid over reflection gain -12..+12 dB, late onset 0 / 10 ms and diffuser
# size. Onset 0 everywhere; the reflections' level against the late field differs by up to 20 dB between programs
# (the plates, Strings, Spring and Echo Room far under it, Percussion Plate and Snare far over). Small Hall keeps its
# voice (it fitted). Values: (diff, er gain dB). Pass 74 (final sweep): Reverb Flange +4 -> +6 dB.
VOICE7 = {'Large Hall': (1.25, 0.0), 'Vocal Plate': (0.6, -8.0), 'Percussion Plate': (0.6, 8.0),
          'Reverb Flange': (0.6, 6.0), 'Spring': (0.6, -8.0), 'Echo Room': (0.6, -8.0), 'Strings': (0.6, -12.0),
          'Snare': (1.0, 8.0), 'Handclaps': (0.8, 4.0)}
VOICE7_NOTE = 'fitted on the REV7 capture of this program (pass 45)'


# REV5 voices fitted on the REV5 captures of the factory programs (forty-fourth pass, docs/design/engine.md),
# replacing the pass-26 / pass-32 fits on the REV5 captures (old taps, programs unknown). Scored on the
# 500 Hz-4 kHz energy per window re the first 5 ms (5-20 / 20-50 / 50-100 / 100-200 / 200-400 ms), C50 and C80, grid
# over reflection gain, late onset and diffuser size: every family fits at late onset 0 (the late field starting 10-18
# ms after ours was a reading), smaller diffusers, and the halls' reflections 1-2 dB forward. Reverb & Gate
# and the combinations (VOICE_FAMILY5) follow the Large Hall (family, inference). Values: (loop or None, diff, onset,
# er gain dB).
VOICE5 = {'Large Hall': (None, 1.0, 0.0, 2.0), 'Small Hall': (None, 0.7, 0.0, 2.0),
          'Vocal Plate': (1.0, 0.6, 0.0, -1.0), 'Percussion Plate': (1.0, 0.7, 0.0, 1.0),
          'Strings': (None, 0.8, 0.0, -1.0), 'Spring': (None, 0.7, 0.0, 1.0),
          'Echo Room': (None, 0.6, 0.0, 1.0), 'Snare': (None, 0.8, 0.0, -1.0),
          'Combination': (None, 0.6, 0.0, 0.0)}
# Pass 68: re-checked per channel (pass 44 scored L + R, weighing our L/R-identical taps about 3 dB over the
# hardware's): Small Hall and Echo Room reflections +1 dB (500 Hz-4 kHz windows and C50 / C80 per channel, 1.18 /
# 0.96 -> 1.02 / 0.68 dB rms); Large Hall stays (01 wants +1, 23 Reverb & Gate -1), Strings and Snare within 0.05 dB,
# the plates and Spring stay.
# Pass 62: Snare's diffusers x1.0 -> x0.8 after era 5's diffuser gain x0.6 (its 20-50 ms gap; features 2.3 -> 1.2 dB rms)
VOICE5_NOTE = 'fitted on the REV5 capture of this program (pass 44)'
# The combinations 91-97 share one reverb network (REV5 captures: 91-96 coherent to 1.8 s), not the Large
# Hall's: their own voice, fitted on the 91 / 92 / 93 / 95 REV MIX 100 % captures with the effect off (fifty-fifth pass)
VOICE5_COMBI_NOTE = 'the combinations\' own network, fitted on the REV5 captures of 91-93 and 95 (pass 55)'
VOICE5_FAMILY_NOTE = 'as the fitted Large Hall (pass 44; family, inference)'

# E/R programs whose reflection field sits off the 1st reflection after pass 43's per-era scale (forty-ninth pass): the
# field (2-200 ms, 300 Hz-8 kHz) re the 1st reflection on the hardware captures against ours (`ref1ratio.py`). Our
# taps are normalised to one energy, the hardware's field is not: its Plate programs at liveness 0-1 sit 3.5 dB under
# the others (REV7) or 2.5-3.5 dB over (REV5). Programs within 1.5 dB keep 0. Values: er gain dB.
# Pass 57: the hardware captures of Kick (REV7 21, REV5 21) carry a tail decaying from the plate taps (T60 about 0.1 s;
# Electric Bass A, the same chart and size at diffusion 5, has none), so both play E/R2 against the printed E/R1 (Q-28).
KICK_ER2 = {'Kick'}
KICK_ER2_NOTE = 'TYPE %s printed; E/R2 here: its hardware capture decays past the taps like the E/R2 programs (pass 57, Q-28)'
VOICE_ER7 = {'Electric Bass A': -3.5, 'Kick': -3.5}
VOICE_ER5 = {'Electric Bass A': 2.5, 'Electric Bass B': -2.5, 'Kick': -3.5}
VOICE_ER_NOTE = 'reflections re the 1st reflection, fitted on the hardware capture of this program (pass 49)'
# Per-program reflection gain over the family voice (pass 60): REV5 10 Reverb Flange plays its late field about 7 dB
# under 02 Small Hall's on the same taps (REV5 captures, 500 Hz-4 kHz windows re 0-5 ms, C50, C80).
VOICE5_PROG_ER = {'Reverb Flange': 6.5}
VOICE5_PROG_NOTE = 'fitted on the REV5 capture of this program (pass 60: its late field 7 dB under the family\'s)'
# Pass 61: each REV7 program reads its taps its own way. Every tap of a REV7 capture carries the same treble
# (8 / 10 / 12.5 kHz re 1-4 kHz, 1.2 ms windows), but the programs differ: as the direct path (Strings, Live
# Reference, Piano), a little under it (Snare, Organ, Brass, the E/R presets), the half-sample read (halls, plates)
# and darker (Gate Reverb, Handclaps, Spring). Values: the offset on the era-7 tap and late-feed averages, fitted on a
# grid of 0.1 against the colour at 4-12.5 kHz in 0-50, 100-300 and 300-800 ms (the E/R bass programs on the taps'
# own colour, their windows being dominated by other gaps).
TAP_READ7 = {'Percussion Plate': 0.1, 'Early Reflection 1': -0.1, 'Early Reflection 2': -0.1, 'Reverb Flange': -0.1,
             'Spring': 0.5, 'Echo Room': -0.25, 'Strings': -0.4, 'Electric Bass A': 0.15, 'Electric Bass B': 0.15,
             'Kick': 0.15, 'Snare': -0.35, 'Gate Reverb': 0.5, 'Reverse Gate': 0.3, 'Piano': -0.15, 'Organ': -0.2,
             'Brass': -0.05, 'Handclaps': 0.5, 'Live Reference': -0.5}
TAP_READ7_NOTE = 'the program\'s tap read, fitted on its REV7 capture (pass 61)'
# Pass 79: the early fill (pass 78) per REV7 reverb program, (level, size): fitted on the REV7 captures (colour
# by window, pass 44's features, the 5 ms envelope, three drift seeds); Large Hall, Percussion Plate and Spring keep it
# off (no gain, or the features worse).
FILL7 = {'Small Hall': (0.2, 2.0), 'Vocal Plate': (1.0, 0.75), 'Reverb Flange': (0.1, 1.0), 'Echo Room': (0.75, 1.0),
         'Strings': (0.75, 0.75), 'Snare': (0.1, 2.0), 'Handclaps': (0.2, 2.0)}
FILL7_NOTE = 'the early fill, fitted on the REV7 capture of this program (pass 79)'


# Pass 81 (regression pass): per-program refits, each accepted only where no metric of that program got worse (colour
# by window, raw and with its own mean removed, pass 44's features, the 5 ms envelope, the early-field match; era 7 over
# six drift seeds). Hidden voice_diff_gain (x the input diffusers' gain): the plates' 10-30 ms field, which passes 62-63
# had left 6-9 dB under the captures. Values: (diffuser size or None, gain, low crossover Hz or None).
#   REV5 Percussion Plate x0.78 / x1.25 (grid gain 1.0-1.35, size 0.6-0.8): envelope 2.87 -> 2.02, features 3.4 -> 2.5
#   REV7 Percussion Plate x1.15, crossover 500 Hz (after pass 81's drift fix its late 125 Hz read 1 dB short)
#   REV7 Strings x1.15 (features and envelope -0.13 / -0.14 dB rms)
# Hidden voice_sub_ratio (x the decay below 120 Hz): REV7 Snare x1.15 (its late 63-125 Hz short; neutral elsewhere),
# Handclaps x0.85 (late colour 0.2-0.5 dB better, the rest within 0.01).
DIFF_GAIN = {('REV5', 'Percussion Plate'): (0.78, 1.25, None), ('REV7', 'Percussion Plate'): (None, 1.15, 500),
             ('REV7', 'Strings'): (None, 1.15, None)}
SUB_RATIO = {('REV7', 'Snare'): 1.15, ('REV7', 'Handclaps'): 0.85}
DIFF_GAIN_NOTE = 'fitted on the hardware capture of this program (pass 81)'
# Pass 82 (regression pass, the rest; same rule): per-program voice refits on the program alone (the families keep
# theirs). REV5 Large Hall: reflections +2 -> +3 dB, diffuser gain x1.15 (envelope -0.48, 100-300 ms colour -0.18,
# features -0.31 dB rms). REV5 Snare: late tilt +2.5 dB, reflections -1 -> +0.5 dB, high corner 8.5 -> 11 kHz, low
# corner 350 -> 400 Hz (its late field 2-5 dB dark above 6 kHz: 100-300 / 300-800 ms colour -1.21 / -0.92 dB rms).
PROG_VOICE = {('REV5', 'Large Hall'): {'voice_er_gain': 3.0, 'voice_diff_gain': 1.15},
              ('REV5', 'Snare'): {'voice_late_tilt': 2.5, 'voice_er_gain': 0.5, 'voice_hi_xover': 11000,
                                  'voice_lo_xover': 400},
              # pass 83: the sub shelf's corner per program (hidden voice_sub_hz). REV7 Small Hall's late 63 Hz read
              # short while its 125 Hz was already heavy, so the 120 Hz shelf could not help: at 56 Hz, x2 (300-800 /
              # 100-300 ms colour -0.39 / -0.12 dB rms). REV5 Small Hall x0.9 below 100 Hz (300-800 ms -0.09).
              ('REV7', 'Small Hall'): {'voice_sub_hz': 56, 'voice_sub_ratio': 2.0},
              # pass 85: REV7 Large Hall's 15-20 ms excess was the input diffusers' echo of tap 1 (2026-11-25 side
              # check); larger diffusers move it under tap 2, more diffuser gain keeps the 55-90 ms build-up and the
              # sub shelf its late 125 Hz (six drift seeds: features 3.93 -> 2.86, 100-300 / 300-800 ms colour
              # 0.99 / 0.85 -> 0.88 / 0.56, envelope 2.70 -> 2.67 dB rms; 15-20 ms L -37.9 -> -51.0 dB against -50.5).
              # pass 87: REV7 Snare's late field spoke too early (20-200 ms +1.2 to +2.7 dB re its first 5 ms in pass 44's
              # features): a 4 ms late onset, with its tap read -0.4 -> -0.35 (TAP_READ7) holding the colour (six seeds:
              # features 1.37 -> 1.07, colour 0-50 / 100-300 / 300-800 ms 1.11 / 1.55 / 1.20 -> 1.02 / 1.52 / 1.00,
              # envelope 1.79 -> 1.77 dB rms)
              ('REV7', 'Snare'): {'voice_late_onset': 4},
              # pass 89: its sub shelf 150 Hz x1.3 -> 130 Hz x1.1: pass 85's shelf held 300-800 ms but left the bass
              # past 800 ms 2-10 dB over the capture (-20 to -40 dB decay at 63 / 125 Hz x1.22 / x1.36), beyond every
              # scorer window; a trade the maintainer took (six seeds: colour 800-1500 / 1500-2500 ms 1.85 / 3.95 ->
              # 1.54 / 2.95, 300-800 ms 0.56 -> 0.66 dB rms, the rest unchanged)
              ('REV7', 'Large Hall'): {'voice_diff_size': 1.4, 'voice_diff_gain': 1.15, 'voice_sub_hz': 130,
                                       'voice_sub_ratio': 1.1},
              ('REV5', 'Small Hall'): {'voice_sub_hz': 100, 'voice_sub_ratio': 0.9}}
PROG_VOICE_NOTE = 'fitted on the hardware capture of this program (passes 82-89)'


def add_voice(p, family):
    base = 'Large Hall' if family == 'Combination' else family  # the combinations keep the hall's loops (pass 55)
    if base in VOICE:
        loop, diff, xo, er, why = VOICE[base]
        note = 'voice best guess (inference): ' + why
        p.ours('voice_loop_size', loop, note); p.ours('voice_diff_size', diff, note)
        p.ours('voice_lo_xover', xo, note); p.ours('voice_er_gain', er, note)
    if p.unit == 'REV7' and family in VOICE7:
        diff, er = VOICE7[family]
        p.put('voice_diff_size', diff, 'derived', VOICE7_NOTE)
        p.put('voice_er_gain', er, 'derived', VOICE7_NOTE)
    er_fit = (VOICE_ER7 if p.unit == 'REV7' else VOICE_ER5).get(p.name)
    if er_fit is not None:
        p.put('voice_er_gain', er_fit, 'derived', VOICE_ER_NOTE)
    if p.unit == 'REV5' and family in VOICE5:
        loop, diff, onset, er = VOICE5[family]
        basis, note = (('derived', VOICE5_COMBI_NOTE) if family == 'Combination'
                       else ('ours', VOICE5_FAMILY_NOTE) if p.name != family else ('derived', VOICE5_NOTE))
        if loop is not None: p.put('voice_loop_size', loop, basis, note)
        p.put('voice_diff_size', diff, basis, note)
        p.put('voice_late_onset', onset, basis, note)
        p.put('voice_er_gain', er, basis, note)
    if p.unit == 'REV7' and p.name in TAP_READ7:
        p.put('voice_tap_read', TAP_READ7[p.name], 'derived', TAP_READ7_NOTE)
    if p.unit == 'REV7' and p.name in FILL7:
        p.put('voice_er_fill', FILL7[p.name][0], 'derived', FILL7_NOTE)
        p.put('voice_er_fill_size', FILL7[p.name][1], 'derived', FILL7_NOTE)
    if (p.unit, p.name) in DIFF_GAIN:
        size, gain, xo = DIFF_GAIN[(p.unit, p.name)]
        if size is not None: p.put('voice_diff_size', size, 'derived', DIFF_GAIN_NOTE)
        p.put('voice_diff_gain', gain, 'derived', DIFF_GAIN_NOTE)
        if xo is not None: p.put('voice_lo_xover', xo, 'derived', DIFF_GAIN_NOTE)
    for k, v in PROG_VOICE.get((p.unit, p.name), {}).items():
        p.put(k, v, 'derived', PROG_VOICE_NOTE)
    if (p.unit, p.name) in SUB_RATIO:
        p.put('voice_sub_ratio', SUB_RATIO[(p.unit, p.name)], 'derived', DIFF_GAIN_NOTE)
    if p.unit == 'REV5' and p.name in VOICE5_PROG_ER:
        p.put('voice_er_gain', VOICE5_PROG_ER[p.name], 'derived', VOICE5_PROG_NOTE)


for r in csv.DictReader(open(CSV7)): progs.append(build7(r))
for r in csv.DictReader(open(CSV5)): progs.append(build5(r))
for p in progs:
    add_voice(p, VOICE_FAMILY5.get(p.number, p.name) if p.unit == 'REV5' else p.name)
for p in progs: entries.append((p.title, p.unit, dict(p.v)))
assert len({(e[0], e[1]) for e in entries}) == len(entries), 'preset names must be unique within a bank'

with open(MAPPING, 'w', newline='') as f:
    w = csv.writer(f)
    w.writerow(['unit', 'number', 'name', 'source', 'param', 'value', 'basis', 'note'])
    for p in progs:
        for pid, v in p.v.items():
            w.writerow([p.unit, p.number, p.name, p.source, pid, ('%g' % v), p.basis[pid], p.note[pid]])

# ---- header output ------------------------------------------------------------------------------------------------
out = ['// GENERATED by tools/gen_factory_presets.py. Do not edit by hand.',
       '// FACTORY and COMBI: generic presets with our own values. REV7 and REV5: the program names and settings printed',
       '// in the owner\'s manuals (ADR 0004 as amended), mapped by the generator; the printed, derived and own values',
       '// are listed in data/presets/manual_programs_mapping.csv. Nothing here comes from firmware, ROM or SysEx data.',
       '#pragma once', '', 'namespace yrev', '{',
       '    struct PresetValue { const char* id; float value; };',
       '    struct FactoryPreset { const char* name; const char* bank; const PresetValue* values; int count; };', '']
for i, (name, bank, vals) in enumerate(entries):
    out.append('    inline constexpr PresetValue kPreset%d[] = {' % i)
    for k, v in vals.items():
        out.append('        { "%s", %rf },' % (k, float(v)))
    out.append('    };')
out.append('')
out.append('    inline constexpr FactoryPreset kFactoryPresets[] = {')
for i, (name, bank, vals) in enumerate(entries):
    out.append('        { %s, %s, kPreset%d, %d },' % (json.dumps(name), json.dumps(bank), i, len(vals)))
out.append('    };')
out.append('}')
open(OUT, 'w').write('\n'.join(out) + '\n')
print(len(entries), 'factory presets (%d manual programs); run clang-format on %s' % (len(progs), OUT))
