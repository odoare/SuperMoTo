/*
  ------------------------------------------------------------------------------
    OutputProcessor.h

    Per-output (per-loudspeaker) processing applied after the matrix sum and the
    output trim, and before the FIR correction: a 2-band cascaded IIR EQ
    (lowpass / highpass / bandpass for the bass-management crossover, peaking for
    correction) and a fractional time-alignment delay. Runs in place on the
    output buffer; no allocation on the audio thread. The coefficient math is
    shared with FrameProcessor's own 2-band EQ via BandFilter.h.

    Author: Olivier Doaré, github.com/odoare
    Licenced under the GNU LGPL Version 3.0
    SPDX-License-Identifier: LGPL-3.0-or-later
  ------------------------------------------------------------------------------
*/

#pragma once

#include <JuceHeader.h>     // fxme::Biquad comes via the FxmeTools module umbrella
#include "../Model/ConfigModel.h"
#include "BandFilter.h"

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
    void updateFilters()
    {
        activeBiquads = buildBiquadCascade (settings.bands.data(), (int) settings.bands.size(),
                                            sr, biquads, maxBiquads);
    }

    double sr = 44100.0;
    OutputSettings settings;

    static constexpr int maxBiquads = numOutputBands * 2;
    fxme::Biquad biquads[maxBiquads];
    int activeBiquads = 0;

    std::vector<float> delayLine;
    int writePos = 0;
    float delaySamples = 0.0f;
};

} // namespace smt
