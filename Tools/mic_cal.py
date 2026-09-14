#!/usr/bin/env python3
"""Build a miniDSP/REW microphone calibration file from a two-channel recording.

Input is a wav file laid out like the ones in this folder: one channel carries a
calibrated reference microphone, the other the microphone under test, both fed the
same stationary excitation (white or pink noise) at the same time.  The transfer
function between the two is the response of the microphone under test relative to
the reference, and that is what a calibration file has to contain, because REW and
the miniDSP tools *subtract* the file from every measurement.

Usage
-----
    python3 mic_cal.py 0deg.wav -o M4260_0deg_cal.txt --serno M4260

    # average takes of the same position, which is the only way to beat down the
    # positioning and mutual-diffraction scatter of a two-microphone rig.  Each
    # file must be two-channel, so the per-channel mono takes Reaper leaves in
    # Media/ have to be paired into one stereo file per take first.
    python3 mic_cal.py take1.wav take2.wav take3.wav -o M4260_0deg_cal.txt

    # take the reference microphone's own response out, so the result is the
    # response of the microphone under test and not the ratio of the two.  The
    # chart may be a REW/miniDSP cal file, the csv measpy's Weighting.to_csv
    # writes, or any two-column frequency and dB text file.  Add
    # --ref-cal-invert if the file is already a correction to apply rather than
    # the reference microphone's own deviation.
    python3 mic_cal.py 0deg.wav --ref-cal BK4190_chart.csv --ref-sens 50

Resolution
----------
Two microphones cannot occupy the same point, so above a few kHz the measured
transfer function carries mutual diffraction and comb filtering that depend on the
exact placement.  On the recordings in this folder that scatter reaches 2 dB rms
between 4 and 9 kHz for two takes at the same nominal angle, while octave-band
values repeat to 0.3 dB.  The default is 1/3 octave; use --smooth 1 for a curve you
can defend from a single position, and average takes whenever you can.
"""

import argparse
import sys

import numpy as np
import measpy as mp
from measpy import Weighting


# ----------------------------------------------------------------- helpers

def welch_nperseg(fs, nperseg):
    """measpy's own tfe_welch default, made explicit.

    Signal.coh does not apply that default, so passing the same value to both
    keeps the coherence on the transfer function's frequency grid.
    """
    return int(2 ** np.ceil(np.log2(fs))) if nperseg is None else nperseg


def read_ref_cal(path):
    """Reference-microphone chart, as a measpy Weighting.

    Weighting.from_csv reads the three-column form measpy itself writes, with a
    description line on top.  Charts from other sources are often whitespace
    separated, so fall back to a reader that skips any line whose first field is
    not a number.
    """
    try:
        w = Weighting.from_csv(path)
        if len(w.freqs) >= 2:
            return w
    except Exception:
        pass
    rows = []
    with open(path, encoding="utf-8") as fid:
        for line in fid:
            fields = line.replace(",", " ").replace(";", " ").split()
            try:
                rows.append((float(fields[0]), float(fields[1]),
                             float(fields[2]) if len(fields) > 2 else 0.0))
            except (ValueError, IndexError):
                continue
    if len(rows) < 2:
        raise SystemExit("%s: fewer than two data rows" % path)
    f, adb, phase = np.array(sorted(rows)).T
    return Weighting(freqs=f, amp=10 ** (adb / 20.0), phase=phase,
                     desc="reference chart " + path)


# ----------------------------------------------------------------- one take

def take(path, args, report):
    """One recording, as measpy objects.

    Returns the 1/nth octave smoothed transfer function of the microphone under
    test over the reference, normalised to unity at --norm-freq, the coherence
    smoothed over the same bands, and the Signal itself.
    """
    sig = mp.Signal.from_wav(path)
    need = max(args.ref_chan, args.dut_chan) + 1
    if sig.nchannels < need:
        raise SystemExit("%s: has %d channel(s), need at least %d"
                         % (path, sig.nchannels, need))

    nperseg = welch_nperseg(sig.fs, args.nperseg)
    full_dur = sig.dur
    if args.trim > 0.0:
        sig = sig.cut(dur=(args.trim, max(args.trim, full_dur - args.trim)))
    # Welch needs a couple of segments to average over, or scipy quietly shortens
    # the window and the low end loses resolution.
    if sig.length < 2 * nperseg:
        raise SystemExit(
            "%s: %.2f s minus %.2f s of trim at each end leaves %.2f s, and a "
            "%d-sample Welch window needs %.2f s. Use a longer recording, a "
            "smaller --trim, or a smaller --nperseg."
            % (path, full_dur, args.trim, sig.dur, nperseg, 2 * nperseg / sig.fs))

    ref, dut = sig[args.ref_chan], sig[args.dut_chan]

    # nth_oct_smooth_complex is a true moving average over a 1/nth octave band,
    # evaluated at every frequency of the spectrum, so the resolution survives and
    # nothing has to be re-gridded here.  freqs_range only stops the filterout it
    # applies, which defaults to 5 Hz - 20 kHz, from zeroing part of what we read
    # back below.
    rng = (args.fmin / 2, sig.fs / 2)
    tfe = dut.tfe_welch(ref, nperseg=nperseg)
    spec = tfe.nth_oct_smooth_complex(args.smooth, mode=args.mode,
                                      freqs_range=rng)
    coh = dut.coh(ref, nperseg=nperseg).nth_oct_smooth(args.smooth,
                                                       freqs_range=rng)

    gain = spec.values_at_freqs(args.norm_freq)
    report.append("  %-26s %6.1f s analysed of %.1f s  fs %5d  nperseg %6d"
                  % (path.rsplit("/", 1)[-1], sig.dur, full_dur, sig.fs, nperseg))
    return spec / gain, coh, sig, np.abs(gain)


# ----------------------------------------------------------------- output

def write_minidsp(path, cal, with_phase, sens_factor, serno):
    """miniDSP/REW calibration file: one quoted header line, then frequency and
    dB.  REW reads a third column, when present, as phase in degrees."""
    with open(path, "w", encoding="utf-8") as fid:
        fid.write('"Sens Factor =%.4fdB, SERNO: %s"\n' % (sens_factor, serno))
        for f, adb, ph in zip(cal.freqs, cal.adb, cal.phase):
            if with_phase:
                fid.write("%.3f\t%.4f\t%.3f\n" % (f, adb, np.degrees(ph)))
            else:
                fid.write("%.3f\t%.4f\n" % (f, adb))


# ----------------------------------------------------------------- main

def parse_args(argv):
    p = argparse.ArgumentParser(
        description="Two-channel recording to miniDSP/REW microphone "
                    "calibration file.",
        formatter_class=argparse.ArgumentDefaultsHelpFormatter)
    p.add_argument("wav", nargs="+",
                   help="recording(s) of one microphone position; several files "
                        "are averaged after individual normalisation")
    p.add_argument("-o", "--out", default=None,
                   help="output file [first input, suffix _cal.txt]")
    p.add_argument("--csv", default=None, metavar="FILE",
                   help="also write the curve as a measpy Weighting csv")
    p.add_argument("--ref-chan", type=int, default=0,
                   help="channel carrying the reference microphone")
    p.add_argument("--dut-chan", type=int, default=1,
                   help="channel carrying the microphone under test")
    p.add_argument("--smooth", type=int, default=3, metavar="N",
                   help="1/N octave moving average; 1 is the resolution that "
                        "repeats on a side-by-side two-microphone rig")
    p.add_argument("--mode", default="power",
                   choices=("power", "amplitude_phase", "complex"),
                   help="how measpy averages complex values inside a band; "
                        "power is the energetic mean, right for noise excitation")
    p.add_argument("--fmin", type=float, default=20.0,
                   help="lowest frequency in the output file")
    p.add_argument("--fmax", type=float, default=20000.0,
                   help="highest frequency in the output file")
    p.add_argument("--ppo", type=int, default=24, metavar="N",
                   help="points per octave written to the output file")
    p.add_argument("--norm-freq", type=float, default=1000.0,
                   help="frequency forced to 0 dB, normally the frequency the "
                        "microphone sensitivity is quoted at")
    p.add_argument("--trim", type=float, default=1.0, metavar="SEC",
                   help="seconds discarded at each end of every recording")
    p.add_argument("--nperseg", type=int, default=None,
                   help="Welch segment length [measpy default, about 1 s]")
    p.add_argument("--ref-cal", default=None, metavar="FILE",
                   help="frequency response chart of the reference microphone, "
                        "applied so the result is the response of the microphone "
                        "under test rather than the ratio of the two. Read as the "
                        "reference microphone's own deviation in dB, the sign a "
                        "calibration chart uses, and added to the measured ratio. "
                        "Accepts a REW/miniDSP cal file, the three-column csv "
                        "measpy's Weighting.to_csv writes, or any two-column "
                        "frequency and dB text file")
    p.add_argument("--ref-cal-invert", action="store_true",
                   help="the --ref-cal file is already a correction to apply "
                        "rather than the reference microphone's own deviation, so "
                        "subtract it instead of adding it")
    p.add_argument("--ref-sens", type=float, default=None, metavar="MV_PER_PA",
                   help="sensitivity of the reference microphone, used only to "
                        "report the resulting sensitivity of the microphone under "
                        "test; assumes both channels saw the same gain")
    p.add_argument("--invert", action="store_true",
                   help="write the inverse curve, for tools that add the file "
                        "instead of subtracting it")
    p.add_argument("--phase", action="store_true",
                   help="add a third column with phase in degrees")
    p.add_argument("--serno", default="unknown",
                   help="serial number written into the header")
    p.add_argument("--sens-factor", type=float, default=0.0,
                   help="sensitivity factor written into the header; REW uses it "
                        "for absolute SPL with USB microphones only")
    p.add_argument("--plot", action="store_true", help="show the result")
    args = p.parse_args(argv)
    if args.smooth < 1:
        raise SystemExit("--smooth must be 1 or more")
    if args.ppo < 1:
        raise SystemExit("--ppo must be 1 or more")
    if args.fmin <= 0 or args.fmax <= args.fmin:
        raise SystemExit("need 0 < --fmin < --fmax")
    if not args.fmin <= args.norm_freq <= args.fmax:
        raise SystemExit("--norm-freq must lie between --fmin and --fmax")
    return args


def main(argv=None):
    args = parse_args(argv)

    report = ["Inputs:"]
    specs, cohs, sigs, gains = zip(*[take(p, args, report) for p in args.wav])
    rates = {s.fs for s in sigs}
    if len(rates) > 1:
        raise SystemExit("all recordings must share one sampling rate, got %s"
                         % ", ".join("%d" % r for r in sorted(rates)))
    fs = rates.pop()

    # Past this the moving-average band would reach beyond Nyquist and be averaged
    # over part of its width only.
    hi = min(args.fmax, fs / 2 / 2 ** (1.0 / (2 * args.smooth)))
    if hi <= args.fmin:
        raise SystemExit("1/%d octave smoothing at fs %d leaves nothing above "
                         "%.0f Hz" % (args.smooth, fs, args.fmin))
    if args.norm_freq > hi:
        raise SystemExit("--norm-freq %.0f Hz is above the highest frequency "
                         "1/%d octave smoothing can reach at fs %d, %.0f Hz"
                         % (args.norm_freq, args.smooth, fs, hi))

    ref_gap = None
    if args.ref_cal:
        w = read_ref_cal(args.ref_cal)
        # Weighting interpolates in log frequency and in dB, and clamps rather
        # than extrapolating outside its own range, so a chart narrower than the
        # output silently applies a constant correction past its ends.
        wlo, whi = float(min(w.freqs)), float(max(w.freqs))
        if wlo > args.fmin * 1.001 or whi < hi * 0.999:
            ref_gap = (wlo, whi)
        specs = [s.apply_weighting(w, inverse=args.ref_cal_invert)
                 for s in specs]
        specs = [s / s.values_at_freqs(args.norm_freq) for s in specs]
        report.append("Reference chart %s from %s (%.0f-%.0f Hz)"
                      % ("subtracted" if args.ref_cal_invert else "added",
                         args.ref_cal, wlo, whi))

    # Spectral arithmetic: magnitudes are averaged so that takes recorded at
    # slightly different positions cannot cancel each other, and the phase comes
    # from the complex mean.
    mag = sum((abs(s) for s in specs[1:]), abs(specs[0])) / len(specs)
    cpx = sum(specs[1:], specs[0]) / len(specs)

    npts = max(2, int(round(args.ppo * np.log2(hi / args.fmin))) + 1)
    grid = np.geomspace(args.fmin, hi, npts)
    amp = np.abs(mag.values_at_freqs(grid))
    phase = np.angle(cpx.values_at_freqs(grid))
    cal = Weighting(freqs=grid,
                    amp=1.0 / amp if args.invert else amp,
                    phase=-phase if args.invert else phase,
                    desc="%s relative to reference, 1/%d oct, 0 dB at %.0f Hz"
                         % (args.serno, args.smooth, args.norm_freq))

    out = args.out or (args.wav[0].rsplit(".", 1)[0] + "_cal.txt")
    write_minidsp(out, cal, args.phase, args.sens_factor, args.serno)
    if args.csv:
        cal.to_csv(args.csv)

    # ------------------------------------------------------------- console
    takes_db = np.array([20 * np.log10(np.abs(s.values_at_freqs(grid)))
                         for s in specs])
    spread = np.ptp(takes_db, axis=0)
    coh = np.min([np.abs(c.values_at_freqs(grid)) for c in cohs], axis=0)

    print("\n".join(report))
    print("\n1/%d octave %s average, %d points from %.0f to %.0f Hz, "
          "0 dB at %.0f Hz"
          % (args.smooth, args.mode, npts, grid[0], grid[-1], args.norm_freq))
    if hi < args.fmax * 0.999:
        print("Top frequency held to %.0f Hz so no band reaches past Nyquist "
              "(asked %.0f Hz)." % (hi, args.fmax))

    centres = 1000.0 * 2.0 ** np.arange(-6, 5, dtype=float)
    print("\n%9s %8s %10s %8s" % ("f [Hz]", "dB", "coherence", "spread"))
    for f in centres[(centres >= grid[0]) & (centres <= grid[-1])]:
        i = int(np.argmin(np.abs(grid - f)))
        print("%9.0f %8.2f %10.4f %8.2f" % (f, cal.adb[i], coh[i], spread[i]))

    if args.ref_sens:
        # H is a ratio of channel voltages, so the sensitivity only carries over
        # if both channels saw the same gain.
        sens = float(np.mean(gains)) * args.ref_sens
        print("\nSensitivity of the microphone under test at %.0f Hz, assuming "
              "both channels saw the same gain:\n  %.1f mV/Pa  (%.1f dBV/Pa), "
              "from %.1f mV/Pa on the reference"
              % (args.norm_freq, sens, 20 * np.log10(sens / 1000.0),
                 args.ref_sens))

    if ref_gap is not None:
        print("\nWarning: the reference chart covers %.0f-%.0f Hz but the output "
              "spans %.0f-%.0f Hz. Outside the chart its end value is held "
              "constant, so those edges carry no reference correction."
              % (ref_gap[0], ref_gap[1], grid[0], grid[-1]))

    if coh.min() < 0.98:
        print("\nWarning: coherence falls to %.3f at %.0f Hz. That band is noise "
              "or nonlinearity, not microphone response."
              % (coh.min(), grid[np.argmin(coh)]))
    if len(specs) == 1:
        print("\nOne take only, so nothing here bounds the positioning error. "
              "Record the same position twice and pass both files.")
    elif spread.max() > 1.0:
        print("\nWarning: takes disagree by up to %.1f dB at %.0f Hz. Raise "
              "--smooth or add takes." % (spread.max(), grid[np.argmax(spread)]))
    print("\nWrote %s%s" % (out, " and " + args.csv if args.csv else ""))

    if args.plot:
        import matplotlib.pyplot as plt
        _, ax = plt.subplots(figsize=(9, 4))
        for path, s in zip(args.wav, specs):
            s.plot(ax=ax, dby=True, plot_phase=False, lw=0.8, alpha=0.55,
                   label=path.rsplit("/", 1)[-1])
        ax.semilogx(cal.freqs, cal.adb, "k", lw=2, label="calibration file")
        ax.set(xlim=(grid[0], grid[-1]),
               ylabel="dB re %.0f Hz" % args.norm_freq)
        ax.grid(True, which="both", alpha=0.3)
        ax.legend(fontsize=8)
        plt.tight_layout()
        plt.show()
    return 0


if __name__ == "__main__":
    sys.exit(main())
