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

    python3 sub_alignment_validation.py --data ~/supermoto_measurements \
        --prefix Mirage_14juin_ --mains L=1,R=2 --sub 3 --crossover 85 \
        --figure summation.png

The files are the plugin's own stereo captures (channel 1 the sent stimulus,
channel 2 the microphone). Needs numpy, scipy and, for `--figure`, matplotlib.

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

## Result on the five-position Mirage set (85 Hz crossover, main L)

Leave-one-position-out, so none of these numbers are fitted to what they are
scored on:

| strategy                        | eff dB | worst | \|dphi\| | notch  |
|---------------------------------|--------|-------|----------|--------|
| no correction, no alignment     | -2.58  | -2.83 | 94 deg   | -10.0  |
| magnitude only (min-phase)      | -3.75  | -4.05 | 110 deg  | -16.7  |
| mag + arrival-time delay        | -3.16  | -4.12 | 118 deg  | -7.1   |
| mag + crossover-band delay      | -0.86  | -1.13 | 67 deg   | -1.8   |
| **all-pass alignment**          | **-0.20** | **-0.32** | **34 deg** | **-0.6** |
| all-pass, subwoofer inverted    | -0.49  | -0.65 | 46 deg   | -4.1   |
| all-pass, no bulk delay         | -0.54  | -0.68 | 47 deg   | -2.3   |
| ablation: unwrapped-angle blend | -0.20  | -0.32 | 34 deg   | -0.6   |
| ablation: unwrapped, no delay   | -1.05  | -1.29 | 65 deg   | -5.5   |

Main R gives the same ordering within 0.2 dB. Points the table makes:

- Correcting the main's magnitude alone makes the summation *worse* than no
  correction at all (-3.75 against -2.58 dB), because a minimum-phase
  magnitude correction moves the main's phase through the crossover region
  without regard for the subwoofer.
- The delay estimator matters more than the correction. The arrival-time
  difference (25.4 ms here) leaves -3.16 dB; the crossover-band group delay
  (29.8 ms) leaves -0.86 dB.
- The all-pass takes the remaining 0.7 dB and leaves the crossover notch at
  -0.6 dB, i.e. essentially coherent summation.
- It also absorbs the subwoofer polarity: inverting the subwoofer costs the
  all-pass 0.3 dB but costs the delay-only alignment 2.4 dB.
- Leave-one-out changes nothing (-0.20 against -0.19 dB in-sample), so the
  subwoofer's phase through the crossover is a room-wide quantity rather than
  a per-position one.
- The phasor blend and a carefully anchored unwrapped-angle blend are
  **equivalent** once the bulk delay has been applied. The phasor blend is
  better only when it has not (-0.54 against -1.05 dB), and it avoids needing
  a phase-unwrapping anchor at all, which is where its real advantage lies.

## The part that still needs the rig

The above predicts summation from dry measurements. To close the loop, measure
it:

1. Measure the mains and the subwoofer dry, several positions, as usual.
2. Design and apply the corrections in Group analysis, "Compute alignment"
   then Apply.
3. Re-measure each main in "System" mode with the subwoofer routed in, same
   positions, and compare the crossover region against the dry run.
4. Repeat step 3 with the all-pass defeated (export minimum-phase, which drops
   A but keeps the delays) for the controlled comparison.

Steps 3 and 4 differ only in the exported filter's phase, so the difference
between them is the measured effect of the alignment alone.
