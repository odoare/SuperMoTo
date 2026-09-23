#!/usr/bin/env python3
"""Builds the crossover-sweep figure of doc/paper.tex from the dry captures.

The campaign was measured at one crossover. The bass management, the delay and
the correction are all modelled from the per-loudspeaker captures, so the same
recordings can be scored at any other crossover without measuring again -- and
the ranking of the alignment strategies turns out to depend on it more than on
anything else in the rig.

    python3 make_crossover_figure.py --dry DIR --system DIR
        [--out ../figures/crossover-sweep.png]

WHAT THIS IS AND IS NOT. Every point is a prediction of the offline model, not
a measurement: only the crossover the campaign actually used was measured, and
Figure "validation"(b) is what says the model can be trusted there (0.8-1.3 dB
RMS over 35-250 Hz). The rest of the curve says where to point the next
measurement, and the paper says so.

Only the crossover varies. The analysis low edge stays where the campaign set
it, so the comparison is one variable at a time; scoring instead with the edge
following the crossover moves every curve by under 0.05 dB above 60 Hz, which
is why the sweep starts there rather than lower.

Author: Olivier Doaré, github.com/odoare/SuperMoTo
(c) 2023-2026 Olivier Doaré
Licenced under the GNU LGPL Version 3.0
SPDX-License-Identifier: LGPL-3.0-or-later
"""
import argparse
import os

import numpy as np
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt

import make_paper_figure as P
import sub_alignment_validation as V
from sub_alignment_validation import (smooth_average, position_average, design,
                                      group_delay_estimate, mic_cal_gain,
                                      load_mic_cal, butterworth, metrics, W)

SR = P.SR

# Crossovers to score. Below 60 Hz the scored band [fx/2, 2fx] reaches far
# enough under the analysis edge for that, rather than the crossover, to be
# what moves the curves.
CROSSOVERS = (60, 70, 80, 90, 100, 110, 120, 135, 150)

# The same colours the strategy panel of the validation figure uses, so the two
# read together.
STRATEGIES = [
    ('no correction',                    'none',  'zero',  'tab:gray'),
    ('magnitude only',                   'mag',   'zero',  'tab:orange'),
    ('magnitude + crossover-band delay', 'mag',   'xover', 'tab:green'),
    ('magnitude + arrival-time delay',   'mag',   'arr',   'tab:purple'),
    ('all-pass alignment',               'mixed', 'xover', 'tab:blue'),
]

INVERTED = [
    ('magnitude + crossover-band delay', 'mag',   'xover', 'tab:green'),
    ('all-pass alignment',               'mixed', 'xover', 'tab:blue'),
]


def score(mains, f, fx, kind, which, invert, g):
    """One strategy at one crossover: the summation efficiency of every
    position of both mains, averaged, exactly as Table 1 scores them."""
    hp = butterworth(np.maximum(f, 1e-6), fx, 'hp')
    lp = butterworth(np.maximum(f, 1e-6), fx, 'lp')
    out = []

    for Hm, Hs, Ta in mains.values():
        Hsm = smooth_average(position_average(Hm), SR, V.SMOOTH_LO, V.SMOOTH_HI)
        Ssm = smooth_average(position_average(Hs), SR, V.SMOOTH_LO, V.SMOOTH_HI)
        T = {'zero': 0.0, 'arr': Ta, 'xover': group_delay_estimate(Ssm, SR, fx, True)}[which]
        C = design(Hsm, Ssm, f, fx, T, invert, kind)
        s = -g if invert else g
        out += [metrics(np.exp(-2j * np.pi * f * T / 1000.0) * hp * C * Hm[i],
                        s * lp * Hs[i], f, fx)['eff_db']
                for i in range(len(Hm))]

    return float(np.mean(out))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--dry', required=True, help='folder with the per-loudspeaker captures')
    ap.add_argument('--system', required=True, help='folder with the runs and the exports')
    ap.add_argument('--sub-gain', type=float, default=None,
                    help="subwoofer routing level in dB; fitted from the no-FIR run by default")
    ap.add_argument('--out', default='../figures/crossover-sweep.png')
    a = ap.parse_args()
    a.positions = ''

    P.RUNS, P.EXPORTS = P.discover(a.system)
    rep = P.read_report(P.EXPORTS['linear'])
    P.EXCLUDED = rep['excluded']
    P.FX = P.FX_BM = rep['crossover']
    P.T_REPORT, P.FIR_TAPS = rep['delays'], rep['fir_length']
    V.ANALYSIS_LO = rep['analysis_lo']
    V.SMOOTH_LO, V.SMOOTH_HI = 1 / 6, 1 / 3
    V.MAX_BOOST_DB = rep['max_boost']

    f = np.arange(W // 2 + 1) * SR / W
    cal = mic_cal_gain(load_mic_cal(a.dry), f)

    if a.sub_gain is None:
        hp = butterworth(np.maximum(f, 1e-6), P.FX, 'hp')
        lp = butterworth(np.maximum(f, 1e-6), P.FX, 'lp')
        sm0 = lambda X: np.abs(V.smooth_var_octave(np.abs(X).astype(complex), SR, 1 / 6, 1 / 3))
        nz0 = lambda x: (20 * np.log10(np.maximum(x, 1e-12))
                         - 20 * np.log10(np.maximum(x, 1e-12))[(f >= 500) & (f <= 2000)].mean())
        a.sub_gain = P.fit_sub_gain(a, f, cal, hp, lp, sm0, nz0)
    g = 10 ** (a.sub_gain / 20.0)
    print(f'campaign crossover {P.FX:g} Hz, subwoofer routing {a.sub_gain:+.2f} dB, '
          f'positions left out: {", ".join(sorted(P.EXCLUDED)) or "none"}')

    # The captures, read once: the sweep re-scores them, it does not re-read them.
    mains = {}
    for si in (1, 2):
        m = P.Main(a, si, f, cal)
        names = list(m.dry_all)
        mains[si] = (np.array([m.dry_all[p][0] for p in names]),
                     np.array([m.dry_all[p][1] for p in names]),
                     float(np.mean([sub - mn for mn, sub in m.arrivals.values()])) * 1000.0 / SR)

    plt.rcParams.update({'font.size': 12, 'axes.titlesize': 12, 'axes.labelsize': 11,
                         'xtick.labelsize': 10, 'ytick.labelsize': 10})
    fig, ax = plt.subplots(1, 2, figsize=(9.9, 3.4))

    floor = {}
    for panel, (strategies, invert, title) in enumerate(
            [(STRATEGIES, False, '(a) subwoofer the right way up'),
             (INVERTED, True, '(b) subwoofer inverted')]):
        for label, kind, which, colour in strategies:
            y = [score(mains, f, float(fx), kind, which, invert, g) for fx in CROSSOVERS]
            ax[panel].plot(CROSSOVERS, y, color=colour, lw=1.8, marker='o', ms=3.5,
                           label=label)
            floor[panel] = min(floor.get(panel, 0.0), min(y))
            print(f'  {title[:3]} {label:34} ' +
                  ' '.join(f'{v:6.2f}' for v in y))
        ax[panel].set_title(title)
        ax[panel].axvline(P.FX, color='k', lw=.8, alpha=.45)
        ax[panel].annotate('measured here', xy=(P.FX, 0.02), xycoords=('data', 'axes fraction'),
                           xytext=(4, 4), textcoords='offset points', fontsize=9, color='0.35')
        ax[panel].set_xlabel('crossover frequency (Hz)')
        ax[panel].set_xscale('log')
        ax[panel].set_xticks(list(CROSSOVERS))
        ax[panel].set_xticklabels([str(c) for c in CROSSOVERS])
        ax[panel].xaxis.set_minor_formatter(matplotlib.ticker.NullFormatter())
        ax[panel].grid(True, which='major', alpha=.25)

    # Nothing is clipped: a strategy that collapses is the point of the panel,
    # and a curve running off the top of the axis would hide how far it fell.
    ax[0].set_ylabel('summation efficiency (dB)')
    for panel, x in enumerate(ax):
        x.set_ylim(floor[panel] - 0.6, 0.4)
        x.axhline(0.0, color='0.7', lw=.8)

    fig.tight_layout()
    h0, l0 = ax[0].get_legend_handles_labels()
    fig.legend(h0, l0, fontsize=9.5, loc='upper center', bbox_to_anchor=(0.5, 0.02), ncol=3)
    fig.savefig(os.path.abspath(a.out), dpi=220, bbox_inches='tight')
    print('wrote', os.path.abspath(a.out))


if __name__ == '__main__':
    main()
