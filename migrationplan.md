# Migration plan: SuperMoTo → FxmeTools

Goal: stop depending on **FxmeFX** (only WDL was used) and **FxmeJuceTools**
(only `FxmeSlider`, `FxmeButton`, `FxmeLookAndFeel` were used). Make
**FxmeTools** the central shared library: a JUCE user-module holding the GUI
controls, plus reusable `dsp/` and `components/` code migrated out of SuperMoTo.
WDL is provided by FxmeTools' own nested submodule.

## Decisions (locked)

- **Namespace**: migrated shared code goes into `namespace fxme`; SuperMoTo's
  `smt::` references to moved types are renamed to `fxme::` at the call sites.
- **SPL meter**: extract a generic RMS-meter + signal-generator core into
  FxmeTools; the app-coupled routing (`MatrixEngine`, `MeasureMode`,
  `numConfigs`) stays in a thin SuperMoTo wrapper.
- **Theming**: each reusable component gets a public `struct Colours` + setter
  with neutral defaults; SuperMoTo injects its `SuperMoToTheme` palette.

## What the project actually depends on today

- **FxmeJuceTools** (JUCE user-module, `namespace fxme`, via `<JuceHeader.h>`,
  symlinked at `../JUCE/usermodules/FxmeJuceTools`): only `fxme::FxmeSlider`,
  `fxme::FxmeButton`, `fxme::FxmeLookAndFeel`.
- **FxmeFX**: only WDL — `convoengine.cpp`, `fft.c`, `resample.cpp` + the
  `convoengine.h` include in `Source/Dsp/FirFilter.h`. WDL already present at
  `lib/FxmeTools/WDL/WDL/`.
- **CI** (`.github/workflows/release.yml`): checks out FxmeJuceTools separately
  in 3 jobs — removed once FxmeTools is a submodule.

## Coupling analysis

| File | Reusability | Coupling to resolve |
|---|---|---|
| `Source/Dsp/Biquad.h` | Clean | none (only `<cmath>`), `namespace smt` |
| `Source/Dsp/FirFilter.h` | Clean | WDL include path; `namespace smt` |
| `Source/Dsp/SpectrumAnalyzer.h/.cpp` | Generic | FFT constants live in SpectrumTap.h |
| `Source/Dsp/SpectrumTap.h` | **Split** | `SpectrumTap` (ring buffer) + FFT constants generic; `SpectrumBus` + `numSpectrumTaps` depend on `smt::numChannels`/`maxFrameTaps` (app) |
| `Source/Components/SpectrumDisplay.h/.cpp` | Generic | ~18 `SuperMoToTheme::*` colour refs |
| `Source/Components/SpectrumAnalyzerComponent.h` | **Stays** | maps `SpectrumBus` taps to app colours — app glue |
| `Source/Components/SplMeterComponent.h` | Generic | `SuperMoToTheme::*` colours only |
| `Source/Dsp/SplMeterEngine.h/.cpp` | **Split** | `process()` takes `MatrixEngine&`, `MeasureMode`, `numConfigs`. Generic core = sliding-window RMS + sine/noise generator + tap |
| `Source/Components/InfoButton.h` | Generic | `SuperMoToTheme::*` colours only |
| VU meters | from FxmeJuceTools | `FxmeMeters.h` (`VerticalMeter`/`HorizontalMeter`) — brought along as part of the GUI set |

Recurring task: most components hard-reference `SuperMoToTheme` colours and
must be parametrised with a `Colours` struct.

## Target architecture

FxmeTools is a **JUCE user-module** (same mechanism as FxmeJuceTools — keeps the
`fxme::` namespace and `<JuceHeader.h>` include path working):

```
lib/FxmeTools/
  WDL/                        (existing submodule)
  FxmeTools/                  (the JUCE module)
    FxmeTools.h               module decl + namespace fxme { #include ... }
    FxmeTools.cpp / .mm
    components/   FxmeSlider, FxmeButton, FxmeMeters,
                  InfoButton, SpectrumDisplay, SplMeterComponent
    dsp/          Biquad, FirFilter, SpectrumTap(generic), SpectrumAnalyzer,
                  RmsMeter+SignalGenerator (SplMeter core)
    lookandfeels/ FxmeLookAndFeel
  cmake/FxmeTools.cmake       helper: adds the module + WDL sources/include
```

---

## Phase 0 — Scaffold FxmeTools as a JUCE module

1. In `lib/FxmeTools/`, create module folder `FxmeTools/` with:
   - `FxmeTools.h` — `BEGIN_JUCE_MODULE_DECLARATION` (ID `fxme_tools`, deps:
     `juce_audio_basics, juce_audio_utils, juce_core, juce_data_structures,
     juce_dsp, juce_events, juce_graphics, juce_gui_basics`), then
     `namespace fxme { #include ... }`.
   - `FxmeTools.cpp` (includes the `.cpp` units inside `namespace fxme`) and
     `FxmeTools.mm` (includes the `.cpp`), mirroring FxmeJuceTools.
   - Subfolders `components/`, `dsp/`, `lookandfeels/`.
2. Add `cmake/FxmeTools.cmake` helper: given a target, calls
   `juce_add_module(<this>/FxmeTools)`, links it, adds the three WDL sources +
   the `WDL/WDL` include dir. Keeps consumer CMake to ~2 lines.
3. Commit in the FxmeTools repo first (submodule), then bump the submodule
   pointer in SuperMoTo.

## Phase 1 — Bring the GUI controls over (kills the FxmeJuceTools dep)

4. Copy from FxmeJuceTools into `FxmeTools/components/` (+ `lookandfeels/`):
   **FxmeSlider**, **FxmeButton**, **FxmeLookAndFeel**, **FxmeMeters**
   (`VerticalMeter`/`HorizontalMeter`). Keep `namespace fxme` so SuperMoTo's
   existing references compile unchanged.
5. Wire these into `FxmeTools.h`/`.cpp` (replicate the include-the-cpp pattern).
6. No Theme coupling — they read JUCE slider/button ColourIds SuperMoTo already
   sets via `SuperMoToTheme::accentSlider` etc.

## Phase 2 — Move the clean DSP

7. Move **Biquad.h** → `dsp/Biquad.h`, **FirFilter.h** → `dsp/FirFilter.h`, both
   into `namespace fxme`. Change FirFilter's include from the FxmeFX relative
   path to `#include "convoengine.h"` (resolved via the WDL include dir).
8. Rename call sites: `smt::Biquad`→`fxme::Biquad` (3 files),
   `smt::FirFilter`→`fxme::FirFilter` (3 files: `OutputProcessor.h`,
   `MeasurementEngine.h`, `MatrixEngine.h`).

## Phase 3 — Move the spectrum stack (the realtime display)

9. **Split SpectrumTap.h**: generic ring-buffer `SpectrumTap` + FFT constants →
   `dsp/SpectrumTap.h` (`namespace fxme`). `SpectrumBus`, `numSpectrumTaps`, and
   the frame-route table **stay in SuperMoTo** (depend on `smt::numChannels`/
   `maxFrameTaps`) — relocate next to MatrixEngine as `smt::SpectrumBus`.
10. Move **SpectrumAnalyzer.h/.cpp** → `dsp/` (`namespace fxme`).
11. Move **SpectrumDisplay.h/.cpp** → `components/` (`namespace fxme`). Replace
    ~18 `SuperMoToTheme::*` refs with a public `struct Colours` + `setColours()`.
12. **SpectrumAnalyzerComponent.h stays in SuperMoTo**: maps `SpectrumBus` taps
    to `SuperMoToTheme` colours, calls `setColours()` with the app palette.
13. Rename `smt::SpectrumTap/SpectrumAnalyzer`→`fxme::` at call sites.

## Phase 4 — SPL meter (split)

14. Extract generic core into `dsp/` (`namespace fxme`): `RmsMeter`
    (sliding-window RMS → dBFS) and `SignalGenerator` (sine + white noise), plus
    the owned `SpectrumTap`. No `MatrixEngine`/`MeasureMode`/`numConfigs`.
15. SuperMoTo keeps a thin `smt::SplMeterEngine` owning the `fxme` core and
    retaining the app-coupled `process(…, MatrixEngine&, configActive)` routing.
16. Move **SplMeterComponent.h** → `components/` (`namespace fxme`); Theme refs →
    `Colours` struct + setter.

## Phase 5 — InfoButton

17. Move **InfoButton.h** → `components/` (`namespace fxme`); Theme refs →
    `Colours` struct + setter (`measure`, `text`, `panel`, `panelLine`).

## Phase 6 — Wire up build & CI, drop old deps

18. Edit `CMakeLists.txt`: remove `juce_add_module(../JUCE/usermodules/
    FxmeJuceTools)` and the `FxmeJuceTools` link; remove the FxmeFX/WDL block;
    add `include(lib/FxmeTools/cmake/FxmeTools.cmake)` + `fxmetools_attach(
    SuperMoTo)`. Route the test targets' WDL/Biquad needs through the helper too.
19. Edit `release.yml`: delete the three "Checkout/Install FxmeJuceTools" steps.
20. `git submodule deinit lib/FxmeFX` + remove from `.gitmodules` + `git rm`.
21. Delete the migrated originals from `Source/` and fix includes.

## Phase 7 — Verify

22. Self-review the diff (user builds). Checks: every `smt::`→`fxme::` rename
    complete (grep), no dangling `../Theme.h`/FxmeFX includes, WDL include path
    resolves, both offline test targets still link.

## Notes & risks

- `<JuceHeader.h>` reachability: moved `fxme::` types are reached the same way
  `fxme::FxmeSlider` is today — through the module umbrella.
- FirFilter in the module umbrella means every consumer needs the WDL include
  dir (the helper adds it). Alternative: leave FirFilter.h out of the master
  header and include by path if WDL should stay optional.
- `SpectrumBus` relocation (Phase 3) is the fiddliest cut — generic and
  app-specific code interleaved in one header.
- Order: Phases 1–2 are independent and low-risk; do them first to retire
  FxmeJuceTools + FxmeFX early, then tackle the spectrum/SPL splits.
