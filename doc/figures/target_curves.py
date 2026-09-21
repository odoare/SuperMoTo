#!/usr/bin/env python3
"""The factory target curves, for the manual's target-curve chapter.

    python3 target_curves.py [--out target-curves.png]

The curves are not hard-coded here: the factory set is read out of
``Source/Model/TargetCurveStore.h``, so the figure cannot disagree with what
the plugin ships. The curve arithmetic mirrors ``Source/Dsp/TargetCurve.h``
exactly -- a cascade of first-order pole/zero sections, six for the tilt plus
one for the shelf, normalised so the mean linear gain over 200 Hz - 2 kHz is
0 dB -- and the checks at the bottom of build() fail loudly if that
transcription ever drifts from the header.

What is plotted is the curve as defined, which is also what the Target view
plots. The filter the outputs apply is this minus a constant (enough to bring
the highest point to 0 dB, so the layer only ever attenuates), and follows it
to about 0.2 dB at the top of the band; TargetCurve.h has the measurements.

The dotted line is EBU Tech 3276's lower tolerance limit for the operational
room response curve: flat at -3 dB to 2 kHz, then falling 1 dB per octave.
Every factory curve stays inside it except the steepest, "Flat to 1 kHz",
which crosses it by 0.4 dB in the last octave -- the mask is drawn here for
scale rather than as a rule, since it is a tolerance on a measured room
response and not on the target one aims at.

Needs numpy and matplotlib.

Author: Olivier Doaré, github.com/odoare
Licenced under the GNU AGPL Version 3.0, or commercial terms (LICENSE.md)
SPDX-License-Identifier: AGPL-3.0-or-later OR LicenseRef-FXME-Commercial
"""

import argparse
import os
import re

import numpy as np
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt

# The constants of TargetCurve.h.
LO, HI = 20.0, 20000.0
REF_LO, REF_HI = 200.0, 2000.0
TILT_SECTIONS = 6

HEADER = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                      '..', '..', 'Source', 'Model', 'TargetCurveStore.h')

# Name -> line style, so the figure reads in the order the chapter does.
STYLE = {
    'Flat':                 dict(color='0.45', ls='--', lw=1.4),
    'Gentle -3 dB':         dict(color='tab:blue', lw=1.8),
    'Moderate -4 dB':       dict(color='tab:green', lw=2.6),
    'Strong -6 dB':         dict(color='tab:orange', lw=1.8),
    'Harman-like':          dict(color='tab:red', lw=1.8),
    'Flat to 1 kHz, -6 dB': dict(color='tab:purple', lw=1.8),
}


def factory_curves(path=HEADER):
    """The factory set as (name, tilt, turnover, bassDb, bassHz), read from the
    C++ so that the two cannot drift apart."""
    text = open(path, encoding='utf-8').read()
    body = text[text.index('factorySet()'):]
    start = body.index('return {')
    body = body[start:body.index('};', start)]

    rows = re.findall(
        r'\{\s*"([^"]+)"\s*,\s*TargetCurve\s*\{([^}]*)\}\s*\}', body)
    out = []
    for name, values in rows:
        v = [float(x.strip().rstrip('f')) for x in values.split(',')]
        if len(v) != 4:
            raise SystemExit(f'{name}: expected four values, got {v}')
        out.append((name, *v))

    if not out:
        raise SystemExit(f'no factory curves found in {path}')
    return out


def sections_of(tilt_db, turnover_hz, bass_db, bass_hz):
    """targetcurve::sectionsOf(): the (pole, zero) pairs, in Hz."""
    secs = []

    if tilt_db != 0.0:
        lo = min(max(turnover_hz, LO), HI / 2) if turnover_hz > 0.0 else LO
        flo, fhi = lo * 0.25, HI * 4.0
        total = -tilt_db * np.log2(fhi / flo) / np.log2(HI / lo)
        r = 10.0 ** (total / (20.0 * TILT_SECTIONS))
        for k in range(TILT_SECTIONS):
            p = flo * (fhi / flo) ** ((k + 0.5) / TILT_SECTIONS) / np.sqrt(r)
            secs.append((p, p * r))

    if bass_db != 0.0:
        f0 = min(max(bass_hz, 10.0), 1000.0)
        secs.append((f0, f0 * 10.0 ** (bass_db / 20.0)))

    return secs


def raw_db(secs, f):
    """targetcurve::analogDb(): the cascade before normalisation."""
    f = np.clip(np.asarray(f, float), 0.01, 1e6)
    db = np.zeros_like(f)
    for p, z in secs:
        db += 10.0 * np.log10((1.0 + (f / z) ** 2) / (1.0 + (f / p) ** 2))
    return db


def curve_db(tilt_db, turnover_hz, bass_db, bass_hz, f):
    """TargetCurveShape::gainDb(): normalised to 0 dB over 200 Hz - 2 kHz."""
    secs = sections_of(tilt_db, turnover_hz, bass_db, bass_hz)
    grid = REF_LO * (REF_HI / REF_LO) ** (np.arange(33) / 32.0)
    ref = 20.0 * np.log10(np.mean(10.0 ** (raw_db(secs, grid) / 20.0)))
    return raw_db(secs, f) - ref


def ebu_lower_limit(f):
    """EBU Tech 3276 Fig. 2: -3 dB to 2 kHz, then -1 dB per octave."""
    return np.where(f <= 2000.0, -3.0, -3.0 - np.log2(f / 2000.0))


def build(out_path):
    f = np.geomspace(LO, HI, 1000)
    curves = factory_curves()

    # The transcription has to keep agreeing with TargetCurve.h: a plain tilt
    # is a straight line through the band and delivers the fall it names.
    for tilt in (-3.0, -6.0, -10.0):
        db = curve_db(tilt, 0.0, 0.0, 105.0, f)
        fit = np.polyfit(np.log2(f), db, 1)
        worst = np.abs(db - np.polyval(fit, np.log2(f))).max()
        delivered = np.interp(HI, f, db) - np.interp(LO, f, db)
        # The departure from a straight line is largest at the band edges and
        # scales with the tilt, so the fall measured end to end is short by
        # about 1 % of it: 0.05 dB at -6, 0.09 dB at -10.
        assert worst < 0.05, f'{tilt} dB tilt bends by {worst:.3f} dB'
        assert abs(delivered - tilt) < 0.01 * abs(tilt) + 0.02, \
            f'{tilt} dB tilt delivers {delivered:.2f}'

    plt.rcParams.update({'font.size': 11, 'axes.labelsize': 11,
                         'xtick.labelsize': 10, 'ytick.labelsize': 10,
                         'legend.fontsize': 9.5})
    fig, ax = plt.subplots(figsize=(8.2, 3.9))

    ax.semilogx(f, ebu_lower_limit(f), color='0.6', ls=':', lw=1.3,
                label='EBU 3276 lower limit', zorder=1)

    for name, tilt, turnover, bass_db, bass_hz in curves:
        style = STYLE.get(name, dict(lw=1.6))
        label = name + (' (default)' if name.startswith('Moderate') else '')
        ax.semilogx(f, curve_db(tilt, turnover, bass_db, bass_hz, f),
                    label=label, zorder=2, **style)

    ax.axhline(0.0, color='0.8', lw=0.8, zorder=0)
    ax.axvspan(REF_LO, REF_HI, color='0.93', zorder=0)
    ax.text(np.sqrt(REF_LO * REF_HI), -8.6, '0 dB reference band',
            ha='center', va='bottom', fontsize=8.5, color='0.45')

    ax.set_xlim(LO, HI)
    ax.set_ylim(-9.0, 9.0)
    ax.set_xlabel('frequency (Hz)')
    ax.set_ylabel('level (dB re 200 Hz - 2 kHz)')
    ax.set_xticks([20, 50, 100, 200, 500, 1000, 2000, 5000, 10000, 20000])
    ax.set_xticklabels(['20', '50', '100', '200', '500', '1k', '2k', '5k', '10k', '20k'])
    ax.xaxis.set_minor_formatter(matplotlib.ticker.NullFormatter())
    ax.grid(True, which='both', alpha=0.25)
    ax.legend(loc='upper right', ncol=2, framealpha=0.92)

    fig.tight_layout()
    fig.savefig(out_path, dpi=200)
    print('wrote', out_path, f'({len(curves)} factory curves)')


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    ap = argparse.ArgumentParser()
    ap.add_argument('--out', default=os.path.join(here, 'target-curves.png'))
    build(ap.parse_args().out)


if __name__ == '__main__':
    main()
