/*
  ------------------------------------------------------------------------------
    OutputProcessor.h

    Per-output (per-loudspeaker) processing applied after the matrix sum and the
    output trim, and before the FIR correction: a 4-band cascaded IIR EQ
    (lowpass / highpass / bandpass for the bass-management crossover, peaking for
    correction) and a fractional time-alignment delay. Runs in place on the
    output buffer; no allocation on the audio thread.

    Author: Olivier Doaré, github.com/odoare
    Licenced under the GNU LGPL Version 3.0
    SPDX-License-Identifier: LGPL-3.0-or-later
  ------------------------------------------------------------------------------
*/

#pragma once

#include <JuceHeader.h>
#include "Biquad.h"
#include "../Model/ConfigModel.h"

namespace smt
{

class OutputProcessor
{
public:
    void prepare (double sampleRate, int /*maxBlockSize*/)
    {
        sr = sampleRate;
        delayLine.assign ((size_t) (maxDelayMs * 0.001 * sr) + 8, 0.0f);
        reset();
        applySettings (settings, true);
    }

    void reset()
    {
        for (auto& b : biquads)
            b.reset();
        std::fill (delayLine.begin(), delayLine.end(), 0.0f);
        writePos = 0;
    }

    /** Audio-thread update from the model (no allocation). */
    void applySettings (const OutputSettings& s, bool force = false)
    {
        const bool filterChanged = force || s.bands != settings.bands;
        settings = s;

        if (filterChanged)
            updateFilters();

        delaySamples = juce::jlimit (0.0f, (float) delayLine.size() - 2.0f,
                                     (float) (settings.delayMs * 0.001 * sr));
    }

    /** EQ (and optionally the delay) in place. */
    void process (float* data, int n, bool applyDelay = true)
    {
        if (activeBiquads > 0)
        {
            for (int i = 0; i < n; ++i)
            {
                float v = data[i];
                for (int b = 0; b < activeBiquads; ++b)
                    v = biquads[(size_t) b].processSample (v);
                data[i] = v;
            }
        }

        if (! applyDelay || delaySamples < 0.5f)
            return;

        const int dlSize = (int) delayLine.size();
        const int di = (int) delaySamples;
        const float frac = delaySamples - (float) di;

        for (int i = 0; i < n; ++i)
        {
            delayLine[(size_t) writePos] = data[i];

            int r0 = writePos - di;   if (r0 < 0) r0 += dlSize;
            int r1 = r0 - 1;          if (r1 < 0) r1 += dlSize;
            data[i] = delayLine[(size_t) r0] * (1.0f - frac)
                    + delayLine[(size_t) r1] * frac;

            if (++writePos >= dlSize)
                writePos = 0;
        }
    }

private:
    // Rebuild the flat biquad cascade from the enabled bands. Each LP/HP/BP band
    // is 1 (2nd order) or 2 (4th order) biquads; a peaking band is 1 biquad.
    void updateFilters()
    {
        int n = 0;
        for (const auto& band : settings.bands)
        {
            if (! band.on)
                continue;

            const auto  type = (FilterType) band.type;
            const float f = band.freq;
            const float q = band.q;

            if (type == FilterType::peaking)
            {
                biquads[(size_t) n++].c = BiquadCoeffs::peaking (sr, f, q, band.gainDb);
            }
            else if (band.order >= 4)
            {
                const float scale = q / 0.707f;
                const float q1 = 0.54119610f * scale;
                const float q2 = 1.30656296f * scale;
                switch (type)
                {
                    case FilterType::lowpass:
                        biquads[(size_t) n++].c = BiquadCoeffs::lowpass (sr, f, q1);
                        biquads[(size_t) n++].c = BiquadCoeffs::lowpass (sr, f, q2);
                        break;
                    case FilterType::highpass:
                        biquads[(size_t) n++].c = BiquadCoeffs::highpass (sr, f, q1);
                        biquads[(size_t) n++].c = BiquadCoeffs::highpass (sr, f, q2);
                        break;
                    case FilterType::bandpass:
                        biquads[(size_t) n++].c = BiquadCoeffs::bandpass (sr, f, q);
                        biquads[(size_t) n++].c = BiquadCoeffs::bandpass (sr, f, q);
                        break;
                    default: break;
                }
            }
            else
            {
                switch (type)
                {
                    case FilterType::lowpass:  biquads[(size_t) n++].c = BiquadCoeffs::lowpass  (sr, f, q); break;
                    case FilterType::highpass: biquads[(size_t) n++].c = BiquadCoeffs::highpass (sr, f, q); break;
                    case FilterType::bandpass: biquads[(size_t) n++].c = BiquadCoeffs::bandpass (sr, f, q); break;
                    default: break;
                }
            }
        }

        for (int i = 0; i < n; ++i)
            biquads[(size_t) i].reset();
        activeBiquads = n;
    }

    double sr = 44100.0;
    OutputSettings settings;

    static constexpr int maxBiquads = numFrameBands * 2;
    Biquad biquads[maxBiquads];
    int activeBiquads = 0;

    std::vector<float> delayLine;
    int writePos = 0;
    float delaySamples = 0.0f;
};

} // namespace smt
