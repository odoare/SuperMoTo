/*
  ------------------------------------------------------------------------------
    BandFilter.h

    Shared cascaded-biquad builder for a FrameBand array: turns the enabled
    bands of a frame's or an output's EQ into a flat fxme::Biquad chain. Used
    by both FrameProcessor and OutputProcessor so the coefficient logic (2nd
    vs 4th order LP/HP/BP, peaking) exists in exactly one place.

    Author: Olivier Doaré, github.com/odoare
    Licenced under the GNU AGPL Version 3.0, or commercial terms (LICENSE.md)
    SPDX-License-Identifier: AGPL-3.0-or-later OR LicenseRef-FXME-Commercial
  ------------------------------------------------------------------------------
*/

#pragma once

#include <JuceHeader.h>     // fxme::Biquad comes via the FxmeTools module umbrella
#include "../Model/ConfigModel.h"

namespace smt
{

/** Rebuilds `biquads` (capacity `maxBiquads`, must be >= 2 * numBands) from
    the enabled bands in `bands`. Each LP/HP/BP band is 1 (2nd order) or 2
    (4th order) biquads; a peaking band is always 1 biquad. Returns the
    number of biquads actually written (the caller's active count). Callers
    must reset() the returned range before use. No allocation. */
inline int buildBiquadCascade (const FrameBand* bands, int numBands, double sr,
                               fxme::Biquad* biquads, int maxBiquads)
{
    int n = 0;
    for (int i = 0; i < numBands; ++i)
    {
        // Safety net: every band needs at most 2 biquads, so this should
        // never trigger given callers size maxBiquads == 2 * numBands.
        if (n + 2 > maxBiquads)
            break;

        const auto& band = bands[i];
        if (! band.on)
            continue;

        const auto  type = (FilterType) band.type;
        const float f = band.freq;
        const float q = band.q;

        if (type == FilterType::peaking)
        {
            biquads[n++].c = fxme::BiquadCoeffs::peaking (sr, f, q, band.gainDb);
        }
        else if (band.order >= 4)
        {
            const float scale = q / 0.707f;
            const float q1 = 0.54119610f * scale;
            const float q2 = 1.30656296f * scale;
            switch (type)
            {
                case FilterType::lowpass:
                    biquads[n++].c = fxme::BiquadCoeffs::lowpass (sr, f, q1);
                    biquads[n++].c = fxme::BiquadCoeffs::lowpass (sr, f, q2);
                    break;
                case FilterType::highpass:
                    biquads[n++].c = fxme::BiquadCoeffs::highpass (sr, f, q1);
                    biquads[n++].c = fxme::BiquadCoeffs::highpass (sr, f, q2);
                    break;
                case FilterType::bandpass:
                    biquads[n++].c = fxme::BiquadCoeffs::bandpass (sr, f, q);
                    biquads[n++].c = fxme::BiquadCoeffs::bandpass (sr, f, q);
                    break;
                default: break;
            }
        }
        else
        {
            switch (type)
            {
                case FilterType::lowpass:  biquads[n++].c = fxme::BiquadCoeffs::lowpass  (sr, f, q); break;
                case FilterType::highpass: biquads[n++].c = fxme::BiquadCoeffs::highpass (sr, f, q); break;
                case FilterType::bandpass: biquads[n++].c = fxme::BiquadCoeffs::bandpass (sr, f, q); break;
                default: break;
            }
        }
    }

    for (int i = 0; i < n; ++i)
        biquads[i].reset();

    return n;
}

} // namespace smt
