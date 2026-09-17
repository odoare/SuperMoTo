#!/usr/bin/env python3
"""Builds the Validation figure of doc/paper.tex from the measurement campaign.

Left panel  : the offline prediction against the measured linear-phase system,
              which is what licenses the right panel.
Right panel : summation efficiency through the crossover for four alignment
              strategies, predicted from the dry measurements.

    python3 make_paper_figure.py --campaign DIR [--out ../figures/validation.png]
"""
import argparse
import os
import numpy as np
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
from scipy.io import wavfile

import sub_alignment_validation as V
from sub_alignment_validation import (welch_h1, ir_peak_delay, remove_delay,
                                      smooth_var_octave, butterworth, design,
                                      group_delay_estimate, W)

FX = 85.0          # crossover, both in the analysis and in the matrix
FX_BM = 85.0       # bass-management crossover, 4th order, from the matrix
T_REPORT = 29.73   # main delay relative to the subwoofer, from the report
SUB_ROUTE_DB = -4.8  # subwoofer routing level relative to the mains, from the matrix


def curves(dry, ch, n=10):
    Hm, Hs = [], []
    for p in range(1, n + 1):
        _, m = wavfile.read(os.path.join(dry, f'ch{ch:02d}_pos{p:03d}.wav'))
        _, s = wavfile.read(os.path.join(dry, f'sub_pos{p:03d}.wav'))
        A = welch_h1(m[:, 0].astype(float), m[:, 1].astype(float))
        B = welch_h1(s[:, 0].astype(float), s[:, 1].astype(float))
        d = ir_peak_delay(A)
        Hm.append(remove_delay(A, d))
        Hs.append(remove_delay(B, d))
    return np.array(Hm), np.array(Hs)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--campaign', required=True)
    ap.add_argument('--out', default='../figures/validation.png')
    a = ap.parse_args()

    dry = os.path.join(a.campaign, 'Mirage_panneaux_paper')
    test = os.path.join(a.campaign, 'Mirage_panneaux_paper_linear_test')
    firdir = os.path.join(a.campaign, 'Mirage_panneaux_paper_linear')

    V.ANALYSIS_LO = 85.0
    V.SMOOTH_LO, V.SMOOTH_HI = 1 / 6, 1 / 3
    V.MAX_BOOST_DB = 9.0

    sr = 44100
    f = np.arange(W // 2 + 1) * sr / W
    hp = butterworth(np.maximum(f, 1e-6), FX_BM, 'hp')
    lp = butterworth(np.maximum(f, 1e-6), FX_BM, 'lp')
    Hm, Hs = curves(dry, 3)

    def pavg(X, frac=1 / 6):
        p = np.sqrt(np.mean(np.abs(X) ** 2, axis=0))
        return np.abs(smooth_var_octave(p.astype(complex), sr, frac, frac))

    def nz(x):
        db = 20 * np.log10(np.maximum(x, 1e-12))
        return db - db[(f >= 1000) & (f <= 5000)].mean()

    fig, ax = plt.subplots(1, 2, figsize=(9.6, 3.5))

    # ---- left: prediction against the measured system ----------------------
    _, c = wavfile.read(os.path.join(
        firdir, 'Mirage_panneaux_paper_linear_speaker1_correction.wav'))
    c = c.astype(float)
    c = c[:, 0] if c.ndim > 1 else c
    C = np.fft.rfft(c, n=W)
    g = 10 ** (SUB_ROUTE_DB / 20)
    pred = np.array([np.exp(-1j * 2 * np.pi * f * T_REPORT / 1000.0) * hp * C * Hm[p]
                     + g * lp * Hs[p] for p in range(10)])
    meas = []
    for p in range(1, 11):
        _, d = wavfile.read(os.path.join(test, f'in01_pos{p:03d}.wav'))
        H = welch_h1(d[:, 0].astype(float), d[:, 1].astype(float))
        meas.append(remove_delay(H, ir_peak_delay(H)))
    meas = np.array(meas)

    b = (f >= 35) & (f <= 400)
    ax[0].semilogx(f[b], nz(pavg(meas))[b], color='k', lw=2.0, label='measured')
    ax[0].semilogx(f[b], nz(pavg(pred))[b], color='tab:blue', lw=1.3, ls='--',
                   label='predicted')
    ax[0].set_ylabel('level (dB)')
    ax[0].set_title('(a) prediction against measurement', fontsize=9)
    ax[0].set_ylim(-16, 6)

    # ---- right: summation efficiency per strategy --------------------------
    Hsm = smooth_var_octave(Hm.mean(axis=0), sr, V.SMOOTH_LO, V.SMOOTH_HI)
    Ssm = smooth_var_octave(Hs.mean(axis=0), sr, V.SMOOTH_LO, V.SMOOTH_HI)
    T = group_delay_estimate(Ssm, sr, FX, True)
    for lbl, kind, Tc, col, ls in [
            ('no correction', 'none', 0.0, 'tab:gray', '-'),
            ('magnitude only', 'mag', 0.0, 'tab:orange', '-'),
            ('magnitude + delay', 'mag', T, 'tab:green', '-'),
            ('all-pass alignment', 'mixed', T, 'tab:blue', '-')]:
        Cc = design(Hsm, Ssm, f, FX, Tc, False, kind)
        eff = []
        for p in range(10):
            M = np.exp(-1j * 2 * np.pi * f * Tc / 1000.0) * hp * Cc * Hm[p]
            S = g * lp * Hs[p]
            eff.append(20 * np.log10(np.maximum(np.abs(M + S), 1e-12)
                                     / np.maximum(np.abs(M) + np.abs(S), 1e-12)))
        ax[1].semilogx(f[b], np.mean(np.array(eff), axis=0)[b], color=col, lw=1.5,
                       ls=ls, label=lbl)
    ax[1].set_ylabel('summation efficiency (dB)')
    ax[1].set_title('(b) main and subwoofer summation', fontsize=9)
    ax[1].set_ylim(-14, 2)

    for x in ax:
        x.axvline(FX_BM, color='k', lw=.5, alpha=.35)
        x.set_xlim(35, 400)
        x.set_xlabel('frequency (Hz)')
        x.grid(True, which='both', alpha=.25)
        x.legend(fontsize=7.5, loc='lower right')
        x.set_xticks([40, 60, 100, 200, 400])
        x.set_xticklabels(['40', '60', '100', '200', '400'])
    fig.tight_layout()
    fig.savefig(a.out, dpi=220, bbox_inches='tight')
    print('wrote', a.out)


if __name__ == '__main__':
    main()
