# Licensing

SuperMoTo is a JUCE plugin built on [FxmeTools](https://github.com/odoare/FxmeTools)
(vendored as the `lib/FxmeTools` submodule), which is itself split into a
framework-free half and a JUCE half under different licences. Which one applies
to a given file in this repository is stated in that file's own header as an
SPDX identifier; this document explains why, mirroring
`lib/FxmeTools/LICENSE.md`.

```
Everything in Source/ and Tests/    AGPL-3.0-or-later, or commercial terms
Source/Dsp/AmbisonicsDecode.h       LGPL-3.0-or-later
Tools/mic_cal.py                    LGPL-3.0-or-later
doc/ (the manual, the paper, the
  experiment scripts)               LGPL-3.0-or-later
```

## The plugin itself — AGPL-3.0-or-later, or commercial

SuperMoTo's matrix engine, measurement engine, analysis engine, GUI, processor
and editor all compile against JUCE and against `lib/FxmeTools/FxmeTools/`
(FxmeTools' own JUCE module). JUCE 8 is itself dual-licensed — AGPLv3, or a
commercial JUCE licence — and FxmeTools' JUCE module mirrors that shape rather
than fighting it. Distributing SuperMoTo therefore means one of:

    SPDX-License-Identifier: AGPL-3.0-or-later OR LicenseRef-FXME-Commercial

- **Under the AGPLv3** (full text in `LICENSE`): use, modify and distribute
  freely, including as a hosted service, provided the complete corresponding
  source is offered to anyone who receives the plugin (or uses it over a
  network). This pairs with using JUCE under its own AGPLv3 option.
- **Under commercial terms**: available from the author for anyone holding a
  commercial JUCE licence who does not wish to release under the AGPL.
  `LicenseRef-FXME-Commercial` refers to the same commercial terms FxmeTools
  itself offers; contact via [github.com/odoare](https://github.com/odoare) or
  www.fx-mechanics.com. A commercial grant here does not include a JUCE
  licence — that is separate, from Raw Material Software.

The five offline test executables under `Tests/` are in this half too: they
link JUCE modules and drive the plugin's own engines, so they derive from
whatever those do.

## The framework-free code — LGPL-3.0-or-later

    SPDX-License-Identifier: LGPL-3.0-or-later

- `Source/Dsp/AmbisonicsDecode.h` — the periphonic decoding helper the
  configuration tool calls. It includes no JUCE header, links no JUCE library
  and names no JUCE symbol; it takes loudspeaker directions in degrees and
  calls the ACN/SN3D spherical harmonics and max-rE sampling decoder in
  FxmeTools' framework-free half (`core/FxmeTools/dsp/Ambisonics.h`, itself
  LGPL). It is deliberately kept that way, as its own header comment says, so
  that the decoder maths can be used outside a JUCE project.
- `Tools/mic_cal.py` — the microphone-calibration file builder. A standalone
  numpy/measpy script that never sees the plugin.

Full text in `LICENSE.LGPL`. LGPLv3 is written as additional permissions on top
of GPLv3, so `LICENSE.LGPL.GPL` carries the GPLv3 text it incorporates; the two
are read together. Anyone is free to lift these files into a non-JUCE project
under the LGPL alone — which is exactly why they were kept JUCE-free in the
first place.

## Documentation

The manual (`doc/SuperMoTo.tex` and its chapters), the paper (`doc/paper.tex`),
the PDFs built from them and the validation scripts under `doc/experiments/`
carry `LGPL-3.0-or-later`, and that is deliberate rather than left over: they
are write-ups of the measurement method, the correction design and the
subwoofer alignment, they derive from no JUCE code, and the intent is that
their content can be quoted and reused alongside the framework-free half of the
library they describe.

## FxmeTools and WDL

The shared FX-Mechanics code is not in this repository. It lives in the
`lib/FxmeTools` submodule under that repository's own `LICENSE.md`: its
`core/` half (Biquad, the FFT and spectrum tools, the synchronized swept-sine,
the Ambisonics maths, the microphone calibration) is `LGPL-3.0-or-later`, and
its `FxmeTools/` JUCE module is AGPL/commercial on the same terms as above.

`lib/FxmeTools/WDL/` is a further submodule from Cockos Incorporated under the
zlib licence, with its own terms. It supplies the zero-latency convolution
engine behind the per-output FIR correction. It is unaffected by any of the
above.

---

Copyright (c) 2023-2026 Olivier Doaré.
