/*
  ------------------------------------------------------------------------------
    OutputProcessor.h

    Per-output (per-loudspeaker) processing applied after the matrix sum and the
    output trim, and before the FIR correction: a 2-band cascaded IIR EQ
    (lowpass / highpass / bandpass for the bass-management crossover, peaking for
    correction) and a time-alignment delay. Runs in place on the
    output buffer; no allocation on the audio thread. The coefficient math is
    shared with FrameProcessor's own 2-band EQ via BandFilter.h.

    The delay is rounded to a whole number of samples and read straight out of
    the line. Half a sample is 11 us at 44.1 kHz, 4 mm of path, and a relative
    error of one sample between two outputs puts its first cancellation notch
    at fs/2, so it can never comb inside the audio band; what it buys is that
    a delay is a pure sample shift, identical on every output whatever its
    length. Interpolating instead colours the treble by where between two
    samples the delay happens to fall, which measurably differed from output
    to output; FractionalDelay.h holds the windowed-sinc interpolator that
    does it properly, kept for a rig that ever needs the resolution back.

    The delay line self-absorbs this output's own FIR latency (see
    setFirLatencySamples(), pushed by MatrixEngine::recomputeLatencyComp()):
    if the manual delay already has slack, up to that many samples of it are
    "spent" paying for the FIR's own bulk latency instead of being applied as
    an actual delay, so the overall system doesn't need as much (or any)
    compensation delay added elsewhere to stay aligned. Audio-thread only —
    see MatrixEngine.cpp's comment on why this can't be touched from the
    message thread.

    Author: Olivier Doaré, github.com/odoare
    Licenced under the GNU AGPL Version 3.0, or commercial terms (LICENSE.md)
    SPDX-License-Identifier: AGPL-3.0-or-later OR LicenseRef-FXME-Commercial
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

    /** Audio-thread update from the model. No allocation, and no juce::String:
        OutputAudioSettings deliberately omits firPath, so this assignment cannot
        release a String reference (and so call free()) on the audio thread. */
    void applySettings (const OutputAudioSettings& s, bool force = false)
    {
        const bool filterChanged = force || s.bands != settings.bands;
        settings = s;

        if (filterChanged)
            updateFilters();

        updateDelaySamples();
    }

    /** Called by MatrixEngine (audio thread only) whenever this output's own
        FIR latency changes, so up to that many samples of the manual delay
        are self-absorbed instead of MatrixEngine compensating elsewhere —
        see MatrixEngine::recomputeLatencyComp(). */
    void setFirLatencySamples (int samples) noexcept
    {
        firLatencySamples = samples;
        updateDelaySamples();
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

        if (! applyDelay || delaySamples == 0)
            return;

        const int dlSize = (int) delayLine.size();

        for (int i = 0; i < n; ++i)
        {
            delayLine[(size_t) writePos] = data[i];

            int r = writePos - delaySamples;
            if (r < 0)
                r += dlSize;
            data[i] = delayLine[(size_t) r];

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

    void updateDelaySamples()
    {
        // Rounded to the nearest sample. The rounding happens on the user's
        // delay and the FIR latency comes off afterwards, which is the order
        // MatrixEngine::recomputeLatencyComp() uses for the same quantity: it
        // has to be the same integer at both ends or the inter-output
        // compensation is off by a sample. Rounding first is not a detail --
        // juce::roundToInt is round-half-to-even, so it does not commute with
        // subtracting the latency (roundToInt(1284.5) - 17 is 1267, while
        // roundToInt(1267.5) is 1268).
        const int userDelay = juce::roundToInt (settings.delayMs * 0.001 * sr);
        const int effective = juce::jmax (0, userDelay - firLatencySamples);
        const int room = juce::jmax (0, (int) delayLine.size() - 2);
        delaySamples = juce::jlimit (0, room, effective);
    }

    double sr = 44100.0;
    OutputAudioSettings settings;

    static constexpr int maxBiquads = numOutputBands * 2;
    fxme::Biquad biquads[maxBiquads];
    int activeBiquads = 0;

    std::vector<float> delayLine;
    int writePos = 0;
    int delaySamples = 0;       // whole samples, rounded from settings.delayMs
    int firLatencySamples = 0;
};

} // namespace smt
