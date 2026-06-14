# SuperMoTo

SuperMoTo is an FX-Mechanics JUCE audio plugin for the monitoring section,
the big brother of [MoTo](https://github.com/odoare/MoTo). It manages
multiple loudspeaker systems and subwoofers through a full 16x16 routing
matrix, and embeds the tools to measure the speakers and design FIR
correction curves.

## Part 1 — Monitoring matrix

- Full **16x16 input/output matrix**. Each frame (crosspoint) has:
  - gain, **IIR filter** (lowpass / highpass / bandpass, 2nd or 4th order,
    frequency, Q), **phase inversion** and **delay** (0..100 ms, fractional),
  - its own **vu-meter**, and a checkbox to show its signal on the analyzer.
- **One FIR filter per output** (WDL convolution, zero latency) to
  compensate the frequency response of the attached loudspeaker —
  impulse responses are loaded from wav files (right-click an output cell).
- Per-output trim and vu-meter.
- **Global spectrum analyzer** showing any set of matrix frames and/or
  output sums (checkbox in each frame / output strip).
- **6 matrix configurations A..F** stored per preset, engaged from the top
  bar buttons in exclusive or non-exclusive mode (non-exclusive sums the
  active matrices). The edited configuration is selected with the
  "Edit: A..F" buttons above the analyzer.
- **Configuration tool** for standard layouts (2.0, 2.1, 4.0, 5.1, 7.1):
  choose the inputs/outputs of each speaker, gains and the bass-management
  crossover; the mains are highpassed and their sum is sent lowpassed to
  the subwoofer output. Apply writes the frames into a configuration,
  which can then be fine-tuned in the matrix.

Matrix interactions: click = select frame (editor panel at bottom right),
double-click = activate, vertical drag = gain, alt+click = analyzer trace,
right-click = context menu.

The matrix settings are deliberately not host-automatable parameters
(6 x 256 frames); they are saved with the plugin state. Host parameters:
A..F, Exclusive, Level, Mute, Dim, Mono.

## Part 2 — Measurement (Calibration view)

Select the microphone input, the outputs to measure, the stimulus
(band-limited white noise or logarithmic sweep, 10 Hz..20 kHz), the
duration (5..30 s), the level and the base pathname. **Run** measures each
selected output in turn and writes `<base>_<output>.wav` — a stereo file
with channel 1 = sent signal and channel 2 = recorded signal.

The *measure through correction* toggle sends the stimulus through the
output trim + FIR chain so a correction can be verified by re-measuring
(the saved "sent" channel stays the raw stimulus).

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
at 30 Hz) towards low frequencies and an adjustable boost cap (default
+12 dB). The **correction level** interpolates (log-domain) from 0 = no
correction to 1 = flat.

**Export** renders the correction to an impulse response wav (selectable
FIR length, energy centred at half the length) and can directly assign it
to an output of the monitoring part. Note: the centred IR introduces
firLength/2 samples of acoustic delay on that output.

## Building

CMake based, mirroring MechanOdd. Expected sibling layout:

```
../JUCE/                              juce-framework/JUCE checkout
../JUCE/usermodules/FxmeJuceTools/    symlink to FxmeJuceTools/module/FxmeJuceTools
```

```
git submodule update --init --recursive   # FxmeFX + WDL
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

Formats: VST3, AU, Standalone (16 discrete in / 16 discrete out).

## License

LGPL-3.0-or-later — (c) 2023-2026 Olivier Doaré
