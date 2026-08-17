# FX-Mechanics compliance audit — SuperMoTo

Run on 2026-08-17 against commit `4293616` (branch `main`), FxmeTools submodule
at `0c53e1f` (clean, pushed, but the parent's pointer is not yet committed).

Static audit: greps and reads only, nothing compiled. The JUCE 8 deprecation
section is therefore what greps can see (see [Coverage caveat](#coverage-caveat)).

Each item is marked **safe to apply** (mechanical, no behaviour change) or
**decision** (changes behaviour, product surface, or a shared library, so it is
Olivier's call).

Revisions:

- 2026-08-17, initial audit.
- 2026-08-17, applied S1, S1b, S2 and S3 (the macOS release fixes).
- 2026-08-17, applied S4 (state version attribute).
- 2026-08-17, applied S5, S6 and S7 (realtime safety). S5 touches FxmeTools and
  needs committing there first. None of S5-S7 has been compiled yet.
  Everything else is still open.

---

## Silent bugs

Already broken in a shipped build.

### macOS release (one bug, three fixes) — APPLIED 2026-08-17

The `if(APPLE)` block sat after `project()`, so it could not take effect; the
verify step could not fail, so CI never noticed. Any macOS release built before
this fix should be assumed arm64-only with a modern deployment target, and
re-cut.

- [x] **S1** Move the whole `if(APPLE)` block above `project()`.
      **safe to apply**
      The block now sits between `cmake_minimum_required()` and `project()` at
      [CMakeLists.txt:3-19](../CMakeLists.txt#L3-L19), with the reasoning in a
      comment so it is not "tidied" back down later. CMake's Darwin platform
      initialisation runs inside `project()` and creates the
      `CMAKE_OSX_DEPLOYMENT_TARGET` cache entry, so a later
      `set(... CACHE STRING ...)` without `FORCE` was a no-op.
- [x] **S1b** Change the deployment target from 11.0 to 10.13 (11.0 silently
      excludes Catalina). **safe to apply**
      [CMakeLists.txt:16](../CMakeLists.txt#L16)
- [x] **S2** Make the verify step able to fail. **safe to apply**
      [.github/workflows/release.yml:130-152](../.github/workflows/release.yml#L130-L152).
      Now `set -euo pipefail`, checks all three bundles (VST3, AU, Standalone:
      separate link steps, so "the standalone won't open either" is the same bug
      reported twice), requires both an `x86_64` and an `arm64` slice in each,
      emits `::error::` annotations, prints `vtool -show-build` so the deployment
      target is visible in the log, and exits non-zero.
- [x] **S3** Pass the macOS flags on the CI configure line too.
      **safe to apply**
      [.github/workflows/release.yml:111-121](../.github/workflows/release.yml#L111-L121).
      A command-line `-D` is in the cache before any CMakeLists line runs, so it
      cannot be defeated by ordering; a `set(... CACHE ...)` without `FORCE`
      never overrides a `-D`, so the two do not conflict.

Verification done: a configure-only `cmake -B` run of the reordered CMakeLists
completes clean, and the new verify step's shell body was run against a stubbed
`lipo` in four scenarios (true universal exits 0; arm64-only, x86_64-only and a
missing bundle each exit 1, with all three bundles reported rather than the loop
aborting on the first). Not verified: the actual macOS build, which needs a
runner.

### State — APPLIED 2026-08-17

- [x] **S4** Add a version attribute to the saved state. **safe to apply**
      Turned out to be more than the one line the initial audit estimated,
      because the plugin has **two** serialization surfaces, not one: the host
      session (`getStateInformation`, a `"SuperMoToState"` root wrapping the
      parameters) and the preset XML files, which `PresetManager::saveUserPreset`
      writes straight from `apvts.copyState()` without ever touching the wrapper.
      A property on the wrapper root would have versioned sessions and left every
      preset unversioned, and presets are the longer-lived artefact (they get
      shared between machines and kept for years).

      The version therefore lives on `apvts.state` itself, so it travels through
      both surfaces from one place:
    - `currentStateVersion` / `stateVersionProperty` declared at
      [PluginProcessor.h:82-98](../Source/PluginProcessor.h#L82-L98), with the
      format history and the rule for bumping it.
    - `stampStateVersion()`
      ([PluginProcessor.cpp:216-225](../Source/PluginProcessor.cpp#L216-L225)) is
      idempotent: it writes only when the value actually differs. That keeps it
      from flagging the preset dirty, and removes any dependence on the order the
      `apvts.state` listeners run in.
    - Stamped in the constructor
      ([PluginProcessor.cpp:45-48](../Source/PluginProcessor.cpp#L45-L48)) before
      `PresetManager` attaches its dirty-tracking listener, so a fresh instance
      still starts clean.
    - Re-stamped in `restoreFromApvtsState()`
      ([PluginProcessor.cpp:284-289](../Source/PluginProcessor.cpp#L284-L289)).
      Necessary because `APVTS::replaceState` is a wholesale tree reseat
      (`state = newState`), so loading an older state or one of the existing
      unversioned factory presets drops the property; without this the next
      preset saved from that state would be unversioned again.
    - Read in `setStateInformation`
      ([PluginProcessor.cpp:322-328](../Source/PluginProcessor.cpp#L322-L328))
      before `replaceState`, and the pre-existing structural probe for the
      original layout is now gated on `version == 0`, so a v1 state cannot pick
      up a stale sibling `Configurations` by mistake.
    - Also written onto the copy in `getStateInformation`
      ([PluginProcessor.cpp:298-304](../Source/PluginProcessor.cpp#L298-L304)) so
      a session is self-describing regardless of the live tree. Safe because
      `copyState()` deep-copies (`state.createCopy()`), so it cannot dirty the
      live state, and it is the same property at the same path, so a file still
      carries exactly one version number.

      No behaviour change for anything already saved: an unversioned state reads
      as 0 and takes exactly the path it takes today. The unversioned factory
      presets in BinaryData migrate on load (stamped in memory under
      `PresetManager`'s `suppressDirty`, so no spurious modified marker); they
      will carry version 1 if they are ever regenerated.

### Realtime safety — APPLIED 2026-08-17

All three fixed. S5 lives in the FxmeTools submodule and must be committed there
first. None of the three has been compiled yet.

- [x] **S5** `fxme::FirFilter::process` blocks the audio thread on a
      message-thread lock. **decision (FxmeTools, shared library)**
      Fixed in the submodule and API-compatible, so every other plugin picks it
      up on its next pointer bump.
    - `process()` now takes the lock with `juce::ScopedTryLock`
      ([FirFilter.h:136-146](../lib/FxmeTools/FxmeTools/dsp/FirFilter.h#L136-L146))
      and leaves the block dry if a load is in flight, instead of waiting on a
      critical section that spans a file read, a `LagrangeInterpolator` resample
      and a WDL engine rebuild. Loading an IR now costs a few un-convolved
      blocks rather than a blocked callback and an xrun.
    - `hasImpulse()` is lock-free, backed by a new `std::atomic<bool>
      impulseLoaded`, so `MatrixEngine` polling it once per output per block (32
      acquisitions) touches no lock at all. Signature unchanged apart from
      gaining `noexcept`.
    - The atomic is published *last* in `rebuildEngineImpulse()` and cleared
      *first* in `loadSilentImpulse()`, so the lock-free fast path can never see
      "loaded" while the engine holds no impulse.
    - `getImpulseLength()` still takes the lock. It has no callers and is now
      documented message-thread only.

- [x] **S6** `ConfigModel::copyAll` on the audio thread: spinlock, ~400 KB copy,
      and a possible `free()`. **decision**
      All three sub-problems addressed, without introducing any hand-rolled
      lock-free machinery (a triple-buffered snapshot was considered and rejected
      as ~1.2 MB of extra state plus a publish/acquire race that is easy to get
      subtly wrong, for no benefit over a try-lock here):
    - **Priority inversion.** New `ConfigModel::tryCopyForEngine()` uses
      `juce::SpinLock::ScopedTryLockType`. On failure `MatrixEngine` leaves
      `lastModelVersion` untouched and keeps the settings it already has, so the
      next block retries. The audio thread can no longer wait on a GUI edit, and
      `restoreFromValueTree` holding the lock across a whole ValueTree parse is
      now harmless. A GUI edit landing one buffer late is imperceptible.
      `getSpectrumFrames()` also `reserve()`s before taking the lock, so the
      writer side no longer calls `operator new` inside the critical section.
    - **`free()` on the audio thread.** New `OutputAudioSettings` carries
      everything the engine reads (gain, delay, firOn, spectrum, bands) and
      deliberately omits `juce::String firPath`, so neither
      `MatrixEngine::outputSettings` nor `OutputProcessor::settings` can release
      a String reference on the audio thread. The path was never needed there:
      `updateFirFiles()` reads it from the model on the message thread. Verified
      by grepping every `outputSettings` use in the engine. `copyAll()` is kept
      unchanged for `toValueTree()`, message thread only.
    - **Volume.** The copy and the `applySettings` loop are both restricted to
      the visible `visIns x visOuts` sub-range, which is all the engine ever
      processes: ~24 KB and 384 frames for a default 8x8 matrix instead of
      ~384 KB and 6144. The existing reveal-and-reset path still runs first when
      the matrix grows, and the copy now reads the new size *before* taking the
      lock, so a grow always converges (the size store precedes the version
      bump, so a race just means one more pull).

- [x] **S7** Scratch buffers and oversized blocks. **worse than first reported**
      The initial audit called this a low-severity allocation on a defensive
      path. That was wrong in an important way: `inputCopy` was the *only*
      buffer that grew. `MatrixEngine::outScratch`, `FrameProcessor::scratch` and
      `SplMeterEngine::inScratch` are all sized for `maxBlockSize` in `prepare()`
      and then indexed up to `n` with no check, so a host sending a longer block
      than it declared was an **out-of-bounds write**, not merely an allocation.

      Fixed by slicing rather than growing: `processBlock` records
      `preparedBlockSize` and, for anything longer, loops over windowed
      `juce::AudioBuffer` views into the caller's buffer, calling the new
      `processChunk()` per slice
      ([PluginProcessor.cpp:142-178](../Source/PluginProcessor.cpp#L142-L178)).
      No allocation on the audio thread, no re-`prepare()` resetting every filter
      and delay line, and every downstream buffer is guaranteed big enough. The
      defensive `setSize` is gone, replaced by a `jassert`, and `numIn` is now
      also clamped against the window's channel count.

---

## Retrofit

Mechanical, low risk.

- [ ] **R1** Call `setAccentColour()` on the editor's look-and-feel.
      **safe to apply**
      [Source/PluginEditor.cpp:47](../Source/PluginEditor.cpp#L47) sets
      `fxmeLookAndFeel` but never tints it, so all ~40 drop-down menus and all 26
      tooltips render neutral grey against cyan/teal/rose panels. `presetBar` and
      `presetPane` get theirs
      ([PluginEditor.cpp:108](../Source/PluginEditor.cpp#L108),
      [:204](../Source/PluginEditor.cpp#L204)); the editor's own does not. One
      line in the constructor:
      `fxmeLookAndFeel.setAccentColour (SuperMoToTheme::master);`

- [ ] **R2** Migrate 27 deprecated `juce::Font (float[, int])` calls to
      `juce::Font (juce::FontOptions (...))`. **safe to apply**
      No `FontOptions` anywhere in the project yet.
    - [ ] [PluginEditor.cpp:116](../Source/PluginEditor.cpp#L116), [:557](../Source/PluginEditor.cpp#L557)
    - [ ] [FrameEditorComponent.h:36](../Source/Components/FrameEditorComponent.h#L36), [:59](../Source/Components/FrameEditorComponent.h#L59)
    - [ ] [OutputEditorComponent.h:36](../Source/Components/OutputEditorComponent.h#L36), [:74](../Source/Components/OutputEditorComponent.h#L74)
    - [ ] [ConfigToolComponent.h:47](../Source/Components/ConfigToolComponent.h#L47), [:269](../Source/Components/ConfigToolComponent.h#L269)
    - [ ] [AnalysisComponent.h:33](../Source/Components/AnalysisComponent.h#L33), [:39](../Source/Components/AnalysisComponent.h#L39), [:226](../Source/Components/AnalysisComponent.h#L226), [:298](../Source/Components/AnalysisComponent.h#L298), [:483](../Source/Components/AnalysisComponent.h#L483)
    - [ ] [CalibrationComponent.h:47](../Source/Components/CalibrationComponent.h#L47), [:61](../Source/Components/CalibrationComponent.h#L61), [:418](../Source/Components/CalibrationComponent.h#L418), [:439](../Source/Components/CalibrationComponent.h#L439), [:494](../Source/Components/CalibrationComponent.h#L494)
    - [ ] [GroupAnalysisComponent.h:38](../Source/Components/GroupAnalysisComponent.h#L38), [:42](../Source/Components/GroupAnalysisComponent.h#L42), [:503](../Source/Components/GroupAnalysisComponent.h#L503), [:512](../Source/Components/GroupAnalysisComponent.h#L512), [:516](../Source/Components/GroupAnalysisComponent.h#L516), [:521](../Source/Components/GroupAnalysisComponent.h#L521), [:563](../Source/Components/GroupAnalysisComponent.h#L563)
    - [ ] [TransferFunctionPlot.h:580](../Source/Components/TransferFunctionPlot.h#L580), [:620](../Source/Components/TransferFunctionPlot.h#L620)

      Leave the nine bare `g.setFont (11.0f)` calls alone (MatrixComponent.cpp,
      TransferFunctionPlot.h, ConfigToolComponent.h:142): that is the genuine
      `Graphics` overload, not deprecated.

- [ ] **R3** Migrate 4 deprecated `createWriterFor` calls to the
      `AudioFormatWriterOptions` overload. **safe to apply**
      [AnalysisEngine.cpp:1024](../Source/Dsp/AnalysisEngine.cpp#L1024),
      [:1045](../Source/Dsp/AnalysisEngine.cpp#L1045),
      [MeasurementEngine.cpp:291](../Source/Dsp/MeasurementEngine.cpp#L291),
      [Tests/AnalysisTest.cpp:74](../Tests/AnalysisTest.cpp#L74). All four use
      the `stream.get()` + `stream.release()` idiom, so the migration also
      removes the manual release. The new overload binds
      `std::unique_ptr<OutputStream>&`, so the local must be declared as that
      exact type (`presets/EmbeddedAudio.cpp` is the worked example).

- [ ] **R4** Use the typed setter instead of the raw property.
      **safe to apply**
      [BandEqEditor.h:148](../Source/Components/BandEqEditor.h#L148) uses
      `s.getProperties().set ("showLabel", true)`;
      `fxme::FxmeSlider::setShowLabel(bool)` exists at
      [FxmeSlider.h:89](../lib/FxmeTools/FxmeTools/components/FxmeSlider.h#L89).
      Identical behaviour.

- [ ] **R5** Replace `drawFromCentre` with `setCentralValue (0.0)`.
      **safe to apply**
      [AnalysisComponent.h:288](../Source/Components/AnalysisComponent.h#L288) on
      `alignSlider`. The range is -40 to +40 ms (symmetric), so it is correct
      today, but it silently becomes wrong if the range is ever made asymmetric.
      `setCentralValue` is at
      [FxmeSlider.h:84](../lib/FxmeTools/FxmeTools/components/FxmeSlider.h#L84).

---

## House style

Structural, and may be larger than it looks.

### Docs and CI

- [ ] **H1** Add an Installing section to the README (macOS quarantine).
      **safe to apply**
      [README.md](../README.md) goes straight from "Use cases" to "Building"
      (line 253) to "License", with no `xattr -dr com.apple.quarantine` line.
      These builds are not notarised, so a downloaded bundle is quarantined and
      the DAW silently skips it. The per-host MIDI routing section does not apply
      (`NEEDS_MIDI_INPUT FALSE`, `acceptsMidi()` returns false).

- [ ] **H2** Run the three registered tests in CI. **safe to apply**
      `enable_testing()` at [CMakeLists.txt:12](../CMakeLists.txt#L12) and
      `add_test` for `analysis`, `miccal` and `sweep`, but all three targets are
      `EXCLUDE_FROM_ALL` and no workflow job invokes `ctest`. The Linux job is
      the natural home for a build-and-`ctest` step that can fail.

- [ ] **H3** Fix the README build line. **safe to apply**
      [README.md:269](../README.md#L269) says `cmake --build build -j`.
      Unbounded parallelism on a Release build with `-flto` is what has frozen
      this machine before. Should be `-j2` and a named target.

### Layering against FxmeTools

- [ ] **H4** `Source/Dsp/AmbisonicsDecode.h` duplicates `fxme::ambi`.
      **decision**
    - [AmbisonicsDecode.h:43](../Source/Dsp/AmbisonicsDecode.h#L43)
      `numHarmonics` becomes `fxme::ambi::channelsForOrder`
      ([Ambisonics.h:86](../lib/FxmeTools/FxmeTools/dsp/Ambisonics.h#L86))
    - [AmbisonicsDecode.h:46](../Source/Dsp/AmbisonicsDecode.h#L46)
      `degreeForAcn` becomes `fxme::ambi::orderOfChannel`
      ([Ambisonics.h:92](../lib/FxmeTools/FxmeTools/dsp/Ambisonics.h#L92))
    - [AmbisonicsDecode.h:50](../Source/Dsp/AmbisonicsDecode.h#L50)
      `sn3d(acn, x, y, z)` becomes `fxme::ambi::encodeSN3D (dir, gains, order)`

      Conversely `maxRe()`
      ([:79](../Source/Dsp/AmbisonicsDecode.h#L79)) and `decodeMatrix()`
      ([:94](../Source/Dsp/AmbisonicsDecode.h#L94)) are a generic max-rE sampling
      decoder with no SuperMoTo dependency: a reusable ambisonics kernel that
      belongs in FxmeTools alongside the encode side.

- [ ] **H5** Two more generic files sitting in `Source/`. **decision**
    - [ ] [Source/Dsp/MicCalibration.{h,cpp}](../Source/Dsp/MicCalibration.h): a
          REW / miniDSP / Dayton / FRD text calibration-file parser with a
          log-frequency interpolated curve. Zero dependency on this plugin.
          Belongs in FxmeTools `dsp/`.
    - [ ] [Source/Dsp/IemDecoder.{h,cpp}](../Source/Dsp/IemDecoder.h): an IEM
          AllRADecoder JSON parser. Same argument, and it pairs naturally with
          H4's decoder.

- [ ] **H6** `Theme.h::paintBackground` predates `fxme::PanelBackground`.
      **decision**
      [Theme.h:23-30](../Source/Theme.h#L23-L30) hand-rolls a diagonal gradient.
      FxmeTools gained `fxme::paintTintedBackground` /
      `fxme::paintComponentBackground` in the current submodule HEAD (`0c53e1f`).
      A decision rather than a cleanup: the house version is near-black plus a
      whisper of accent, while SuperMoTo's base is `rgba(0.15, 0.15, 0.25)`, so
      adopting it visibly changes the backdrop.

- [ ] **H7** Hand-coloured `juce::TextButton`s where `fxme::AccentToggle` now
      exists. **decision**
      The six view buttons
      ([PluginEditor.cpp:91-105](../Source/PluginEditor.cpp#L91-L105)), the six
      edit-config buttons ([:159-168](../Source/PluginEditor.cpp#L159-L168)) and
      `collapseButton` ([:84-89](../Source/PluginEditor.cpp#L84-L89)) each set
      `buttonColourId` / `buttonOnColourId` by hand.
      [`fxme::AccentToggle`](../lib/FxmeTools/FxmeTools/components/AccentToggle.h)
      is the house latching button for exactly this case. Visual change.

### Controls (flagged for confirmation, not because they are wrong)

- [ ] **H8** Four bare `juce::Slider`s with a text box. **decision**
      [CalibrationComponent.h:771](../Source/Components/CalibrationComponent.h#L771)
      (`sineAmp`, `sineFreq`, `noiseAmp`, `splRef`), configured at
      [:423-432](../Source/Components/CalibrationComponent.h#L423-L432) as
      `IncDecButtons` + `TextBoxLeft`. This is the one place the project departs
      from `fxme::FxmeSlider`, and the comment says it is deliberate.
      `IncDecButtons` is not a style `FxmeLookAndFeel` custom-draws and
      `FxmeSlider` removes the text box, so the exception is defensible.

- [ ] **H9** Knobs use separate `juce::Label`s rather than `setShowLabel(true)`.
      **decision**
      Every component except `BandEqEditor` pairs its `FxmeSlider` with an
      `addLabel()` helper (for example
      [AnalysisComponent.h:481-487](../Source/Components/AnalysisComponent.h#L481-L487)).
      The house default is `setName()` + `setShowLabel(true)` and no separate
      label, but these are horizontal bars in dense two-column panels where a
      label to the left is the better layout. Considered choice, noted for
      completeness.

- [ ] **H10** Optional: grey out `Level` and `Dim` while `Mute` is engaged.
      **decision**
      No automatable APVTS parameter currently supersedes another control, so the
      house `setEnabled()` rule has nothing to bite on. `Mute` / `Dim` / `Level`
      are the one mutually redundant trio, and doing it properly needs a ~10 Hz
      `Timer` poll so host automation is not missed. Cosmetic.

---

## Already correct

Checked and found clean. This section exists so coverage is distinguishable from
silence: these are not gaps in the audit.

**Look-and-feel.** The editor calls `setLookAndFeel (&fxmeLookAndFeel)` on itself
at [PluginEditor.cpp:47](../Source/PluginEditor.cpp#L47), so all ~40 combo boxes
inherit it through the parent chain and `ComboBox::showPopup()` passes it to the
menus. The family-wide "every combo silently falls through to `LookAndFeel_V4`"
finding does not apply to this project. Only the accent (R1) is missing.

**Tooltips.** A `juce::TooltipWindow` is owned and parented to the editor
([PluginEditor.h:80](../Source/PluginEditor.h#L80)), so its 26 `setTooltip()`
calls are actually visible and it picks up the editor's look-and-feel. The early
member position was checked for a z-order problem and is harmless:
`TooltipWindow` calls `setAlwaysOnTop(true)` in its constructor and
`toFront(false)` on display (`juce_TooltipWindow.cpp:42,155`).

**Text entry.** `fxme::TextEntryFocusFixer` on the editor
([PluginEditor.h:121](../Source/PluginEditor.h#L121)) and a second one inside the
`CallOutBox` comment popup
([CalibrationComponent.h:627](../Source/Components/CalibrationComponent.h#L627)).
The popup case is the subtle one and it is handled.

**Controls.** Every slider is `fxme::FxmeSlider` except H8's four. Every
APVTS-bound toggle is `fxme::FxmeButton`, with zero hand-rolled
`ButtonAttachment`s. The remaining bare `juce::ToggleButton`s are all driven by
`ConfigModel` rather than the APVTS, so `FxmeButton` genuinely does not apply.

**`setEnabled()`.** Used in six places for superseded controls (FrameEditor,
BandEq order-vs-peaking, OutputEditor clear button, Calibration sub channel,
GroupAnalysis busy state). The model-driven callbacks are correct; see H10 for
the one optional addition.

**FxmeTools reuse.** No hand-rolled biquad, meter, spectrum tap, sweep generator
or thread pool. Correct near-miss choices: `fxme::RmsMeter` (not `VuMeter`) for
the SPL bar, mono `fxme::FirFilter` per output, `fxme::SpectrumTap` +
`SpectrumDisplay`, `SynchronizedSweep`, `BackgroundTaskRunner`,
`saveComponentAsPng`. `Source/Dsp/BandFilter.h` correctly centralises coefficient
maths over `fxme::Biquad` for both `FrameProcessor` and `OutputProcessor`.

**State and self-containment.** `fxme::PresetManager` wired at
[PluginProcessor.cpp:54-59](../Source/PluginProcessor.cpp#L54-L59), with both
`fxme::PresetComponent` (Presets page) and `fxme::PresetBarComponent` (top-right
strip). `fxme::InfoButton` with per-view help text. FIR impulses go through
`fxme::EmbeddedAudio` ([:51](../Source/PluginProcessor.cpp#L51),
[:235-237](../Source/PluginProcessor.cpp#L235-L237)) with the embedded copy
deliberately winning over the file path, so presets and sessions are
self-contained. The only state gap is S4.

**Realtime (the parts that are right).** No `DBG`, `Logger`, `std::cout` or
`printf` anywhere in `Source/`. No `getPlayHead()` use at all. All APVTS reads go
through `getRawParameterValue()->load()`. `MeasurementEngine::process` and
`SplMeterEngine::process` are allocation- and lock-free, with the wav write
deferred to `handleAsyncUpdate` via `triggerAsyncUpdate`
([MeasurementEngine.cpp:266-267](../Source/Dsp/MeasurementEngine.cpp#L266-L267)).
`recomputeLatencyComp()` is deliberately not called from the message thread:
`updateFirFiles` sets `latencyCompDirty`
([MatrixEngine.cpp:359](../Source/Dsp/MatrixEngine.cpp#L359)) and `process()`
consumes it with an `exchange`
([:201-213](../Source/Dsp/MatrixEngine.cpp#L201-L213)), with the reasoning in the
comment. S5 and S6 are the two places that care is not carried through.

**Plugin type / AU.** `NEEDS_MIDI_INPUT FALSE` + `AU_MAIN_TYPE
kAudioUnitType_Effect` + `acceptsMidi()` false + `isMidiEffect()` false, all
internally consistent, no MIDI-on-`aufx` trap. Run `/au-logic-audit` for the full
matrix.

**Registration completeness.** Single target, present in the root CMakeLists and
in `release.yml`'s three build jobs. No plugin table to fall out of sync. No
findings.

---

## Coverage caveat

The JUCE 8 deprecations surface only as compiler warnings, and this audit never
compiled anything. R2 and R3 are what greps can see.

- [ ] Paste the warnings from the last Release build so the rest can be closed
      out, particularly any `juce::Font` conversion hiding behind a typedef, and
      anything in
      [Source/Components/GroupAnalysisComponent.h](../Source/Components/GroupAnalysisComponent.h)
      (1280 lines, the largest file and the most recently touched).

---

## Commit plan

S1, S1b, S2, S3 and S4 are applied and unstaged. Everything else is untouched.
The order is submodule first, then the bumped pointer in the parent.

- [ ] **0a. The macOS release fix, on its own commit** (applied, ready to commit)
      ```
      git add CMakeLists.txt .github/workflows/release.yml
      ```
      Worth isolating so a release note can point at it. Then re-cut any macOS
      release built before it: the previous artefacts are very likely arm64-only.
- [ ] **0b. The state version attribute** (applied, ready to commit)
      ```
      git add Source/PluginProcessor.h Source/PluginProcessor.cpp
      ```
      Worth its own commit: it is the one change here that touches saved-state
      semantics.
- [ ] **0c. Realtime safety, submodule first** (applied, ready to commit)
      ```
      cd lib/FxmeTools
      git add FxmeTools/dsp/FirFilter.h          # S5
      git commit && git push                    # shared: every plugin gets this
      cd ../..
      git add lib/FxmeTools                      # bumped pointer
      git add Source/Model/ConfigModel.h Source/Dsp/OutputProcessor.h \
              Source/Dsp/MatrixEngine.h Source/Dsp/MatrixEngine.cpp   # S6
      git add Source/PluginProcessor.h Source/PluginProcessor.cpp     # S7
      ```
      S6 and S7 both touch the hot path and neither has been compiled or heard
      yet, so this is the one batch worth building and listening to before
      committing.

- [ ] **1. FxmeTools submodule** (only if S5, H4 or H5 is taken)
      ```
      cd lib/FxmeTools
      # dsp/FirFilter.h   — ScopedTryLock in process(), atomic hasImpulse flag (S5)
      # dsp/Ambisonics.h  — max-rE sampling decoder (H4)
      # dsp/MicCalibration.h/.cpp, dsp/IemDecoder.h/.cpp (H5)
      git commit && git push        # shared library: every plugin picks this up
      ```
- [ ] **2. Parent repo.** The pointer is already dirty (` M lib/FxmeTools`) from
      the `0c53e1f` PanelBackground commit, independently of anything above.
      ```
      git add lib/FxmeTools                    # bumped pointer
      git add CMakeLists.txt                   # S1
      git add .github/workflows/release.yml    # S2, S3, H2
      git add Source/PluginProcessor.cpp       # S4
      git add Source/PluginEditor.cpp Source/Components/*.h \
              Source/Dsp/*.cpp Tests/AnalysisTest.cpp     # R1-R5
      git add README.md                        # H1, H3
      ```
      Two commits read better than one: isolating the macOS release fix (S1-S3)
      makes it easy to point at from a release note.
- [ ] **3. Build and verify** (Olivier's to run, not the agent's)
      ```
      cmake -B build -DCMAKE_BUILD_TYPE=Release
      cmake --build build -j2 --target SuperMoTo_VST3
      cmake --build build -j2 --target SuperMoToTests SuperMoToMicCalTests SuperMoToSweepTests
      ctest --test-dir build
      ```
      The test targets are `EXCLUDE_FROM_ALL`, so they must be named explicitly.
      Then copy the `.vst3` into the VST3 folder and make the DAW rescan (the
      build does not install and the host caches the module).
