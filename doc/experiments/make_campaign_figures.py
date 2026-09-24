#!/usr/bin/env python3
"""Builds the Validation section of doc/paper.tex from one campaign folder.

    python3 make_campaign_figures.py \
        --campaign ~/Documents/supermoto/Measurements_20260924 \
        --preset "~/Documents/supermoto/Measurements_20260924/Mesures 20260924.xml" \
        --lf-exclude "Linear 70Hz 8192 Limit"

The campaign folder holds three kinds of subfolder:

  Measurements/          the dry captures: each main and the subwoofer on its
                         own, at every position, correction bypassed
  System measurements/   the whole system through the matrix, one sweep per
                         input at every position
  anything holding a *_report.md
                         one group-analysis export per configuration

Every configuration sits on three consecutive outputs -- speaker 1, speaker 2,
subwoofer -- as its report says, and the matrix feeds it from two consecutive
inputs: input 2k+1 drives speaker 1 and the subwoofer of the k-th configuration
in output order, input 2k+2 speaker 2 and the subwoofer. That wiring is not
recorded anywhere, so it is checked against the captures: each input's arrival
time has to follow the delay its report applied, and each input's treble has
to match the dry capture of the loudspeaker it claims to be.

`--preset` is the plugin state the system was measured with, as saved. Every
output's FIR is embedded in it, so the script checks the mains' filters against
the exports and puts any filter left active on a subwoofer output into the
model's subwoofer term. The campaign of 24 September 2026 needs that: its
first variant was measured with a left-main correction still loaded on its
subwoofer output. `--lf-exclude` then keeps such a variant out of the
comparisons between variants below 250 Hz, where its subwoofer path is not its
siblings'. The `sub route` and `stray filter` lines are the evidence: the
difference between two variants whose mains are identical below twice the
crossover is the same on both mains, and the model reproduces it once the
stray filter is in.

Writes ../figures/crossover-measured.png and ../figures/delay-sweep.png and
prints every number the Validation section quotes. Transfer functions come from
deconvolving each sweep whole (sub_alignment_validation.sweep_tf), not from
Welch averaging, which biases a delayed sweep; see that function.

Author: Olivier Doaré, github.com/odoare/SuperMoTo
(c) 2023-2026 Olivier Doaré
Licenced under the GNU LGPL Version 3.0
SPDX-License-Identifier: LGPL-3.0-or-later
"""
import argparse
import base64
import glob
import io
import os
import re
import zlib
import numpy as np
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
from scipy.io import wavfile
from scipy.signal import butter, sosfilt, sosfiltfilt, hilbert

import sub_alignment_validation as V
import make_paper_figure as P

SR = 44100
W = V.W
f = np.arange(W // 2 + 1) * SR / W
CH_OF = {1: 3, 2: 4}                 # main number -> dry capture channel
MAIN_NAME = {1: 'left main', 2: 'right main'}
BANDS = [63, 125, 250, 500, 1000, 2000, 4000, 8000]
TB = [63, 125, 250, 500, 1000, 2000, 4000]
PRE = 20000                          # where the direct sound sits in a rolled IR


def sweep_tf(path):
    _, d = wavfile.read(path)
    return V.sweep_tf(d[:, 0].astype(float), d[:, 1].astype(float), sr=SR)


def db(x):
    return 20 * np.log10(np.maximum(x, 1e-12))


def sm(X):
    """Magnitude, smoothed 1/6 octave in the bass to 1/3 in the treble."""
    return np.abs(V.smooth_var_octave(np.abs(X).astype(complex), SR, 1 / 6, 1 / 3))


def nz(x, lo=500.0, hi=2000.0):
    d = db(x)
    return d - d[(f >= lo) & (f <= hi)].mean()


# ------------------------------------------------------------------ campaign
class Config:
    """One group-analysis export: its settings, its two correction filters and,
    once wired, the system inputs that played it."""

    def __init__(self, folder):
        self.folder, self.name = folder, os.path.basename(folder)
        txt = open(P.report_path(folder), encoding='utf-8', errors='replace').read()
        rep = P.read_report(folder)
        self.fx, self.lo = rep['crossover'], rep['analysis_lo']
        self.boost, self.taps, self.T = rep['max_boost'], rep['fir_length'], rep['delays']
        m = re.search(r'- Smoothing: 1/(\d+) oct \(LF\) / 1/(\d+) oct', txt)
        self.smooth = (1 / int(m.group(1)), 1 / int(m.group(2))) if m else (1 / 6, 1 / 3)
        phase = re.search(r'- Phase type: ([^\n]*)', txt).group(1)
        self.minimum = phase.startswith('Minimum')
        self.limited = (not self.minimum) and 'not limited' not in phase
        self.outputs = [int(x) for x in re.findall(r'Assigned to output (\d+)', txt)]
        self.C, self.lat = {}, {}
        for si in (1, 2):
            c = wavfile.read(glob.glob(os.path.join(folder, f'*speaker{si}_correction.wav'))[0])[1]
            c = c.astype(float)
            self.lat[si] = int(np.argmax(np.abs(c)))
            # What the matrix applies once the output delay has paid for the
            # filter's own bulk latency (Section "Latency and time alignment").
            self.C[si] = np.fft.rfft(c, n=W) * np.exp(2j * np.pi * f * self.lat[si] / SR)
        self.inputs = {}

    @property
    def label(self):
        kind = 'minimum' if self.minimum else ('linear, limited' if self.limited else 'linear, not limited')
        return f'{self.fx:g} Hz, {kind}, {self.taps} taps, T = {self.T[1]:.2f}/{self.T[2]:.2f} ms'


class Campaign:
    def __init__(self, root, lf_exclude=()):
        self.root = root
        self.dry_dir = os.path.join(root, 'Measurements')
        self.sys_dir = os.path.join(root, 'System measurements')
        exports = sorted(d for d in glob.glob(os.path.join(root, '*')) if os.path.isdir(d) and P.report_path(d))
        self.configs = sorted((Config(d) for d in exports), key=lambda c: c.outputs[0])
        for k, c in enumerate(self.configs):
            c.inputs = {1: 2 * k, 2: 2 * k + 1}            # 0-based system input index
        self.lf = [c for c in self.configs if c.name not in lf_exclude]
        self.cal = V.mic_cal_gain(V.load_mic_cal(self.dry_dir), f)

        # the dry captures, delay-anchored on the main as the plugin does
        idx = P.dry_index(self.dry_dir)
        self.dry_names = {k: n for n, k in idx.items()}
        self.dry = {si: {} for si in (1, 2)}
        for n, k in sorted(idx.items(), key=lambda z: z[1]):
            S = sweep_tf(os.path.join(self.dry_dir, f'sub_pos{k:03d}.wav'))
            for si in (1, 2):
                A = sweep_tf(os.path.join(self.dry_dir, f'ch{CH_OF[si]:02d}_pos{k:03d}.wav'))
                d = V.ir_peak_delay(A)                   # on the raw capture, as analyzeFile() does
                self.dry[si][k] = (V.remove_delay(A, d) / self.cal, V.remove_delay(S, d) / self.cal)

        # the system runs, paired with the dry capture of the same name
        man = P.manifest(self.sys_dir)
        self.positions = sorted((p for p in man if p in idx), key=lambda z: (len(z), z))
        self.pair = {p: idx[p] for p in self.positions}
        self.sys = {}
        for i in range(2 * len(self.configs)):
            for p in self.positions:
                self.sys[(i, p)] = sweep_tf(os.path.join(self.sys_dir, man[p][i])) / self.cal
        self.g = {}
        self.sub_fir = {}                  # variant name -> filter found on its subwoofer output

    def meas(self, c, si, p):
        return self.sys[(c.inputs[si], p)]

    def parts(self, c, si, p, C=None, T=None, invert=False, stray=True):
        """The main's and the subwoofer's contributions at position p, from
        the dry captures of the same name (eq:predsum). `stray` keeps a filter
        found on the variant's subwoofer output (apply_preset) in the
        subwoofer's term, as it was when the system was measured."""
        hp, lp = filters(c.fx)
        Hm, Hs = self.dry[si][self.pair[p]]
        T = c.T[si] if T is None else T
        C = c.C[si] if C is None else C
        g = self.g[c.fx] * (-1 if invert else 1) * (self.sub_fir.get(c.name, 1.0) if stray else 1.0)
        return np.exp(-2j * np.pi * f * T / 1000) * hp * C * Hm, g * lp * Hs


def read_preset(path):
    """The plugin state as saved: {output number: (firOn, IR or None)}.

    Each output's impulse response is embedded in the state
    (fxme::EmbeddedAudio): version 1 is a 32-bit float WAV, deflated and
    Base64-encoded, under slot "outFir<n>", n counting from 1. Version 0,
    FLAC, is not decoded here; the outputs that still carry one had their FIR
    switched off."""
    xml = open(os.path.expanduser(path), encoding='utf-8', errors='replace').read()
    irs = {}
    for m in re.finditer(r'<Audio\b([^>]*?)/?>', xml, re.S):
        at = dict(re.findall(r'(\w+)="([^"]*)"', m.group(1)))
        if int(at.get('version', '0')) < 1:
            continue
        x = wavfile.read(io.BytesIO(zlib.decompress(base64.b64decode(at['data']), 15 + 32)))[1]
        x = np.asarray(x, float)
        irs[int(at['slot'].replace('outFir', ''))] = x[:, 0] if x.ndim > 1 else x
    out = {}
    for m in re.finditer(r'<Output\b([^>]*?)/?>', xml, re.S):
        at = dict(re.findall(r'(\w+)="([^"]*)"', m.group(1)))
        n = int(at['index']) + 1
        out[n] = (at.get('firOn') == '1', irs.get(n))
    return out


def apply_preset(cam, path):
    """Check the mains' filters against the exports, and put any FIR left
    active on a subwoofer output into that variant's model."""
    state = read_preset(path)
    print(f'\n== preset {os.path.basename(path)}: what the outputs of each variant carried')
    for c in cam.configs:
        notes = []
        for si, o in zip((1, 2), c.outputs[:2]):
            on, ir = state.get(o, (False, None))
            ref = np.fft.irfft(c.C[si] * np.exp(-2j * np.pi * f * c.lat[si] / SR), n=W)[:len(ir)] if ir is not None else None
            same = on and ir is not None and np.max(np.abs(ir - ref)) < 1e-6
            notes.append(f'output {o} {"= export" if same else "DIFFERS FROM THE EXPORT"}')
        on, ir = state.get(c.outputs[2], (False, None))
        if on and ir is not None:
            lat = int(np.argmax(np.abs(ir)))
            # advanced by its own latency, which the compensation absorbs
            cam.sub_fir[c.name] = np.fft.rfft(ir, n=W) * np.exp(2j * np.pi * f * lat / SR)
            notes.append(f'SUBWOOFER output {c.outputs[2]} carries a {len(ir)}-tap FIR (peak at {lat}), modelled')
        elif on:
            notes.append(f'SUBWOOFER output {c.outputs[2]} has its FIR on but none embedded')
        else:
            notes.append(f'subwoofer output {c.outputs[2]}: no FIR')
        print(f'  {c.name:32s} ' + '; '.join(notes))


def stray_correction(cam):
    """A variant measured with a stray filter F on its subwoofer, taken back
    to what it should have been: Y - (F - 1) S, S being the model's subwoofer
    term brought to the capture's gain and time. Checked against a sibling
    whose mains are the same filter below 2fx, which the correction never
    sees."""
    ref = (f >= 500) & (f <= 2000)
    for c in cam.configs:
        if c.name not in cam.sub_fir or c.minimum:
            continue
        twin = next((b for b in cam.lf if b.fx == c.fx and not b.minimum and b.taps == c.taps
                     and abs(b.T[1] - c.T[1]) < 0.01), None)
        if twin is None:
            continue
        F = cam.sub_fir[c.name]
        print(f'\n== stray filter: {c.name} against {twin.name}, the same mains below 2fx')
        for si in (1, 2):
            raw, fixed = [], []
            for p in cam.positions:
                Y = cam.meas(c, si, p)
                d = V.ir_peak_delay(Y)
                Mm, S = cam.parts(c, si, p, stray=False)
                gy = np.exp(np.mean(np.log(sm(Y))[ref]) - np.mean(np.log(sm(Mm + S))[ref]))
                Yc = Y - (F - 1) * gy * S * np.exp(-2j * np.pi * f * (d / SR - c.T[si] / 1000))
                Z = cam.meas(twin, si, p)
                raw.append(xband_level(Y, c.fx) - xband_level(Z, c.fx))
                fixed.append(xband_level(Yc, c.fx) - xband_level(Z, c.fx))
            pred = np.mean([xband_level(sum(cam.parts(c, si, p)), c.fx) - xband_level(sum(cam.parts(twin, si, p)), c.fx)
                            for p in cam.positions])
            print(f'  main {si}: crossover band against the twin {np.mean(raw):+.2f} dB measured, {pred:+.2f} predicted '
                  f'with the stray filter; corrected capture {np.mean(fixed):+.2f} dB '
                  f'(positions {min(fixed):+.2f}..{max(fixed):+.2f})')


def filters(fx):
    return (V.butterworth(np.maximum(f, 1e-6), fx, 'hp'), V.butterworth(np.maximum(f, 1e-6), fx, 'lp'))


# ------------------------------------------------------------------- metrics
def xband_level(Y, fx):
    """Level through [fx/sqrt2, fx sqrt2], relative to 500 Hz - 2 kHz."""
    q = nz(sm(Y))
    b = (f >= fx / np.sqrt(2)) & (f <= fx * np.sqrt(2))
    return 10 * np.log10(np.mean(10 ** (q[b] / 10)))


def efficiency(Yabs, Mabs, Sabs, fx):
    """Summation efficiency over [fx/2, 2fx] on smoothed magnitudes, weighted
    by the overlap of the two sources. The same definition as
    sub_alignment_validation.metrics, smoothed so that a measured |M+S| can be
    set against the |M|+|S| of captures taken on another day."""
    b = (f >= fx / 2) & (f <= 2 * fx)
    ov = np.minimum(Mabs, Sabs)[b]
    e = db(Yabs) - db(Mabs + Sabs)
    return float((e[b] * ov).sum() / ov.sum())


def rolled_ir(Y):
    """Impulse response with the direct sound at PRE. Pass it an uncalibrated
    response: the microphone calibration is a zero-phase magnitude with
    1/24-octave detail, and dividing it out spreads a little of the direct
    sound tens of milliseconds ahead of it, 4 dB of the minimum-phase reading
    at 1 kHz. The level comparisons keep it; the timing ones cannot."""
    ir = np.fft.irfft(Y, n=W)
    return np.roll(ir, PRE - int(np.argmax(np.abs(ir))))


def band_energy(ir, fc):
    """Energy envelope through a causal octave filter, so nothing is moved
    earlier than it arrived."""
    sos = butter(3, [fc / np.sqrt(2), min(fc * np.sqrt(2), 0.45 * SR)], btype='band', fs=SR, output='sos')
    return sosfilt(sos, ir) ** 2


def ms(x):
    return int(round(x * SR / 1000))


def early(ir, fc, a=30.0, b=3.0):
    """Energy arriving between a and b ms before the direct sound, relative to
    the octave's energy in the 300 ms after it."""
    e = band_energy(ir, fc)
    return 10 * np.log10(e[PRE - ms(a):PRE - ms(b)].sum() / e[PRE:PRE + ms(300)].sum())


def floor(ir, fc, span=27.0):
    """The same window length taken far from any filter: 400 to 150 ms before
    the direct sound, where the longest filter here reaches nothing."""
    e = band_energy(ir, fc)
    return 10 * np.log10(e[PRE - ms(400):PRE - ms(150)].mean() * ms(span) / e[PRE:PRE + ms(300)].sum())


def band_time(ir, fc):
    """Arrival of an octave's envelope peak, zero-phase filtered, in ms."""
    sos = butter(2, [fc / 2 ** 0.5, fc * 2 ** 0.5], btype='band', fs=SR, output='sos')
    e = np.abs(hilbert(sosfiltfilt(sos, ir)))
    seg = e[PRE - ms(50):PRE + ms(50)]
    return (int(np.argmax(seg)) - ms(50)) / SR * 1000


def pmean(v, axis=0):
    return 10 * np.log10(np.mean(10 ** (np.asarray(v) / 10), axis=axis))


# ----------------------------------------------------------- offline design
class Designer:
    """The plugin's design chain, re-run offline on the dry captures, for the
    delay sweep and the ablations the campaign could not measure."""

    def __init__(self, cam, ref):
        self.cam = cam
        V.SMOOTH_LO, V.SMOOTH_HI = ref.smooth
        V.MAX_BOOST_DB = ref.boost
        self.avg = {}

    def averages(self, si, keys):
        key = (si, tuple(keys))
        if key not in self.avg:
            Hm = np.array([self.cam.dry[si][k][0] for k in keys])
            Hs = np.array([self.cam.dry[si][k][1] for k in keys])
            self.avg[key] = (V.smooth_average(V.position_average(Hm), SR),
                             V.smooth_average(V.position_average(Hs), SR))
        return self.avg[key]

    def design(self, si, c, T, minimum, limited=True, keys=None, invert=False):
        keys = sorted(self.cam.dry[si]) if keys is None else keys
        V.ANALYSIS_LO = c.lo
        Hsm, Ssm = self.averages(si, keys)
        V.PHASE_LIMITED = limited
        C = V.design(Hsm, Ssm, f, c.fx, T, invert, 'min_export' if minimum else 'mixed')
        V.PHASE_LIMITED = True
        if minimum:
            ir, lat = V.render_min_ir(C, f, SR, c.taps), 0
        else:
            ir, lat = V.render_ir(C, f, SR, c.taps), c.taps // 2
        return np.fft.rfft(ir, n=W) * np.exp(2j * np.pi * f * lat / SR)


def dry_scores(cam, c, si, T, C, keys, invert=False):
    """Summation efficiency and early energy in the 63 and 125 Hz octaves at
    each dry position, for a correction C at design delay T."""
    hp, lp = filters(c.fx)
    g = cam.g[c.fx] * (-1 if invert else 1)
    out = []
    for k in keys:
        Hm, Hs = cam.dry[si][k]
        M = np.exp(-2j * np.pi * f * T / 1000) * hp * C * Hm
        S = g * lp * Hs
        ir = rolled_ir((M + S) * cam.cal)
        out.append((efficiency(sm(M + S), sm(M), sm(S), c.fx), early(ir, 63), early(ir, 125)))
    return np.array(out)


# ------------------------------------------------------------------ reports
def check_wiring(cam):
    print('\n== wiring: system delay per input against the delays each report applied')
    ref = cam.configs[0]
    for si in (1, 2):
        d0 = np.array([V.ir_peak_delay(cam.meas(ref, si, p)) for p in cam.positions])
        for c in cam.configs:
            d = np.array([V.ir_peak_delay(cam.meas(c, si, p)) for p in cam.positions])
            dd = (d - d0) * 1000 / SR
            print(f'  main {si} {c.name:32s} ({c.lat[si]:4d}-sample filter): system delay '
                  f'{d.min() * 1000 / SR:6.2f}-{d.max() * 1000 / SR:6.2f} ms, against the first '
                  f'{dd.min():+7.3f}..{dd.max():+7.3f} ms (reports: {c.T[si] - ref.T[si]:+.2f})')
    print('== wiring: which dry loudspeaker each input matches above 500 Hz (spread over positions, dB)')
    hi = (f >= 500) & (f <= 8000)
    for c in cam.configs:
        s = []
        for si in (1, 2):
            spread = [np.mean(np.std([db(sm(cam.meas(c, si, p))) - db(sm(cam.dry[o][cam.pair[p]][0]))
                                      for p in cam.positions], axis=0)[hi]) for o in (1, 2)]
            s.append(f'input {c.inputs[si] + 1}: {spread[0]:.2f} vs L, {spread[1]:.2f} vs R')
        print(f'  {c.name:32s} ' + '; '.join(s))


def fit_sub_level(cam):
    """The subwoofer's routing level per crossover, and its polarity, fitted
    over every configuration at that crossover and both mains."""
    print('\n== subwoofer level and polarity, fitted on the system runs over 35-250 Hz')
    band = (f >= 35) & (f <= 250)
    for fx in sorted({c.fx for c in cam.lf}):
        group = [c for c in cam.lf if c.fx == fx]
        meas = {(c.name, si, p): nz(sm(cam.meas(c, si, p))) for c in group for si in (1, 2) for p in cam.positions}

        def rms(gdb, inv):
            cam.g[fx] = 10 ** (gdb / 20)
            return np.sqrt(np.mean([np.mean((nz(sm(sum(cam.parts(c, si, p, invert=inv)))) - meas[(c.name, si, p)])[band] ** 2)
                                    for c in group for si in (1, 2) for p in cam.positions]))
        res = {}
        for inv in (False, True):
            coarse = min((rms(gdb, inv), gdb) for gdb in np.arange(-14, 4.001, 0.5))
            res[inv] = min((rms(gdb, inv), gdb) for gdb in np.arange(coarse[1] - 0.5, coarse[1] + 0.501, 0.05))
        cam.g[fx] = 10 ** (res[False][1] / 20)
        print(f'  {fx:g} Hz: {res[False][1]:+.2f} dB, normal polarity {res[False][0]:.2f} dB RMS, '
              f'inverted {res[True][0]:.2f} dB RMS -> {"INVERTED" if res[True][0] < res[False][0] else "normal"}')


def check_sub_routes(cam):
    """Two linear-phase configurations at the same crossover and delay have
    identical mains below 2fx whatever their limit, so their difference there
    is the subwoofer's. The same difference on both mains says it is."""
    print('\n== sub route: difference between configurations with identical mains below 2fx')
    lin = [c for c in cam.configs if not c.minimum]
    for i, a in enumerate(lin):
        for b in lin[i + 1:]:
            if a.fx != b.fx or abs(a.T[1] - b.T[1]) > 0.01 or a.taps != b.taps:
                continue
            sel = (f >= 30) & (f <= 120)
            D = {si: np.array([cam.meas(a, si, p) - cam.meas(b, si, p) for p in cam.positions]) for si in (1, 2)}
            corr = np.mean([np.abs(np.vdot(D[1][j][sel], D[2][j][sel]))
                            / np.sqrt(np.vdot(D[1][j][sel], D[1][j][sel]).real * np.vdot(D[2][j][sel], D[2][j][sel]).real)
                            for j in range(len(cam.positions))])
            lm = np.mean([xband_level(cam.meas(a, si, p), a.fx) - xband_level(cam.meas(b, si, p), b.fx)
                          for si in (1, 2) for p in cam.positions])
            lp = np.mean([xband_level(sum(cam.parts(a, si, p)), a.fx) - xband_level(sum(cam.parts(b, si, p)), b.fx)
                          for si in (1, 2) for p in cam.positions])
            print(f'  {a.name} vs {b.name}: correlation of the difference between the two mains '
                  f'{corr:.3f}; crossover-band level {lm:+.2f} dB measured, {lp:+.2f} predicted')


def model_accuracy(cam):
    print('\n== model against measurement, 35-250 Hz, levels re 500 Hz - 2 kHz (dB RMS, mean over positions)')
    band = (f >= 35) & (f <= 250)
    for c in cam.configs:
        if c.fx not in cam.g:
            continue
        r = [np.mean([np.sqrt(np.mean((nz(sm(sum(cam.parts(c, si, p)))) - nz(sm(cam.meas(c, si, p))))[band] ** 2))
                      for p in cam.positions]) for si in (1, 2)]
        flag = '' if c in cam.lf else '   (left out below 250 Hz)'
        print(f'  {c.name:32s} {r[0]:.2f} / {r[1]:.2f}{flag}')


def find(cam, fx, minimum, T=None, limited=None, taps=None):
    for c in cam.lf:
        if (c.fx == fx and c.minimum == minimum and (T is None or abs(c.T[1] - T) < 0.05)
                and (limited is None or c.limited == limited) and (taps is None or c.taps == taps)):
            return c
    return None


def pairs(cam):
    """The comparisons Table 1 makes: every one between two configurations
    measured at the same placement of the microphone."""
    out = []
    for fx in sorted({c.fx for c in cam.lf}):
        Ts = sorted({round(c.T[1], 2) for c in cam.lf if c.fx == fx}, reverse=True)
        for T in Ts:
            a = find(cam, fx, False, T, taps=8192)
            b = find(cam, fx, True, T)
            if a and b:
                out.append((f'{fx:g} Hz, T = {T:.1f} ms: all-pass minus minimum phase', a, b))
        if len(Ts) == 2:
            for minimum, what in ((False, 'all-pass'), (True, 'minimum phase')):
                a, b = find(cam, fx, minimum, Ts[0]), find(cam, fx, minimum, Ts[1])
                if a and b:
                    out.append((f'{fx:g} Hz, {what}: T = {Ts[0]:.1f} minus T = {Ts[1]:.1f} ms', a, b))
        a, b = find(cam, fx, False, taps=4096), find(cam, fx, False, taps=8192)
        if a and b:
            out.append((f'{fx:g} Hz, all-pass: 4096 minus 8192 taps', a, b))
    return out


def table_levels(cam):
    print('\n== Table: crossover-band level differences, [fx/sqrt2, fx sqrt2] re 500 Hz - 2 kHz, dB')
    print('   (measured left/right | predicted left/right | positions where the first is higher)')
    for label, a, b in pairs(cam):
        m = {si: np.array([xband_level(cam.meas(a, si, p), a.fx) - xband_level(cam.meas(b, si, p), b.fx)
                           for p in cam.positions]) for si in (1, 2)}
        q = {si: np.array([xband_level(sum(cam.parts(a, si, p)), a.fx) - xband_level(sum(cam.parts(b, si, p)), b.fx)
                           for p in cam.positions]) for si in (1, 2)}
        print(f'  {label:52s} {m[1].mean():+5.2f} / {m[2].mean():+5.2f} | {q[1].mean():+5.2f} / {q[2].mean():+5.2f} | '
              f'{int((m[1] > 0).sum())}, {int((m[2] > 0).sum())} of {len(cam.positions)}'
              f'   (range {min(m[1].min(), m[2].min()):+.2f}..{max(m[1].max(), m[2].max()):+.2f})')


def measured_efficiency(cam, c, si):
    """Summation efficiency of the measured sum, against the |M|+|S| of the
    dry captures at the same positions, the run scaled to the model over
    500 Hz - 2 kHz where the subwoofer plays no part."""
    out = []
    ref = (f >= 500) & (f <= 2000)
    for p in cam.positions:
        M, S = cam.parts(c, si, p)
        Y = cam.meas(c, si, p)
        gy = np.exp(np.mean(np.log(sm(M + S))[ref]) - np.mean(np.log(sm(Y))[ref]))
        out.append(efficiency(sm(Y) * gy, sm(M), sm(S), c.fx))
    return np.array(out)


def table_efficiency(cam):
    print('\n== summation efficiency, [fx/2, 2fx], dB: measured | predicted from the same filters (left/right)')
    for c in cam.lf:
        m = [measured_efficiency(cam, c, si).mean() for si in (1, 2)]
        q = [np.mean([efficiency(sm(sum(cam.parts(c, si, p))), sm(cam.parts(c, si, p)[0]), sm(cam.parts(c, si, p)[1]), c.fx)
                      for p in cam.positions]) for si in (1, 2)]
        print(f'  {c.label:58s} {m[0]:+.2f} / {m[1]:+.2f} | {q[0]:+.2f} / {q[1]:+.2f}')


def table_early(cam):
    print('\n== Table: energy 30 to 3 ms before the direct sound | in the last 3 ms (3 to 1 ms), dB re the '
          'octave\'s energy after it, power mean over positions (left/right)')
    print(f'{"":62}' + ''.join(f'{b:>16g}' for b in BANDS))
    irs = {}
    for c in cam.configs:
        for si in (1, 2):
            irs[(c.name, si)] = [rolled_ir(cam.meas(c, si, p) * cam.cal) for p in cam.positions]
    for c in cam.configs:
        row = []
        for fc in BANDS:
            a = [pmean([early(ir, fc) for ir in irs[(c.name, si)]]) for si in (1, 2)]
            b = [pmean([early(ir, fc, 3, 1) for ir in irs[(c.name, si)]]) for si in (1, 2)]
            row.append(f'{a[0]:4.0f}/{a[1]:<3.0f}|{b[0]:4.0f}/{b[1]:<3.0f}')
        note = '' if c in cam.lf else '  (<250 Hz: sub route differs)'
        print(f'  {c.label:60s}' + ''.join(row) + note)
    c = cam.configs[0]
    fl = [pmean([floor(ir, fc) for si in (1, 2) for ir in irs[(c.name, si)]]) for fc in BANDS]
    print(f'  {"noise floor, 27 ms window":60s}' + ''.join(f'{x:8.0f}        ' for x in fl))
    return irs


def table_times(cam, irs):
    print('\n== octave arrival re the direct sound, ms: median seat left|right (worst seat of both, absolute)')
    print(f'{"":62}' + ''.join(f'{b:>18g}' for b in TB))
    for c in cam.configs:
        cells = []
        for fc in TB:
            v = []
            for si in (1, 2):
                t = np.array([band_time(ir, fc) for ir in irs[(c.name, si)]])
                v.append((np.median(t), t[np.argmax(np.abs(t))]))
            cells.append(f'{v[0][0]:5.2f}|{v[1][0]:<5.2f}({max(abs(v[0][1]), abs(v[1][1])):4.1f}) ')
        print(f'  {c.label:60s}' + ''.join(cells))
    for si in (1, 2):
        cells = []
        for fc in TB:
            t = np.array([band_time(rolled_ir(cam.dry[si][k][0] * cam.cal), fc) for k in cam.dry[si]])
            cells.append(f'{np.median(t):5.2f}/{t[np.argmax(np.abs(t))]:<6.2f}')
        print(f'  {"dry " + MAIN_NAME[si] + ", no filter, no subwoofer (median/worst)":60s}' + ''.join(cells))


def treble(cam):
    print('\n== treble: same-session ratio of two configurations, 200 Hz - 10 kHz, mean over positions (dB RMS)')
    for a in cam.configs:
        for b in cam.configs:
            if a.fx == b.fx and a is not b and b.minimum and not a.minimum and abs(a.T[1] - b.T[1]) < 0.05:
                s = (f >= 200) & (f <= 10000)
                r = [np.mean([db(sm(cam.meas(a, si, p))) - db(sm(cam.meas(b, si, p))) for p in cam.positions], axis=0)
                     for si in (1, 2)]
                print(f'  {a.name:32s} / {b.name:28s}: {np.sqrt(np.mean(r[0][s] ** 2)):.3f} / {np.sqrt(np.mean(r[1][s] ** 2)):.3f}')


def length(cam):
    a, b = find(cam, 70.0, False, taps=4096), find(cam, 70.0, False, taps=8192)
    if not (a and b):
        return
    print(f'\n== filter length: {a.name} minus {b.name}, level averaged over positions (dB): measured | predicted')
    band = (f >= 35) & (f <= 250)
    for si in (1, 2):
        rm = np.mean([nz(sm(cam.meas(a, si, p))) - nz(sm(cam.meas(b, si, p))) for p in cam.positions], axis=0)
        rp = np.mean([nz(sm(sum(cam.parts(a, si, p)))) - nz(sm(sum(cam.parts(b, si, p)))) for p in cam.positions], axis=0)
        k = np.argmax(np.abs(rm) * band)
        print(f'  main {si}: largest {rm[k]:+.2f} dB at {f[k]:.0f} Hz (predicted {rp[k]:+.2f}); '
              f'35-250 Hz RMS {np.sqrt(np.mean(rm[band] ** 2)):.2f} measured, {np.sqrt(np.mean(rp[band] ** 2)):.2f} predicted; '
              f'measured minus predicted {np.sqrt(np.mean((rm - rp)[band] ** 2)):.2f} dB RMS')


def offline(cam, sweep_step=1.0):
    """What the campaign could not measure: the design reproduced, leave one
    position out, the subwoofer inverted, and the design delay swept."""
    ref = cam.lf[0]
    des = Designer(cam, ref)
    print('\n== offline chain against the plugin\'s exports (40-200 Hz | 200 Hz-1 kHz | 1-16 kHz), dB RMS / deg RMS')
    for c in cam.configs:
        out = []
        for si in (1, 2):
            C = des.design(si, c, c.T[si], c.minimum, c.limited)
            for lo, hi in ((40, 200), (200, 1000), (1000, 16000)):
                s = (f >= lo) & (f < hi)
                dm = db(sm(C)) - db(sm(c.C[si]))
                dp = np.degrees(np.angle(V.smooth_var_octave(C * np.conj(c.C[si]), SR, 1 / 6, 1 / 3)))
                out.append((np.sqrt(np.mean(dm[s] ** 2)), np.sqrt(np.mean(dp[s] ** 2))))
        print(f'  {c.name:32s} ' + '  '.join(f'{m:.2f}/{p:.1f}' for m, p in out))

    # leave one position out: a capture repeated at the same place goes out with it
    print('\n== summation efficiency on the dry captures: in-sample | leave-one-position-out (left/right)')
    keys = {si: sorted(cam.dry[si]) for si in (1, 2)}
    place = {k: re.sub(r'b$', '', cam.dry_names[k]) for k in keys[1]}
    for c in cam.configs:                     # dry captures only: every design is usable here
        if c.taps != 8192 or (not c.minimum and not c.limited):
            continue
        for invert in (False, True):
            row = []
            for si in (1, 2):
                C = des.design(si, c, c.T[si], c.minimum, invert=invert)
                ins = dry_scores(cam, c, si, c.T[si], C, keys[si], invert)[:, 0].mean()
                lo = []
                for pl in sorted(set(place.values())):
                    held = [k for k in keys[si] if place[k] == pl]
                    tr = [k for k in keys[si] if place[k] != pl]
                    Cj = des.design(si, c, c.T[si], c.minimum, keys=tr, invert=invert)
                    lo += list(dry_scores(cam, c, si, c.T[si], Cj, held, invert)[:, 0])
                row += [ins, np.mean(lo)]
            print(f'  {c.label:58s} sub {"inverted" if invert else "normal  "}: '
                  f'{row[0]:+.2f} | {row[1]:+.2f}   /   {row[2]:+.2f} | {row[3]:+.2f}')

    # the design delay swept, for the figure
    sweep = {}
    Ts = np.arange(10.0, 40.0 + 1e-9, sweep_step)
    for fx in sorted({c.fx for c in cam.lf}):
        c = [x for x in cam.lf if x.fx == fx][0]
        for minimum in (False, True):
            v = []
            for T in Ts:
                r = [dry_scores(cam, c, si, T, des.design(si, c, T, minimum), keys[si]) for si in (1, 2)]
                v.append([[x[:, 0].mean(), pmean(x[:, 1]), pmean(x[:, 2])] for x in r])
            sweep[(fx, minimum)] = np.array(v)          # T x main x (eff, early63, early125)
            e = sweep[(fx, minimum)][:, :, 0].mean(axis=1)
            print(f'  delay sweep {fx:g} Hz {"minimum phase" if minimum else "all-pass     "}: efficiency '
                  f'{e.min():+.2f} (T = {Ts[np.argmin(e)]:.0f} ms) to {e.max():+.2f} (T = {Ts[np.argmax(e)]:.0f} ms) '
                  f'over {Ts[0]:.0f}-{Ts[-1]:.0f} ms')
    return Ts, sweep


# ------------------------------------------------------------------ figures
def figure_crossover(cam, out):
    """Panel per measured delay: all-pass and minimum phase, measured and
    predicted, averaged over the positions."""
    cols = []
    for fx in sorted({c.fx for c in cam.lf}):
        for T in sorted({round(c.T[1], 2) for c in cam.lf if c.fx == fx}, reverse=True):
            a, b = find(cam, fx, False, T, taps=8192), find(cam, fx, True, T)
            if a and b:
                cols.append((fx, T, a, b))
    plt.rcParams.update({'font.size': 12, 'axes.titlesize': 12, 'axes.labelsize': 11,
                         'xtick.labelsize': 10, 'ytick.labelsize': 10})
    fig, ax = plt.subplots(2, len(cols), figsize=(3.3 * len(cols), 5.4), sharey=True)
    band = (f >= 35) & (f <= 300)
    for j, (fx, T, a, b) in enumerate(cols):
        for i, si in enumerate((1, 2)):
            x = ax[i, j]
            for c, col, lab in ((a, 'tab:blue', 'all-pass'), (b, 'tab:red', 'minimum phase')):
                mm = np.mean([nz(sm(cam.meas(c, si, p))) for p in cam.positions], axis=0)
                pp = np.mean([nz(sm(sum(cam.parts(c, si, p)))) for p in cam.positions], axis=0)
                x.semilogx(f[band], mm[band], color=col, lw=1.6, label=f'{lab}, measured')
                x.semilogx(f[band], pp[band], color=col, lw=1.0, ls='--', label=f'{lab}, predicted')
            x.axvline(fx, color='k', lw=.5, alpha=.35)
            x.set_xlim(35, 300)
            x.set_xticks([40, 60, 100, 200, 300])
            x.set_xticklabels(['40', '60', '100', '200', '300'])
            x.xaxis.set_minor_formatter(matplotlib.ticker.NullFormatter())
            x.grid(True, which='both', alpha=.25)
            x.text(0.03, 0.06, MAIN_NAME[si], transform=x.transAxes, fontsize=10, color='0.35')
            if j == 0:
                x.set_ylabel('level (dB re 0.5-2 kHz)')
            if i == 1:
                x.set_xlabel('frequency (Hz)')
        ax[0, j].set_title(f'({"abcd"[j]}) {fx:g} Hz, T = {T:.1f} ms')
    fig.tight_layout()
    h, l = ax[0, 0].get_legend_handles_labels()
    fig.legend(h, l, fontsize=9.5, loc='upper center', bbox_to_anchor=(0.5, 0.02), ncol=4,
               columnspacing=1.4, handlelength=1.8, frameon=False)
    fig.savefig(out, dpi=220, bbox_inches='tight')
    print('wrote', out)


def figure_delay(cam, Ts, sweep, irs, out):
    fxs = sorted({c.fx for c in cam.lf})
    plt.rcParams.update({'font.size': 12, 'axes.titlesize': 12, 'axes.labelsize': 11,
                         'xtick.labelsize': 10, 'ytick.labelsize': 10})
    fig, ax = plt.subplots(2, len(fxs), figsize=(4.95 * len(fxs), 5.6), sharex=True)
    for j, fx in enumerate(fxs):
        for minimum, col, lab in ((False, 'tab:blue', 'all-pass'), (True, 'tab:red', 'minimum phase')):
            s = sweep[(fx, minimum)]
            ax[0, j].plot(Ts, s[:, :, 0].mean(axis=1), color=col, lw=1.5, label=f'{lab}, predicted')
            ax[1, j].plot(Ts, pmean(s[:, :, 2], axis=1), color=col, lw=1.5)
            for c in cam.lf:
                if c.fx != fx or c.minimum != minimum or c.taps != 8192:
                    continue
                for si, mk in ((1, 'o'), (2, '^')):
                    e = measured_efficiency(cam, c, si).mean()
                    pre = pmean([early(ir, 125) for ir in irs[(c.name, si)]])
                    kw = dict(color=col, marker=mk, ms=6, mfc='white', mew=1.5, ls='none')
                    ax[0, j].plot(c.T[si], e, label=f'{lab}, measured, {MAIN_NAME[si]}', **kw)
                    ax[1, j].plot(c.T[si], pre, **kw)
        ax[0, j].set_title(f'({"ab"[j]}) crossover {fx:g} Hz')
        ax[0, j].set_ylabel('summation efficiency (dB)')
        ax[1, j].set_ylabel('125 Hz octave, 30-3 ms early (dB)')
        ax[1, j].set_xlabel('design delay T (ms)')
        for x in ax[:, j]:
            x.grid(True, alpha=.25)
    lo = min(x.get_ylim()[0] for x in ax[0])
    for x in ax[0]:
        x.set_ylim(lo, 0.3)
    fig.tight_layout()
    h, l = ax[0, -1].get_legend_handles_labels()
    seen = {}
    for hh, ll in zip(h, l):
        seen.setdefault(ll, hh)
    fig.legend(list(seen.values()), list(seen.keys()), fontsize=9.5, loc='upper center',
               bbox_to_anchor=(0.5, 0.02), ncol=2, columnspacing=2.0, handlelength=1.8, frameon=False)
    fig.savefig(out, dpi=220, bbox_inches='tight')
    print('wrote', out)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--campaign', required=True)
    ap.add_argument('--preset', default='',
                    help='the plugin state the system was measured with (.xml)')
    ap.add_argument('--lf-exclude', default='',
                    help='comma-separated configuration folder names to leave out below 250 Hz')
    ap.add_argument('--out-crossover', default='../figures/crossover-measured.png')
    ap.add_argument('--out-delay', default='../figures/delay-sweep.png')
    ap.add_argument('--sweep-step', type=float, default=1.0, help='design delay step, ms')
    a = ap.parse_args()

    cam = Campaign(a.campaign, [s.strip() for s in a.lf_exclude.split(',') if s.strip()])
    print(f'{len(cam.configs)} configurations, {len(cam.positions)} system positions, '
          f'{len(cam.dry[1])} dry captures')
    for c in cam.configs:
        print(f'  inputs {c.inputs[1] + 1:2d},{c.inputs[2] + 1:2d} -> outputs {c.outputs}: {c.name:32s} {c.label}'
              + ('' if c in cam.lf else '   [left out below 250 Hz]'))
    check_wiring(cam)
    if a.preset:
        apply_preset(cam, a.preset)
    fit_sub_level(cam)
    check_sub_routes(cam)
    stray_correction(cam)
    model_accuracy(cam)
    table_levels(cam)
    table_efficiency(cam)
    irs = table_early(cam)
    table_times(cam, irs)
    treble(cam)
    length(cam)
    Ts, sweep = offline(cam, a.sweep_step)
    figure_crossover(cam, a.out_crossover)
    figure_delay(cam, Ts, sweep, irs, a.out_delay)


if __name__ == '__main__':
    main()
