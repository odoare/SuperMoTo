# Subwoofer alignment validation

Offline validation of the all-pass subwoofer phase alignment described in
`doc/chapters/app-analysis-theory.tex` (Section "Subwoofer phase alignment")
and in `doc/paper.tex`.

`sub_alignment_validation.py` re-implements the plugin's analysis chain in
numpy and uses it to predict the acoustic main-plus-subwoofer summation at
every microphone position, for several alignment strategies, from measurement
files the plugin itself wrote. It needs no plugin build and no new
measurements.

## Why the prediction is exact

Every subwoofer capture is delay-anchored on the main capture of the same
position, so the same propagation delay has been removed from both. The
acoustic sum at position *p* is therefore, up to that common factor,

    Y_p(f) = e^{-j 2 pi f T} HP(f) C(f) H_p(f)  ±  LP(f) S_p(f)

with *T* the main's delay relative to the subwoofer, HP and LP the
bass-management filters the configuration tool writes (4th-order Butterworth),
and C the designed correction. Nothing is simulated: H_p and S_p are the
measured responses, and the only modelled parts are the filters the plugin
would itself apply.

## Running it

On a measurement folder the plugin wrote, `--manifest` reads `measurement.xml`
for the channels, the subwoofer and the positions:

    python3 sub_alignment_validation.py --manifest \
        --data ~/Documents/supermoto/MeasurementCampaign/Mirage_panneaux_paper \
        --crossover 85 --sub-gain -4.8 --analysis-low 85 \
        --smooth-lo 0.1667 --smooth-hi 0.3333 --max-boost 9

The design settings should match the group analysis that produced the
corrections (they are printed at the top of its report). `--sub-gain` is the
subwoofer's routing level relative to the mains as set in the matrix, which the
dry captures do not carry, and `--bm-crossover` the bass-management crossover
if it differs from the one the analysis was run at.

Folders with ad-hoc file names are read instead with an explicit pattern:

    python3 sub_alignment_validation.py --data ~/supermoto_measurements \
        --prefix Mirage_14juin_ --mains L=1,R=2 --sub 3 --crossover 85 \
        --figure summation.png

The files are the plugin's own stereo captures (channel 1 the sent stimulus,
channel 2 the microphone). Needs numpy, scipy and, for `--figure`, matplotlib.

`make_paper_figure.py` builds the Validation figure of `doc/paper.tex` from the
September 2026 campaign.

## What it reports

Per main loudspeaker, for every strategy:

| column   | meaning |
|----------|---------|
| eff dB   | `20log10|M+S| - 20log10(|M|+|S|)`, weighted by the overlap of the two sources over [fx/2, 2fx]. 0 dB is perfectly in phase, negative is cancellation. |
| worst    | the same, at the worst of the microphone positions. |
| \|dphi\| | overlap-weighted RMS phase difference between main and subwoofer over the band. |
| ripple   | standard deviation of the summed level over the band. |
| notch    | worst single-frequency cancellation in the band. |
| max rot  | largest rotation the alignment factor A applies over 20-400 Hz. The phasor blend is bounded to 180 deg by construction; the unwrapped-angle ablation is not. |

Each table is printed twice. "design on all positions" designs the correction
from all the positions it is then scored on. "leave-one-position-out" designs
it from the other positions only, so the score is on data the filter never
saw.

The run also prints the crossover-band delay estimator (power-weighted and
plain) against the crossover setting, which is the stability claim of
`doc/chapters/app-delays.tex`.

![summation](summation.png)

Top: the summed level, power-averaged over the five positions. Bottom: the
summation efficiency, where 0 dB is coherent addition. The grey curve is the
uncorrected system, with a 9.5 dB cancellation at 80 Hz; the orange one is a
magnitude-only correction, which moves the cancellation to 115 Hz and deepens
it to 16 dB; the blue one is the all-pass alignment, flat across the region.

## Result on the September 2026 campaign (10 positions, main L)

Leave-one-position-out, so none of these numbers are fitted to what they are
scored on. This is the set the paper reports:

| strategy                        | eff dB | worst | \|dphi\| | notch  |
|---------------------------------|--------|-------|----------|--------|
| no correction, no alignment     | -1.27  | -1.97 | 79 deg   | -5.5   |
| magnitude only (min-phase)      | -2.53  | -3.55 | 92 deg   | -13.5  |
| mag + arrival-time delay        | -0.85  | -1.40 | 57 deg   | -2.1   |
| mag + crossover-band delay      | -1.55  | -2.68 | 72 deg   | -3.5   |
| **all-pass alignment**          | **-0.37** | **-0.98** | **36 deg** | **-1.4** |
| all-pass, subwoofer inverted    | -0.32  | -0.88 | 37 deg   | -1.5   |
| all-pass, no bulk delay         | -0.43  | -1.07 | 41 deg   | -1.6   |
| ablation: unwrapped-angle blend | -0.37  | -0.98 | 36 deg   | -1.4   |
| ablation: unwrapped, no delay   | -0.48  | -1.14 | 47 deg   | -1.9   |

Main R gives the same ordering within 0.2 dB. The earlier five-position set
(`~/supermoto_measurements`, `--prefix Mirage_14juin_`) gives the same ranking
with the all-pass at -0.20 dB, except that its subwoofer arrival time is poorly
defined (6.6 ms of position-to-position spread against 0.2 ms here), which is
why the crossover-band delay clearly beats the arrival-time delay there and the
two are within 0.7 dB here.

Points the table makes:

- Correcting the main's magnitude alone makes the summation *worse* than no
  correction at all (-3.75 against -2.58 dB), because a minimum-phase
  magnitude correction moves the main's phase through the crossover region
  without regard for the subwoofer.
- Most of what is recovered comes from the bulk delay, whichever estimator
  supplies it. The all-pass then takes the remainder and leaves the crossover
  notch at 1.4 dB, where the magnitude-corrected system loses 13 dB.
- It also absorbs the subwoofer polarity: inverting the subwoofer changes the
  all-pass result by less than 0.1 dB but costs the delay-only alignment 3 dB.
- Leave-one-out changes nothing (-0.37 against -0.36 dB in-sample), so the
  subwoofer's phase through the crossover is a room-wide quantity rather than
  a per-position one.
- The phasor blend and a carefully anchored unwrapped-angle blend are
  **equivalent** once the bulk delay has been applied. The phasor blend is
  better only when it has not (-0.54 against -1.05 dB), and it avoids needing
  a phase-unwrapping anchor at all, which is where its real advantage lies.

## Checked against the measured system

The September 2026 campaign also re-measured the corrected system in "System"
mode at the same ten positions. Predicting that run from the separate dry
measurements and the exported filter, with nothing fitted (4th-order crossover
at 85 Hz, subwoofer routed -4.8 dB below the mains, delays from the report),
reproduces it to **0.30 dB RMS over 200 Hz - 2 kHz** and **1.8 dB RMS over
35 - 200 Hz** on the left channel. The right channel agrees less well (1.3 and
2.6 dB) at all frequencies, including where the subwoofer plays no part, which
is a question of how exactly its microphone positions were reproduced between
the two sessions. The same run shows the FIR
latency self-absorption working: the filter's peak is at 23.3 ms, yet the
system delay is 47.2-48.1 ms rather than 23 ms more, the bulk latency having
been absorbed into the 29.7 ms alignment delay.

## The part that still needs the rig

A measured A/B of the alignment itself needs two system runs differing only in
the exported filter's phase:

1. Run the group analysis once.
2. Export linear phase, measure the system at every position.
3. Export minimum phase from the *same* analysis, changing nothing but Phase
   type, and measure again without leaving the room.

The minimum-phase run of the September 2026 campaign cannot serve for this. It
departs from its own exported filter by 2.0 dB RMS above the crossover (the
linear run matches to 0.6 dB), with 5-9 dB dips at 250-700 Hz at every one of
the ten positions, in a band where the subwoofer is more than 60 dB down and
the filter's phase cannot affect the magnitude at all. Coherence is 0.97-0.99
in both runs, so neither is noisy: something in the room or the chain changed
between them. Its sub trim also differed (0.70 ms against 0.00 ms), so the two
exports were not a controlled pair to begin with.
