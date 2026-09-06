# SuperMoTo — to do

## Done

- [x] Calibration : Add comments text entriers that are saved in the xml/markdown file. Two text entries:

        * a general comment for the series of measurements (accessible via a button: you click and open a text entry). If modified between two series of measurements, it it written again at the next file saved
        * a comment for one series of measurements : a one line text entry.

    *Calibration view: `Folder comment...` (pre-filled from the folder's existing
    manifest) and the one-line `Run comment`. Written to `measurement.xml`
    (`<GeneralComment>`, per-run `comment`) and to `readme_measurement.md`.*

- [x] Calibration : If the microphone has been calibrated using an SPL meter, write the calibration coefficient in the files

    *`<SplCalibration offsetDb>` at folder level, plus `splOffsetDb` on the run
    that was actually made with it. Carried over from the previous manifest when
    a later run is made without it.*

- [x] Calibration  : If a microphone correction curve has been used, write the data in the file too

    *`<MicCalibration name>` holding a verbatim copy of the calibration file, so
    the folder carries its own microphone correction. Same carry-over rule.*

- [x] Analysis group analysis : If a calibration data is found in the file, use it. It is still possible to load a different calibration file. In the case of single analysis tab, we should look for an xml info file in the folder to look for that information.

    *Mic-cal source selector (`Global mic cal` / `Folder mic cal` / `No mic cal`)
    in both panes; `Folder` is auto-selected when the set carries one. Group
    analysis reads the manifest of the folder it loaded; the single Analysis tab
    reads it from the parent directory of the selected files.*

- [x] Analysis and group analysis : Currently the plotted frequency response data is normalized to a 0 dB reference. That could be interesting to have an option with no normalization. If it was done on a calibrated data, we could even see the actual SPL.

    *`Level` selector: `Normalized` / `Absolute dB` / `dB SPL`. The SPL option
    enables only when the manifest supplies both the SPL calibration and the
    run's stimulus level.*

- [x] Analysis and group analysis : Plot impulse response (switch from frequency response to impulse response using a dropdown)

    *`View` selector: `Frequency response` / `Impulse response`, showing the
    measured IR and the correction IR at the current FIR length.*

- [x] Analysis and group analysis : If the log sweep has been used for the measurement, we should implement the Farina's method for system's identification (linear+nonlinear components+phase). Please refer to Novak et al, JAES 2015 "Synchronized swept-sine: Theory, application, and implementation" for the implementation. Do it again by putting reusable code in FxmeTools. Here again, if we are in the single analysis tab, we look for the xml file in the folder to get the information on the used forcing signal.

    *`fxme::SynchronizedSweep` (FxmeTools core) does the quantised sweep rate,
    the analytic-inverse deconvolution and the harmonic extraction.
    `AnalysisEngine::TfMethod::sweep` uses it and exposes H2..H5; the `TF`
    selector offers "Sweep (Farina)" and auto-selects it when the manifest
    carries f1/f2/L. Falls back to Welch per file otherwise.*

- [x] In order to plot the impulse response, we should implement a new component that can display audio data in the time domain. The component will be used in other projects. We should hence implement it in FxmeTools. It should be capable of drawing short to long signals (dozens of seconds), be used for realtime monitoring of signals, as well as drawing static buffers, or wavefile data. It should have a marker/region system. Zooming in/out should be implemented: Mouse wheel for vertical zooming. Ctl-mouse for horizontal zooming, and click dra for navigating in the data. Double-click should rested to the full view. Axis label + grid should be drawn the same way as the spectrum plots. Please implement it with a clear and reusable API, and properly document the header.

    *`fxme::WaveformDisplay`. Sources: `setBuffer()` (static), `setTap()`
    (realtime, scrolling), and a wavefile reader overload. Markers and regions
    via `setMarkers()` / `setRegions()`. Wheel = amplitude zoom, ctrl+wheel =
    time zoom at the cursor, drag = pan, double-click = full view. A min/max
    peak cache keeps signals of dozens of seconds fast at any zoom. Grid and
    labels match `SpectrumDisplay`; header documents the API.*

- [x] In the spectrum and phase display components, we should also implement the ctl-wheel to zoom horizontally, click-drag in both directions, and double-click for default view.

    *Done in the project's `TransferFunctionPlot` (magnitude + phase) and in
    `fxme::SpectrumDisplay`.*

- [x] Reorganize eventually the controls for better readability/logic. The order of controls should reflect the order of actions. For instance, in the group analysis, the load measurement folder should come first. See everywhere if something could be done to improve the GUI look and feel.

    *Commit `9192dea`. Group analysis row 0 now reads left to right as the
    workflow: load measurements, describe the set, compute alignment, apply &
    export.*

## To do

- [] In the group analysis, the detected delay between the subwoofer and the main speakers is displayed but it is not possible to modify this value. Like in the single analysis panel, the user should be allowed to modify this value and the graph sould be updated to show the effect on the phase alignment of the change.

    *Design note (2026-09-05), to settle before implementing. The two panels do
    not measure the same thing, so "the same control as the single analysis
    panel" is not a straight port.*

    *The single panel has two independent numbers: `estimateMainSubOffsetMs()`
    (a least-squares fit of the sub's unwrapped phase over crossover/2 ..
    crossover x2, i.e. the sub's group delay in the crossover region), shown as
    a suggestion in `alignInfo`, and the `Mains delay` slider, which the user
    sets freely in +/-40 ms and which feeds `setTimeAlignMs` so the plot follows
    live. The group panel never calls that estimator. Its row readout is
    `alignedDelayMs`, an arrival-time difference derived from the median IR-peak
    delay of each entry (`getPropagationDelayMs`), computed only when Compute
    alignment is pressed, and the same value is later written to the output as
    its bulk delay by `finalizeApply`.*

    *So a speaker's readout only reads as a main/sub offset when the sub happens
    to be the farthest driver; it is really a group-wide relative delay, and the
    sub row carries one too. Two ways to go:*

      *1. Make `alignedDelayMs` editable per row (an override of the group
      alignment). It would have to feed `setTimeAlignMs` on that speaker to move
      the phase plot; `finalizeApply` already picks it up for the output delay.
      But the group is then no longer aligned on a common reference and the
      "farthest driver gets 0" invariant becomes the user's problem.*

      *2. Surface `estimateMainSubOffsetMs()` in the group panel as well and let
      `timeAlignMs` diverge from `alignedDelayMs`, matching the single panel
      exactly. Cleaner conceptually, but it splits one readout into two numbers
      per row, and a row already carries name, load, status, delay, trim and
      output.*

    *Either way there is a staleness question: Compute alignment currently
    overwrites these fields wholesale, so an edit must either survive a
    recompute or be visibly reset by it. Note also that changing any shared
    setting (crossover included) silently leaves `alignedDelayMs` and every trim
    figure stale until Compute is pressed again.*

- [] Tooltips should be generalized to all controls and show a buton '(?)' to enable/disable them left to the matrix button.

- [] Implementation of an even more compact view with on first row: the existing controls until the (Exclusive) button. Second row: the Volume button, the mute/dim/mono buttons, and small vertical meters for the outputs.

