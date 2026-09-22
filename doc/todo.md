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

- [x] In the group analysis, the detected delay between the subwoofer and the main speakers is displayed but it is not possible to modify this value. Like in the single analysis panel, the user should be allowed to modify this value and the graph sould be updated to show the effect on the phase alignment of the change.

    *Neither of the two options in the 2026-09-05 design note was taken. Instead
    of making the per-row `alignedDelayMs` editable, or surfacing
    `estimateMainSubOffsetMs()` on every row, the sub row alone carries a `Sub
    trim` slider (+/-40 ms) that offsets the sub against the rest of the group:
    `SpeakerGroupAnalysis::setSubTrimMs`, applied on top of the arrival-time
    delays so the "farthest driver gets 0" invariant survives. A `Use x-over`
    button fills it from the measured crossover-band phase slope
    (`getSuggestedSubTrimMs()`), with a read-out next to it, and the button
    stays disabled while that estimate is not usable. The plots follow the
    slider live, and `finalizeApply` writes the trimmed delay to the output.*

- [x] Tooltips should be generalized to all controls and show a buton '(?)' to enable/disable them left to the matrix button.

    *`Source/Tooltips.h` holds every hover-help string in one place: 113 strings
    in a namespace per pane (`bar`, `mtx`, `cfg`, `cal`, `ana`, `grp`) plus
    `shared`, wired into 118 `setTooltip` call sites across the editor and all
    seven component headers. The `?` toggle ended up at the RIGHT of the bottom
    view switcher rather than left of the matrix button, next to the other view
    buttons. It drives a `ToggleableTooltipWindow` (a `juce::TooltipWindow`
    whose `getTipFor` returns nothing when off), so one flag silences the whole
    plugin; the state persists via `smt::getUiTooltips()`.*

- [x] Implementation of an even more compact view with on first row: the existing controls until the (Exclusive) button. Second row: the Volume button, the mute/dim/mono buttons, and small vertical meters for the outputs.

    *`PluginEditor::Compact` now has three states cycled by the top-bar collapse
    button: `off` (full editor), `strip` (top bar plus the matrix output strip)
    and `mini` (the requested two-row layout). Mini uses the new
    `smt::OutputMetersStrip`, a row of narrow vertical meters fed from
    `MatrixEngine::getOutputLevelDb()` -- display only, so a stray click cannot
    act on an output the way the matrix strip would. `miniWindowSize()` fixes
    the window to the output count. The mode persists via
    `smt::getUiCompactMode()`, falling back to the older boolean `uiCollapsed`
    setting.*

- [x] Measurement : reduce the pre-ringing of the sweep-deconvolved impulse responses by running the sweep up to Nyquist over an integer number of octaves, and by shortening the fade-out on sweeps. Papers: Farina, AES 122 (2007) "Advancements in impulse response measurements by sine sweeps", section 3.1; Vetter & di Rosario (2011) "ExpoChirpToolbox", sections 2.1 and 2.5.

    *Analysis note (2026-09-10), measured before implementing. The ringing
    around the peak of a sweep-deconvolved IR is NOT Farina's fade-out
    artifact: it is the symmetric sinc of the stimulus band edge at
    `sweepF2Hz` = 20 kHz. Simulating our own path (the synchronized sweep of
    `nextStimulusSample` deconvolved by `SynchronizedSweep::deconvolve`, with a
    pure delay standing in for a loopback) gives, as worst ringing within 20 ms
    of the peak relative to the peak:*

    | Stimulus band | 44.1 kHz | 48 kHz | 96 kHz |
    |---|---|---|---|
    | 10 Hz .. 20 kHz (current) | -21.0 dB | -17.9 dB | -14.9 dB |
    | f1 .. Nyquist, integer octaves | -52.9 dB | -61.0 dB | -62.2 dB |

    *So the artifact is large (~-18 dB at 48 kHz) and it gets WORSE as the
    sample rate rises, because the gap between 20 kHz and Nyquist grows. The
    other remedies in the papers measure much smaller here: removing the 20 ms
    fade-out alone changes the result by under 0.5 dB (it tapers only
    19695 .. 20000 Hz, i.e. 0.022 octave -- Farina's warning applies to his
    case where the sweep already ran to Nyquist, so the fade was the only thing
    shaping the top edge).*

    *A code fact supports the same conclusion independently: the analytic
    inverse in `SynchronizedSweep::deconvolve` is applied unregularized from
    bin 1 to Nyquist. Its group delay is -L*ln(f/f1), so content ABOVE f2 is
    assigned negative delay and lands BEFORE the impulse, while the sqrt(f)
    term amplifies it. Attenuating 20 .. 24 kHz with a converter low-pass
    improved the current setup by 10 dB, which confirms the mechanism. Sweeping
    to Nyquist removes the out-of-band region entirely, so no epsilon(f)
    regularization of the inverse is needed. If f2 is ever kept below Nyquist
    instead, such an epsilon(f) must rise only OUTSIDE [f1, f2]: a half-octave
    raised-cosine skirt reaching inside the band cost 40 dB in the same test.*

    *Reaching Nyquist accounts for most of the gain (-18 -> -53 dB at 48 kHz).
    Choosing an integer OCTAVE count rather than any integer ratio adds a
    further 8 dB, by landing the final sample closer to the true zero crossing.
    An integer frequency ratio also makes the sweep's end phase an exact
    multiple of 2*pi on top of Novak's f1*L integer condition, so the sweep
    ends at a zero crossing and needs no fade-out; verified at every rate
    tested.*

    *Plan:*

      *1. Make the sweep band per-run instead of the `sweepF1Hz` / `sweepF2Hz`
      constants in `MeasurementEngine.h`. Set f2 = sr/2 and f1 = f2 / 2^P, with
      P = 11 at 44.1/48 kHz and P = 12 at 88.2/96 kHz. That puts f1 between
      10.8 and 11.7 Hz at every rate, so the analysis band and its half-octave
      skirt lose nothing.*

      *2. Split the fade logic in `nextStimulusSample`: noise keeps a 20 ms
      fade-out, sweeps get a token one. The two changes are COUPLED -- at
      Nyquist the fade-out becomes the only thing shaping the top edge, so a
      20 ms one costs 15..25 dB.*

      *3. Pin the noise band at 20 kHz rather than letting `noiseLp` follow f2,
      so noise measurements do not silently change.*

      *4. Nothing changes on the analysis side. `measurement.xml` already
      records f1/f2/L per run and the analysis reads them back, so existing
      measurement folders keep deconvolving exactly as they do now.*

    *Two caveats to check before committing. The tweeter now receives content
    up to Nyquist, though the per-octave energy is unchanged and the top octave
    lasts ~90 ms of a 10 s sweep, and the DAC reconstruction filter attenuates
    it; worth one listening check at reduced level. And the fundamental range
    whose 2nd harmonic aliases grows, which affects the H2..H5 curves but is
    not a regression, since it already aliased above sr/4.*

    *Deliberately NOT doing two things the papers suggest, both measured as not
    worth it. A Kirkeby "packing" filter derived from a loopback reference
    (Farina 2007, section 3.1, after Kirkeby/Rubak/Farina "Fast Deconvolution
    using Frequency-Dependent Regularization") improved far pre-ringing from
    -22 to -32 dB on an AC-coupled chain but did nothing within 1 ms of the
    peak, and too large a regularization made it worse; it would also need a
    new reference-measurement workflow. And replacing the soft-knee tanh boost
    limiter in `recomputeCorrection` with Kirkeby's conj(H)/(|H|^2 + eps) gave
    no measurable change in the correction FIR's pre-echo at matched peak
    boost: the existing limiter plus the complex fractional-octave smoothing
    already does that job.*

    *Implemented 2026-09-10. Two deviations from the plan above, both found by
    measuring during implementation rather than before it.*

    *Truncating the sweep at its last zero crossing was DROPPED: it does
    nothing. The sweep now ends at Nyquist, where consecutive samples differ
    only in sign, so every candidate end sample carries the same magnitude and
    there is no zero crossing to cut to. Measured, truncation left the last
    sample at 0.960 and the ringing at -61.0 dB, i.e. unchanged. Farina's
    remedy only applies when the sweep stops below Nyquist.*

    *The fade-out was therefore NOT removed but shortened to 0.5 ms
    (`sweepFadeOutSeconds`). Scanning 0 .. 20 ms across 44.1/48/96 kHz and
    5/10/30 s durations, everything up to about 1 ms sits within 1 dB of the
    optimum while 4 ms and beyond costs real dB. Zero was marginally best on
    average, but it leaves the final sample stepping from ~0.96 to silence,
    which is a broadband click into the tweeter that the deconvolution happens
    to tolerate; 0.5 ms costs at most 0.6 dB and removes it.*

    *Everything else went in as planned: `MeasurementEngine::sweepBandFor`
    derives f2 = Nyquist and f1 = f2/2^P with P from a ~10.5 Hz target (11
    octaves at 44.1/48 kHz, 12 at 88.2/96 kHz), the noise band is pinned to its
    own `noiseF1Hz`/`noiseF2Hz`, and the per-run band goes to `measurement.xml`
    and to `readme_measurement.md`. The analysis side is untouched and old
    folders still deconvolve with the band recorded in their own manifest. The
    Calibration signal combo and the `cal::signal` tooltip now say the sweep
    runs to Nyquist. NOT yet done: the two hardware checks from the caveats
    above, namely a listening check at reduced level and a look at what the
    widened aliasing range does to the H2..H5 curves.*

    *Written up in full in `doc/note_on_pre-ringing_fix/`: the derivations, the
    loopback measurements that separated the three candidate mechanisms, the
    proof that Farina's zero-crossing cut cannot work once the sweep reaches
    Nyquist, and the two rejected options with their numbers.*

- [x] For now the phase switch occurs at the frame level. I am considering adding a phase switch also at the output level. Both can be useful. Everything can be done at frame level except when working with ambisonics: in this case, we could want to tune the system (mainly sub to mains equilibrium) using the phase switch in the outputs, and use the frame switches for the ambisonic decoding matrix.

    *Plan (2026-09-14).*

    **What exists.** `FrameSettings::phaseInvert` is folded into the frame's
    smoothed gain target by `FrameProcessor::applySettings`, so a flip ramps
    through zero without a click. It is serialised as `phaseInvert` on Frame
    nodes, tagged `Ø` in the matrix, toggled with `p`, offered in the frame's
    context menu, and written by the Config tool for negative decode
    coefficients. The output chain (`OutputSettings`: trim, 2-band EQ, delay,
    FIR, analyzer tap) has no polarity. Trim is `MatrixEngine::outputGains`,
    smoothed over 50 ms, while `processOutputChainOnly` (FIR measurement mode
    and the SPL meter) applies the trim unsmoothed. "Invert sub" in both
    analysis panes only changes the design and writes nothing to the matrix,
    so the user currently has to flip the sub by hand.

    Everything here is SuperMoTo-specific, so it all stays in `Source/`.

    **Steps.**

    1. Model (`Model/ConfigModel.{h,cpp}`). Add `bool phaseInvert = false` to
       `OutputSettings`, to the String-free `OutputAudioSettings` and to
       `audio()`, and include it in `isDefault()`. Write and read it as a
       `phaseInvert` property on Output nodes with a default of false, so old
       sessions and presets load unchanged. The property is additive, so no
       `stateVersion` bump is needed.
    2. DSP (`Dsp/MatrixEngine.cpp`). Fold the sign into the `outputGains`
       target where the trim is set, so a flip ramps through zero on the
       existing 50 ms smoothing, as frames do. Apply the same sign in
       `processOutputChainOnly`. As a consequence, "Dry" measurements bypass it
       (the raw speaker as wired), while "FIR" and "System" include it. Nothing
       changes on the audio thread's locking or allocation profile.
    3. Output editor (`Components/OutputEditorComponent.h`). A "Phase invert"
       toggle styled like `FrameEditorComponent`'s `phaseButton`
       (`juce::ToggleButton` with `SuperMoToTheme::accentToggleButton`, since
       these are model settings, not APVTS parameters), placed with the FIR and
       analyzer toggles, plus an `mtx::outPhase` tooltip.
    4. Matrix (`Components/MatrixComponent.cpp`). A `Ø` tag on the output cell
       next to "EQ", `p` toggling it when an output is selected, and a "Phase
       invert" item in the output's right-click menu.
    5. Analysis integration. The natural payoff is letting "Invert sub" write
       the sub's polarity onto its assigned output, in Group analysis at
       "Apply & export" (with a line in the report) and in the single-speaker
       pane alongside "Apply bulk delay". Dry measurements see the physical
       wiring, so fixing it at the output is correct for every configuration
       that feeds the sub.
    6. Config tool. It must keep leaving output polarity alone (it writes
       frames, crossovers and optionally the radius trim and delay). Check at
       implementation time that every output write goes through a
       get-modify-set that preserves the new field.
    7. Docs and tests. Matrix chapter ("What an output contains" and the
       `fig:frame` chain), the gestures table, the Ambisonics section of the
       Config tool chapter (output polarity for the sub-to-mains balance, frame
       polarity for the decoder), the glossary's "Polarity (Invert)" entry, the
       README feature list and the tooltips. In `SuperMoToTests`, a
       serialisation round trip (absent, false, true) and an engine check that
       the output sign flips after the ramp.

    **Decisions to take before implementing.**

    - Step 5: write the sub's polarity automatically, behind a toggle, or not
      at all. Recommended: write it wherever the matching delay is written, so
      "applied" means the same thing for both. The docs then need to warn that
      a frame-level invert on the same sub route would flip it back.
      DECISION: Write the sub's polarity
    - Whether `p` on a selected output is the right key, given it already means
      the frame's polarity.
      DECISION: 'p' can be kept

    *Implemented 2026-09-14, as planned apart from the points below. Not yet
    built or run.*

    *The single-speaker Analysis pane writes nothing. It has no notion of the
    sub's output (its "Apply bulk delay" only ever writes the main's delay), so
    "wherever the matching delay is written" is Group analysis alone. Instead,
    "Export correction IR" with an assigned output ends its status message with
    a reminder to set Phase inv. on the sub output when Invert sub is on, and
    the pane's Invert sub toggle now carries the shared tooltip, which says the
    same.*

    *Group analysis writes the polarity in both directions (Invert sub off
    clears it), because the design is relative to the Dry measurement, which
    bypasses the output chain. The report's Sub section gains an "Output
    polarity" line, and the sub row's output combo has its own `grp::subOutput`
    tooltip, since the speaker rows' one promised a FIR.*

    *UI: "Phase inv." is the first toggle of the output editor's row 1, which
    fills the editor's minimum 320 px exactly. The output context menu puts
    "Phase invert" before "Show on analyzer", as the frame menu does. The
    output strip tag order is Ø, EQ, D. The matrix info text lists the new key
    and menu, and `mtx::framePhase` now points to the output switch.*

    *The engine test needs the real `MatrixEngine`, hence the whole FxmeTools
    module and WDL, so it is a new target `SuperMoToOutputTests`
    (`Tests/OutputPolarityTest.cpp`, registered with CTest as `output`) rather
    than an addition to `SuperMoToTests`, which links no module. It checks the
    state round trip (direct, through XML text, and with the property absent),
    the steady-state sign, the ramp on a flip, and `processOutputChainOnly`.*

    *Docs: matrix chapter (output list, a "Frame or output polarity" paragraph,
    `fig:frame`, gestures table), measurement modes (Dry excludes it, FIR
    includes it), analysis and workflows B/F, group analysis (section renamed
    "The subwoofer gets no correction filter"), a "Subwoofer polarity"
    paragraph in the Config tool's Ambisonics section, the glossary and the
    README, including the test list.*

- [x] In the group analysis pane, we should be allowed to select which measurement runs are to be retained for the analysis. We could have a button, which when clicked shows a window with the list of individual measurements comments in column and a checkbox in front of each. The user can (de)select individually and the calculation is retriggered when the user click OK. It should show a cancel button too.

    *Plan (2026-09-14).*

    **What exists.** Each run in `measurement.xml` is a `<Run>` element with
    `time`, `mode`, `signal`, `durationS`, `levelDb`, an optional `comment` and
    one `<File name=...>` per capture. `readMeasurementFolderInfo` only keeps
    `fileRuns` (file name to `MeasurementRunInfo`), and that struct has no time,
    no comment and no run index. Group analysis loads each channel's files
    sorted by position, and `AnalysisEngine::loadSubFiles` pairs the sub with a
    main by load order (`curves[min(i, n-1)]`). Dry and FIR runs write the same
    `ch<NN>_pos<PPP>.wav` names, so a folder that also holds FIR verification
    runs currently loads them as extra positions of the uncorrected speaker.

    **Steps.**

    1. Manifest reader (project, `Dsp/MeasurementEngine.{h,cpp}`). Add `index`,
       `time` and `comment` to `MeasurementRunInfo`, and a
       `std::vector<MeasurementRun> runs` to `MeasurementFolderInfo` (manifest
       order, each run with its file names). Folders without the XML manifest
       get no run list, and the button below stays disabled for them.
    2. Filtering (project, next to `scanMeasurementFolder`). A pure helper that
       takes the scanned `MeasurementFolderContents` and a set of excluded run
       indices and returns the same structure with those runs' files removed
       from every channel and from the sub. No GUI, easy to cover in
       `SuperMoToTests`.
    3. Pairing by position, not load order. Excluding a run that did not
       measure every channel leaves channels with different position lists,
       and load-order pairing would then anchor a sub file on the wrong main
       position, silently. Build, for each speaker, a sub file list aligned to
       that speaker's own positions (position parsed from the file name, with
       `parseCaptureName` exposed from `MeasurementEngine.cpp`), dropping
       positions one side lacks. `AnalysisEngine` stays unchanged. This also
       fixes the existing case of a position missing from one channel.
    4. Dialog. A checklist popup is not specific to SuperMoTo, so it belongs in
       the FxmeTools JUCE module: `fxme::ChecklistPopup` in
       `lib/FxmeTools/FxmeTools/components/`, rows of text columns with a
       checkbox, All / None / OK / Cancel, `setColours()` like the other
       components, and an async callback returning the checked rows (never a
       synchronous modal loop in a plugin). Launch it the way
       `CalibrationComponent` launches its folder-comment popup
       (`juce::CallOutBox::launchAsynchronously`, `SafePointer` in the
       callback), or through `DialogWindow::LaunchOptions::launchAsync` if a
       callout is too cramped for a long list. Add a `TextEntryFocusFixer` only
       if a text field is ever added. Record the new component in
       `lib/FxmeTools/doc/api-changes.md` and the catalog.
    5. SuperMoTo side (`GroupAnalysisComponent`). A "Runs..." button next to
       "Load measurement folder...", enabled only for a loaded folder with a
       run list and disabled by `setBusy()` while a batch runs, with a `grp`
       tooltip. Columns: run number, date and time, mode, signal, channels
       measured, comment. Its label shows the count when something is
       excluded ("Runs 5/7"). OK is disabled when the selection would leave a
       loaded channel with no file.
    6. Recompute on OK. Apply the filtered lists to each `Entry::files` and to
       the sub, then reuse the `reanalyzeAll()` background path (one job per
       engine, never two engines in one job, per the 2026-07-07 rule). If
       "Compute alignment" had been run, re-run it after the batch so the
       delays and trims do not go stale. Cancel changes nothing.
    7. Report and docs. `report.md` lists the excluded runs (time and
       comment). Document the button in the group analysis chapter (loading
       section) and in workflow F.

    **Decisions to take before implementing.**

    - Should FIR-mode (and System-mode) runs start unchecked on load? They
      measure the corrected chain, so leaving them in biases a correction
      design. Recommended: yes, with the count shown on the button.
      DECISION: yes. Note that in practice, a FIR or system measurement can be done in another folder, and analyzed individually in the single analysis.
    - Where the selection lives. Recommended: in memory for the loaded folder,
      reset by the next folder load, and recorded in the report. The manifest
      is a log of what was measured and should not be rewritten for this.
      DECISION: in memory for the loaded folder
    - Whether the single-speaker Analysis pane gets the same dialog later. The
      helper in step 2 and the popup in step 4 would make it cheap.
      DECISION: Less useful in the single speaker analysis, as the user can simply select the files to load. Do not implement.

    *Implemented 2026-09-14. Not yet built or run. Where it departs from the
    plan above:*

    *Step 3 pairs by run, not by position. Position numbers turned out to be
    counted per channel (`MeasurementEngine::start` takes each channel's own
    max + 1), so a speaker that missed one run is a position behind the sub
    from then on, and position pairing would have been exactly as wrong as
    load order. Every channel measured in one run was measured at the same
    mic position, so `smt::pairSubFiles` pairs files of the same run first,
    then pairs whatever is left in load order (a sub measured in runs of its
    own, files no run lists, folders without a manifest: the old behaviour).
    A speaker keeps all its files, paired ones first; sub files left without a
    partner are dropped rather than anchored on the last speaker file as
    before. `AnalysisEngine` is unchanged. The pairing applies to the folder
    load, to the re-analysis after a window change, to a run selection, and to
    a sub loaded by hand (`SpeakerGroupAnalysis::loadSubFiles` now takes the
    manifest and re-loads a speaker only when its order changes). Run
    information is used only when the files really come from the loaded
    folder, since run numbers are looked up by file name.*

    *Steps 1 and 2 live in a new `Source/Dsp/MeasurementFolder.{h,cpp}`: the
    folder reader moved out of `MeasurementEngine.{h,cpp}` unchanged apart
    from the run log, so it depends on juce_core only and has its own test
    target, `SuperMoToFolderTests` (`Tests/MeasurementFolderTest.cpp`,
    CTest `folder`). `MeasurementEngine.h` includes it, so no include changed
    elsewhere. `MeasurementFolderInfo::runs` is oldest first, with
    `MeasurementRunInfo::number` chronological from 1; `runNumberOf()` gives a
    file's run, 0 for files no run lists. `smt::withoutRuns` takes run
    numbers, 0 included.*

    *Dialog (step 4): `fxme::ChecklistPopup`, header-only in
    `lib/FxmeTools/FxmeTools/components/`, in the umbrella, recorded in
    FxmeTools' `doc/api-changes.md` and in the skill's catalog. Launched as a
    CallOutBox. Only runs that contribute files to the scan get a row (System
    runs write `in<NN>` files, which the scan never loads, so they never
    appear), plus one row for files no run lists when there are any. Columns:
    run, time, mode, signal, channels, comment; a row's tooltip shows it in
    full.*

    *Pane (step 5): "Runs..." sits right of "Load measurement folder..."; the
    rest of row 0 was tightened to keep its width. It reads "Runs 5/7..." when
    runs are left out, is disabled for folders without a run log, and is
    withdrawn when a row or the sub is loaded by hand, since applying a
    selection would replace those files.*

    *Step 6 as planned; "Compute alignment" is re-run only if it had been run
    since the folder was loaded. If the selection leaves no sweep run, the
    Farina estimator is switched off.*

    *Step 7: the report's group settings list the runs left out. Manual:
    group analysis chapter (pairing paragraph and a new "Choosing the
    measurement runs" subsection, `sec:groupruns`) and workflow F. README
    (Part 4, use case E, the test list) and tooltips (`grp::runs`, and
    `grp::loadFolder`, which still named the readme as the source).*

- [x] When closed and reopened, the plugin editor looses all its state. If we were working on an analysis or a group analysis for instance, we loose all our current work. The state of the plugin should be kept outside the plugin editor.

    *Plan (2026-09-14), not implemented yet.*

    **What exists.** The processor owns only the audio side: `ConfigModel`,
    `MatrixEngine`, `MeasurementEngine`, `SplMeterEngine` and the presets.
    Everything else belongs to the editor's view components and dies with the
    editor.

    - Analysis: its `AnalysisEngine` (curves, correction, sub pairing), the
      loaded files, the folder manifest and embedded mic cal, the recommended
      mains delay, every control (window, TF, smoothing, range, level, boost,
      phase, FIR length, Assign to, crossover, Invert sub, Mains delay, Apply
      bulk delay, View, Level, mic cal source) and the plot zoom.
    - Group analysis: `SpeakerGroupAnalysis` (17 engines, files, assignments,
      alignment, sub trim), the `fxme::BackgroundTaskRunner`, the loaded folder
      and its run selection, the prefix, Figures, Preview and every shared
      control.
    - Calibration: folder, comments, mode, signal, duration, level, mic input,
      channel toggles, Sub switch and channel, SPL meter and generator
      controls. Its constructor pushes those controls' defaults into
      `processor.splMeter`, so reopening the editor silently stops a test tone
      that kept playing while it was closed.
    - Config tool: layout, target, order, speaker count, every row (inputs,
      outputs, gains, azimuth, elevation, radius), bass management, crossover,
      the two write toggles.
    - Matrix view: the edited configuration (re-derived from the engaged one),
      the selected frame or output, the analyzer's avg/peak mode.
    - Editor: the window size (always 1280x820 on open). View, compact mode and
      tooltips do survive, but machine-wide in `AppSettings`, so every instance
      shares them.

    Closing the editor during a Group analysis batch destroys the runner:
    `cancelAndWait()` blocks the message thread for up to 5 s on the job in
    flight and discards the completion. For "Apply & export" that can leave IR
    files written, no output updated and no report.

    `getStateInformation` writes a wrapper root `SuperMoToState` around
    `apvts.state`, while presets carry `apvts.state` alone. A second child of
    the wrapper therefore travels with host sessions and never into presets.

    Everything here is SuperMoTo-specific, so it all stays in `Source/`.

    **Steps.** Ownership moves to the processor; the components become views of
    it.

    1. Workspace (`Source/Model/Workspace.{h,cpp}`). A per-instance
       `smt::Workspace` owned by `SuperMoToAudioProcessor`, message thread
       only. A `juce::ValueTree` with one child per pane (Editor, Matrix,
       ConfigTool, Calibration, Analysis, GroupAnalysis) holds the control
       values. Beside it, the live objects no tree can hold: an
       `AnalysisSession` (engine, loaded files, folder info, folder mic cal)
       and a `GroupAnalysisSession` (step 4).
    2. Binding pattern, the same in every pane. The constructor restores each
       control from its tree child with `dontSendNotification` and then
       refreshes the view from the live objects, without re-running any load
       or analysis. Every change handler writes its property. Explicit
       get/set rather than `Value::referTo`: a tree property's Value notifies
       asynchronously by default (`getPropertyAsValue`), so the callbacks
       would land after construction and re-trigger the very loads and
       recomputes the restore is meant to skip. Editable combos
       (range, crossover) store their text, not an id.
    3. Analysis. Move the engine and its companions into `AnalysisSession`;
       `AnalysisComponent` takes a reference. Loads stay synchronous as today.
    4. Group analysis, the largest step. Split `GroupAnalysisComponent`
       (about 1850 lines) into `smt::GroupAnalysisSession` (the group, the
       runner, the loaded folder, the run selection, alignmentComputed, the
       batch status and progress) and the view. The operations move with it:
       folder load, hand loads, re-analysis, run selection, Compute alignment
       and Apply & export. Two things change shape. `pushSettingsTo` reads
       controls today, so the shared settings become a plain struct kept in
       the session (and mirrored into the tree). And the runner callbacks,
       which capture the component, update the session instead and broadcast
       a change (`juce::ChangeBroadcaster`), so a live view refreshes its rows,
       plot, busy state and status, and a reopened one shows a batch still
       running with its progress. Apply & export then finishes with the editor
       closed: the figures are already rendered offscreen from the engines
       (`ReportFigures.h`) and `finalizeApply` only needs the model. The file
       choosers and the Runs popup stay in the view, since they need a
       window, but only start session operations.
    5. Calibration. Keep its controls in the tree. On reopen, restore them
       before the SPL push, so the push re-sends what is already playing
       instead of the defaults. A measurement in progress already lives in
       the processor, and the view's timer picks it up.
    6. Config tool. Layout, target, order, count, toggles, crossover, and the
       rows as one child node per row.
    7. Matrix and editor. Edited configuration, selection, analyzer mode, and
       the window size per instance (the editor constructor sizes itself from
       the workspace).
    8. Session (if chosen in the decisions). `getStateInformation` adds the
       `Workspace` tree as a sibling of `apvts.state` under the wrapper, and
       `setStateInformation` restores it. Engines are never serialized: the
       loaded file paths are, and they are re-analyzed on restore (Group
       analysis in the background, Analysis when its pane is first shown),
       with missing files named in the status line. Some hosts call
       `getStateInformation` off the message thread, so the tree is mirrored
       into a locked snapshot on every change, the way `ConfigModel` is
       mirrored into `apvts.state`. Older plugin versions ignore the unknown
       child; no `stateVersion` bump.
    9. Tests and docs. A workspace round trip (and an absent child loading
       defaults) in `SuperMoToOutputTests` or a new target, and a session
       check of the Group analysis re-analysis on restore if step 8 is done.
       A short section in the hosts chapter on what survives closing the
       editor and saving the session, plus the README.

    **Decisions to take before implementing.**

    - Scope. (a) Editor close and reopen only, in memory. (b) Also saved with
      the host session: control values and loaded file paths, re-analyzed on
      load, never in presets. Recommended: (b), built as (a) first (steps 1-7)
      with step 8 on top, so each half can be tested on its own.
      DECISION: (a)
    - A Group analysis batch running when the editor closes: let it finish
      (recommended, it is what makes Apply & export safe) or cancel it.
      DECISION: let it finish
    - View and compact mode: per instance (recommended, with the machine-wide
      value as the default for a new instance) or machine-wide as now.
      Tooltips stay a machine-wide preference either way.
      DECISION: per instance
    - Calibration test tone on reopen: keep what was playing (recommended,
      it plays on while the editor is closed anyway) or stop it, which is what
      happens today by accident.
      DECISION: keep what was playing
    - Order of work: one pane per commit, Group analysis first since it holds
      the most work and the only background jobs. Recommended.
      DECISION: one pane per commit, group first

    *Progress.*

    - [x] *Group analysis (2026-09-14, not yet built). `smt::Workspace`
      (`Source/Model/Workspace.h`) is owned by the processor and holds
      `smt::GroupAnalysisSession` (`Source/Model/GroupAnalysisSession.{h,cpp}`):
      the group, the settings, the loaded folder and run selection, the
      runner, the batch status and progress, and every operation (folder and
      hand loads, re-analysis, run selection, Compute alignment, Apply &
      export). `GroupAnalysisComponent` only opens the choosers and the Runs
      popup, writes settings, and redraws when the session notifies. A batch
      started with the editor open finishes with it closed, figures and
      `finalizeApply` included. Where it departs from the steps above: the
      settings are a plain struct (`GroupAnalysisSettings`), not a ValueTree,
      since decision (a) keeps everything in memory and a tree would only be
      an indirection; a setting moved while a batch runs is applied when the
      batch ends (it used to wait on a retried timer that died with the
      view); the runner is created by the first batch, so an instance that
      never uses Group analysis (or a host's plugin scan) starts no thread
      pool; the plot's frequency window is kept too.*
    - [x] *Analysis (2026-09-15, not yet built). `smt::AnalysisSession`
      (`Source/Model/AnalysisSession.{h,cpp}`) in the workspace holds the
      engine, the loaded main and sub files, the folder manifest and its mic
      cal, the recommended offset, the status line and `AnalysisSettings`
      (every control, including Assign to, Mains delay, Apply bulk delay and
      the plot's frequency window). Everything stays synchronous, so no
      listener. The exports stay in the view, reading its synced controls.
      One fix on the way: changing the window size or the TF method used to
      drop a loaded sub set (loading the main set clears the pairing); the
      session now re-pairs it after re-analyzing.*
    - [x] *Calibration (2026-09-15, not yet built). `smt::CalibrationSettings`
      (`Source/Model/CalibrationSettings.h`) in the workspace holds every
      control of the pane (mic input, mode, channel toggles, Sub and its
      channel, signal, duration, level, folder, folder and run comments, and
      the SPL meter and generator controls including the last SPL reading),
      plus the status line. A plain struct is enough: the measurement and the
      generator already live in the processor, and both calibrations are
      machine-wide. The view shows these values before its first push to
      `SplMeterEngine`, so a tone left playing keeps playing, and the status
      line shows the measurement engine's instead of the one left if a run
      finished (or the host stopped it) while the editor was closed. The
      folder field keeps what was typed; only the first view of an instance
      fills it with the last browsed folder, as before. Channels the matrix
      lost while the editor was closed are unticked on reopen.*
    - [x] *Config tool (2026-09-15, not yet built). `smt::ConfigToolSettings`
      (`Source/Model/ConfigToolSettings.h`) in the workspace holds the rig
      (layout, Ambisonics order and speaker count), one `Speaker` per row
      (input, output, gain or trim, azimuth, elevation, radius), the target
      configuration, bass management, crossover, the two radius toggles and
      the status line. A row's name, and whether it is the sub, still come
      from the layout, so only the editable values are kept. The rows start
      over from the rig's defaults when the layout, the order or the count
      changes, as before, and are rebuilt as they were left when the editor
      reopens. Double-click still returns a row control to the rig's default.
      `ConfigToolComponent` now takes the settings next to the model.*
    - [x] *Matrix and editor (2026-09-15, not yet built).
      `smt::EditorSettings` (`Source/Model/EditorSettings.h`) in the
      workspace holds the page, the compact mode, the full layout's window
      size, the configuration shown in the matrix, the selected frame or
      output (and which detail editor is up), and the analyzer's view. Page
      and compact mode are still written machine-wide as they change, and
      only a new instance's first editor reads them from there; tooltips stay
      machine-wide. The edited configuration is kept, except that a
      configuration engaged while the editor was closed (automation, a
      preset) is shown instead, since an open editor follows engagement; the
      frame selection is then dropped, as switching configuration does. A
      selection the matrix no longer has (smaller size) is dropped. The window
      size, the engaged set and the analyzer view are taken in the editor's
      destructor, since nothing notifies their changes. The analyzer view
      needed an FxmeTools addition: `fxme::SpectrumDisplay::ViewState` with
      `getViewState()` / `setViewState()` (detector, window size, averaging,
      dB and frequency windows, traces hidden from the legend), recorded in
      FxmeTools' `doc/api-changes.md`. The Calibration pane's spectrum uses it
      too (`CalibrationSettings::spectrumView`), which that pane's step had
      missed.*
    - [x] *Docs (step 9): not needed. Keeping the editor's state is what a
      user expects of a plugin, so the manual does not describe it.*

- [x] Average the positions in power, not as complex vectors: the correction is
  inverting a cancellation that exists at no microphone position.

    Found while chasing a reported oddity, and it is the real cause of the
    harsh treble — it demotes the two items below. With the ch03 set of
    `supermoto_paper6` loaded in Analysis, every individual curve is flat
    within a couple of dB above 5 kHz, yet the average shows a 12 dB dip
    around 13.35 kHz. That is not a plotting bug and not a delay bug:
    `AnalysisEngine::computeAverage()` sums the *complex* responses, so the
    curve drawn is |mean H|, not mean |H|, and the two part company as soon as
    the per-position phase stops agreeing. At 13.35 kHz the ten positions'
    phases are 145, 177, 169, 163, -6, 0, -57, 28, -158 and 178 degrees: they
    cancel.

    How far apart the two averages are on that set (per bin, before smoothing;
    |R| is the mean unit phasor over the ten positions — 1.00 = every position
    shares a phase, 0.32 = what ten random phases give; dB = what the complex
    average loses against the power average):

              Hz     30     50     80    125    200    315    500   1250   2000   5000   8000  12.5k    16k
        ch03 |R|   1.00   0.99   0.99   0.99   0.99   0.93   0.91   0.77   0.62   0.79   0.55   0.27   0.38
        ch03  dB   -0.0   -0.1   -0.2   -0.1   -0.2   -1.1   -1.1   -2.8   -4.1   -2.3   -5.3  -11.5   -8.2
        ch04 |R|   1.00   0.98   0.99   0.99   0.96   0.91   0.95   0.81   0.66   0.60   0.54   0.22   0.30
        ch04  dB   -0.0   -0.2   -0.2   -0.1   -0.5   -1.2   -0.8   -2.1   -3.7   -4.5   -5.7  -13.3  -11.7

    The knee sits between 200 and 315 Hz. That is the room's transition
    frequency, arrived at from the data rather than from a rule of thumb, and
    it happens to land exactly where EBU Tech 3276 says to stop equalising.
    Below it the complex average is right and costs nothing. Above 2 kHz the
    vectors are near-random and the average reads 4 to 13 dB below every curve
    that went into it.

    What it costs us:

    - The correction inverts that loss. Designed from the power average
      instead, the same ten positions ask for **+1.6 dB (ch03) and +2.8 dB
      (ch04)** of net treble lift over the midrange, against +5.7 and +8.1 dB
      from the complex average. The difference — about 4 dB on ch03 and 5 dB
      on ch04 — is what the shipped FIRs are boosting for no acoustic reason,
      out of the +4.8 / +5.6 dB of net treble lift they carry.
    - Where the vectors nearly cancel, the displayed level is not a stable
      property of the measurement: at 13 kHz it moves 1.7 dB with the
      smoothing setting alone, and the depth of the dip follows the window
      size and the mic calibration too. That is most of why the Analysis and
      Group panes disagree at the ends (next item).
    - Level matching inherits a smaller version of the same bias: over
      500 Hz - 2 kHz the L/R difference reads +0.22 dB from the complex
      average and +0.47 dB from the power average.
    - `computeAverage()` already knows better for the harmonic curves, which
      it power-averages, with a comment saying that a complex average would
      cancel because distortion phase is not coherent between positions. The
      argument is the same for the fundamental above the transition frequency;
      it was just never applied there.

    **Fix.** Magnitude from the power average, phase from the complex one:

        A(f) = sqrt(mean |H_i(f)|^2) * mean(H_i(f)) / |mean(H_i(f))|

    Every phase feature survives (the LF phase correction, the subwoofer
    all-pass, the minimum-phase render), and the magnitude stops depending on
    how well ten microphone positions happen to agree. Steps:

    1. `AnalysisEngine::computeAverage()`: accumulate `sum(H)` and
       `sum(|H|^2)` in one pass and combine as above. Same for
       `computeSubAverage()`.
    2. `applySmoothing()`: smooth magnitude and phase **separately**, or the
       complex moving average puts the cancellation straight back — at 13 kHz
       a 1/3-octave window spans 3 kHz, over which the residual phase turns
       several times. Smooth the real magnitude, smooth the complex average
       for its argument alone, recombine. Individual curves can stay as they
       are: per position, complex smoothing costs 0.1 to 0.6 dB.
    3. Re-check `getBandLevelDb()` and the group's level matching against the
       new average (they should get slightly more consistent, not less).
    4. Worth adding while in there: expose |R(f)| as a "positional coherence"
       trace on the plot. It says where the correction is meaningful, it is
       nearly free to compute, and it makes the transition frequency visible
       instead of assumed. A natural follow-on is to taper the *phase*
       correction with it, so phase stops being corrected exactly where the
       positions disagree about it.

    **Verification.** With the fix, the ch03 average should read within about
    1 dB of every individual curve from 5 to 16 kHz instead of 12 dB below
    them; the curve should barely move when the smoothing fraction changes
    (measured: the power average moves 0.5 dB between 1/12 and 1 octave where
    the complex one moves 1.7 dB); the two panes should then agree; and the
    exported FIR for ch03 should lose about 4 dB of treble boost. Re-run
    `doc/experiments/sub_alignment_validation.py` afterwards: it mirrors the
    engine and will need the same change, and the campaign-6 A/B numbers in
    the paper should be re-derived from it (the subwoofer work is below
    200 Hz, where |R| is 0.99, so the conclusions should not move — but that
    has to be shown, not assumed).

    *Implemented 21 Sep 2026, not yet built. `computeAverage()` and
    `computeSubAverage()` accumulate both the complex sum and the power sum in
    one pass and combine them as above; `applySmoothing()` gained a
    `smoothMagPhase` lambda that smooths the averages' magnitude and phase
    separately and leaves the individual curves on plain complex smoothing.
    `getBandLevelDb()` needed no change — it was already a mean-power over the
    band, and it now reads a magnitude that is a mean power over the positions
    too, which is what its own comment argues for. The offline mirror follows:
    `position_average()` and `smooth_average()` in
    `doc/experiments/sub_alignment_validation.py`, used there and in
    `make_paper_figure.py`.*

    *Verified on the campaign-6 ch03 set through the mirror. The average now
    sits inside the spread of the curves it came from at 13.35 kHz (curves
    -3.3 to -1.4 dB, average -1.6, where it used to read -12.0). It barely
    depends on the smoothing fraction any more: 0.6 dB from 1/12 to 1 octave
    against 1.7 dB before. The designed correction loses 4.8 dB (ch03) and
    4.9 dB (ch04) of mean gain over 2-16 kHz, and its net treble lift over the
    midrange falls from +4.8 / +5.6 dB to +1.0 / +2.3 dB.*

    *One consequence to expect on re-export, beyond the treble: the 200 Hz -
    2 kHz reference the correction normalises to was itself being cancelled
    by 1 to 4 dB, so the bass was being cut against a reference that sat too
    low. The new design gives about 2.5 dB more at 63-125 Hz relative to the
    midrange, and the modelled crossover-band level rises with it, from
    -0.7 dB to +1.1 dB at 8192 taps. The subwoofer level and trim were fitted
    against the old averaging and should be checked again after re-exporting.*

    *Left to do from this item: the positional-coherence trace (step 4) is not
    implemented — nothing computes or displays |R(f)| yet, and the phase
    correction is still not tapered with it. And the paper's numbers move; see
    the item below.*

- [x] Re-derive the paper's Table 1 and Figure 1 from the fixed averaging.

    The measured A/B is untouched, as it should be — it compares two measured
    runs and never goes near the averaging: still +0.78 dB (main 1) and
    +0.88 dB (main 2) at 8 of 8 positions, same per-position spread, same
    polarity checks, same model-vs-measured RMS, same predicted and measured
    crossover-band levels. What moves is everything the offline design feeds.

    *Done for everything that is purely offline (21 Sep 2026). Table 1 carries
    the new leave-one-out scores, `doc/figures/validation.png` is regenerated
    — its panels (a) and (b) redraw the same numbers, since they compare
    measurements against the exported filters and never touch the averaging;
    only panel (c) moved — and the paragraphs that quote the table, plus the
    Figure 1(c) caption, follow. The README's design-set table and its bullets
    are updated with it. The paper builds at 11 pages, no warnings.*

    *What the change did to the table: magnitude only -1.50 -> -1.21, magnitude
    + arrival-time delay -0.41 -> -0.38, magnitude + crossover-band delay
    -1.21 -> -0.63, all-pass -0.45 -> -0.42, wrong polarity -5.2 -> -6.1. The
    ordering and every conclusion hold: the all-pass and the arrival-time
    delay still land within 0.05 dB of each other and each still wins at five
    of the ten positions on both mains, the polarity argument gets stronger,
    and leave-one-out still matches in-sample (to 0.03 dB now). The gap
    between the two delay estimators narrows from 0.8 dB to 0.25 dB, because
    the power-averaged subwoofer magnitude is a better weight for the
    crossover-band phase fit (29.22 -> 29.80 ms against an arrival difference
    of 30.49 ms), so the "worth most of a decibel" line in the Figure 1(c)
    caption became "sensitive to which of the two it is given where the
    all-pass is not".*

- [x] Group analysis: the per-speaker Load button does less than every other
  load path.

    `GroupAnalysisSession::loadSpeakerFiles()` pushes the current settings and
    the sweep identity, but unlike `loadMeasurementFolder()` next to it, and
    unlike `AnalysisSession::loadFiles()`, it does not:

    - read the folder's `measurement.xml` and adopt the embedded mic
      calibration (`readMeasurementFolderInfo` + `folderMicCal` +
      `micCalSource = 2`), and
    - call `updateSweepAvailability(files, true)`, which is what switches the
      TF method to the Farina deconvolution when the manifest carries the
      sweep identity.

    So the same files loaded through that button are analysed with whatever
    mic calibration and TF method the pane was last left on, while the
    Analysis pane adopts both from the folder. `loadSubFiles()` has the same
    gap. What each is worth, measured on ch03 of campaign 6 (dB, relative to
    the 200 Hz - 2 kHz mean):

    - mic calibration on vs off: +2.0 dB at 25-63 Hz, -2.4 dB at 8 kHz,
      -1.3 dB at 13 kHz. The obvious suspect for a curve that differs at both
      ends.
    - TF method, Welch vs Farina: 0.2 dB or less everywhere above 40 Hz,
      0.8 dB at 25 Hz. Not the explanation, but still an inconsistency.
    - window size 16384 vs 131072: nothing above 40 Hz, 5 dB at 25 Hz.
    - HF smoothing 1/6 vs 1/3 octave: about 1 dB at 2-4 kHz, and up to 1.7 dB
      at 13 kHz — but that last one only because of the item above.

    Fix: give the per-speaker and sub load paths the same folder-info work the
    folder load does, so every path through the pane analyses a folder the
    same way. Then the two panes can only disagree if a control really is set
    differently, and the first item removes the rest.

    *Done 21 Sep 2026, not yet built. `GroupAnalysisSession::adoptFolderInfo()`
    reads the manifest next to a hand-picked set and takes the two things that
    belong to the measurements rather than to the pane: the embedded
    microphone calibration (selecting `Folder mic cal`, still overridable) and
    the sweep identity, which selects the Farina deconvolution through
    `updateSweepAvailability()`. Both `loadSpeakerFiles()` and `loadSubFiles()`
    call it, then `applySettings()` rather than pushing to their own engine
    alone, since both settings are shared by the whole group.*

    *Two details that took some thought. A folder with no `measurement.xml`
    adopts nothing rather than clearing what is in force, so picking a row out
    of a pre-manifest folder does not silently drop the calibration the rest
    of the group is using. And when the manifest does change the
    transfer-function method, every set already loaded was analysed with the
    other one, so that case re-runs the lot through `buildReloadJobs()`
    instead of just the row being loaded — the row's files are assigned on the
    message thread first, since the jobs read them. The status line says which
    happened, and reports the adopted calibration the way the folder load
    does.*

    *`doc/chapters/group-analysis.tex` gains a sentence on it: the folder
    travels with the files whichever button brings them in. The manual builds
    at 77 pages with no unresolved references.*

- [x] A string description should be associated to each output frame, at the preset level (e.g. "[Speaker brand and model] Left", "Surround Right", etc.). This string should be edited as a one-line string entry in the component below the Fourier analysis graph (OutputEdidorComponent.h). The measurement run whould save that field in the produced .xml readme.md files. When passing the mouse over an output frame, a tooltip shoud be shown everytime, showing "Output n: [string]" + eventual info on delay management. When passing over a an output button in the Measurement & Calibration pane, the tooltip "Output n: [string]" should be shown.

    *Done 21 Sep 2026, not yet built. `OutputSettings::description` in
    `Source/Model/ConfigModel.h`, saved in the model's ValueTree and so in
    presets and sessions, and kept out of `OutputAudioSettings` for the same
    reason `firPath` is: assigning a juce::String on the audio thread can call
    free(). Every other writer of an output starts from `getOutput()` and
    modifies, so none of them erases a name.*

    *Edited in `OutputEditorComponent` as a "Description" row under the title,
    written on every keystroke rather than on Return, so a name left unfinished
    is still the one a preset save or a measurement run picks up. The refresh
    path never types over the caret (`hasKeyboardFocus`), except when the
    selected output changes, where the new output's name has to replace what is
    shown. The row costs the 2-band EQ 28 px of the panel's fixed 280, which it
    has: its knobs clamp at 54 px and had 156.*

    *The matrix tooltip now always says something — "Output 3: Genelec 8030
    Left", with the delay and latency-compensation paragraphs after it when
    there are any, where before it said nothing at all unless there were. The
    Calibration pane's channel squares name themselves the same way, refreshed
    from `modelChanged()`, which is how a rename in the matrix reaches them,
    and say "Input n" in System mode, where the numbers mean inputs.*

    *A run writes the names into both manifests:
    `<Channel number="3" description="..."/>` in `measurement.xml`, and
    "Channels: 3 (Genelec 8030 Left), ..." plus the per-run "Channels measured"
    line in `readme_measurement.md`. Two things that needed care there. The
    folder's channel list mixes `chNN` captures with the `inNN` ones a System
    run writes, so only the former (and the sub) can carry an output's name.
    And a run that cannot name a channel must not erase a name an earlier run
    wrote, so the descriptions are carried over from the previous manifest the
    way the calibration record already is.*

    *Documented in `doc/chapters/matrix.tex` (the output chain) and
    `doc/chapters/measurement.tex` (what the manifest records). Manual builds
    at 77 pages, no unresolved references. Not done, and not asked for: nothing
    reads the description back out of a manifest — `MeasurementFolderInfo`
    still ignores the attribute, so Group analysis cannot yet label its rows
    with it.*

- [x] Monitor target curve ("house curve"): a selectable gentle downward tilt
  on the monitor path, so that a correction designed against the measured
  in-room response does not end up sounding harsh.

    **The practice is real.** A loudspeaker whose direct sound is flat does
    not measure flat in a room: its directivity index rises with frequency
    (ITU-R BS.1116-3 §7.2.2.2 even *requires* the monitor's DI to rise
    smoothly, 6-12 dB over 500 Hz - 10 kHz), so the reverberant contribution
    to a steady-state, spatially averaged measurement falls as frequency
    rises, and absorption in a treated room takes more off the top. The ear
    follows the direct sound and the first arrivals; the microphone average
    follows the steady state. Flatten the steady state and you have raised the
    direct sound in the treble by the difference. Both listening standards
    make room for the tilt and neither asks for it to be removed: EBU Tech
    3276 Fig. 2 holds the upper limit flat at +3 dB but lets the lower limit
    fall at **1 dB per octave** above 2 kHz (-6 dB at 16 kHz), and ITU-R
    BS.1116-3 Fig. 2 widens its -3 dB limit downwards at **1.5 dB per octave**
    above 2 kHz. Preference work puts the middle of the range lower still: the
    Harman in-room target is a roughly 1 dB/octave fall across the band with a
    low shelf under ~105 Hz. So 0 dB at the bottom to -3...-6 dB at the top is
    a conservative reading of the literature, not an eccentric one.

    **How much of it applies here (revised 21 Sep 2026).** The first version of
    this entry read the tilt straight off the engine's averaged curves and
    concluded that the room fell 4 to 15 dB above 2 kHz and that the FIRs were
    putting all of it back. Most of that fall was the complex-averaging
    artefact of the first item above, not the room. With the positions
    power-averaged instead, the same campaign-6 set (`supermoto_paper6`,
    10 positions, mic calibration divided out, 1/6-1/3 octave smoothing,
    normalised to the 200 Hz - 2 kHz mean) gives:

                       40     63    125    250    500     1k     2k     4k     8k  12.5k    16k
        ch03 power   -7.3   +1.4   +2.3   +0.8   -0.2   +2.3   -2.6   -0.9   -1.5   -1.8   -1.7
        ch04 power   -5.8   -0.4   +1.9   +1.1   +3.1   +1.5   -2.8   -5.0   -3.0   -2.7   -3.2
        ch03 cplx    -5.1   +4.0   +4.8   +3.1   +1.4   +3.8   -4.3   -2.9   -4.1  -11.0   -7.2
        ch04 cplx    -3.5   +2.0   +4.3   +2.7   +4.9   +2.7   -4.6   -9.5   -6.1  -14.9  -12.2

    So the room's real steady-state fall from the midband to 16 kHz is about
    1.5 dB on ch03 and 3 dB on ch04 — the gentle tilt the literature describes,
    not the cliff the plots show. The exported FIRs
    (`Mirage_linear/Mirage_panneaux_paper_speaker{1,2}_correction.wav`)
    nevertheless carry

        band gain      63    125    250    500     1k     2k     4k     8k  12.5k    16k
        speaker 1    -4.4   -3.4   -2.7   -1.3   -3.3   +5.5   +3.0   +4.1   +8.7   +7.0  dB
        speaker 2    -2.7   -4.4   -0.1   -4.7   -2.6   +5.4   +8.3   +6.0   +9.6   +9.1  dB

    i.e. +1.5 dB power-averaged over 200 Hz - 2 kHz against +6.3 dB over
    2 - 16 kHz for speaker 1 (+2.2 / +7.8 for speaker 2), a net treble lift of
    **+4.8 and +5.6 dB** over the midrange — of which about 4 dB (ch03) and
    5 dB (ch04) is the averaging artefact and the remaining 1.5 to 3 dB is the
    room, which is what this item is for. The verification set (`Mirage_linear_test`, 8 positions)
    shows the correction did what it was asked: the corrected system measures
    flat within about +/-2.5 dB from 63 Hz to 16 kHz, in the same complex
    average, which is the condition every source above says will sound bright.

    Practical order, then: fix the averaging first, re-export, listen. What is
    left to dislike is what this item addresses, and it should be worth about
    3 to 6 dB across the band rather than the 10 dB the first reading
    suggested.

    *Side note, not a target-curve matter:* the two mains really do differ, by
    about 4 dB at 4 kHz in the power average (ch04 is the dull one). No global
    curve fixes that. Worth checking toe-in, the panels and the tweeter axis
    before blaming the electronics.

    **References** (all checked, numbers taken from the documents):

    - EBU Tech 3276 (2nd ed., 1998), *Listening conditions for the assessment
      of sound programme material*, §2.4 and Fig. 2. The tolerance mask above,
      plus the note that matters here: "To avoid degrading the quality of
      reproduction, electrical equalization should be used carefully. It is
      advisable to make the corrections in the low-frequency range (f < 300 Hz)
      only. All channels should be adjusted in the same way."
    - ITU-R BS.1116-3 (2015), §8.3.4.1 and Fig. 2 (the mask), §7.2.2.2 (the
      rising directivity index that causes the tilt).
    - F. E. Toole, *The Measurement and Calibration of Sound Reproducing
      Systems*, JAES 63(7/8), pp. 512-541, 2015 — the case against equalising
      a steady-state room curve flat, and what to equalise instead.
    - F. E. Toole, *Sound Reproduction: The Acoustics and Psychoacoustics of
      Loudspeakers and Rooms*, 3rd ed., Routledge, 2017 — the long version,
      chapters on room curves and on what a room curve does and does not tell
      you.
    - S. Olive, T. Welti, E. McMullin, *Listener Preferences for In-Room
      Loudspeaker and Headphone Target Responses*, AES 135th Convention, New
      York, Oct. 2013, paper 8994 — the preference experiment behind the
      Harman target (about -1 dB/octave with a bass shelf near 105 Hz).
    - I. Allen, *The X-Curve: Its Origins and History*, SMPTE Motion Imaging
      Journal, July/Aug. 2006, and the standards it describes (SMPTE ST 202,
      ISO 2969: -3 dB/octave above 2 kHz, rooms over 125 m3). Cited as a
      warning, not a model: that curve is for large rooms, and Allen documents
      what happened in the 1980s when small dry mix rooms were tuned to it —
      material sounded dull there, engineers compensated, and the mixes came
      out bright. A small room needs *less* tilt than a cinema, and the useful
      idea from that paper is that the turnover frequency, not the slope, is
      what should move with room size and listening distance.

    **Decision: a live layer on the monitor path, not baked into the FIR.**
    Reasons, in order of weight:

    1. It is taste and room, not calibration. It has to be turnable while
       listening; a redesign-and-reload round trip is the wrong instrument for
       judging 2 dB of treble.
    2. It must apply to every output, including the ones with no FIR (the
       subwoofer has none). Identical filtering everywhere leaves the
       main/sub relative phase exactly as designed, which is what the
       campaign-6 all-pass work went to some trouble to get right; a tilt
       baked into the mains' FIRs only would put a small but real phase error
       back at the crossover. It is also what EBU 3276 asks for in so many
       words ("All channels should be adjusted in the same way").
    3. It cannot get out of step with a batch of FIRs exported under some
       earlier target, so there is no way to apply the tilt twice.
    4. It costs 4 biquads per output, next to the 4 the output EQ already
       allows, and nothing next to the convolution.

    The one thing the layer costs us is that the Analysis pane's "corrected"
    prediction would no longer be what the room does. That is fixed by
    teaching the *plots* about the target (step 6 below) — never the exported
    correction.

    **The curve.** One global definition, used by the DSP and by the plots:

    - `tiltDb` (0 ... -12 dB, default -4): the total drop from 20 Hz to
      20 kHz, straight in dB against log f. -4 dB is -0.4 dB/octave; -10 dB is
      the Harman slope.
    - `turnoverHz` (0 = straight line over the whole band, the default;
      otherwise flat below it and the whole drop spread over
      `[turnoverHz, 20 kHz]`, the EBU/Allen shape). Allen's point about the
      turnover moving with room size lives here.
    - `bassDb` / `bassHz` (default 0 dB, 105 Hz): the optional Harman-style low
      shelf, one `fxme::BiquadCoeffs::lowShelf`.
    - Offset so the power mean over 200 Hz - 2 kHz is 0 dB — the same
      reference `AnalysisEngine::recomputeCorrection()` normalises to, so the
      plots and the filter agree — and then offset again so the maximum gain is
      0 dB. Attenuation only: the layer can never clip an output, and the
      master level makes up the loudness.
    - `on`: a bypass, because the whole point is the A/B.

    **Realisation.** Interleaved pole/zero sections, not shelves: section k is
    `(1 + s/z_k)/(1 + s/p_k)` with `z_k = p_k * 10^(T/20N)`, poles geometric
    over the band widened by two octaves at each end so the line stays straight
    to 20 Hz and 20 kHz. Measured against the ideal straight tilt (a dozen
    lines of numpy, worth redoing while implementing): 6 sections = 3 biquads
    give +/-0.03 dB at
    -6 dB of tilt and +/-0.045 dB at -10 dB. For comparison, one RBJ S=1 shelf
    is +/-1.2 dB off a straight line at only -3 dB of tilt, and two are
    +/-0.26 dB, so the obvious "just use a high shelf" is not good enough.

    **Steps.**

    1. `Source/Dsp/TargetCurve.h` (new): the struct above, `targetGainDb(curve,
       f)` (the definition — one function, used by the plots and by the test)
       and `buildTargetCascade(curve, sr, biquads, maxBiquads)` (the
       realisation), in the shape of `BandFilter.h`'s `buildBiquadCascade`.
    2. `Source/Model/ConfigModel.h`: one `TargetCurve` for the whole plugin
       (not per output — identical everywhere is the point), in the ValueTree
       so presets and sessions carry it, version counter bumped on edit, and
       carried in `tryCopyForEngine` so the engine pulls it like everything
       else.
    3. `Source/Dsp/OutputProcessor.h`: a second cascade (`targetBiquads[4]`,
       `activeTargetBiquads`), rebuilt in `applySettings()` when the curve
       changes, run before the delay, skipped entirely when the curve is off,
       reset with the rest. Note this gives the right behaviour in the
       Calibration pane for free: `processOutputChainOnly()` runs
       `OutputProcessor` only when `applyFir` is true, so a "Dry" measurement
       bypasses the target and an "output + FIR" measurement includes it.
    4. `Source/PluginProcessor.cpp`: an `AudioParameterBool "Target"` next to
       Mute/Dim/Mono, so the bypass can be automated or bound to a key for the
       A/B. The shape stays in the model, not in APVTS.
    5. UI: on/off plus the tilt in the monitor row; turnover and bass shelf in
       a small panel with a drawing of the resulting curve.
    6. `AnalysisEngine` / `GroupAnalysisSession` / the plots: draw the target
       as a dashed line, and add it to the *predicted* corrected response
       (`getCorrectedDb`, `renderCorrectedIR`, `renderSystemIR`) so the
       prediction is what will be heard. It must not enter `correction`: the
       exported FIR stays the pure correction. Name the target in the markdown
       report and in the figure legends.
    7. Manual and tooltips, with one sentence of the why and a pointer to EBU
       3276.

    Optional, later, off by default: a "bake the target into the exported FIR"
    checkbox, for handing a calibration to a rig that has no SuperMoTo. If it
    is ever added, the exported file has to be marked so it cannot be loaded
    with the live layer on as well.

    **Verification.**

    - A test next to `Tests/FractionalDelayTest.cpp`: the cascade against
      `targetGainDb()` to 0.05 dB from 20 Hz to 20 kHz, at -1, -4 and -10 dB of
      tilt and at 44.1, 48 and 96 kHz; unity when off; maximum gain never
      above 0 dB.
    - Measure it: one main in "output + FIR" mode with the target on and off,
      and check the difference against the designed curve.
    - Then the real one: re-measure the room and confirm the in-room average
      now falls by the intended amount, and listen.

    **Settle when implementing.** Whether the subwoofer output gets the curve
    too. It should: with the 200 Hz - 2 kHz normalisation a -4 dB straight tilt
    is within about 1 dB of unity below 100 Hz, and an identical minimum-phase
    filter on both sides of the crossover cancels out of the main/sub phase
    difference, while mains-only would not. If the bass level then wants
    changing, that is what `bassDb` is for.

    *Done 21 Sep 2026. Builds clean, and `SuperMoToTargetTests`,
    `SuperMoToOutputTests` and `SuperMoToDelayTests` pass. Built as planned,
    with the decisions the plan left open settled as follows.*

    *`Source/Dsp/TargetCurve.h` holds both halves, and they are the same
    object: the curve is a cascade of first-order pole/zero sections, and the
    filter is those same sections discretised, so the plot and the outputs
    cannot drift apart. That replaced the plan's "definition = an ideal line,
    realisation = a fit", which broke on the turnover: an ideal knee is
    something six gentle sections cannot make, and the two disagreed by 1.8 dB
    around the corner — with the filter carrying gain, which the whole
    attenuation-only argument forbids. Defining the curve as what the sections
    do fixes both.*

    *Measured on the shipped code rather than on the analog sketch in the plan.
    The tilt is straight to 0.011 dB (-3 dB), 0.022 (-6) and 0.036 (-10)
    against a least-squares line, and delivers the fall it is asked for to
    0.05 dB. The discretisation is the only thing between the plot and the
    filter: 0.00 dB to 1 kHz, 0.04 at 5 kHz, 0.18 at 20 kHz for a -6 dB tilt
    at 44.1 kHz, and 0.37 around 14 kHz for a turnover, whose slope is
    steeper. The plan's "+/-0.03 dB" was an analog-domain figure and did not
    survive the bilinear transform at the top of the band; everything else
    held. `Tests/TargetCurveTest.cpp` (target `SuperMoToTargetTests`,
    `ctest -R target`) pins all of it at 44.1, 48 and 96 kHz, including that no
    curve has gain anywhere in the band.*

    *The chain is as planned: `TargetCurve` in the ConfigModel (saved with
    sessions and presets, kept out of `OutputAudioSettings` like `firPath`),
    an `AudioParameterBool "Target"` for the engagement, four biquads in
    `OutputProcessor` after the output EQ and before the delay, built once by
    `MatrixEngine::updateTargetCascade()` and pushed to every output. One
    thing the plan did not have: engaging steps the level by a decibel or two,
    and this is a control made to be flicked while listening, so the stage
    fades over 50 ms (`OutputProcessor::setTargetEngaged`) rather than
    switching. The filter keeps running through the fade and is reset when it
    comes back from fully bypassed, so no stale tail escapes.*

    *The GUI is its own view, "Target", second in the switcher after Matrix
    (the View enum appends it, so a saved page index still opens what it used
    to). `Source/Components/TargetCurveComponent.h`: the bank on the left, a
    plot on the right, the four values under both, and the caveat across the
    bottom — a target curve only means something on a system that has already
    been measured, corrected and aligned. The plot subclasses
    `fxme::SpectrumDisplay` and draws through `paintOverTraces()`, which needed
    one addition to FxmeTools: `setBadgesVisible(false)`, since a display with
    no taps has nothing its fft/avg/detector badges can describe. That is
    additive, defaulted on, and recorded in FxmeTools' `doc/api-changes.md` —
    the submodule has its own commit to make.*

    *The bank is `Source/Model/TargetCurveStore.h`: one xml file per curve in
    `<app data>/FXMechanics/SuperMoTo/target_curves`, beside the preset folder.
    Six factory curves (Flat, Gentle -3, Moderate -4, Strong -6, Harman-like,
    Flat to 1 kHz -6), rewritten if the folder loses them and not editable
    through the GUI; Save, Save as, Rename and Delete act on the user's.
    fxme::PresetManager was not reused: it round-trips the whole APVTS, where
    a curve is four numbers that must be swappable without touching anything
    else.*

    *Three things the compiler and the test found, all now fixed: `fxme::FxmeButton`
    is a wrapper component, so a tooltip goes on the `button` inside it rather
    than on the wrapper; `Named` is ambiguous in a JUCE translation unit, so
    the test's little struct is `NamedCurve`; and the plugin target carries
    `juce_recommended_warning_flags`, which includes -Wfloat-equal, so the
    curve compares its stored values with `juce::exactlyEqual`. The test also
    caught a claim that was too strong: a turnover's knee is rounded rather
    than square, so a fall measured from the turnover itself comes out about
    0.1 dB short of what was asked for. That is the smooth knee working as
    intended, and it is what the header, the manual and the test now say.*

    *Documented as a chapter of its own, `doc/chapters/target-curve.tex`,
    between the matrix and the config tool, with EBU Tech 3276, BS.1116-3,
    Toole 2015, Olive 2013 and Allen 2006 added to the bibliography. Manual
    builds at 80 pages with no unresolved references. Still not done, and
    still optional: baking the target into an exported FIR for a rig that has
    no SuperMoTo.*

- [x] Splash screen: check `Source/Components/SplashScreenComponent.h` and include it in the plugin project.

    *A drawn card, 600x380, centred over the editor and holding the acoustic
    blueprint grid, the product name and tagline, the mains-versus-sub wave
    pair pulling into phase over two seconds, and the FX-Mechanics logo above
    a link to fx-mechanics.com. The only artwork is `logo686.png`, the one
    the top bar already draws.*

    *Kept from the draft: the whole look. Fixed in it: a timer that never
    stopped (it repainted at 60 Hz for the life of the editor once the card
    had run), no way for the owner to learn that it had finished, a
    hard-coded "v1.0.0 Pro" where the top bar reads `ProjectInfo`, that same
    version tag drawn in the middle of the card because its strip was taken
    after the footer had been removed, a silent font fallback (neither Space
    Grotesk nor Inter is installed here, so the design was rendering in
    whatever the system picked -- `styledFont()` now asks
    `findAllTypefaceNames()` and falls back on purpose), polar grid lines
    with a hard-coded 350 px reach, a zero-width rounded rectangle, a missing
    licence header, and `juce::DegreesToRadians` for `degreesToRadians`. The
    animation is driven from seconds since `show()` rather than from the
    millisecond counter, which at that magnitude had no precision left for a
    smooth wave.*

    *The progress bar and its five invented loading steps are gone: nothing
    in the startup takes 2.5 seconds and nothing the steps named was running
    while they were shown. What is left is an opening, and it claims
    nothing.*

    *Visibility, all of it the editor's: at startup, once per plugin instance
    (`smt::EditorSettings::splashShown`, which lives on the processor and is
    not saved with the session) and only from the full layout, where it goes
    by itself after three seconds; and on a click on the top bar's company
    logo, where it stays until it is clicked. The logo is painted, not a
    child component, so the editor hit-tests it itself -- `topBar::logoRect()`
    is now the one place its rectangle is written, used by both `paint()` and
    `logoHitArea()`. Collapsing to a compact layout takes the card down: it
    is wider than either compact window. Clicking the card dismisses it,
    except on the logo and the address, which open the site and leave it up.*

    *One latent bug found on the way in: the constructor clamped the restored
    page with `jlimit (0, 5, state.view)`, a literal left behind by the
    Target view being appended to the enum, so the editor could never reopen
    on Target. Clamped against `View::targetCurve` now.*

- [x] Manual: give some guidance on choosing how much tilt, and check
  whether a quantitative criterion exists (direct-to-reverberant level, or
  similar).

    *New section, `doc/chapters/target-curve.tex` §"Choosing how much
    tilt", between the "why a flat response is wrong" argument and the
    curve parameters. A qualitative rule (drier and closer favours Flat or
    Gentle; livelier and further favours Strong or Harman-like), tied to
    the same directivity-index mechanism as the rest of the chapter and to
    Olive et al.'s preference finding, already cited there. Then the actual
    quantitative handle: critical distance $d_c$, the distance at which
    direct and reverberant levels are equal, estimated from room volume and
    $T_{60}$ via the standard Sabine + room-constant approximation, giving a
    direct-to-reverberant ratio in dB at the listening distance. Worked with
    two rooms of the same volume and listening distance differing only in
    $T_{60}$ (0.25 s vs 0.6 s), which flips the sign from +1.3 dB to
    -2.5 dB -- the arithmetic checked in Python before it went in the
    manual. Caveated: the room-constant approximation understates $d_c$ in
    a live room, $Q$ is frequency-dependent, and a small room's low end
    rarely reaches the diffuse-field assumption the formula needs, so it is
    an order-of-magnitude starting point, not a setting to compute once.*

    *Honest about what SuperMoTo does not do: it does not measure $T_{60}$
    or apply the formula anywhere. Noted that Calibration's captured
    impulse responses already hold the room's decay in their tail, which is
    what a $T_{60}$ estimate would use, so a one-click suggested starting
    curve is a plausible future addition -- not built, and not asked for
    yet, just flagged as available if wanted.*

- [x] Compute the T60 the target-curve chapter talks about. Is it meaningful
  on a windowed/smoothed FRF, or on an averaged one? Wanted: a T60 read-out
  on the IR plot, and an optional log-amplitude view of the IR.

    *Both questions answered by measuring rather than reasoning: a synthetic
    room built with a known T60 (decaying gaussian noise), put through each
    path, to see what comes back. Scripts in the session scratchpad; the
    numbers are in `Source/Dsp/ReverbTime.h` and in the manual.*

    *Smoothed: no. Smoothing is a convolution in frequency, so it is a
    multiplication in time by a window of about 1/(bandwidth) -- 1/6 octave
    is 43 ms at 200 Hz and 4.3 ms at 2 kHz. A 0.40 s room reads 0.28-0.30 s
    broadband through it, and no octave band gives a readable decay at all.
    What is left to measure is the kernel.*

    *Averaged: it depends which average. Averaging the decay TIMES per
    position, which is what ISO 3382 asks for, reads 0.401 for 0.400 with a
    0.002 spread. A plain complex average of the responses also survives
    (0.398), because decorrelated tails shrink by 1/sqrt(N) without changing
    slope -- that surprised me, I had expected the tail to cancel. But the
    engine's own average cannot be used: magnitude from a power mean and
    phase from a complex mean do not describe one response, and transformed
    back its energy does not fall monotonically (-38 dB at 0.5 s, back to
    -35 dB at 1.0 s). So: per position, then average the times.*

    *A third finding nobody asked for: the Welch estimator cannot be used
    either, when the excitation is a sweep. The segmentation smears the
    response in time and the same 0.40 s room reads 0.63-0.66 s; a longer
    sweep does not help (0.66 at 20 s). White noise through the same path
    reads 0.377, which is the small window-autocorrelation bias one would
    expect, so it is the non-stationary excitation that does the damage. The
    sweep deconvolution reads 0.399. The estimate is therefore refused in
    Welch mode, with the read-out saying why, rather than reported wrong.*

    *`Source/Dsp/ReverbTime.h` is the estimator: Schroeder backward
    integration, a compacted Lundeby for the truncation point (envelope ->
    noise estimate -> line fit -> crossing, iterated), the fitted tail added
    back so the curve does not bend into its own end, and the ISO 3382 fits
    EDT / T20 / T30. No JUCE in it, which `Tests/ReverbTimeTest.cpp` and a
    plain `add_executable` keep true. It recovers 0.250/0.400/0.600/0.895 for
    0.25/0.40/0.60/0.90, still reads 0.418 with a -35 dB floor over the
    decay, and refuses silence, a bare impulse and 40 ms of response.*

    *`AnalysisEngine::getReverbTime()` drives it, per octave band 125 Hz to
    8 kHz, from the RAW per-position `Curve::H` band-limited in the frequency
    domain (raised-cosine skirts, not a brick wall, which would ring for as
    long as the decay it is measuring -- with hard edges the 125 Hz band
    failed outright). Derived by `loadFiles()` rather than lazily, following
    the note on `effectiveCorrection()`: accessors on that class stay pure
    reads because a background export job reads them while the message thread
    plots. Validated per band in Python first: 250 Hz upwards recovers within
    1-2 %, 125 Hz is noisier and is reported with its position count.*

    *GUI: a third View entry, "Impulse response (raw)", showing the
    measurements themselves -- one trace per position, the whole window long,
    unsmoothed and uncorrected, direct sound at t = 0. A "dB" button puts the
    amplitude on a log axis, which that view turns on for you since a decay
    is the reason to open it. The T60 read-out sits beside the selector and
    lists the octave bands on hover. The dB axis is a new mode on
    `fxme::WaveformDisplay` (columns drawn as bars from the floor to their
    peak, dB grid, wheel moves the floor, dB in the cursor read-out),
    additive and recorded in FxmeTools' `doc/api-changes.md`.*

    *Manual: a new subsection `sec:rawir` in the analysis chapter with all
    three refusals and their numbers, and the target-curve chapter's "SuperMoTo
    does not measure T60" paragraph replaced by how to read it. The manual
    still says what is NOT measured for you there -- the room volume and the
    listening distance are a tape measure, and Q stays an assumption.*

- [x] When hitting stop button during a measurement run, all the measurements
  of this run should be cancelled. Currently, if stopping after a few channels
  have been measured, they end in the data. They shouldn't.

    *`MeasurementEngine::stop()` now deletes the captures the run had already
    written, and the status line says how many went. The engine already kept
    the list (`filesWrittenThisRun`, which the manifest is built from), so the
    fix is mostly about knowing when it is safe to act on it.*

    *It is safe because of how positions are numbered: `start()` scans the
    folder and gives each channel `max + 1`, so a run never writes over an
    earlier run's captures and that list can only ever hold its own. Deleting
    them also puts the numbering back, so the next run takes the positions the
    cancelled one was using. The manifest needs no repair either -- it is only
    written when a run completes, so it still describes the folder exactly as
    it now is.*

    *Two guards were needed, both against deleting a run that had succeeded.
    `prepare()` calls `stop()`, so a host changing the sample rate after a
    finished run would have deleted it: the deletion is conditional on the
    engine actually running. And `handleAsyncUpdate()` now clears the list once
    it has written the manifest, so what the list holds is always an unfinished
    run's. `cancelPendingUpdate()` in stop() handles a capture that completed in
    the moment Stop was hit.*

    *The write-failure path got the same treatment, not having been asked for
    but being the same bug: a run that dies on a full disk left exactly the
    half-set the item is about. It now discards the run too, including the file
    it failed on, and says so.*

    *Documented in the manual's measurement chapter and on the Run button's
    tooltip, since deleting files on a button press should not be a surprise.*

## Decided against

- [~] Correction level as a function of frequency: stop inverting the
  steady-state room curve above the transition frequency.

    *Dropped before the first release, on the reasoning that the two things
    it was for are now done by other means: the averaging fix removed the
    treble lift that motivated it (most of the 4.8-5.6 dB it was written
    about was the artefact, not the room), and the target curve decides the
    high-frequency balance explicitly and audibly instead of by a correction
    level nobody can hear the units of.*

    *The detail that was here went with it, deliberately: its worked example
    and its suggested starting values were computed BEFORE the averaging was
    fixed, so every number in it described a measurement that no longer
    exists. Keeping stale arithmetic around as a plan is worse than keeping
    nothing. The standing argument is not stale and is where it belongs
    anyway --- Chapter "The monitor target curve" of the manual, which cites
    EBU 3276's "corrections below 300 Hz only" and the rest. If this comes
    back, it wants re-deriving from a fresh measurement rather than
    restoring.*

    *The further-off idea it ended with is worth remembering on its own: to
    correct the DIRECT sound at high frequency by gating the impulse response
    with a frequency-dependent window before inverting it, short at HF and
    long at LF. That is the thing the standards are really asking for, it is
    a bigger job than a level curve, and nothing about it depended on the
    numbers above.*

    **Is that gating not just the frequency-dependent smoothing we already
    do?** Asked 2026-09-22, and the answer is worth keeping because half of it
    is yes.

    *Yes, in mechanism, and exactly. Smoothing with a kernel of width `df` is
    a convolution in frequency, so it is a multiplication in time by a window
    of about `1/df`. Fractional-octave smoothing makes `df` proportional to
    `f`, so the equivalent time window already scales as `1/f` -- long at LF,
    short at HF, which is the very shape the gating idea asks for. 1/6 octave
    is 43 ms at 200 Hz and 4.3 ms at 2 kHz (the same identity `ReverbTime.h`
    rests on).*

    *Yes, in practice too, for one of the two smoothings. Measured on a
    synthetic room with a flat direct sound and a reverberant share that falls
    with frequency, levels re the direct sound: the steady state sits at
    +11.8 dB at 125 Hz and +6.9 dB at 8 kHz; per-curve COMPLEX smoothing pulls
    that to +8.1 and +0.4 dB. It removed about 8 dB of reverberant energy at
    HF against 3.7 at LF. That is gating, with the right frequency
    dependence.*

    *But no, where it would have to count. Two reasons, and the second is the
    real one:*

    - *`computeAverage()` accumulates `c.H`, the RAW per-curve spectra. The
      complex-smoothed `c.Hs` are the thin lines on the plot and go nowhere
      else -- they never reach the average, so they never reach the
      correction.*
    - *The average's magnitude is a power mean, and it is smoothed as a real
      magnitude sequence: a weighted average of a magnitude curve removes
      ripple, not energy. Measured, it lands within 0.2 dB of the unsmoothed
      steady state (tilt -5.03 dB against -4.85). And it cannot be made to
      gate, because a time window needs phase and the power mean discarded
      it. That was the right call for the magnitude -- it IS the averaging
      fix -- but it forecloses the gating reading of smoothing at exactly the
      point where the correction is derived.*

    *So the idea survives, and is cheaper than it looked: the gate has to
    happen PER POSITION, BEFORE averaging, on the raw impulse responses, which
    `AnalysisEngine::renderRawIR()` now hands over. Gate each position,
    transform back, power-average as now.*

    *One honest caveat from the experiment: the explicit-gate column overshot
    BELOW the direct sound at HF (-8 to -9 dB at 4-8 kHz), which was a
    brick-wall octave split ringing for longer than the window rather than a
    result. The constraint it stumbled into is real though, and is the actual
    difficulty of the whole approach: a gate shorter than about 1/bandwidth
    eats the signal and not only the room. Any serious attempt needs proper
    band filters and a window no shorter than they ring.*

    *A shape difference worth noting too, since it is the other thing gating
    buys: a smoothing kernel's time window is symmetric about t = 0 and is
    whatever shape the frequency kernel implies (sinc-like, with negative
    lobes for a rectangular one). A gate is shaped on purpose and can be
    asymmetric -- short before the arrival, longer after -- which is how one
    keeps specific early reflections and drops only the late field.*

## To do

- [ ] Regenerate Figure 2 of the paper, and re-check two numbers in
  Section 5, once the correction FIRs have been re-exported from the
  fixed engine and the system measured again with them.

    Figure 2 (`fir-length.png`) compares a *measurement* against renderings of
    a *re-derived* design. Those were the same design until the averaging fix;
    they are not any more, because the campaign-6 filters were exported by the
    old averaging and the current code designs about 1.8 dB more level through
    the crossover region. Regenerated with the fixed code, the measured curve
    no longer follows the 8192-tap rendering, which is an artefact of mixing
    two engine versions rather than a result — so the committed figure is
    deliberately still the one made before the change, and the file was
    reverted to it. Regenerating it honestly needs the correction re-exported
    from the fixed engine *and* the system measured again.

    Two other numbers wait on the same re-export:

    - Section 5's "that chain reproduces the plugin's own exported filters to
      between 0.06 and 0.13 dB RMS from 40 Hz to 8 kHz". It is a claim about
      two implementations of the same arithmetic agreeing, and it will hold
      again once the exports come from the fixed engine; right now it cannot
      be checked, because the chain and the exported filters are two different
      designs (they differ by about 0.6 dB RMS over 200 Hz - 10 kHz, which is
      the size of the fix rather than an error).
    - Section 5's "the same pair rendered at 2048 taps differs from itself by
      1.63 dB RMS across the crossover region". Re-deriving it with the new
      averaging gives about 1.5 dB on the left main, but the exact figure in
      the text could not be reproduced by either averaging, so it wants
      recomputing rather than editing — the claim it supports (neither 2048-tap
      rendering has room for the structure) is unaffected.
