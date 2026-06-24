# SuperMoTo

SuperMoTo is an FX-Mechanics JUCE audio plugin for the monitoring section,
the big brother of [MoTo](https://github.com/odoare/MoTo). It manages
multiple loudspeaker systems and subwoofers through a routing matrix of up to
32x32, and embeds the tools to measure the speakers, design FIR correction
curves and integrate a subwoofer in phase with the mains.

Every page has an **info button (i)** in its corner with a summary of the
controls and shortcuts.

## Part 1 — Monitoring matrix

- **Input/output matrix up to 32x32** (default 8 in / 8 out; choose the active
  size with the Inputs/Outputs selectors). Each frame (crosspoint) is a routing
  cell: **gain**, **phase inversion**, its own **vu-meter** and a checkbox to
  show its signal on the analyzer.
- **Per-output (per-speaker) processing**, edited by clicking an output cell:
  - **trim**, a **4-band EQ** (lowpass / highpass / bandpass for the
    bass-management crossover, peaking for correction; 2nd or 4th order),
  - a **time-alignment delay** (0..100 ms, fractional),
  - **one FIR correction filter** (WDL convolution, zero latency) loaded from a
    wav file (Load IR… in the editor, or right-click an output cell),
  - an output **vu-meter** and analyzer checkbox.
- **Automatic inter-output latency compensation**: when outputs that are fed
  by the engaged preset carry FIRs of different lengths, the shorter (or
  FIR-less) outputs are delayed so all stay time-aligned with the longest
  output FIR. A **gold LED** on an output cell shows it received compensation
  (hover for the amount). This is what keeps a subwoofer aligned with a
  linear-phase-corrected main.
- **Global spectrum analyzer** showing any set of matrix frames and/or
  output sums (checkbox in each frame / output strip), with a clickable
  **avg / peak** badge to switch the per-point aggregation.
- **6 matrix configurations A..F** stored per preset, engaged from the top
  bar buttons in exclusive or non-exclusive mode (non-exclusive sums the
  active matrices). The edited configuration is selected with the
  "Edit: A..F" buttons above the analyzer.
- **Configuration tool** for standard layouts (2.0, 2.1, 4.0, 4.1, 5.1, 7.1)
  and **periphonic Ambisonics** (orders 1-3): choose the inputs/outputs of each
  speaker, gains and the bass-management crossover; the mains are highpassed and
  their sum is sent lowpassed to the subwoofer output. For Ambisonics, set each
  loudspeaker's azimuth/elevation/radius and Apply builds the AmbiX (ACN / SN3D)
  max-rE sampling decoder (B-format on inputs 1..(order+1)^2; bass management
  lowpasses W to the sub). The **Speakers** control sets the output count
  (canonical 8/12/16, or any count laid out on a near-uniform sphere).
  **Radius compensation** delays and attenuates closer speakers (referenced to
  the farthest) so an irregular rig still sums correctly at the centre; turn off
  **Write delay compensation** to apply the decode without overwriting delays you
  already set by hand (e.g. after measured alignment). Apply writes the frames
  into a configuration, which can then be fine-tuned in the matrix.

Matrix interactions: click = select frame (editor panel at bottom right),
double-click = activate, vertical drag = gain, alt+click = analyzer trace,
right-click = context menu. Output strip: double-click = toggle FIR,
vertical drag = trim, alt+click = analyzer trace, right-click = load/clear IR.

The matrix settings are deliberately not host-automatable parameters
(6 x 256 frames); they are saved with the plugin state. Host parameters:
A..F, Exclusive, Level, Mute, Dim, Mono.

## Part 2 — Measurement (Calibration view)

Select the microphone input, the **measurement type**, the channels to
measure, the stimulus (band-limited white noise or logarithmic sweep,
10 Hz..20 kHz), the duration (5..30 s), the level and the base pathname.
**Run** measures each selected channel in turn and writes a stereo file with
channel 1 = sent signal and channel 2 = recorded signal.

Three measurement modes:

- **Dry** — stimulus straight to an output (the raw loudspeaker; the first
  step before designing a FIR). Files `<base>_<output>.wav`.
- **FIR** — stimulus to an output through its trim + FIR chain (verify a
  correction by re-measuring). The saved "sent" channel stays the raw
  stimulus.
- **System** — stimulus into a plugin **input**, run through the whole
  engine (matrix, crossover filters, output FIRs, latency compensation);
  the complete system as heard. Channels mean inputs in this mode; files
  `<base>_in<input>.wav`.

A right-hand **SPL meter** section reuses the microphone / channel /
measurement-type selections: it shows the mic RMS on a dual dBFS / dB SPL
bar and can emit a sine and/or white-noise test signal (routed exactly like
the measurement mode). Calibrate the dB SPL scale by playing a tone, reading
a real SPL meter and typing its value.

## Part 3 — Analysis & correction design

Load the set of measurements of one speaker (microphone moved around the
reference position). For each file the transfer function is estimated with
the **Welch method** (cross-spectrum over auto-spectrum averaged on Hann
windows of selectable size, default 65536). The propagation delay of each
measurement is removed so the complex responses can be averaged.

The plot shows the individual smoothed transfer functions (thin), their
average (thick), the proposed correction and the corrected response —
magnitude (normalized to the 200 Hz..2 kHz mean) and phase (propagation
delay removed). An **nth-octave smoothing** (off to 1 octave, default 1/6)
applies to the displayed curves and to the average the correction is
derived from. The correction compensates **modulus and phase**: it is the
inverse of the smoothed average, with a slope (2nd-order highpass target
at 30 Hz) towards low frequencies and a **soft-knee boost ceiling** (default
+12 dB, tanh knee so deep notches are tamed without a kink). The
**correction level** interpolates (log-domain) from 0 = no correction to
1 = flat. An **analysis range** limits the band the correction acts on:
outside it the correction is unity and the exported measured IR rolls off,
so out-of-band noise and content the speaker cannot reproduce are ignored.

**Subwoofer integration** (optional): load a second measurement set taken at
the same positions as the main, anchored on the main's per-position delay so
the relative timing is preserved. Around the chosen **crossover** the
correction carries an all-pass that steers the corrected main's phase onto
the sub's, so they sum coherently. The relative main/sub timing comes from
the measurements; **Invert** flips the sub polarity if needed. A **Mains
delay** control (±40 ms, auto-detected from the sub's group delay around the
crossover) lets you declare a bulk delay applied physically to the mains so
the all-pass only corrects the residual — keeping the FIR short; the corrected
phase flattens as you tune it, and an on-screen message states the exact
per-output delay to set in the matrix.

**Export IR** saves the measured response itself; **Export correction IR**
renders the correction to an impulse response wav (selectable FIR length,
energy centred at half the length) and can directly assign it to an output
of the monitoring part. The centred IR introduces firLength/2 samples of
delay on that output — which the matrix's inter-output latency compensation
then absorbs automatically.

## Use cases

### A. Stereo monitoring with corrected speakers
1. Matrix view (or Config tool → Stereo 2.0 → Apply to A): route input 1→out 1,
   input 2→out 2.
2. Calibration, **Dry** mode: measure outputs 1 and 2 (one mic per speaker
   position set), several mic positions around the listening spot.
3. Analysis: load each speaker's set, design the correction, **Export
   correction IR** and assign it to the output. Repeat per speaker.
4. Calibration, **FIR** mode: re-measure to verify the corrected response is
   flat.

### B. 2.1 / bass-managed system with an aligned subwoofer
1. Config tool → Stereo 2.1 (or 5.1 / 7.1) → set the crossover → Apply.
2. Measure and correct each main (use case A).
3. Measure the **sub** alone (Dry) at the same mic positions as one main.
4. Analysis: load the main set, then **Load sub measurements**; set the
   crossover (toggle **Invert** if the sub is wired out of polarity) — the
   corrected-main phase is steered onto the sub's through the crossover.
5. Read the recommended **Mains delay** and raise the slider until the
   corrected-main phase flattens through the crossover; note the delay it asks
   for. Export and assign.
6. Time-align the drivers physically: enter that **Mains delay** in the
   per-output **delays** in the matrix; it then automatically delays the sub
   output to match the mains' FIR latency too (gold LED on the sub output).
   Verify in Calibration **System**
   mode by measuring the input: the summed response should be smooth through
   the crossover.

### C. Verifying the complete system end to end
- Calibration, **System** mode, select the plugin input your source uses. The
  stimulus runs through the engaged preset exactly as monitored (routing,
  crossover, FIRs, latency compensation). Analysis of that capture shows the
  real in-room system response.

### D. Multiple speaker sets / quick A/B
- Store different rigs or rooms in configs A..F (e.g. A = mains, B = mains+sub,
  C = nearfields) and switch from the top bar. Use **Exclusive** for A/B,
  non-exclusive to sum. Collapse (▲) to a compact output-strip-only window.

## Building

CMake based, mirroring MechanOdd. Expected sibling layout:

```
../JUCE/                              juce-framework/JUCE checkout
```

Shared FX-Mechanics code — GUI controls, look-and-feel, DSP and the WDL
convolution engine — is provided by the FxmeTools submodule under `lib/`
(no FxmeJuceTools symlink needed).

```
git submodule update --init --recursive   # FxmeTools + WDL
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

Formats: VST3, AU, Standalone (discrete in/out, selectable 8 / 16 / 24 / 32,
default 8).

## License

LGPL-3.0-or-later — (c) 2023-2026 Olivier Doaré
