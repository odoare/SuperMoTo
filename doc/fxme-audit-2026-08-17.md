# FX-Mechanics compliance audit — SuperMoTo

**Status: complete.** Opened and closed on 2026-08-17. Every finding is either
applied or closed by an explicit decision; nothing is left outstanding.

Audited commit `4293616` (branch `main`), with the FxmeTools submodule starting at
`0c53e1f`. The audit itself was static (greps and reads, nothing compiled); the
fixes were applied and committed incrementally, and Olivier smoke-tested the
realtime batch.

| | count | outcome |
|---|---|---|
| Silent bugs (S1-S7) | 8 | all applied |
| Retrofit (R1-R5) | 5 | all applied |
| House style, docs and CI (H1-H3) | 3 | all applied |
| House style, layering (H4-H7) | 4 | all applied |
| House style, controls (H8-H11) | 4 | closed, won't do (see below) |

Each finding was marked **safe to apply** (mechanical, no behaviour change) or
**decision** (changes behaviour, product surface, or a shared library). All eight
*decision* items were put to Olivier; four were taken (S5, S6, H4-H7 — two of them
deliberate visual changes) and four were declined as unnecessary (H8-H11).

Three findings turned out to be wrong or incomplete as first written, and the
corrections are recorded in place rather than quietly fixed: **S7** was more
serious than reported (a latent out-of-bounds write, not just an allocation),
**R4** had a second site the original grep pattern could not see, and the max-rE
comment corrected under **H4**. Two of the checklist's own standing predictions
did *not* apply to this project and are recorded under "Already correct" so a
future run does not re-raise them.

Revisions:

- 2026-08-17, initial audit.
- 2026-08-17, applied S1, S1b, S2 and S3 (the macOS release fixes).
- 2026-08-17, applied S4 (state version attribute).
- 2026-08-17, applied S5, S6 and S7 (realtime safety). Smoke-tested by Olivier.
- 2026-08-17, applied R1 (menu and tooltip accent). Added H11, the per-panel
  alternative it surfaced.
- 2026-08-17, applied R2 and R3 (the JUCE 8 deprecation sweep).
- 2026-08-17, applied R4, including a second site the initial audit missed.
- 2026-08-17, applied R5. **The retrofit section is complete.**
- 2026-08-17, applied H1, H2 and H3 (docs and CI). **Every finding marked
  "safe to apply" is done.**
- 2026-08-17, applied H4 (ambisonics layering) and H5 (MicCalibration and
  IemDecoder into FxmeTools).
- 2026-08-17, applied H6 and H7 (house backdrop and latching buttons), both
  deliberate visual changes.
- 2026-08-17, **H8-H11 closed as "won't do" by decision. Audit complete.**

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

- [x] **R1** Call `setAccentColour()` on the editor's look-and-feel.
      **safe to apply** — APPLIED 2026-08-17
      [Source/PluginEditor.cpp:47-57](../Source/PluginEditor.cpp#L47-L57). The
      editor set `fxmeLookAndFeel` but never tinted it, so all ~40 drop-down
      menus and all 26 tooltips rendered neutral grey against cyan/teal/rose
      panels, while `presetBar` and `presetPane` already had theirs
      ([PluginEditor.cpp:117](../Source/PluginEditor.cpp#L117),
      [:213](../Source/PluginEditor.cpp#L213)). Tinted with
      `SuperMoToTheme::master` for consistency with those two.

      The accent reaches the menu panel's hairline, the highlighted row, the tick
      marking the current selection in a long list, and the tooltip hairline.

      Note that this project owns a single `FxmeLookAndFeel` on the editor rather
      than one per panel, so every menu and tooltip gets the same tint regardless
      of which page opened it. See H11 for the per-panel alternative.

- [x] **R2** Migrate 27 deprecated `juce::Font (float[, int])` calls to
      `juce::Font (juce::FontOptions (...))`. **safe to apply** — APPLIED 2026-08-17
      All 27 sites across 8 files, verified 1:1 (the diff is exactly 27
      insertions and 27 deletions, so nothing collateral was rewritten).
      `juce::FontOptions (float)` and `(float, int styleFlags)` map directly onto
      the two deprecated forms and set the same JUCE height, so nothing changes
      visually.
    - [x] PluginEditor.cpp (2), FrameEditorComponent.h (2),
          OutputEditorComponent.h (2), ConfigToolComponent.h (2)
    - [x] AnalysisComponent.h (5), CalibrationComponent.h (5),
          GroupAnalysisComponent.h (7), TransferFunctionPlot.h (2)

      The ten bare `g.setFont (11.0f)` calls were left alone (MatrixComponent.cpp,
      TransferFunctionPlot.h, ConfigToolComponent.h:142): that is the genuine
      `Graphics` overload, not deprecated.

      Two sites in `TransferFunctionPlot.h` were not plain `Label::setFont` calls
      but temporary fonts built for `GlyphArrangement::getStringWidth`. Wrapping
      them pushed both lines past 120 columns, so:
      [TransferFunctionPlot.h:575-586](../Source/Components/TransferFunctionPlot.h#L575-L586)
      now hoists one `legendFont` out of the legend loop (it was being rebuilt per
      entry on every repaint) and the readout site is simply wrapped. Both keep
      measuring with a *plain* 11 px font while `g.setFont (11.0f)` continues to
      set only the height, preserving whatever style the Graphics carries —
      exactly what the code did before.

- [x] **R3** Migrate 4 deprecated `createWriterFor` calls to the
      `AudioFormatWriterOptions` overload. **safe to apply** — APPLIED 2026-08-17
      [AnalysisEngine.cpp:1019](../Source/Dsp/AnalysisEngine.cpp#L1019) and
      [:1046](../Source/Dsp/AnalysisEngine.cpp#L1046) (1 channel),
      [MeasurementEngine.cpp:285](../Source/Dsp/MeasurementEngine.cpp#L285) and
      [Tests/AnalysisTest.cpp:72](../Tests/AnalysisTest.cpp#L72) (2 channels).

      Each local is now declared `std::unique_ptr<juce::OutputStream>` rather than
      to the concrete `FileOutputStream`, because the new overload binds it by
      reference; the test's `auto` had to change for the same reason. Ownership
      moves out only on success, so all four `stream.release()` calls are gone and
      the failure paths free the stream naturally.

      **Byte-identical output**, checked rather than assumed: the deprecated
      overload forwards to exactly `withSampleRate` / `withNumChannels` /
      `withBitsPerSample` / `withMetadataValues` / `withQualityOptionIndex` and
      leaves `sampleFormat` at `automatic`. Passing the same three values (the
      metadata was empty and quality defaults to 0, which WAV ignores anyway)
      reproduces the old call. That matters here: 32-bit WAV is the one case where
      `SampleFormat` decides between integer and float PCM, and these files are
      re-read by the analysis engine and loaded as FIR impulses.

      Half of this is covered by an existing test target: `SuperMoToTests`
      compiles both `AnalysisEngine.cpp` and `AnalysisTest.cpp`, so building and
      running it exercises 2 of the 4 sites without touching the plugin. See H2.

- [x] **R4** Use the typed setter instead of the raw property.
      **safe to apply** — APPLIED 2026-08-17
      Both are exactly equivalent: the typed setters do nothing but the same
      `getProperties().set()` call, so this is naming only.
    - [x] [BandEqEditor.h:148](../Source/Components/BandEqEditor.h#L148)
          `getProperties().set ("showLabel", true)` becomes `setShowLabel (true)`
          ([FxmeSlider.h:89](../lib/FxmeTools/FxmeTools/components/FxmeSlider.h#L89)).
    - [x] [BandEqEditor.h:59](../Source/Components/BandEqEditor.h#L59)
          `getProperties().set ("centralValue", 0.0)` becomes
          `setCentralValue (0.0)`
          ([FxmeSlider.h:84](../lib/FxmeTools/FxmeTools/components/FxmeSlider.h#L84)).

      **The second site was missed by the original audit.** R4 was found by
      grepping `showLabel` and R5 by grepping `drawFromCentre|setCentralValue`,
      and neither pattern catches a raw `"centralValue"` string literal. Closed
      the category properly this time by grepping
      `getProperties() *\. *set` across `Source/`, which finds all three raw
      property writes in the project. The only one left is R5's
      `drawFromCentre`, which needs a different fix rather than a rename (there
      is deliberately no `setDrawFromCentre`, because `setCentralValue` is the
      form that stays correct on an asymmetric range).

- [x] **R5** Replace `drawFromCentre` with `setCentralValue (0.0)`.
      **safe to apply** — APPLIED 2026-08-17
      [AnalysisComponent.h:288-291](../Source/Components/AnalysisComponent.h#L288-L291)
      on `alignSlider`. Renders identically today and stays correct if the range
      ever stops being symmetric: `originProportion()` prefers `centralValue` and
      resolves it through `valueToProportionOfLength()`, which on a linear,
      unskewed -40 to +40 range returns exactly the 0.5 that `drawFromCentre`
      hardcoded.

      This closes the retrofit section. The project now has no raw
      `getProperties().set()` writes and no `drawFromCentre` left, and both of its
      bipolar controls (this one and `BandEqEditor`'s band gain) use the same
      `setCentralValue` idiom.

---

## House style

Structural, and may be larger than it looks.

### Docs and CI

- [x] **H1** Add an Installing section to the README (macOS quarantine).
      **safe to apply** — APPLIED 2026-08-17
      New [Installing](../README.md) section ahead of Building, with a
      "macOS — read this first" subsection. It states plainly that the builds are
      not signed with an Apple Developer ID and that a DAW therefore skips the
      plug-in **silently** during the scan (no error, no dialog), gives the
      `xattr -dr com.apple.quarantine` command for the VST3, the AU and the
      standalone app, covers the unsigned `.pkg` needing right-click then Open,
      and states the supported version (10.13+, following the deployment target
      fixed in S1b). A short Linux/Windows subsection gives the VST3 folders.

      The per-host MIDI routing section deliberately does **not** apply here
      (`NEEDS_MIDI_INPUT FALSE`, `acceptsMidi()` returns false).

- [x] **H2** Run the three registered tests in CI. **safe to apply** — APPLIED
      2026-08-17
      New "Build & run offline tests" step in the Linux job
      ([release.yml:43-56](../.github/workflows/release.yml#L43-L56)), placed
      between the build and Package so a failure blocks packaging and, through
      `needs:`, the release job itself.

      The targets are `EXCLUDE_FROM_ALL`, so they are named explicitly. Checked
      before adding this: none of the three links `juce_gui_basics` or
      `juce_recommended_lto_flags`, so they are headless-safe on the runner and
      cheap to build. `ctest --output-on-failure` exits non-zero on a failure,
      which is the whole point of the step. Linux only, since the code under test
      is platform-independent and running it three times would only cost CI
      minutes.

      **Caveat:** these tests have never run in CI before, so if any of them is
      currently failing the next tagged release will go red. Run them locally once
      first (see H2's commands in the commit plan).

- [x] **H3** Fix the README build line. **safe to apply** — APPLIED 2026-08-17
      Now `cmake --build build -j2 --target SuperMoTo_VST3`, with a sentence
      explaining why (a Release build links with `-flto`, and a full-parallelism
      link of every format at once needs more RAM than most machines have spare).
      Also added the two things a reader needed and the README did not say: that
      builds do not install themselves and the host caches the module, and a
      Tests subsection naming the three `EXCLUDE_FROM_ALL` targets plus `ctest`.

### Layering against FxmeTools

- [x] **H4** `Source/Dsp/AmbisonicsDecode.h` duplicates `fxme::ambi`.
      **decision** — APPLIED 2026-08-17

      **Added to FxmeTools** ([dsp/Ambisonics.h](../lib/FxmeTools/FxmeTools/dsp/Ambisonics.h),
      +96 lines, purely additive so nothing else in the family is affected):
    - `maxREGain (order, degree)`, the per-degree max-rE decoder weight, next to
      the existing `diffuseFieldOrderGain`.
    - `samplingDecodeMatrix (order, dirs)`, the decode counterpart of
      `encodeSN3D`. FxmeTools had an encoder but no decoder, so this is genuinely
      new shared functionality rather than a second copy of anything.
    - Both listed in the header's API summary. `<vector>` added to its includes.

      **Removed from the project.** `Source/Dsp/AmbisonicsDecode.h` drops from 137
      lines to 65 and is now only the adapter that is genuinely
      SuperMoTo-specific: `SpeakerDir` (degrees, as the config tool's GUI collects
      it) and a `decodeMatrix` that converts to unit vectors and delegates. The
      local `numHarmonics`, `degreeForAcn`, `sn3d` and `maxRe` are gone, and their
      five call sites in `IemDecoder.cpp` and `ConfigToolComponent.h` now use
      `fxme::ambi::channelsForOrder` / `orderOfChannel` / `maxREGain` directly.
      `IemDecoder.cpp` no longer includes the adapter at all, since it only ever
      wanted the generic helpers.

      **Equivalence verified numerically, not by inspection.** All 16 SH terms and
      `directionFromAngles` matched the local formulas character for character on
      review, but a decode matrix is exactly the wrong place to trust a read-through,
      so the old implementation was extracted from git and diffed against the new
      path in a standalone compile (`Ambisonics.h` is JUCE-free, so this needs no
      plugin build): **1624 coefficients** over orders 1-3 and five layouts (8, 12
      and 16-speaker rings, a 13-speaker dome, and a deliberately irregular
      7-speaker rig).

      | | |
      |---|---|
      | worst absolute difference | 8.194e-08 |
      | worst relative difference | 1.392e-06 |
      | sign flips (\|coeff\| > 1e-6) | none |

      The residual is the old code evaluating the harmonics in `double` where
      `fxme::ambi` uses `float`, and it sits about 1200x below the 1e-4 threshold
      at which `ConfigToolComponent` prunes coefficients. The only sign
      disagreements are on coefficients that are numerically zero (2e-17 in double
      against -1.6e-8 in float, for a speaker at exactly 90 degrees), and both
      paths prune those. Existing saved configs are untouched either way: the
      config tool writes frame gains into the model when Apply is pressed, so only
      a newly applied decode goes through this code at all.

      One thing this surfaced: the max-rE table is `a_l = P_l(r_E)`, Legendre
      *evaluated* at the largest root of `P_order+1`, not `r_E^l`. (For order 2,
      `r_E = 0.774597` but `a_2 = 0.4`, not 0.6.) The original comment said "raised
      to the degree", which was wrong; the FxmeTools version documents it
      correctly and keeps the shipped numbers verbatim.

- [x] **H5** Two more generic files sitting in `Source/`. **decision** —
      APPLIED 2026-08-17
      Both moved into the submodule as `fxme::MicCalibration` and
      `fxme::IemDecoder`, in `lib/FxmeTools/FxmeTools/dsp/`.
    - [x] `MicCalibration.{h,cpp}` — the REW / miniDSP / Dayton / FRD text
          calibration-file parser and its log-frequency interpolated curve. The
          *curve type* is what moved; owning the process-wide instance and
          remembering its stored path stays in `Source/AppSettings.h`, which is
          genuinely application state. The header now says so.
    - [x] `IemDecoder.{h,cpp}` — the IEM AllRADecoder JSON parser. It sits next
          to `Ambisonics.h`, whose helpers it uses, and its doc now frames it as
          the alternative to `ambi::samplingDecodeMatrix` for an irregular rig
          rather than as a SuperMoTo config-tool feature.

      Wiring: `namespace smt` became `namespace fxme` in all four files, the two
      headers were added to the `FxmeTools.h` umbrella under a new
      "DSP with out-of-line definitions" heading, and the two `.cpp` files to the
      `FxmeTools.cpp` unity build. `IemDecoder.cpp`'s include of Ambisonics
      became an intra-module relative one and its `fxme::ambi::` calls became
      plain `ambi::`.

      19 references updated across 8 project files. `IemDecoder` had only one
      consumer (`ConfigToolComponent`); `MicCalibration` had eight.

      **Two traps worth recording.**
    - `MeasurementEngine.cpp` uses `"MicCalibration"` as an **XML element name**
      at three places (lines 379, 414, 540) — those are persisted state keys in
      every `measurement.xml` already written, so a blanket rename would have
      silently broken reading existing measurement folders. The renames were done
      individually rather than with a sed, and those three strings are untouched.
    - `SuperMoToMicCalTests` compiled `Source/Dsp/MicCalibration.cpp` directly and
      had **no FxmeTools include directory** at all. Both test targets now point
      at the submodule path, and that target gained the include dir it needed. The
      two tests still avoid linking the whole module (they have no GUI
      dependencies), which is why the files are compiled directly rather than via
      `fxmetools_attach`.

      Verified: a full `cmake` configure is clean, so the new paths and include
      dirs resolve. Not verified: compilation. `cmake --build build -j2 --target
      SuperMoToMicCalTests && ctest --test-dir build -R miccal` is the cheap check
      — two translation units, no GUI, no LTO — and it exercises the moved file,
      the namespace change and the new include path in one go.

- [x] **H6** `Theme.h::paintBackground` predates `fxme::PanelBackground`.
      **decision** — APPLIED 2026-08-17
      [Theme.h:45-55](../Source/Theme.h#L45-L55) now delegates to
      `fxme::paintComponentBackground (g, b, master)`. The old hand-rolled
      gradient over `rgba(0.15, 0.15, 0.25)` is gone, so the backdrop is
      near-black with a whisper of the master teal, matching the rest of the
      family. **This is a visible change**: the window was dark blue-purple and is
      now near-black. The `panel` colour (0xff20202c) still sits lighter than the
      new backdrop, so panel-against-background contrast is preserved.

      `paintComponentBackground` rather than `paintTintedBackground`: the reference
      assigns the latter to "the outermost editor" because in FxmeFX an effect
      component covers most of it, whereas this editor fills the plugin window
      itself. The component variant is also the aspect-ratio-stable one (its
      gradient runs corner to corner), which matters over a 1100x720 to 2400x1600
      resize range. `paintBackground` had to move below the colour declarations in
      Theme.h so it can reference `master`.

- [x] **H7** Hand-coloured `juce::TextButton`s where `fxme::AccentToggle` now
      exists. **decision** — APPLIED 2026-08-17
      All thirteen are now `fxme::AccentToggle`: the six view buttons, the six
      Edit A..F buttons and the collapse button. No `juce::TextButton` and no
      `juce::TextButton::` colour id is left anywhere in the editor.

      Three things worth recording, because none of them was a straight swap:
    - **Click-latching turned off on every one.** `AccentToggle`'s constructor
      calls `setClickingTogglesState (true)`, but all thirteen have a state
      derived from application state (which view is up, which config is edited,
      whether the window is collapsed) and set by `setView` / `setEditConfig` /
      `setCollapsed`. Left latching, clicking the already-selected view button
      would flip it off before `setView` put it back. Each therefore calls
      `setClickingTogglesState (false)`, preserving exactly the previous
      behaviour.
    - **A new `viewSelected` colour, because the house default assumes a bright
      accent.** `AccentToggle::setAccent` hardcodes black "on" text, which is
      right for a gold or bright cyan but not for `master` (0xff007070): measured,
      black on that is **3.55:1**, below the 4.5:1 AA threshold for the ~13 px
      bold text it draws. `SuperMoToTheme::viewSelected` is `master.brighter(0.5)`
      (#55a0a0), which measures **6.92:1**. The Edit A..F buttons use
      `configEngage` raw, which is already 16.75:1. Contrast figures computed, not
      eyeballed.
    - **Keyboard focus.** `AccentToggle` also sets
      `setMouseClickGrabsKeyboardFocus (false)`, which these buttons did not do
      before. That is the house behaviour for view and transport buttons and is an
      improvement here (clicking one no longer pulls focus away from a text field
      or from the matrix's arrow-key navigation). Checked that nothing depends on
      the old focus-grab to commit a pending edit: every text field in the project
      is read with `getText()` at action time rather than relying on a
      focus-loss commit, and switching view only hides a panel.

### Controls — all four closed, won't do

Raised for confirmation rather than as defects, reviewed with Olivier on
2026-08-17, and **deliberately declined**. Each is a place where the house
default and this project's design disagree and the project is right. Recorded
here so a later audit does not re-raise them as fresh findings.

- [x] **H8** Four bare `juce::Slider`s with a text box. **CLOSED — won't do**
      [CalibrationComponent.h:771](../Source/Components/CalibrationComponent.h#L771)
      (`sineAmp`, `sineFreq`, `noiseAmp`, `splRef`), configured at
      [:423-432](../Source/Components/CalibrationComponent.h#L423-L432) as
      `IncDecButtons` + `TextBoxLeft`. The one place the project departs from
      `fxme::FxmeSlider`, and the code comment says it is deliberate.
      `IncDecButtons` is not a style `FxmeLookAndFeel` custom-draws, and
      `FxmeSlider` removes the text box these entries exist to provide. Swapping
      them would lose the type-a-number affordance to gain nothing.

- [x] **H9** Knobs use separate `juce::Label`s rather than `setShowLabel(true)`.
      **CLOSED — won't do**
      Every component except `BandEqEditor` pairs its `FxmeSlider` with an
      `addLabel()` helper (for example
      [AnalysisComponent.h:481-487](../Source/Components/AnalysisComponent.h#L481-L487)).
      The house default is `setName()` + `setShowLabel(true)` with no separate
      label, but these are horizontal bar sliders in dense two-column panels where
      a label to the left is the better layout — `setShowLabel` puts the label
      *below* the control, which is right for a knob and wrong for a row of bars.

- [x] **H10** Grey out `Level` and `Dim` while `Mute` is engaged.
      **CLOSED — won't do**
      No automatable APVTS parameter actually supersedes another control here, so
      the house `setEnabled()` rule has nothing to bite on. `Mute` / `Dim` /
      `Level` are the one mutually redundant trio, and doing it correctly would
      need a ~10 Hz `Timer` poll so host automation is not missed — real
      machinery for a purely cosmetic gain. The existing six `setEnabled()` uses
      already cover every case that matters.

- [x] **H11** Per-panel menu tinting (surfaced while applying R1).
      **CLOSED — won't do**
      The house pattern is one `FxmeLookAndFeel` per component so each panel's
      drop-down menus match its own accent. This project owns a single one on the
      editor ([PluginEditor.h:79](../Source/PluginEditor.h#L79)) that every panel
      inherits, so R1's accent is necessarily global: the rose Calibration page
      and the lime Analysis page share one menu tint.

      Doing it per panel would mean giving `ConfigToolComponent`,
      `CalibrationComponent`, `AnalysisComponent`, `GroupAnalysisComponent` and
      `BandEqEditor` each their own `FxmeLookAndFeel` member, declared first so it
      outlives its children, with `setLookAndFeel(nullptr)` in each destructor.
      Five files, purely cosmetic, and the destructor ordering is exactly the kind
      of thing that dangles when got wrong. Drop-downs are transient overlays, and
      one consistent menu appearance across the whole plugin is a defensible
      design choice rather than a compromise.

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

**Bipolar controls.** Swept every `setRange` with a negative lower bound while
applying R5. Only two controls are meant to read from a centre and both now use
`setCentralValue`. `CalibrationComponent`'s `level`, `sineAmp` and `noiseAmp` run
-60 to 0 dB, so they are not bipolar at all. `ConfigToolComponent`'s per-speaker
gain and `FrameEditorComponent`'s frame level are -60 to +12 dB: growing the fill
from the minimum is the fader convention and reads correctly, and forcing a
centre on a range that asymmetric is exactly what the house guidance warns
against. Left alone deliberately, not overlooked.

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

Recorded so the limits of this audit are on the record, not as outstanding work.

**What a static pass cannot see.** The JUCE 8 deprecations surface only as
compiler warnings, and the audit never compiled anything. R2 and R3 covered every
site a grep can find (27 `juce::Font` constructions and 4 `createWriterFor` calls,
all migrated), but a grep cannot see a deprecated call reached through a typedef
or a template. Nothing further is expected; if the next Release build warns about
either, it is a site of that kind and a one-line fix of the same shape.

**What was applied but not compiled by the agent.** Per the working convention,
Olivier builds. Every change here was self-reviewed, and where a mistake would
have been expensive it was verified by other means rather than by inspection
alone:

- **S6/S7** (hot path): reviewed against the JUCE source for `replaceState`
  semantics and `AudioBuffer` windowing; smoke-tested by Olivier.
- **S1-S3** (macOS release): the new verify step's shell body was run against a
  stubbed `lipo` in four scenarios (universal passes, arm64-only / x86_64-only /
  missing bundle each fail with all bundles reported).
- **H4** (ambisonics): the old and new decode paths were compiled standalone and
  diffed over 1624 coefficients, worst error 8.194e-08.
- **H5** (file moves) and **H6/H7** (GUI): validated by a clean `cmake` configure.
  H6 and H7 change how the plugin looks, so they need eyes rather than a compiler.

**Not covered by this audit at all.** Plugin-type and Logic/AU specifics beyond
the MIDI-on-`aufx` check — run `/au-logic-audit` for that matrix. The audit also
did not exercise the plugin in a host: preset round-tripping, the Ambisonics
config tool against a real rig, and the measurement/analysis chain were all
reasoned about, not played.

---

## Commit plan

Everything up to and including H5 is committed, in the parent and in the
submodule (`9334d1e` there, pointer bumped). What remains uncommitted is H6 and
H7 plus this document.

- [ ] **H6 + H7, the two visual changes**
      ```
      git add Source/Theme.h Source/PluginEditor.h Source/PluginEditor.cpp
      git add doc/fxme-audit-2026-08-17.md
      ```
      Worth one commit of their own, and worth looking at the running plugin
      first: the backdrop changes from dark blue-purple to near-black, and
      thirteen buttons change from hand-coloured `juce::TextButton` to
      `fxme::AccentToggle`. If the new look is wrong, this is the commit to
      revert, and nothing else depends on it.

      Specifically worth a look: the near-black backdrop against the
      `0xff20202c` panels, the legibility of a selected view button (black text on
      `viewSelected`, measured at 6.92:1), and the ▲ collapse glyph, which
      `AccentToggle` draws at about 12.4 px bold in its 28x20 box rather than at
      the look-and-feel's default size.

- [ ] **Build and verify** (Olivier's, not the agent's)
      ```
      cmake -B build -DCMAKE_BUILD_TYPE=Release
      cmake --build build -j2 --target SuperMoTo_VST3
      cmake --build build -j2 --target SuperMoToTests SuperMoToMicCalTests SuperMoToSweepTests
      ctest --test-dir build --output-on-failure
      ```
      The test targets are `EXCLUDE_FROM_ALL`, so they must be named explicitly.
      Then copy the `.vst3` into the VST3 folder and make the DAW rescan (the
      build does not install and the host caches the module).

- [ ] **Before the next tag.** CI now runs `ctest` on every tagged release
      (H2), and it never did before. Confirm the three tests pass locally first,
      rather than discovering a pre-existing failure during a release.

- [ ] **Re-cut any macOS release built before `c3c788c`.** Those artefacts are
      very likely arm64-only with the wrong deployment target (S1-S3), and the old
      verify step could not fail, so they went out green.

---

## If this project is audited again

Start from "Already correct" and this section, not from a blank checklist.

- The two family-wide predictions that **do not apply here**: combo boxes are
  fine because the look-and-feel is set on the editor rather than per widget, and
  the `TooltipWindow` is correctly owned and parented.
- **H8-H11 are settled "won't do"**, with reasons. Do not re-raise them.
- Grep `getProperties() *\. *set` rather than individual property names when
  checking for raw look-and-feel property writes — that is the pattern that
  finds all of them (the lesson from R4).
- `MeasurementEngine.cpp` uses `"MicCalibration"` as a **persisted XML element
  name**. Never include it in a bulk rename.
- `MicCalibration` and `IemDecoder` now live in FxmeTools; two test targets
  compile `MicCalibration.cpp` straight from the submodule path.
