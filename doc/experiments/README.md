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
for the channels, the subwoofer, the positions and the microphone calibration:

    python3 sub_alignment_validation.py --manifest \
        --data ~/Documents/supermoto/MeasurementCampaign/Mirage_panneaux_paper \
        --crossover 85 --sub-gain -4.8 --analysis-low 85 \
        --smooth-lo 0.1667 --smooth-hi 0.3333 --max-boost 10

The design settings should match the group analysis that produced the
corrections (they are printed at the top of its report). `--sub-gain` is the
subwoofer's routing level relative to the mains as set in the matrix, which the
dry captures do not carry, and `--bm-crossover` the bass-management crossover
if it differs from the one the analysis was run at. `--no-mic-cal` drops the
calibration the plugin always applies; it is there to show what that costs, not
to be used.

Folders with ad-hoc file names are read instead with an explicit pattern:

    python3 sub_alignment_validation.py --data ~/supermoto_measurements \
        --prefix Mirage_14juin_ --mains L=1,R=2 --sub 3 --crossover 85 \
        --figure summation.png

The files are the plugin's own stereo captures (channel 1 the sent stimulus,
channel 2 the microphone). Needs numpy, scipy and, for `--figure`, matplotlib.

`make_paper_figure.py` builds the Validation figure of `doc/paper.tex` and
prints every number the paper's Section "Validation" quotes:

    python3 make_paper_figure.py --check-polarity \
        --dry    ~/Documents/supermoto/MeasurementCampaign/Mirage_panneaux_paper \
        --system ~/Documents/supermoto/supermoto_paper4

`--dry` is the folder the group analysis was run on; `--system` the folder
holding the three system runs and the two exports. Positions are matched
between the two sessions by the comment each run carries, so they need not be
named in the same order or be the same in number.

## Following the plugin exactly

Two details of `AnalysisEngine` are easy to get wrong offline and both move the
crossover-region scores by more than the effect being measured:

- **The microphone calibration is part of the design.** The plugin divides the
  mic response out of every spectrum before it inverts anything, so a chain
  that skips it designs a different filter. Applying it took the agreement with
  the plugin's own export from 1.5 dB RMS to 0.08 dB above 1 kHz.
- **The band-edge fade comes before the minimum-phase render.** `renderIR`
  takes the magnitude of the *finished* correction, fade included, and hands
  that to the cepstrum. Rendering first and fading afterwards changes the
  magnitude-only rows by about 0.7 dB, because the cepstrum spreads a change
  confined to the half-octave skirt over the phase of the whole band.
- **`--analysis-low` and `--smooth-hi` have to reach the code that uses them.**
  `band_weight` and `smooth_var_octave` took those settings as *default
  arguments*, which Python binds once at def time, so the command line rebound
  the module globals and nothing read them: every design was faded from 20 Hz
  and smoothed at 1/6 octave throughout. Fixed by reading the globals at call
  time. This one was worth 0.3-0.5 dB on the magnitude rows and turned the
  inverted-subwoofer row from -0.32 into -1.05 dB.
- **Compare like with like.** The plugin's export is a 2048-tap render; an
  unrendered frequency-domain design is not comparable to it below a few
  hundred hertz.

With all four right, the offline chain reproduces the plugin's exported filters
to **0.02-0.13 dB RMS in every band from 60 Hz to 8 kHz**, for both renderings
and both mains.

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

Top: the summed level, power-averaged over the positions. Bottom: the
summation efficiency, where 0 dB is coherent addition. The grey curve is the
uncorrected system; the orange one a magnitude-only correction, which deepens
the crossover cancellation rather than curing it; the blue one is the all-pass
alignment, flat across the region.

## Result on the September 2026 campaign (10 positions, main L)

Leave-one-position-out, so none of these numbers are fitted to what they are
scored on. This is the set the paper reports:

| strategy                        | eff dB | worst | \|dphi\| | notch  |
|---------------------------------|--------|-------|----------|--------|
| no correction, no alignment     | -1.27  | -1.97 | 79 deg   | -5.5   |
| magnitude only (min-phase)      | -1.79  | -2.46 | 81 deg   | -9.9   |
| mag + arrival-time delay        | -0.45  | -0.69 | 42 deg   | -1.4   |
| mag + crossover-band delay      | -0.76  | -1.23 | 52 deg   | -1.8   |
| **all-pass alignment**          | **-0.39** | **-0.94** | **42 deg** | **-1.7** |
| all-pass, subwoofer inverted    | -1.05  | -1.33 | 77 deg   | -4.9   |
| all-pass, no bulk delay         | -0.58  | -1.38 | 46 deg   | -2.3   |
| ablation: unwrapped-angle blend | -0.39  | -0.95 | 42 deg   | -1.7   |
| ablation: unwrapped, no delay   | -0.63  | -1.45 | 51 deg   | -2.5   |

Run at the campaign's own settings, analysis range from 85 Hz. See "The
analysis band edge" below: that edge is why the inverted row is as bad as it
is.

Main R gives the same ordering within 0.2 dB.

Points the table makes:

- Correcting the main's magnitude alone makes the summation *worse* than no
  correction at all (-1.79 against -1.27 dB), because a minimum-phase
  magnitude correction moves the main's phase through the crossover region
  without regard for the subwoofer.
- Most of what is recovered comes from the bulk delay, whichever estimator
  supplies it. The all-pass then takes the remainder and leaves the crossover
  notch at 1.7 dB, where the magnitude-corrected system loses 10 dB.
- The all-pass is the best of the five at 7 of the 10 positions on the left
  main and 6 on the right. Where it loses, it loses to the arrival-time delay,
  which on this rig is a close second at -0.45 dB. The average is not what
  separates them; the robustness is.
- Leave-one-out changes nothing (-0.39 against -0.37 dB in-sample), so the
  subwoofer's phase through the crossover is a room-wide quantity rather than
  a per-position one.
- The phasor blend and a carefully anchored unwrapped-angle blend are
  **equivalent** once the bulk delay has been applied. The phasor blend is
  better only when it has not, and it avoids needing a phase-unwrapping anchor
  at all, which is where its real advantage lies.
- On this rig the arrival-time estimator beats the crossover-band one by
  0.31 dB for a delay-only alignment. That is a property of this rig, not a
  general result: an earlier one whose subwoofer arrival wandered by 6.6 ms
  across positions went the other way. Under the all-pass the choice matters
  less: dropping the bulk delay altogether costs 0.19 dB, where changing which
  estimator supplies it costs a delay-only alignment 0.31 dB.

## Checked against the measured system (`supermoto_paper4`, 18 Sep 2026)

One analysis, exported twice a minute apart, linear phase and minimum phase,
with nothing else touched. The system was then measured in "System" mode at
the same ten positions in three states, no FIR, linear phase, minimum phase,
each state visited in turn at a position before the microphone was moved.

- **The system applies the filter that was designed.** The ratio of a
  corrected run to the no-FIR run at the same position follows the exported
  filter to 0.23 dB RMS (left, linear), 0.25 (left, minimum), 0.30 and 0.34
  (right) over 200 Hz - 10 kHz. Both renderings peak above full scale
  (+4.7 and +6.4 dBFS for minimum phase, +3.0 for the right linear one) and
  both are realised exactly, which is the check on the 32-bit float preset
  container that replaced the integer one. The embedded copies in the preset
  are bit-identical to the exported files.
- **The offline model is good to about 0.9 dB in the crossover region.**
  Predicting each of the six cases (three states, two mains) from the dry
  captures and the exported filter reproduces the measurement to 0.87-0.93 dB
  RMS over 35-250 Hz, worst position 1.27 dB, and the crossover-band level to
  0.43 dB.
- **The alignment is measurable.** The aligned export carries **0.47 dB** more
  level through [fx/sqrt2, fx*sqrt2] than the minimum-phase one on both mains,
  at 10 of 10 positions each, from 0.17 to 0.82 dB. The model predicts 0.32 and
  0.31 dB for the same quantity. What an alignment buys is coherence rather
  than level, and a single system measurement cannot separate coherence from
  the room; the band level is the part a measurement can hold.
- **...but 2048 taps are not enough to make it a pure phase experiment.** At
  and above the crossover the two renderings carry the same designed magnitude
  (|A| = 1 and the band-edge fade is complete there), yet the *rendered*
  magnitudes differ by 1.63 dB RMS over 85-200 Hz on the left main, 4.1 dB at
  the worst frequency, and 0.77 dB on the right. Over 200 Hz - 1 kHz they agree
  to 0.12 and 0.30 dB, and above 1 kHz to 0.01 dB. A linear-phase filter
  spreads its response over +/-23 ms and a minimum-phase one front-loads it,
  and at 100 Hz neither has room for the structure the design asks for.
  Holding the magnitude fixed and swapping only the phase separates the two:
  the aligned phase is worth 0.21 dB (left) and 0.42 dB (right) on the
  minimum-phase magnitude, 0.31 and 0.44 dB on the linear-phase one. So between
  a fifth and a half of a decibel is the alignment; the rest of the 0.47 dB is
  the two renderings disagreeing about a magnitude they were meant to share.
  A longer FIR would tighten this.
- **Latency self-absorption works.** The linear-phase filter peaks 1026
  samples in (23.3 ms) yet all three states show the same 47.2-48.1 ms system
  delay, the bulk latency having been absorbed into the 29.7 ms alignment
  delay.

## The analysis band edge

The correction is faded back to unity over a half-octave raised-cosine skirt
below the analysis low edge, and the alignment factor fades with it. An edge
set *at* the crossover therefore switches the phase steering off below
fx/sqrt2 -- inside the region the subwoofer dominates. The campaign ran with
the edge at 85 Hz, the crossover itself, and it shows:

| analysis low | all-pass | all-pass, sub inverted |
|--------------|----------|------------------------|
| 85 Hz        | -0.39    | **-1.05**              |
| 60 Hz        | -0.39    | -0.31                  |
| 40 Hz        | -0.38    | -0.32                  |

Moving the edge an octave below the crossover restores the alignment's
immunity to subwoofer polarity at no cost to the aligned score. It costs
nothing else either: the correction down there is faded out in both cases.
**Set the analysis range to start below the crossover, not at it.**

## What FIR length buys

The two phase renderings of one design only carry the same magnitude if the
filter is long enough to realise it. Rendering the campaign's own design both
ways (this prediction reproduces the measured 2048-tap disagreement, 1.59 and
0.75 dB against 1.63 and 0.77 measured):

| taps  | \|linear\| - \|minimum\|, 85-200 Hz | linear vs its own designed magnitude |
|-------|-----------------------------------|--------------------------------------|
| 2048  | 1.59 / 0.75 dB RMS (peak 4.1)     | 1.88 / 1.56 dB RMS                   |
| 4096  | 0.45 / 0.29 (peak 1.6)            | 0.52 / 0.32                          |
| 8192  | 0.06 / 0.05                       | 0.07 / 0.05                          |
| 16384 | 0.01                              | 0.01                                 |

Left main / right main. `make_paper_figure.py` also writes
`doc/figures/fir-length.png` (`--out-length`), which puts the same sweep
through the summation model and against the measured state: the 2048
prediction follows the measurement dip for dip, including a 6 dB cancellation
at 115 Hz on the left main that is not in the design and is gone by 4096 taps,
and from 8192 up the curves coincide. In band power the four lengths are within
0.05 dB of each other (left: +0.69, +0.73, +0.73, +0.73 dB; right: +0.88,
+0.78, +0.83, +0.83), so what length buys is the shape, and the agreement
between the two renderings, not the level.

Two consequences. For the A/B, 4096 leaves a
magnitude confound of the same order as the phase effect being measured and
8192 does not, so a repeat that wants to be a phase experiment needs 8192. For
the plugin, a 2048-tap linear-phase correction misses its own designed
magnitude by about 1.9 dB RMS between 85 and 200 Hz, which is an argument for
the 4096 default whenever the analysis reaches that low.

The price is latency: a linear-phase filter of n taps carries n/2 samples of
bulk latency, and what the output's own alignment delay cannot absorb is added
to every output by the compensation of `MatrixEngine::recomputeLatencyComp`.
At 2048 taps and a 29.7 ms alignment delay it is free; at 4096 it adds 16.7 ms
to everything, at 8192, 63 ms. Relative timing between outputs is preserved
either way, so the experiment stays valid.

## The subwoofer polarity

Each configuration routes into the subwoofer output through its own crosspoint,
and the analysis report states the polarity of that **output**, not of the
route into it. A polarity switch left set differently in one of the
configurations being compared inverts the subwoofer silently, and a half turn
at the crossover is worth several decibels, which swamps the effect being
measured. It happened twice in this campaign's earlier runs.

`make_paper_figure.py --check-polarity` fits each state's polarity against the
dry captures and prints it. On `supermoto_paper4` all six cases come out
normal at 0.90-0.95 dB RMS, where the opposite sign costs 2.7-5.7 dB. Before
running an A/B, check the crosspoints in the matrix and then check them again
in the data.
