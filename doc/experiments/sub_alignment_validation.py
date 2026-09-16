#!/usr/bin/env python3
"""
Offline validation of SuperMoTo's subwoofer phase alignment.

Re-implements the plugin's analysis chain in numpy (Welch H1 transfer
functions, impulse-response-peak delay removal with subwoofer anchoring,
complex spatial averaging, variable-octave complex smoothing, regularised
inverse with soft-knee boost limit and low-frequency slope, and the all-pass
subwoofer alignment term), then predicts the acoustic main-plus-subwoofer
summation at every microphone position for a set of alignment strategies and
scores them against each other.

The prediction is exact given linearity. Each subwoofer capture is anchored on
the main capture of the same position, so the same propagation delay has been
removed from both and the summation at that position is

    Y_p(f) = e^{-j 2 pi f T} HP(f) C(f) H_p(f)  +/-  LP(f) S_p(f)

up to that common factor, with T the main's delay relative to the subwoofer,
HP and LP the bass-management filters the configuration tool writes (4th-order
Butterworth) and C the designed correction.

Usage
-----
    python3 sub_alignment_validation.py --data DIR [--crossover 85] [--figure out.png]

Expects `<prefix>pos<P>_<C>.wav` stereo captures (channel 1 = sent stimulus,
channel 2 = microphone) as written by the measurement view, with two main
channels and one subwoofer channel measured at each position.

Author: Olivier Doaré, github.com/odoare
Licenced under the GNU LGPL Version 3.0
SPDX-License-Identifier: LGPL-3.0-or-later
"""
import argparse
import os
import numpy as np
from scipy.io import wavfile

# Plugin defaults (AnalysisEngine.h).
W = 65536
SMOOTH_LO = SMOOTH_HI = 1.0 / 6.0
SMOOTH_ANCHOR_LO, SMOOTH_ANCHOR_HI = 100.0, 10000.0
MAX_BOOST_DB = 12.0
LF_CORNER = 30.0
ANALYSIS_LO, ANALYSIS_HI = 20.0, 20000.0
ALIGN_WIDTH_OCT = 1.0


# ------------------------------------------------------------------ analysis
def welch_h1(x, y, w=W):
    """H1 = Pxy / Pxx over Hann windows with 50 % overlap."""
    hop = w // 2
    win = 0.5 * (1.0 - np.cos(2 * np.pi * np.arange(w) / (w - 1)))
    n = min(len(x), len(y))
    pxx = np.zeros(w // 2 + 1)
    pxy = np.zeros(w // 2 + 1, dtype=complex)
    for s in range(0, n - w + 1, hop):
        X = np.fft.rfft(x[s:s + w] * win)
        Y = np.fft.rfft(y[s:s + w] * win)
        pxx += np.abs(X) ** 2
        pxy += np.conj(X) * Y
    return pxy / (pxx + pxx.max() * 1e-10 + 1e-30)


def ir_peak_delay(H, w=W):
    """Propagation delay in samples from the impulse-response peak. A peak in
    the second half is a small negative delay wrapped around."""
    ir = np.fft.irfft(H, n=w)
    k = int(np.argmax(np.abs(ir)))
    return float(k if k <= w // 2 else k - w)


def remove_delay(H, delay, w=W):
    return H * np.exp(1j * 2 * np.pi * np.arange(len(H)) * delay / w)


def smooth_var_octave(H, sr, lo=SMOOTH_LO, hi=SMOOTH_HI, w=W):
    """Moving complex average over a +/- (fraction/2) octave band, the octave
    fraction log-interpolated between the two anchors."""
    n = len(H)
    pre = np.concatenate([[0], np.cumsum(H)])
    f = np.arange(n) * sr / w
    t = np.zeros(n)
    ok = f > 0
    t[ok] = np.clip((np.log2(f[ok]) - np.log2(SMOOTH_ANCHOR_LO))
                    / (np.log2(SMOOTH_ANCHOR_HI) - np.log2(SMOOTH_ANCHOR_LO)), 0, 1)
    r = 2.0 ** ((lo + t * (hi - lo)) * 0.5)
    k = np.arange(n)
    klo = np.clip(np.floor(k / r).astype(int), 0, n - 1)
    khi = np.clip(np.ceil(k * r).astype(int), 0, n - 1)
    return (pre[khi + 1] - pre[klo]) / (khi - klo + 1)


def load_set(data, prefix, positions, main_idx, sub_idx):
    """Per-position transfer functions, mains with their own delay removed and
    subwoofers anchored on the main of the same position."""
    out, sr = {}, None
    for name, idx in main_idx.items():
        mains, subs, dmain, dsub = [], [], [], []
        for p in positions:
            sr_m, m = wavfile.read(os.path.join(data, f'{prefix}pos{p}_{idx}.wav'))
            sr_s, s = wavfile.read(os.path.join(data, f'{prefix}pos{p}_{sub_idx}.wav'))
            if sr_m != sr_s:
                raise SystemExit('sample rates differ between main and sub captures')
            sr = sr_m
            Hm = welch_h1(m[:, 0].astype(float), m[:, 1].astype(float))
            Hs = welch_h1(s[:, 0].astype(float), s[:, 1].astype(float))
            d = ir_peak_delay(Hm)
            dmain.append(d)
            dsub.append(ir_peak_delay(Hs))
            mains.append(remove_delay(Hm, d))
            subs.append(remove_delay(Hs, d))
        out[name] = dict(H=np.array(mains), S=np.array(subs),
                         d=np.array(dmain), d_sub_own=np.array(dsub))
    return sr, np.arange(W // 2 + 1) * sr / W, out


def group_delay_estimate(Ssm, sr, fx, weighted=True, npts=48):
    """Least-squares slope of the unwrapped anchored subwoofer phase over
    [fx/2, 2fx], weighted by the measured power. Returns milliseconds."""
    flo, fhi = max(1.0, fx * 0.5), fx * 2.0
    f = flo * (fhi / flo) ** (np.arange(npts) / (npts - 1))
    b = f * W / sr
    k0 = np.clip(b.astype(int), 0, len(Ssm) - 1)
    k1 = np.clip(k0 + 1, 0, len(Ssm) - 1)
    S = Ssm[k0] * (1 - (b - k0)) + Ssm[k1] * (b - k0)
    ph = np.unwrap(np.angle(S))
    wt = np.abs(S) ** 2 if weighted else np.ones_like(ph)
    sw, sf, sp = wt.sum(), (wt * f).sum(), (wt * ph).sum()
    sff, sfp = (wt * f * f).sum(), (wt * f * ph).sum()
    den = sw * sff - sf * sf
    if abs(den) < 1e-30:
        return 0.0
    return -((sw * sfp - sf * sp) / den) / (2 * np.pi) * 1000.0


# -------------------------------------------------------------------- design
def butterworth(f, fc, kind, order=4):
    """4th-order Butterworth as two biquads with the Butterworth Q pair, which
    is what the configuration tool writes for bass management."""
    s = 1j * f / fc
    qs = (0.54119610, 1.30656296) if order == 4 else (0.70710678,)
    H = np.ones_like(s)
    for q in qs:
        H *= (s * s if kind == 'hp' else 1.0) / (s * s + s / q + 1.0)
    return H


def band_weight(f, lo=ANALYSIS_LO, hi=ANALYSIS_HI):
    w = np.ones_like(f)
    below, above = f < lo, f > hi
    d = np.clip(np.log2(np.maximum(f[below], 1e-6) / lo) / 0.5 + 1.0, 0, 1)
    w[below] = 0.5 - 0.5 * np.cos(np.pi * d)
    d2 = np.clip(np.log2(f[above] / hi) / 0.5, 0, 1)
    w[above] = 0.5 + 0.5 * np.cos(np.pi * d2)
    return w


def align_weight(f, fx, width=ALIGN_WIDTH_OCT):
    x = np.clip(np.log2(np.maximum(f, 1e-6) / fx) / width, 0, 1)
    return 0.5 + 0.5 * np.cos(np.pi * x)


def base_correction(Hsm, f):
    """Regularised complex inverse, soft-knee boost limit, LF slope."""
    ref = np.abs(Hsm[(f >= 200) & (f <= 2000)]).mean()
    h = Hsm / ref
    nh = np.abs(h) ** 2
    c = np.where(nh > 1e-12, np.conj(h) / np.maximum(nh, 1e-300),
                 10 ** (MAX_BOOST_DB / 20))
    db = 20 * np.log10(np.maximum(np.abs(c), 1e-12))
    knee = min(6.0, MAX_BOOST_DB)
    T = MAX_BOOST_DB - knee
    lim = np.where(db > T, T + knee * np.tanh((db - T) / knee), db)
    c = c * 10 ** ((np.where(db > 0, lim, db) - db) / 20)
    jw = 1j * f / LF_CORNER
    return c * (jw * jw) / (jw * jw + np.sqrt(2) * jw + 1.0)


def allpass_term(Ssm, f, fx, T_ms, invert=False, mode='phasor'):
    """The alignment factor A(f).

    'phasor'    SuperMoTo: blend the unit phasor towards 1 above the crossover
                and renormalise, so the rotation stays on the short arc.
    'unwrapped' ablation: scale an unwrapped phase angle instead, unwrapped
                outwards from the crossover (the fairest version, since
                anchoring at DC where the subwoofer is noise is worse).
    """
    S = (-Ssm if invert else Ssm) * np.exp(1j * 2 * np.pi * f * T_ms / 1000.0)
    a = np.abs(S)
    ph = np.where(a > 1e-20, S / np.maximum(a, 1e-300), 1.0)
    wx = align_weight(f, fx)
    if mode == 'phasor':
        A = (1 - wx) + wx * ph
        aA = np.abs(A)
        return np.where(aA > 1e-12, A / np.maximum(aA, 1e-300), 1.0)
    k0 = int(np.argmin(np.abs(f - fx)))
    a_ = np.angle(ph)
    ang = np.zeros_like(f)
    ang[k0] = a_[k0]
    for k in range(k0 + 1, len(f)):
        ang[k] = ang[k - 1] + np.angle(np.exp(1j * (a_[k] - a_[k - 1])))
    for k in range(k0 - 1, -1, -1):
        ang[k] = ang[k + 1] - np.angle(np.exp(1j * (a_[k + 1] - a_[k])))
    return np.exp(1j * wx * ang)


def min_phase(C):
    """Real-cepstrum minimum-phase response with the same magnitude."""
    n = (len(C) - 1) * 2
    lm = np.log(np.maximum(np.abs(C), 1e-12))
    cep = np.real(np.fft.ifft(np.concatenate([lm, lm[-2:0:-1]])))
    lift = np.zeros(n)
    lift[0] = lift[n // 2] = 1.0
    lift[1:n // 2] = 2.0
    return np.exp(np.fft.fft(cep * lift))[:len(C)]


def design(Hsm, Ssm, f, fx, T_ms, invert, kind):
    """kind: none | mag | mixed | mixed_unwrapped"""
    if kind == 'none':
        return np.ones_like(Hsm)
    c = base_correction(Hsm, f)
    if kind == 'mag':
        c = min_phase(c)
    else:
        c = c * allpass_term(Ssm, f, fx, T_ms, invert,
                             'phasor' if kind == 'mixed' else 'unwrapped')
    w = band_weight(f)
    return (1 - w) + w * c


def render_ir(C, f, sr, n=16384):
    """Mixed/linear-phase render: resample onto the FIR grid, centre at n/2,
    Tukey 10 % taper."""
    fg = np.arange(n // 2 + 1) * sr / n
    spec = (np.interp(fg, f, C.real) + 1j * np.interp(fg, f, C.imag))
    spec = spec * ((-1.0) ** np.arange(n // 2 + 1))
    ir = np.fft.irfft(spec, n=n)
    taper = int(0.1 * n)
    win = np.ones(n)
    win[:taper] = 0.5 * (1 - np.cos(np.pi * np.arange(taper) / taper))
    win[-taper:] = win[:taper][::-1]
    return ir * win


def ir_span_ms(ir, sr, frac=0.99):
    """Smallest window around the peak holding `frac` of the energy."""
    e = ir ** 2
    tot = e.sum()
    pk = int(np.argmax(np.abs(ir)))
    acc, lo, hi = e[pk], pk, pk
    while acc < frac * tot and (lo > 0 or hi < len(ir) - 1):
        left = e[lo - 1] if lo > 0 else -1.0
        right = e[hi + 1] if hi < len(ir) - 1 else -1.0
        if left >= right:
            lo -= 1
            acc += left
        else:
            hi += 1
            acc += right
    return (hi - lo + 1) / sr * 1000.0


# ------------------------------------------------------------------- scoring
def metrics(M, S, f, fx, band=(0.5, 2.0)):
    """Summation quality of one position over [fx/2, 2fx].

    eff_db    20log10|M+S| - 20log10(|M|+|S|), weighted by the overlap of the
              two sources. 0 dB is perfectly in phase, negative is cancellation.
    dphi_deg  overlap-weighted RMS phase difference between the two.
    ripple_db standard deviation of the summed level over the band.
    notch_db  worst single-frequency cancellation in the band.

    The table also reports max rot, the largest rotation the alignment factor
    A applies anywhere over 20-400 Hz. The phasor blend is bounded to 180 deg
    by construction (it interpolates on the short arc between 1 and the
    subwoofer phasor); the unwrapped-angle ablation is not.
    """
    sel = (f >= fx * band[0]) & (f <= fx * band[1]) & (f > 0)
    m, s = M[sel], S[sel]
    tot, inc = np.abs(m + s), np.abs(m) + np.abs(s)
    ov = np.minimum(np.abs(m), np.abs(s))
    wsum = ov.sum() + 1e-30
    eff = 20 * np.log10(np.maximum(tot, 1e-12) / np.maximum(inc, 1e-12))
    dphi = np.angle(m * np.conj(s))
    return dict(eff_db=float((eff * ov).sum() / wsum),
                dphi_deg=float(np.degrees(np.sqrt(((dphi ** 2) * ov).sum() / wsum))),
                ripple_db=float(np.std(20 * np.log10(np.maximum(tot, 1e-12)))),
                notch_db=float(eff.min()))


CONDITIONS = [
    ('no correction, no alignment',      'none',            'zero',  False),
    ('magnitude only (min-phase)',       'mag',             'zero',  False),
    ('mag + arrival-time delay',         'mag',             'arr',   False),
    ('mag + crossover-band delay',       'mag',             'xover', False),
    ('mag + x-over delay, sub inverted', 'mag',             'xover', True),
    ('all-pass alignment (SuperMoTo)',   'mixed',           'xover', False),
    ('all-pass, sub inverted',           'mixed',           'xover', True),
    ('all-pass, no bulk delay',          'mixed',           'zero',  False),
    ('ablation: unwrapped-angle blend',  'mixed_unwrapped', 'xover', False),
    ('ablation: unwrapped, no delay',    'mixed_unwrapped', 'zero',  False),
]


def evaluate(d, f, sr, fx, loo):
    """Score every condition. With loo=True the correction for position p is
    designed from the other positions only."""
    npos = len(d['H'])
    rows = {}
    for label, kind, tmode, inv in CONDITIONS:
        per, spans = [], []
        for p in range(npos):
            tr = [q for q in range(npos) if q != p] if loo else list(range(npos))
            Hsm = smooth_var_octave(d['H'][tr].mean(axis=0), sr)
            Ssm = smooth_var_octave(d['S'][tr].mean(axis=0), sr)
            if tmode == 'zero':
                T = 0.0
            elif tmode == 'arr':
                T = (np.median(d['d_sub_own'][tr]) - np.median(d['d'][tr])) / sr * 1000.0
            else:
                T = group_delay_estimate(Ssm, sr, fx, True)
            C = design(Hsm, Ssm, f, fx, T, inv, kind)
            M = np.exp(-1j * 2 * np.pi * f * T / 1000.0) * butterworth(np.maximum(f, 1e-6), fx, 'hp') * C * d['H'][p]
            S = (-1.0 if inv else 1.0) * butterworth(np.maximum(f, 1e-6), fx, 'lp') * d['S'][p]
            per.append(metrics(M, S, f, fx))
            if p == 0 and kind.startswith('mixed'):
                A = allpass_term(Ssm, f, fx, T, inv,
                                 'phasor' if kind == 'mixed' else 'unwrapped')
                sel = (f >= 20) & (f <= 400)
                spans.append(np.degrees(np.abs(np.angle(A[sel])).max()))
        agg = {k: float(np.mean([r[k] for r in per])) for k in per[0]}
        agg['worst'] = float(np.min([r['eff_db'] for r in per]))
        agg['rot_deg'] = spans[0] if spans else float('nan')
        rows[label] = agg
    return rows


def print_table(rows, title):
    print(f'\n--- {title} ---')
    print(f'{"condition":36s} {"eff dB":>7s} {"worst":>7s} {"|dphi|":>7s} '
          f'{"ripple":>7s} {"notch":>7s} {"max rot":>8s}')
    for label, r in rows.items():
        rot = '      --' if np.isnan(r['rot_deg']) else f'{r["rot_deg"]:7.0f}d'
        print(f'{label:36s} {r["eff_db"]:7.2f} {r["worst"]:7.2f} '
              f'{r["dphi_deg"]:6.1f}d {r["ripple_db"]:7.2f} {r["notch_db"]:7.2f} '
              f'{rot}')


def make_figure(d, f, sr, fx, path):
    import matplotlib
    matplotlib.use('Agg')
    import matplotlib.pyplot as plt

    hp = butterworth(np.maximum(f, 1e-6), fx, 'hp')
    lp = butterworth(np.maximum(f, 1e-6), fx, 'lp')
    Hsm = smooth_var_octave(d['H'].mean(axis=0), sr)
    Ssm = smooth_var_octave(d['S'].mean(axis=0), sr)
    T = group_delay_estimate(Ssm, sr, fx, True)

    shown = [('no correction', 'none', 0.0, 'tab:gray'),
             ('magnitude only', 'mag', 0.0, 'tab:orange'),
             ('magnitude + bulk delay', 'mag', T, 'tab:green'),
             ('all-pass alignment', 'mixed', T, 'tab:blue')]

    fig, ax = plt.subplots(2, 1, figsize=(8, 7), sharex=True)
    band = (f > 25) & (f < 350)
    for label, kind, Tc, col in shown:
        C = design(Hsm, Ssm, f, fx, Tc, False, kind)
        sums, effs = [], []
        for p in range(len(d['H'])):
            M = np.exp(-1j * 2 * np.pi * f * Tc / 1000.0) * hp * C * d['H'][p]
            S = lp * d['S'][p]
            sums.append(M + S)
            effs.append(20 * np.log10(np.maximum(np.abs(M + S), 1e-12)
                                      / np.maximum(np.abs(M) + np.abs(S), 1e-12)))
        tot = np.mean(np.abs(np.array(sums)) ** 2, axis=0) ** 0.5
        tot = np.abs(smooth_var_octave(tot.astype(complex), sr, 1 / 3, 1 / 3))
        ax[0].semilogx(f[band], 20 * np.log10(tot[band]), color=col, lw=1.6, label=label)
        ax[1].semilogx(f[band], np.mean(np.array(effs), axis=0)[band],
                       color=col, lw=1.6, label=label)
    for a in ax:
        a.axvline(fx, color='k', lw=.5, alpha=.4)
        a.grid(True, which='both', alpha=.25)
        a.legend(fontsize=8)
    ax[0].set_ylabel('summed level, power average over positions (dB)')
    ax[0].set_title(f'Main + subwoofer summation, {len(d["H"])} positions, '
                    f'crossover {fx:.0f} Hz')
    ax[1].set_ylabel('summation efficiency (dB)')
    ax[1].set_xlabel('frequency (Hz)')
    ax[1].set_ylim(-20, 2)
    fig.tight_layout()
    fig.savefig(path, dpi=130)
    print(f'\nwrote {path}')


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--data', required=True, help='folder holding the captures')
    ap.add_argument('--prefix', default='Mirage_14juin_', help='file name prefix')
    ap.add_argument('--positions', default='1,2,3,4,5')
    ap.add_argument('--mains', default='L=1,R=2', help='name=file index pairs')
    ap.add_argument('--sub', type=int, default=3, help='subwoofer file index')
    ap.add_argument('--crossover', type=float, default=85.0)
    ap.add_argument('--figure', default=None, help='write the summation figure here')
    args = ap.parse_args()

    positions = [int(p) for p in args.positions.split(',')]
    mains = dict((kv.split('=')[0], int(kv.split('=')[1])) for kv in args.mains.split(','))
    fx = args.crossover

    print('loading and estimating transfer functions ...', flush=True)
    sr, f, sets = load_set(args.data, args.prefix, positions, mains, args.sub)
    print(f'sample rate {sr} Hz, {len(f)} bins, resolution {sr / W:.2f} Hz')

    for name, d in sets.items():
        print(f'\n################  main {name}  ################')
        Ssm = smooth_var_octave(d['S'].mean(axis=0), sr)
        tw = group_delay_estimate(Ssm, sr, fx, True)
        tu = group_delay_estimate(Ssm, sr, fx, False)
        tarr = (np.median(d['d_sub_own']) - np.median(d['d'])) / sr * 1000.0
        print(f'main arrival {np.median(d["d"]) / sr * 1000:.2f} ms, '
              f'sub arrival {np.median(d["d_sub_own"]) / sr * 1000:.2f} ms '
              f'(spread {np.ptp(d["d_sub_own"]) / sr * 1000:.1f} ms)')
        print(f'T: crossover-band {tw:.2f} ms (weighted) / {tu:.2f} ms (unweighted), '
              f'arrival difference {tarr:.2f} ms')
        print_table(evaluate(d, f, sr, fx, loo=False), 'design on all positions')
        print_table(evaluate(d, f, sr, fx, loo=True), 'leave-one-position-out')

    print('\n--- estimator stability vs crossover setting (ms) ---')
    print(f'{"fx":>6s} ' + ' '.join(f'{n + " weighted":>14s}{n + " plain":>13s}' for n in sets))
    for fxi in (60, 85, 100, 120, 150):
        line = f'{fxi:6.0f} '
        for name, d in sets.items():
            Ssm = smooth_var_octave(d['S'].mean(axis=0), sr)
            line += (f'{group_delay_estimate(Ssm, sr, fxi, True):14.1f}'
                     f'{group_delay_estimate(Ssm, sr, fxi, False):13.1f}')
        print(line)

    if args.figure:
        make_figure(list(sets.values())[0], f, sr, fx, args.figure)


if __name__ == '__main__':
    main()
