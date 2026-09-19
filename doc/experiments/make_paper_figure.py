#!/usr/bin/env python3
"""Builds the Validation figure of doc/paper.tex from the measurement campaign.

Panel (a) : the correction the system really applied, against the one that was
            designed, for both phase renderings of the same analysis.
Panel (b) : the measured system through the crossover against the prediction
            of the offline model, for the three configurations that were run.
Panel (c) : summation efficiency through the crossover for five alignment
            strategies, predicted from the dry measurements.

    python3 make_paper_figure.py --dry DIR --system DIR [--out ../figures/validation.png]

`--dry` is the folder holding the per-loudspeaker captures the group analysis
was run on; `--system` the folder holding the three system runs and the two
analysis exports. Positions are matched between the two sessions by the
comment each run carries, so the sessions need not name them in the same order
or visit the same number of them.

Author: Olivier Doaré, github.com/odoare/SuperMoTo
(c) 2023-2026 Olivier Doaré
Licenced under the GNU LGPL Version 3.0
SPDX-License-Identifier: LGPL-3.0-or-later
"""
import argparse
import glob
import os
import re
import numpy as np
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
from scipy.io import wavfile

import sub_alignment_validation as V
from sub_alignment_validation import (welch_h1, ir_peak_delay, remove_delay,
                                      smooth_var_octave, butterworth, design,
                                      group_delay_estimate, load_mic_cal,
                                      mic_cal_gain, render_ir, W)

SR = 44100
CH_OF = {1: 3, 2: 4}          # main number -> dry capture channel

# Everything else about a campaign is read from it. FX, the crossover, and
# T_REPORT, the aligned delay per main, come from the group analysis report;
# the three system runs and the two exports are found by name. Every
# configuration must route into the subwoofer with the same crosspoint
# polarity, or the runs differ by a half turn as well as by their filter (see
# README.md, "The subwoofer polarity"); `--check-polarity` tests that against
# the captures.
FX = 85.0            # crossover, in the analysis and in the matrix alike
FX_BM = 85.0         # bass-management crossover, 4th order, from the matrix
SUB_ROUTE_DB = -4.8  # subwoofer routing level relative to the mains
T_REPORT = {1: 29.73, 2: 29.41}
FIR_TAPS = 0         # the length the measured run's filter was rendered at
RUNS = {}            # display name -> the folder holding that system run
EXPORTS = {}         # display name -> the folder holding that export


def discover(system):
    """The three system runs and the two exports of one campaign, by name. A
    run folder ends in _test and says which of the three it is; an export
    folder holds the analysis report and the correction files."""
    runs, exports = {}, {}
    for name in sorted(os.listdir(system)):
        path = os.path.join(system, name)
        if not os.path.isdir(path):
            continue
        which = ('minimum' if 'minimum' in name else
                 'linear' if 'linear' in name else
                 'no FIR' if 'nofir' in name else None)
        if which is None:
            continue
        if name.endswith('_test'):
            runs[which] = path
        elif os.path.exists(os.path.join(path, 'Mirage_panneaux_paper_report.md')):
            exports[which] = path
    missing = {'minimum', 'linear', 'no FIR'} - set(runs)
    if missing:
        raise SystemExit(f'{system}: no system run for {sorted(missing)}')
    if set(exports) != {'linear', 'minimum'}:
        raise SystemExit(f'{system}: expected a linear and a minimum export, found {sorted(exports)}')
    return runs, exports


def read_report(folder):
    """The group settings the analysis wrote next to its exports. The paper's
    numbers depend on the crossover and on the delay each main was given, and
    those belong to the campaign rather than to this script."""
    txt = open(os.path.join(folder, 'Mirage_panneaux_paper_report.md'),
               encoding='utf-8', errors='replace').read()

    def one(pattern, cast=float):
        m = re.search(pattern, txt)
        return cast(m.group(1)) if m else None

    delays = [float(v) for v in re.findall(r'- Applied \(aligned\) delay: ([\d.]+) ms', txt)]
    return dict(crossover=one(r'- Crossover: ([\d.]+) Hz'),
                analysis_lo=one(r'- Analysis range: ([\d.]+) Hz'),
                max_boost=one(r'- Max boost: ([\d.]+) dB'),
                fir_length=one(r'- FIR length: (\d+) samples', int),
                sub_trim=one(r'- Sub trim: (-?[\d.]+) ms'),
                # the speakers in order, the subwoofer's own 0.00 last
                delays={i + 1: d for i, d in enumerate(delays[:-1])})


def manifest(folder):
    """position comment -> the files that run wrote, in channel order. The
    file numbering is not always the position numbering, so it is read rather
    than assumed."""
    xml = open(os.path.join(folder, 'measurement.xml'),
               encoding='utf-8', errors='replace').read()
    out = {}
    for m in re.finditer(r'<Run [^>]*comment="([^"]*)"[^>]*>(.*?)</Run>', xml, re.S):
        out[m.group(1)] = re.findall(r'name="([^"]+)"', m.group(2))
    return out


def dry_index(folder):
    """position comment -> the position number the dry captures are named by."""
    out = {}
    for c, files in manifest(folder).items():
        m = re.search(r'_pos(\d+)\.wav$', files[0])
        if m:
            out[c] = int(m.group(1))
    return out


def position_map(spec):
    """'sysname=dryname,...' -> the dry capture each system position pairs with.

    Positions are matched by the comment each run carries, which only works
    while the two sessions name them the same way. They have not always: a
    campaign whose '1' is 8 cm above the dry session's '1' (which that session
    called 'h1') pairs four of six positions wrongly and silently. Pass the
    correspondence here when the names have drifted; the geometry file that
    comes with a campaign is what settles it, and the arrival times and the
    room responses both confirm it."""
    out = {}
    for pair in spec.split(','):
        if pair.strip():
            sysname, dryname = pair.split('=')
            out[sysname.strip()] = dryname.strip()
    return out


def paired(meas, idx, spec):
    """The system positions that have a dry counterpart, and which one.

    A non-empty `spec` is the whole correspondence: a system position it does
    not name has no dry capture at all and is left out of everything the model
    touches. It still counts for what is measured directly. Campaign 6 is why:
    its positions 2 and 3 are 8 cm above anything the dry session visited, and
    pairing them with the captures of the same name would have compared two
    different places in the room."""
    pos = position_map(spec)
    if pos:
        return {p: idx[d] for p, d in pos.items() if p in meas and d in idx}
    return {p: idx[p] for p in meas if p in idx}


def tf(path):
    _, d = wavfile.read(path)
    return welch_h1(d[:, 0].astype(float), d[:, 1].astype(float))


class Main:
    """Everything one main loudspeaker contributes: its two exported filters,
    the three measured runs, and the dry captures at the positions the two
    sessions share."""

    def __init__(self, a, si, f, cal):
        self.si, self.f = si, f
        self.C, self.Czero, self.lat, self.peak = {}, {}, {}, {}
        for kind, folder in EXPORTS.items():
            hits = sorted(glob.glob(os.path.join(folder, f'*speaker{si}_correction.wav')))
            if not hits:
                raise SystemExit(f'{folder}: no correction file for main {si}')
            _, c = wavfile.read(hits[0])
            c = c.astype(float)
            c = c[:, 0] if c.ndim > 1 else c
            self.lat[kind] = int(np.argmax(np.abs(c)))
            self.peak[kind] = float(np.abs(c).max())
            self.C[kind] = np.fft.rfft(c, n=W)
            # What the matrix applies is the filter advanced by its own bulk
            # latency, which the output delay has already paid for (Section
            # "Latency and time alignment").
            self.Czero[kind] = self.C[kind] * np.exp(2j * np.pi * f * self.lat[kind] / SR)
        self.Czero['no FIR'] = np.ones_like(f, dtype=complex)

        self.meas = {}
        for name, folder in RUNS.items():
            files = manifest(folder)
            self.meas[name] = {p: tf(os.path.join(folder, files[p][si - 1])) / cal
                               for p in files}

        self.positions = sorted(self.meas['linear'], key=lambda z: (len(z), z))
        pairs = paired(self.meas['linear'], dry_index(a.dry), a.positions)
        self.shared = [p for p in self.positions if p in pairs]
        # Every dry position, and the subset the two sessions share. The
        # strategy comparison is a property of the design set and uses all of
        # them; anything checked against a measured run uses the shared ones.
        idx = dry_index(a.dry)
        self.dry_all, self.arrivals = {}, {}
        for name, k in idx.items():
            A = tf(os.path.join(a.dry, f'ch{CH_OF[si]:02d}_pos{k:03d}.wav'))
            B = tf(os.path.join(a.dry, f'sub_pos{k:03d}.wav'))
            d = ir_peak_delay(A)
            self.dry_all[name] = (remove_delay(A, d) / cal, remove_delay(B, d) / cal)
            # Kept unanchored: the arrival-time estimator is the difference of
            # the two peaks, and the anchoring would take it out.
            self.arrivals[name] = (d, ir_peak_delay(B))
        bynum = {k: name for name, k in idx.items()}
        self.dry = {p: self.dry_all[bynum[pairs[p]]] for p in self.shared}

    def predict(self, name, p, hp, lp, g, invert=False):
        """The system this model expects at position p, from~(eq:predsum)."""
        Hm, Hs = self.dry[p]
        s = -g if invert else g
        return (np.exp(-2j * np.pi * self.f * T_REPORT[self.si] / 1000.0)
                * hp * self.Czero[name] * Hm + s * lp * Hs)


def fit_sub_gain(a, f, cal, hp, lp, sm, nz, lo=-14.0, hi=2.0, step=0.1):
    """The subwoofer's routing level, recovered from the run with no FIR in
    circuit. That run is the cleanest handle on it: the correction drops out,
    leaving only the two bass-management filters and this one number, and the
    dry captures carry the rest. Both mains are fitted together."""
    band = (f >= 35) & (f <= 250)
    mains = [Main(a, si, f, cal) for si in (1, 2)]
    best = None
    for gdb in np.arange(lo, hi + 1e-9, step):
        gg = 10 ** (gdb / 20)
        err = []
        for m in mains:
            for p in m.shared:
                e = (nz(sm(m.predict('no FIR', p, hp, lp, gg)))
                     - nz(sm(m.meas['no FIR'][p])))
                err.append(np.mean(e[band] ** 2))
        rms = np.sqrt(np.mean(err))
        if best is None or rms < best[0]:
            best = (rms, gdb)
    return best[1]


MAIN_NAME = {1: 'left main', 2: 'right main'}


def panel_a(ax, m, f, sm, nz):
    """The correction the system applied, against the one designed."""
    b = (f >= 150) & (f <= 16000)
    ax.semilogx(f[b], nz(sm(m.C['linear']), 1000, 5000)[b], color='k', lw=2.2,
                label='designed filter')
    for name, col in [('linear', 'tab:blue'), ('minimum', 'tab:red')]:
        ratio = np.sqrt(np.mean([(sm(m.meas[name][p]) / np.maximum(sm(m.meas['no FIR'][p]), 1e-12)) ** 2
                                 for p in m.meas[name]], axis=0))
        ax.semilogx(f[b], nz(ratio, 1000, 5000)[b], color=col, lw=1.2, ls='--',
                    label=f'applied, {name} phase')
    ax.set_ylabel('correction (dB)')
    ax.set_xlim(150, 16000)
    ax.set_xticks([200, 500, 1000, 5000, 10000])
    ax.set_xticklabels(['200', '500', '1k', '5k', '10k'])


def panel_b(ax, m, f, hp, lp, g, sm, nz):
    """The three measured states through the crossover, against the model."""
    b = (f >= 35) & (f <= 250)
    for name, col in [('no FIR', 'tab:gray'), ('minimum', 'tab:red'), ('linear', 'tab:blue')]:
        meas = np.mean([nz(sm(m.meas[name][p]), 500, 2000) for p in m.shared], axis=0)
        pred = np.mean([nz(sm(m.predict(name, p, hp, lp, g)), 500, 2000) for p in m.shared], axis=0)
        ax.semilogx(f[b], meas[b], color=col, lw=1.6, label=name)
        ax.semilogx(f[b], pred[b], color=col, lw=1.0, ls='--')
    ax.set_ylabel('level (dB re 0.5-2 kHz)')
    ax.set_xlim(35, 250)
    ax.set_xticks([40, 60, 100, 150, 250])
    ax.set_xticklabels(['40', '60', '100', '150', '250'])


def panel_c(ax, m, f, hp, lp, g):
    """Summation efficiency through the crossover, per strategy. Over every
    position of the design set, as Table 1 is: it needs no measured run."""
    b = (f >= 35) & (f <= 400)
    Hm = np.array([v[0] for v in m.dry_all.values()])
    Hs = np.array([v[1] for v in m.dry_all.values()])
    Hsm = smooth_var_octave(Hm.mean(axis=0), SR, V.SMOOTH_LO, V.SMOOTH_HI)
    Ssm = smooth_var_octave(Hs.mean(axis=0), SR, V.SMOOTH_LO, V.SMOOTH_HI)
    # The two delay estimators of Table 1, both of them: they can be more than
    # a millisecond apart, and which one a magnitude-only correction is given
    # is worth most of a decibel, so plotting only one of the two would make
    # the all-pass look better or worse than it is.
    T = group_delay_estimate(Ssm, SR, FX, True)
    Ta = float(np.mean([sub - main for main, sub in m.arrivals.values()])) * 1000.0 / SR
    for lbl, kind, Tc, col in [
            ('no correction', 'none', 0.0, 'tab:gray'),
            ('magnitude only', 'mag', 0.0, 'tab:orange'),
            ('magnitude + crossover-band delay', 'mag', T, 'tab:green'),
            ('magnitude + arrival-time delay', 'mag', Ta, 'tab:purple'),
            ('all-pass alignment', 'mixed', T, 'tab:blue')]:
        Cc = design(Hsm, Ssm, f, FX, Tc, False, kind)
        eff = []
        for p in range(len(Hm)):
            M = np.exp(-2j * np.pi * f * Tc / 1000.0) * hp * Cc * Hm[p]
            S = g * lp * Hs[p]
            eff.append(20 * np.log10(np.maximum(np.abs(M + S), 1e-12)
                                     / np.maximum(np.abs(M) + np.abs(S), 1e-12)))
        ax.semilogx(f[b], np.mean(np.array(eff), axis=0)[b], color=col, lw=1.5, label=lbl)
    ax.set_ylabel('summation efficiency (dB)')
    ax.set_xlim(35, 400)
    ax.set_xticks([40, 60, 100, 200, 400])
    ax.set_xticklabels(['40', '60', '100', '200', '400'])


LENGTHS = (2048, 4096, 8192, 16384)
LENGTH_COLOURS = ('tab:blue', 'tab:green', 'tab:orange', 'tab:red')


def rendered(C, f, n):
    """The design rendered linear phase at n taps, in the zero-latency form the
    matrix presents once the output delay has paid for the filter's own bulk
    latency. Below a few hundred hertz this is not the design: n taps span
    n/sr seconds, and the crossover region needs more of them than it gets."""
    return np.fft.rfft(render_ir(C, f, SR, n), n=W) * np.exp(2j * np.pi * f * (n // 2) / SR)


def panel_length(ax, m, f, hp, lp, g, sm, nz):
    """The measured linear-phase state against what the same design would give
    at four filter lengths."""
    b = (f >= 35) & (f <= 250)
    Hm = np.array([m.dry[p][0] for p in m.shared])
    Hs = np.array([m.dry[p][1] for p in m.shared])
    Hsm = smooth_var_octave(Hm.mean(axis=0), SR, V.SMOOTH_LO, V.SMOOTH_HI)
    Ssm = smooth_var_octave(Hs.mean(axis=0), SR, V.SMOOTH_LO, V.SMOOTH_HI)
    T = group_delay_estimate(Ssm, SR, FX, True)
    C = design(Hsm, Ssm, f, FX, T, False, 'mixed')
    meas = np.mean([nz(sm(m.meas['linear'][p]), 500, 2000) for p in m.shared], axis=0)
    ax.semilogx(f[b], meas[b], color='k', lw=2.2, label=f'measured, {FIR_TAPS}')
    for n, col in zip(LENGTHS, LENGTH_COLOURS):
        Cz = rendered(C, f, n)
        cur = np.mean([nz(sm(np.exp(-2j * np.pi * f * T_REPORT[m.si] / 1000.0)
                              * hp * Cz * m.dry[p][0] + g * lp * m.dry[p][1]), 500, 2000)
                       for p in m.shared], axis=0)
        ax.semilogx(f[b], cur[b], color=col, lw=1.3, label=f'predicted, {n}')
    ax.set_ylabel('level (dB re 0.5-2 kHz)')
    ax.set_xlim(35, 250)
    ax.set_xticks([40, 60, 100, 150, 250])
    ax.set_xticklabels(['40', '60', '100', '150', '250'])


def figure_length(a, f, hp, lp, g, cal, sm, nz, out):
    plt.rcParams.update({'font.size': 12, 'axes.titlesize': 12, 'axes.labelsize': 11,
                         'xtick.labelsize': 10, 'ytick.labelsize': 10})
    fig, ax = plt.subplots(1, 2, figsize=(9.9, 3.2))
    for col, si in enumerate((1, 2)):
        m = Main(a, si, f, cal)
        panel_length(ax[col], m, f, hp, lp, g, sm, nz)
        ax[col].set_title(f'({"ab"[col]}) {MAIN_NAME[si]}')
    share_column(ax)
    for x in ax:
        x.axvline(FX_BM, color='k', lw=.5, alpha=.35)
        x.set_xlabel('frequency (Hz)')
        x.grid(True, which='both', alpha=.25)
        x.xaxis.set_minor_formatter(matplotlib.ticker.NullFormatter())
    fig.tight_layout()
    # Below the panels: at this width there is no corner free of curves.
    h, l = ax[0].get_legend_handles_labels()
    fig.legend(h, l, fontsize=9.5, loc='upper center', bbox_to_anchor=(0.5, 0.02),
               ncol=5, columnspacing=1.4, handlelength=1.8, frameon=False)
    fig.savefig(out, dpi=220, bbox_inches='tight')
    print('wrote', out)


def share_column(column, pad=1.0):
    """One y range per column, so the two mains can be read against each other."""
    lo = min(min(l.get_ydata().min() for l in x.get_lines() if len(l.get_ydata())) for x in column)
    hi = max(max(l.get_ydata().max() for l in x.get_lines() if len(l.get_ydata())) for x in column)
    for x in column:
        x.set_ylim(lo - pad, hi + pad)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--dry', required=True, help='folder with the per-loudspeaker captures')
    ap.add_argument('--system', required=True, help='folder with the system runs and the exports')
    ap.add_argument('--positions', default='',
                    help="how the system positions pair with the dry ones when the two "
                         "sessions name them differently, e.g. '1=h1,b1=1'; by default "
                         "they are matched by name")
    ap.add_argument('--check-polarity', action='store_true',
                    help='fit each run\'s subwoofer polarity against the dry captures')
    ap.add_argument('--sub-gain', default='fit',
                    help="subwoofer routing level relative to the mains, in dB, as set "
                         "in the matrix; 'fit' recovers it from the no-FIR run")
    ap.add_argument('--bm-crossover', type=float, default=None,
                    help='bass-management crossover if it differs from the analysis one')
    ap.add_argument('--out', default='../figures/validation.png')
    ap.add_argument('--out-length', default='../figures/fir-length.png',
                    help='where to write the filter-length figure')
    a = ap.parse_args()

    global FX, FX_BM, SUB_ROUTE_DB, T_REPORT, RUNS, EXPORTS, FIR_TAPS
    RUNS, EXPORTS = discover(a.system)
    rep = read_report(EXPORTS['linear'])
    FX = rep['crossover']
    FX_BM = a.bm_crossover if a.bm_crossover is not None else FX
    T_REPORT = rep['delays']
    FIR_TAPS = rep['fir_length']
    V.ANALYSIS_LO = rep['analysis_lo']
    V.SMOOTH_LO, V.SMOOTH_HI = 1 / 6, 1 / 3
    V.MAX_BOOST_DB = rep['max_boost']
    print(f"campaign: crossover {FX:g} Hz, analysis from {V.ANALYSIS_LO:g} Hz, "
          f"max boost {V.MAX_BOOST_DB:g} dB, FIR {rep['fir_length']} taps, "
          f"sub trim {rep['sub_trim']:g} ms, delays " +
          ', '.join(f'{k}: {v:g} ms' for k, v in T_REPORT.items()))

    f = np.arange(W // 2 + 1) * SR / W
    cal = mic_cal_gain(load_mic_cal(a.dry), f)
    hp = butterworth(np.maximum(f, 1e-6), FX_BM, 'hp')
    lp = butterworth(np.maximum(f, 1e-6), FX_BM, 'lp')

    def sm0(X):
        return np.abs(smooth_var_octave(np.abs(X).astype(complex), SR, 1 / 6, 1 / 3))

    def nz0(x):
        db = 20 * np.log10(np.maximum(x, 1e-12))
        return db - db[(f >= 500) & (f <= 2000)].mean()

    if a.sub_gain == 'fit':
        SUB_ROUTE_DB = fit_sub_gain(a, f, cal, hp, lp, sm0, nz0)
        print(f'subwoofer routing level fitted from the no-FIR run: {SUB_ROUTE_DB:+.2f} dB')
    else:
        SUB_ROUTE_DB = float(a.sub_gain)
    g = 10 ** (SUB_ROUTE_DB / 20)

    def sm(X, lo=1 / 6, hi=1 / 3):
        return np.abs(smooth_var_octave(np.abs(X).astype(complex), SR, lo, hi))

    def nz(x, lo, hi):
        db = 20 * np.log10(np.maximum(x, 1e-12))
        return db - db[(f >= lo) & (f <= hi)].mean()

    # Sized so that dropping the figure into \textwidth scales it by about
    # two thirds, which puts these point sizes near 7-8 pt on the page.
    plt.rcParams.update({'font.size': 12, 'axes.titlesize': 12, 'axes.labelsize': 11,
                         'xtick.labelsize': 10, 'ytick.labelsize': 10})
    fig, ax = plt.subplots(2, 3, figsize=(9.9, 5.4))
    for row, si in enumerate((1, 2)):
        m = Main(a, si, f, cal)
        panel_a(ax[row, 0], m, f, sm, nz)
        panel_b(ax[row, 1], m, f, hp, lp, g, sm, nz)
        panel_c(ax[row, 2], m, f, hp, lp, g)
        for x in ax[row]:
            x.text(0.03, 0.94, MAIN_NAME[si], transform=x.transAxes, fontsize=10,
                   va='top', color='0.35',
                   bbox=dict(facecolor='white', edgecolor='none', alpha=0.7, pad=1.5))

    for col, title in enumerate(['(a) the correction applied',
                                 '(b) model against measurement',
                                 '(c) main and subwoofer summation']):
        ax[0, col].set_title(title)
        share_column(ax[:, col])
    for x in ax.ravel():
        x.axvline(FX_BM, color='k', lw=.5, alpha=.35)
        x.set_xlabel('frequency (Hz)')
        x.grid(True, which='both', alpha=.25)
        x.xaxis.set_minor_formatter(matplotlib.ticker.NullFormatter())
    fig.tight_layout()
    # One legend under each column rather than inside the panels: at this size
    # there is no corner of any of them that some curve does not cross.
    for col, (ncol, title) in enumerate([(1, None),
                                         (1, 'measured (solid) / predicted (dashed)'),
                                         (1, None)]):
        h, l = ax[0, col].get_legend_handles_labels()
        box = ax[1, col].get_position()
        fig.legend(h, l, fontsize=8.5, loc='upper center', ncol=ncol,
                   bbox_to_anchor=((box.x0 + box.x1) / 2, 0.015),
                   bbox_transform=fig.transFigure, columnspacing=1.0,
                   handlelength=1.6, frameon=False, title=title, title_fontsize=8)
    fig.savefig(a.out, dpi=220, bbox_inches='tight')
    print('wrote', a.out)
    figure_length(a, f, hp, lp, g, cal, sm, nz, a.out_length)
    report(a, f, hp, lp, g, cal, sm, nz)


def report(a, f, hp, lp, g, cal, sm, nz):
    """The numbers the paper quotes, for both mains, so that the text and the
    repository cannot drift apart."""
    band_hi = (f >= 200) & (f <= 10000)
    band_lo = (f >= 35) & (f <= 250)
    xband = (f >= FX / np.sqrt(2)) & (f <= FX * np.sqrt(2))
    for si in (1, 2):
        m = Main(a, si, f, cal)
        for kind in EXPORTS:
            print(f'  main {si} {kind:8s} export: peak {m.peak[kind]:.4f} '
                  f'({20 * np.log10(m.peak[kind]):+.2f} dBFS) at sample {m.lat[kind]}')
        # what the system applied, against what was designed
        for kind in EXPORTS:
            e = [nz(sm(m.meas[kind][p]) / np.maximum(sm(m.meas['no FIR'][p]), 1e-12), 1000, 5000)
                 - nz(sm(m.C[kind]), 1000, 5000) for p in m.meas[kind]]
            e = np.sqrt(np.mean(np.array(e) ** 2, axis=0))
            print(f'  main {si} {kind:8s}: applied vs designed '
                  f'{np.sqrt(np.mean(e[band_hi] ** 2)):.2f} dB RMS over 200 Hz - 10 kHz')
        if a.check_polarity:
            for name in RUNS:
                s = []
                for inv in (False, True):
                    s.append(np.sqrt(np.mean([np.mean((nz(sm(m.predict(name, p, hp, lp, g, inv)), 500, 2000)
                                                       - nz(sm(m.meas[name][p]), 500, 2000))[band_lo] ** 2)
                                              for p in m.shared])))
                print(f'  main {si} {name:8s}: subwoofer polarity normal {s[0]:.2f} dB RMS, '
                      f'inverted {s[1]:.2f} dB RMS -> '
                      f'{"INVERTED" if s[1] < s[0] else "normal"}')
        # the model against each measured run, and the crossover-band level
        band = {}
        for name in RUNS:
            e, pv, mv = [], [], []
            for p in m.shared:
                q = nz(sm(m.predict(name, p, hp, lp, g)), 500, 2000)
                mm = nz(sm(m.meas[name][p]), 500, 2000)
                e.append(np.sqrt(np.mean((q - mm)[band_lo] ** 2)))
                pv.append(10 * np.log10(np.mean(10 ** (q[xband] / 10))))
                mv.append(10 * np.log10(np.mean(10 ** (mm[xband] / 10))))
            band[name] = np.array([10 * np.log10(np.mean(10 ** (nz(sm(m.meas[name][q]), 500, 2000)[xband] / 10)))
                                   for q in m.positions])
            print(f'  main {si} {name:8s}: model vs measured {np.mean(e):.2f} dB RMS over '
                  f'35-250 Hz (worst {max(e):.2f}); crossover band predicted '
                  f'{np.mean(pv):+.2f} dB, measured {np.mean(mv):+.2f} dB')
        # what a longer filter would do to the same band (Figure: fir-length)
        Hm = np.array([m.dry[q][0] for q in m.shared])
        Hs = np.array([m.dry[q][1] for q in m.shared])
        Hsm = smooth_var_octave(Hm.mean(axis=0), SR, V.SMOOTH_LO, V.SMOOTH_HI)
        Ssm = smooth_var_octave(Hs.mean(axis=0), SR, V.SMOOTH_LO, V.SMOOTH_HI)
        Cd = design(Hsm, Ssm, f, FX, group_delay_estimate(Ssm, SR, FX, True), False, 'mixed')
        lev = []
        for n in LENGTHS:
            Cz = rendered(Cd, f, n)
            v = [10 * np.log10(np.mean(10 ** (nz(sm(np.exp(-2j * np.pi * f * T_REPORT[si] / 1000.0)
                                                          * hp * Cz * m.dry[q][0] + g * lp * m.dry[q][1]),
                                                 500, 2000)[xband] / 10)))
                 for q in m.shared]
            lev.append(f'{n}: {np.mean(v):+.2f}')
        print(f'  main {si} crossover band, linear phase rendered at ' + ', '.join(lev) + ' dB')
        d = band['linear'] - band['minimum']
        print(f'  main {si} A/B: the aligned export carries {d.mean():+.2f} dB more through the '
              f'crossover band than the minimum-phase one, at {int((d > 0).sum())} of '
              f'{len(d)} positions (per position {d.min():+.2f} to {d.max():+.2f})')
        # the delay the system really shows, against the filter's own latency
        dd = {name: [ir_peak_delay(m.meas[name][p]) for p in m.meas[name]] for name in RUNS}
        print(f'  main {si} system delay: ' + ', '.join(
            f'{k} {min(v) * 1000 / SR:.1f}-{max(v) * 1000 / SR:.1f} ms' for k, v in dd.items()))


if __name__ == '__main__':
    main()
