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

## To do

