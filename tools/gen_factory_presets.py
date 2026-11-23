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
INSTRUMENT_STAND_IN = {8: 0, 9: 1, 10: 4, 11: 5}
# The REV7 reverb programs print no ER mode: "3 early reflections are programmed for each channel" (REV7 OM p. 14,
# OCR). They play the REV7 Rev sets read from the REV7 captures (twenty-third pass, docs/design/engine.md):
# Large Hall, Small Hall and Vocal Plate have a capture at their factory initial delay (Rev Hall 12, Rev Room 14, Rev
# Plate 13); the others take the nearest set by name (ours, inference).
REVERB_ER_MODE = {'Large Hall': 12, 'Small Hall': 14, 'Vocal Plate': 13, 'Percussion Plate': 13, 'Spring': 13,
                  'Echo Room': 14, 'Strings': 12, 'Snare': 14, 'Handclaps': 14}
REV7_MEASURED = {'Large Hall', 'Small Hall', 'Vocal Plate'}
# The REV5 reverb programs print no ER mode: they play their own early taps (Rev Hall 12, Rev Plate 13; read from the
# REV5 captures, twenty-second pass, docs/design/engine.md). The plates take Rev Plate, everything else Rev Hall.
REV5_REVERB_ER_MODE = {'Vocal Plate': 13, 'Percussion Plate': 13}
REV_NOTE = "the reverb program's own early taps (no ER mode on the REV5's reverb programs; read from captures, pass 22)"
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
        # the REV7's (a flange whose feedback becomes a decay). The engine at the printed settings on the Rev Hall
        # taps (the REV5 Reverb Flange capture sits on the shared reverb-program pattern, 2026-11-20); the slot's
        # plain flange before it, its feedback at the REV7 program's fixed 30 % (the REV5 prints none; inference).
        base_choices(p, 12, 'mod', mod='flange')
        p.ours('er_mode', 12, REV_NOTE)
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
        base_choices(p, 12, fx, order, REV5_COMBO_MOD.get(n, 2))
        p.ours('er_mode', 12, REV_NOTE)
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
        p.derived('voice_late_onset', g('rev_delay_ms') - g('er_delay_ms'), 'REV DLY 20 ms less ER DLY 0.1 ms (the reverb starts that much after the reflections)')
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
        p.ours('er_mode', REVERB_ER_MODE[nm], "the reverb program's three early reflections per channel (REV7 OM p. 14; times read from "
               + ('its capture, pass 23)' if nm in REV7_MEASURED else 'captures, pass 23; nearest set by name)'))
        p.pr('rt', g('reverb_time_s')); p.pr('hi_ratio', g('hi_ratio')); p.pr('lo_ratio', g('lo_ratio')); p.pr('diffusion', g('diffusion'))
        p.ours('rev2_level', 0, 'the REV7 has no secondary reverb')
    elif typ.startswith('E/R'):
        base_choices(p, int(g('er_mode')) - 1, 'off')
        p.pr('er_mode', int(g('er_mode')) - 1, 'E/R MODE %d' % int(g('er_mode')))
        p.pr('liveness', g('liveness')); p.pr('room_size', g('room_size')); p.pr('diffusion', g('diffusion'))
        p.ours('er_count', 18, 'the REV7 prints no count; 18 reflections per channel (era-7 maximum)')
        # E/R1 or E/R2 as the ROM chart's type column prints it (pass 25: E/R2 diffuses the reflections). Source
        # conflict: the manual's program notes call Gate Reverb an E/R1 program (OM printed p. 18, page image); the chart wins.
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
        # like the REV5 program (pass 32): the engine at the printed REV TIME on the Rev Hall taps, the slot's plain
        # flange before it. The REV5 Reverb Flange capture is a dense reverb with no comb (2026-11-20), so a reverb, not
        # a decaying comb (our earlier guess, which rang like a spring). The REV7 prints no HIGH, LOW or DIFFUSION for
        # this program: the REV5 Reverb Flange's printed values stand in (REV5 OM p. 65; inference).
        base_choices(p, 12, 'mod', mod='flange')
        p.ours('er_mode', 12, "Rev Hall taps, as the REV5 Reverb Flange (the REV7 prints no reflections for this program)")
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
VOICE_FAMILY5 = {23: 'Large Hall', 91: 'Large Hall', 92: 'Large Hall', 93: 'Large Hall', 94: 'Large Hall',
                 95: 'Large Hall', 96: 'Large Hall', 97: 'Large Hall', 99: 'Plate & Hall'}

# REV7 diffuser sizes fitted on the REV7 captures (twenty-fourth pass, docs/design/engine.md): the late field's build-up
# above 300 Hz after the three early reflections (envelope per 5 ms from the first tap, 0-150 ms) fits Large Hall at
# the largest diffusers (x1.5: 4.4 / 5.0 -> 3.6 / 3.8 dB rms on Giant / Vast) and the plate at the smallest (x0.6: 6.6
# -> 3.6 on a plate capture); Small Hall's 0.95 already fits. Percussion Plate follows the plate (family, inference). The
# REV5 programs keep VOICE (no era-5 check).
VOICE7_DIFF = {'Large Hall': (1.5, 'derived', 'diffuser size fitted on the REV7 Large Hall captures (pass 24)'),
               'Vocal Plate': (0.6, 'derived', 'diffuser size fitted on the REV7 Vocal Plate capture (pass 24)'),
               'Percussion Plate': (0.6, 'ours', 'diffuser size as the fitted Vocal Plate (pass 24; family, inference)')}

# REV5 voices fitted on the REV5 captures (twenty-sixth pass, docs/design/engine.md): the late build-up above 300 Hz
# from each capture's first tap (buildup.py --jobs). The late onset is the lever: the plate captures  fall
# silent between their first taps and about 40 ms, and the halls' late field starts about 10 ms after ours (
# Large 3A, Large 3B, Small A: 5.4 -> 4.5 dB rms; plates 13.1 -> 6.8). Diffusers x1.0 on both; the plates' loops x1.0
# (x0.8 made bass ripple worse on every plate capture: 2.7 / 2.4 -> 0.5 / 1.0 dB). Era 7 fits best at onset 0 and the
# springs, the snare and the own-tap captures too, so the other programs keep 0. Percussion Plate follows the plate
# and the hall-based combinations (VOICE_FAMILY5) the Large Hall (family, inference). Values: (loop or None, diff, onset)
VOICE5 = {'Large Hall': (None, 1.0, 10.0, 'derived', 'diffusers and late onset fitted on the REV5 hall captures (pass 26)'),
          'Small Hall': (None, 1.0, 10.0, 'derived', 'diffusers and late onset fitted on the REV5 hall captures (pass 26)'),
          'Vocal Plate': (1.0, 1.0, 18.0, 'derived', 'loops, diffusers and late onset fitted on the REV5 plate captures (pass 26)'),
          'Percussion Plate': (1.0, 1.0, 18.0, 'ours', 'as the fitted Vocal Plate (pass 26; family, inference)')}

# REV5 ER gain fitted on the REV5 spring captures (thirty-second pass, with the secondary reverb at its new share):
# Two spring captures at the printed ER/REV BAL 35 % and LPF 3.6 kHz read C80 2.5 / 2.8 dB late-heavy at the
# guessed -2 dB and within about 0.5 dB at +1.5 dB. The REV7 Spring keeps VOICE (no era-7 check).
VOICE5_ERG = {'Spring': (1.5, 'derived', 'reflection gain fitted on the REV5 spring captures (pass 32)')}


def add_voice(p, family):
    if family not in VOICE: return
    loop, diff, xo, er, why = VOICE[family]
    note = 'voice best guess (inference): ' + why
    p.ours('voice_loop_size', loop, note); p.ours('voice_diff_size', diff, note)
    p.ours('voice_lo_xover', xo, note); p.ours('voice_er_gain', er, note)
    if p.unit == 'REV7' and family in VOICE7_DIFF:
        val, basis, note = VOICE7_DIFF[family]
        p.put('voice_diff_size', val, basis, note)
    if p.unit == 'REV5' and family in VOICE5:
        loop, diff, onset, basis, note = VOICE5[family]
        if loop is not None: p.put('voice_loop_size', loop, basis, note)
        p.put('voice_diff_size', diff, basis, note)
        p.put('voice_late_onset', onset, basis, note)
    if p.unit == 'REV5' and family in VOICE5_ERG:
        val, basis, note = VOICE5_ERG[family]
        p.put('voice_er_gain', val, basis, note)


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
