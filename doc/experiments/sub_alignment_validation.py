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
import html
import io
import os
import re
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
PHASE_LIMITED = True             # AnalysisEngine::phaseLimited, on by default


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


def smooth_var_octave(H, sr, lo=None, hi=None, w=W):
    """Moving complex average over a +/- (fraction/2) octave band, the octave
    fraction log-interpolated between the two anchors.

    lo and hi are read from the module globals when omitted rather than taken
    as default arguments: the command line rebinds those globals, and a default
    would have been bound once, at def time."""
    lo = SMOOTH_LO if lo is None else lo
    hi = SMOOTH_HI if hi is None else hi
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


def position_average(H):
    """The average across microphone positions, as AnalysisEngine::computeAverage()
    builds it: magnitude from the power mean, phase from the complex mean, and
    the phase faded toward minimum phase where the positions disagree.

    |mean H| is not mean |H|. Above the room's transition frequency the
    positions no longer agree on phase, and a plain complex mean of ten
    near-random phasors reads about 1/sqrt(10) low -- 0.2 dB at 200 Hz but
    12 dB at 13 kHz on the campaign-6 set, at bins where every individual
    curve is flat. The power mean is the spatial average of the field.

    The complex mean still supplies the phase, but only as far as it is worth
    anything. Where the positions disagree it nearly cancels, and its ARGUMENT
    is then the direction of a residual between near-random phasors: it can
    turn 180 degrees between neighbouring bins while the power-mean magnitude
    walks smoothly through, so nothing in the magnitude shows it up. A
    linear-phase render turns that step into a near-zero on the unit circle --
    Q 518 and -25 dB at 697 Hz on the September 2026 campaign's right main,
    which rings audibly on an F; minimum phase, using the magnitude alone,
    never sees it. So the measured direction is blended on the short arc
    toward the minimum-phase equivalent of the same magnitude, weighted by the
    agreement between positions, debiased: N unit phasors agreeing on nothing
    still sum to 1/sqrt(N), and w = 0 means no better than chance. No phase is
    unwrapped anywhere, so nothing can jump."""
    N = len(H)
    mag = np.sqrt((np.abs(H) ** 2).mean(axis=0))
    c = H.mean(axis=0)
    a = np.abs(c)

    if N < 2:
        return np.where(a > 1e-30, c / np.maximum(a, 1e-300) * mag, mag)

    coh = a / np.maximum(mag, 1e-300)                     # |mean H| / sqrt(mean |H|^2)
    w = np.clip((N * coh ** 2 - 1.0) / (N - 1.0), 0.0, 1.0)

    measured = np.where(a > 1e-30, c / np.maximum(a, 1e-300), 1.0)
    mp = min_phase(mag.astype(complex))
    mp = mp / np.maximum(np.abs(mp), 1e-300)

    blend = w * measured + (1.0 - w) * mp
    length = np.abs(blend)
    # Opposed directions at w = 1/2 leave nothing to normalise; the
    # minimum-phase one is the safe half of that pair.
    return np.where(length > 1e-6, blend / np.maximum(length, 1e-300) * mag, mp * mag)


def smooth_average(H, sr, lo=None, hi=None, w=W):
    """Smooths an average's magnitude and phase separately, as
    AnalysisEngine::applySmoothing() does. A complex moving average would put
    back the cancellation position_average() avoids: at 13 kHz a 1/3-octave
    window spans 3 kHz, over which the residual phase turns several times."""
    m = np.real(smooth_var_octave(np.abs(H).astype(complex), sr, lo, hi, w))
    p = smooth_var_octave(H, sr, lo, hi, w)
    a = np.abs(p)
    return np.where(a > 1e-30, p / np.maximum(a, 1e-300) * m, m)


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


def load_mic_cal(data):
    """The microphone calibration the plugin embedded in `measurement.xml`, as
    the linear gain to divide the captures by, on the analysis bin grid. The
    file carries magnitude only, so the correction is magnitude only, which is
    what fxme::MicCalibration::correctionAt does with it. Returns None when the
    folder has no calibration."""
    path = os.path.join(data, 'measurement.xml')
    if not os.path.exists(path):
        return None
    xml = io.open(path, encoding='utf-8', errors='replace').read()
    m = re.search(r'<MicCalibration[^>]*>(.*?)</MicCalibration>', xml, re.S)
    if m is None:
        return None
    pts = re.findall(r'^\s*([\d.]+)\s+(-?[\d.]+)\s*$', html.unescape(m.group(1)), re.M)
    if len(pts) < 2:
        return None
    return (np.array([float(a) for a, _ in pts]),
            np.array([float(b) for _, b in pts]))


def mic_cal_gain(cal, f):
    """The calibration sampled on `f`, held flat outside its own range."""
    if cal is None:
        return np.ones_like(f)
    fc, dc = cal
    return 10.0 ** (np.interp(f, fc, dc, left=dc[0], right=dc[-1]) / 20.0)


def load_manifest(data, positions=None, exclude=()):
    """Read a measurement folder the plugin wrote. Returns the main channel
    numbers, the subwoofer channel (0 = none) and the position list.

    `exclude` names runs by the comment they carry, and drops the positions
    they wrote. A folder can hold a position measured twice -- the same place
    with something in the room moved -- and the group analysis is then told to
    leave one of them out; designing here from a set the plugin did not use
    compares two different designs."""
    import re
    xml = io.open(os.path.join(data, 'measurement.xml'), encoding='utf-8').read()
    sub = int(re.search(r'subChannel="(\d+)"', xml).group(1))
    npos = int(re.search(r'positions="(\d+)"', xml).group(1))
    chans = [int(c) for c in re.findall(r'<Channel number="(\d+)"', xml)]
    mains = [c for c in chans if c != sub]
    pos = positions or list(range(1, npos + 1))

    if exclude:
        drop = set()
        for m in re.finditer(r'<Run [^>]*comment="([^"]*)"[^>]*>(.*?)</Run>', xml, re.S):
            if m.group(1) in exclude:
                for name in re.findall(r'name="([^"]+)"', m.group(2)):
                    k = re.search(r'_pos(\d+)\.wav$', name)
                    if k:
                        drop.add(int(k.group(1)))
        pos = [p for p in pos if p not in drop]

    return mains, sub, pos


def load_folder(data, mains, sub, positions, mic_cal=True):
    """Per-position transfer functions from a plugin measurement folder, mains
    with their own delay removed and subwoofers anchored on the main of the
    same position. File naming is the plugin's: chNN_posPPP.wav, sub_posPPP.wav.
    The microphone calibration embedded in the folder is divided out, as the
    plugin's own analysis does, unless mic_cal is False."""
    out, sr = {}, None
    cal = load_mic_cal(data) if mic_cal else None
    gain = None
    for ch in mains:
        H_, S_, dm, ds = [], [], [], []
        for p in positions:
            sr, m = wavfile.read(os.path.join(data, f'ch{ch:02d}_pos{p:03d}.wav'))
            _, s_ = wavfile.read(os.path.join(data, f'sub_pos{p:03d}.wav'))
            Hm = welch_h1(m[:, 0].astype(float), m[:, 1].astype(float))
            Hs = welch_h1(s_[:, 0].astype(float), s_[:, 1].astype(float))
            if gain is None:
                gain = mic_cal_gain(cal, np.arange(W // 2 + 1) * sr / W)
            Hm, Hs = Hm / gain, Hs / gain
            d = ir_peak_delay(Hm)
            dm.append(d)
            ds.append(ir_peak_delay(Hs))
            H_.append(remove_delay(Hm, d))
            S_.append(remove_delay(Hs, d))
        out[f'ch{ch:02d}'] = dict(H=np.array(H_), S=np.array(S_),
                                  d=np.array(dm), d_sub_own=np.array(ds))
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


def band_weight(f, lo=None, hi=None):
    """The raised-cosine skirt that fades the correction back to unity outside
    the analysis band (AnalysisEngine::bandWeight). Globals, not defaults, for
    the reason given in smooth_var_octave."""
    lo = ANALYSIS_LO if lo is None else lo
    hi = ANALYSIS_HI if hi is None else hi
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


def phase_limit_weight(f, fx):
    """AnalysisEngine::phaseLimitWeight: the share of the designed phase the
    correction keeps -- all of it up to twice the crossover, where the
    alignment window ends, released over the octave above to none."""
    x = np.clip(np.log2(np.maximum(f, 1e-6) / (2.0 * fx)), 0, 1)
    return 0.5 + 0.5 * np.cos(np.pi * x)


def limit_phase(c, f, fx):
    """The finished correction with its phase handed over, above the crossover
    region, to the minimum-phase phase of its own magnitude, blended on the
    short arc, as AnalysisEngine::recomputeCorrection() does when the phase is
    limited. From four times the crossover up it is then the filter the
    minimum-phase render exports; the magnitude does not move.

    It has to be the finished correction, not the average: the average's
    minimum phase also carries what the correction never inverts (the band
    edges of the measurement, the roll-off below the analysis range), and
    inverting their phase without their magnitude is a pre-echo of its own.

    Above that region a linear-phase inversion of the average room phase buys
    no timing anyone can hear and puts each seat's unshared part of it ahead of
    the direct sound: on the September 2026 campaign 10 to 25 dB more energy
    there than minimum phase, from 125 Hz to 8 kHz, heard as a short
    pre-reverberation on impacts. Limited, the summation through the crossover
    moves by under 0.01 dB."""
    a = np.abs(c)
    mp = min_phase(a.astype(complex))
    mp = mp / np.maximum(np.abs(mp), 1e-300)
    kept = np.where(a > 1e-30, c / np.maximum(a, 1e-300), mp)
    w = phase_limit_weight(f, fx)
    blend = w * kept + (1.0 - w) * mp
    length = np.abs(blend)
    return np.where(length > 1e-6, blend / np.maximum(length, 1e-300) * a, mp * a)


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
    """kind: none | mag | mixed | mixed_unwrapped

    kind: none | mag | min_export | mixed | mixed_unwrapped

    `mag` drops the alignment and renders the magnitude minimum phase, which is
    also what the plugin's Phase type switch produces, since the all-pass is
    unit magnitude. `min_export` keeps the all-pass in the complex correction
    before taking the magnitude, as the plugin literally does; the two differ
    only inside the band-edge skirt, where the fade blends towards unity and
    (1-w) + w c A is therefore not (1-w) + w c in magnitude, and they score the
    same here to 0.01 dB.

    The fade must come before the cepstrum, as it does in AnalysisEngine: the
    minimum-phase render works on the magnitude of the finished correction, and
    rendering before the fade instead moves the score of the magnitude-only
    rows by about 0.7 dB.
    """
    if kind == 'none':
        return np.ones_like(Hsm)
    c = base_correction(Hsm, f)
    if kind != 'mag':
        c = c * allpass_term(Ssm, f, fx, T_ms, invert,
                             'unwrapped' if kind == 'mixed_unwrapped' else 'phasor')
    w = band_weight(f)
    c = (1 - w) + w * c
    if kind in ('mag', 'min_export'):
        return min_phase(c)
    return limit_phase(c, f, fx) if PHASE_LIMITED else c


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


def render_min_ir(C, f, sr, n=16384):
    """Minimum-phase render at n taps, as AnalysisEngine::renderIR does it with
    minimumPhase = true.

    The cepstrum runs on the FIR grid rather than at full resolution, which is
    the whole point when the question is what a short filter can hold: n taps
    span n/sr seconds, and the minimum-phase reconstruction of a magnitude the
    grid cannot resolve is not the reconstruction of the one it was asked for.
    The taper is one-sided for the same reason the plugin's is -- the impulse
    is front-loaded, so fading its head would remove the filter."""
    fg = np.arange(n // 2 + 1) * sr / n
    mag = np.abs(np.interp(fg, f, C.real) + 1j * np.interp(fg, f, C.imag))

    lm = np.log(np.maximum(mag, 1e-6))              # the plugin's -120 dB floor
    cep = np.real(np.fft.ifft(np.concatenate([lm, lm[-2:0:-1]])))
    lift = np.zeros(n)
    lift[0] = lift[n // 2] = 1.0
    lift[1:n // 2] = 2.0
    ir = np.real(np.fft.ifft(np.exp(np.fft.fft(cep * lift))))

    taper = max(1, int(0.1 * n))
    win = np.ones(n)
    win[-taper:] = 0.5 * (1 + np.cos(np.pi * np.arange(taper) / taper))
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


def evaluate(d, f, sr, fx, loo, fx_bm=None, sub_gain_db=0.0):
    """Score every condition. With loo=True the correction for position p is
    designed from the other positions only."""
    npos = len(d['H'])
    rows = {}
    for label, kind, tmode, inv in CONDITIONS:
        per, spans = [], []
        for p in range(npos):
            tr = [q for q in range(npos) if q != p] if loo else list(range(npos))
            Hsm = smooth_average(position_average(d['H'][tr]), sr)
            Ssm = smooth_average(position_average(d['S'][tr]), sr)
            if tmode == 'zero':
                T = 0.0
            elif tmode == 'arr':
                T = (np.median(d['d_sub_own'][tr]) - np.median(d['d'][tr])) / sr * 1000.0
            else:
                T = group_delay_estimate(Ssm, sr, fx, True)
            C = design(Hsm, Ssm, f, fx, T, inv, kind)
            fb = fx_bm or fx
            M = np.exp(-1j * 2 * np.pi * f * T / 1000.0) * butterworth(np.maximum(f, 1e-6), fb, 'hp') * C * d['H'][p]
            S = ((-1.0 if inv else 1.0) * 10 ** (sub_gain_db / 20.0)
                 * butterworth(np.maximum(f, 1e-6), fb, 'lp') * d['S'][p])
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
    Hsm = smooth_average(position_average(d['H']), sr)
    Ssm = smooth_average(position_average(d['S']), sr)
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
    ap.add_argument('--manifest', action='store_true',
                    help='read measurement.xml and the plugin file naming instead of --prefix')
    ap.add_argument('--analysis-low', type=float, default=ANALYSIS_LO)
    ap.add_argument('--smooth-lo', type=float, default=SMOOTH_LO)
    ap.add_argument('--smooth-hi', type=float, default=SMOOTH_HI)
    ap.add_argument('--max-boost', type=float, default=MAX_BOOST_DB)
    ap.add_argument('--sub-gain', type=float, default=0.0,
                    help="subwoofer routing level relative to the mains, in dB, "
                         "as set in the matrix (the dry captures carry neither)")
    ap.add_argument('--exclude', default='',
                    help='run comments to leave out of the design set, comma '
                         'separated: a position measured twice, once with the room '
                         'changed, is in the folder but not in the analysis (the '
                         'group report lists what it was told to leave out)')
    ap.add_argument('--no-mic-cal', action='store_true',
                    help='ignore the microphone calibration embedded in the folder '
                         '(the plugin always applies it)')
    ap.add_argument('--bm-crossover', type=float, default=None,
                    help='bass-management crossover actually in the matrix, if it '
                         'differs from the crossover the analysis was run at')
    args = ap.parse_args()

    globals()['ANALYSIS_LO'] = args.analysis_low
    globals()['SMOOTH_LO'] = args.smooth_lo
    globals()['SMOOTH_HI'] = args.smooth_hi
    globals()['MAX_BOOST_DB'] = args.max_boost

    positions = [int(p) for p in args.positions.split(',')]
    mains = dict((kv.split('=')[0], int(kv.split('=')[1])) for kv in args.mains.split(','))
    fx = args.crossover

    print('loading and estimating transfer functions ...', flush=True)
    if args.manifest:
        drop = [c.strip() for c in args.exclude.split(',') if c.strip()]
        mn, sb, positions = load_manifest(args.data,
                                          None if args.positions == '1,2,3,4,5' else positions,
                                          exclude=drop)
        print(f'manifest: mains {mn}, sub channel {sb}, {len(positions)} positions'
              + (f' (left out: {", ".join(drop)})' if drop else ''))
        cal = None if args.no_mic_cal else load_mic_cal(args.data)
        print('mic calibration: ' + ('none' if cal is None else
                                     f'{len(cal[0])} points, {cal[0][0]:g}-{cal[0][-1]:g} Hz'))
        sr, f, sets = load_folder(args.data, mn, sb, positions,
                                  mic_cal=not args.no_mic_cal)
    else:
        sr, f, sets = load_set(args.data, args.prefix, positions, mains, args.sub)
    print(f'settings: crossover {fx:g} Hz, analysis from {ANALYSIS_LO:g} Hz, '
          f'smoothing {SMOOTH_LO:.3f}/{SMOOTH_HI:.3f} oct, max boost {MAX_BOOST_DB:g} dB, '
          f'sub routing {args.sub_gain:+g} dB')
    print(f'sample rate {sr} Hz, {len(f)} bins, resolution {sr / W:.2f} Hz')

    for name, d in sets.items():
        print(f'\n################  main {name}  ################')
        Ssm = smooth_average(position_average(d['S']), sr)
        tw = group_delay_estimate(Ssm, sr, fx, True)
        tu = group_delay_estimate(Ssm, sr, fx, False)
        tarr = (np.median(d['d_sub_own']) - np.median(d['d'])) / sr * 1000.0
        print(f'main arrival {np.median(d["d"]) / sr * 1000:.2f} ms, '
              f'sub arrival {np.median(d["d_sub_own"]) / sr * 1000:.2f} ms '
              f'(spread {np.ptp(d["d_sub_own"]) / sr * 1000:.1f} ms)')
        print(f'T: crossover-band {tw:.2f} ms (weighted) / {tu:.2f} ms (unweighted), '
              f'arrival difference {tarr:.2f} ms')
        print_table(evaluate(d, f, sr, fx, loo=False, fx_bm=args.bm_crossover,
                             sub_gain_db=args.sub_gain), 'design on all positions')
        print_table(evaluate(d, f, sr, fx, loo=True, fx_bm=args.bm_crossover,
                             sub_gain_db=args.sub_gain), 'leave-one-position-out')

    print('\n--- estimator stability vs crossover setting (ms) ---')
    print(f'{"fx":>6s} ' + ' '.join(f'{n + " weighted":>14s}{n + " plain":>13s}' for n in sets))
    for fxi in (60, 85, 100, 120, 150):
        line = f'{fxi:6.0f} '
        for name, d in sets.items():
            Ssm = smooth_average(position_average(d['S']), sr)
            line += (f'{group_delay_estimate(Ssm, sr, fxi, True):14.1f}'
                     f'{group_delay_estimate(Ssm, sr, fxi, False):13.1f}')
        print(line)

    if args.figure:
        make_figure(list(sets.values())[0], f, sr, fx, args.figure)


if __name__ == '__main__':
    main()
