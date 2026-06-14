/*
  ------------------------------------------------------------------------------
    FrameProcessor.h

    Processing chain of one matrix frame (crosspoint): configurable IIR
    filter (lowpass/highpass/bandpass, 2nd or 4th order), fractional delay,
    phase inversion and smoothed gain. The result of process() is *added*
    into the destination output channel.

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

class FrameProcessor
{
public:
    void prepare (double sampleRate, int maxBlockSize)
    {
        sr = sampleRate;
        scratch.resize ((size_t) maxBlockSize);
        delayLine.assign ((size_t) (maxDelayMs * 0.001 * sr) + 8, 0.0f);
        smoothedGain.reset (sr, 0.05);
        reset();
        applySettings (settings, true);
    }

    void reset()
    {
        for (auto& b : biquads)
            b.reset();
        std::fill (delayLine.begin(), delayLine.end(), 0.0f);
        writePos = 0;
        level.store (0.0f);
    }

    /** Called on the audio thread when the model changed (no allocation). */
    void applySettings (const FrameSettings& s, bool force = false)
    {
        const bool filterChanged = force
            || s.filterOn != settings.filterOn
            || s.filterType != settings.filterType
            || s.filterOrder != settings.filterOrder
            || s.filterFreq != settings.filterFreq
            || s.filterQ != settings.filterQ;

        const bool wasActive = settings.active;
        settings = s;

        if (filterChanged)
            updateFilter();

        const float target = settings.active
            ? juce::Decibels::decibelsToGain (settings.gainDb) * (settings.phaseInvert ? -1.0f : 1.0f)
            : 0.0f;

        if (force || (! wasActive && settings.active))
        {
            reset();
            smoothedGain.setCurrentAndTargetValue (target);
        }
        else
        {
            smoothedGain.setTargetValue (target);
        }

        delaySamples = juce::jlimit (0.0f, (float) delayLine.size() - 2.0f,
                                     (float) (settings.delayMs * 0.001 * sr));
    }

    bool isActive() const noexcept
    {
        // Keep processing while the gain ramps down after deactivation.
        return settings.active || smoothedGain.getCurrentValue() != 0.0f
                               || smoothedGain.isSmoothing();
    }

    /** Routing intent (ignores the post-deactivation gain ramp): true only while
        the frame is switched on. Used to decide which outputs are fed. */
    bool isRouted() const noexcept          { return settings.active; }

    bool wantsSpectrum() const noexcept     { return settings.spectrum; }

    /** Filters/delays input, adds into dest. Returns pointer to the frame's
        own (post-chain) signal for metering/spectrum use, valid until the
        next call. */
    const float* process (const float* input, float* dest, int n)
    {
        auto* tmp = scratch.data();

        // Filter
        if (settings.filterOn)
        {
            const int nb = numBiquads();
            for (int i = 0; i < n; ++i)
            {
                float v = input[i];
                for (int b = 0; b < nb; ++b)
                    v = biquads[(size_t) b].processSample (v);
                tmp[i] = v;
            }
        }
        else
        {
            std::copy (input, input + n, tmp);
        }

        // Fractional delay (linear interpolation) + smoothed gain, in place.
        const int dlSize = (int) delayLine.size();
        const int di = (int) delaySamples;
        const float frac = delaySamples - (float) di;

        float peak = 0.0f;
        for (int i = 0; i < n; ++i)
        {
            delayLine[(size_t) writePos] = tmp[i];

            int r0 = writePos - di;       if (r0 < 0) r0 += dlSize;
            int r1 = r0 - 1;              if (r1 < 0) r1 += dlSize;
            const float delayed = delayLine[(size_t) r0] * (1.0f - frac)
                                + delayLine[(size_t) r1] * frac;

            const float v = delayed * smoothedGain.getNextValue();
            tmp[i] = v;
            dest[i] += v;
            peak = juce::jmax (peak, std::abs (v));

            if (++writePos >= dlSize)
                writePos = 0;
        }

        // Peak level with decay, for the frame vu-meter.
        const float prev = level.load();
        level.store (juce::jmax (peak, prev * 0.85f));

        return tmp;
    }

    /** Frame vu-meter level in dB. */
    float getLevelDb() const noexcept
    {
        return juce::Decibels::gainToDecibels (level.load(), -90.0f);
    }

    const FrameSettings& getSettings() const noexcept { return settings; }

private:
    int numBiquads() const noexcept { return settings.filterOrder >= 4 ? 2 : 1; }

    void updateFilter()
    {
        const auto type = (FilterType) settings.filterType;
        const float f = settings.filterFreq;
        const float q = settings.filterQ;

        if (settings.filterOrder >= 4)
        {
            // 4th order: two cascaded biquads. For LP/HP use Butterworth
            // section Qs scaled by the user Q (q = 0.707 -> Butterworth).
            const float scale = q / 0.707f;
            const float q1 = 0.54119610f * scale;
            const float q2 = 1.30656296f * scale;
            switch (type)
            {
                case FilterType::lowpass:
                    biquads[0].c = BiquadCoeffs::lowpass (sr, f, q1);
                    biquads[1].c = BiquadCoeffs::lowpass (sr, f, q2);
                    break;
                case FilterType::highpass:
                    biquads[0].c = BiquadCoeffs::highpass (sr, f, q1);
                    biquads[1].c = BiquadCoeffs::highpass (sr, f, q2);
                    break;
                case FilterType::bandpass:
                    biquads[0].c = BiquadCoeffs::bandpass (sr, f, q);
                    biquads[1].c = BiquadCoeffs::bandpass (sr, f, q);
                    break;
            }
        }
        else
        {
            switch (type)
            {
                case FilterType::lowpass:  biquads[0].c = BiquadCoeffs::lowpass  (sr, f, q); break;
                case FilterType::highpass: biquads[0].c = BiquadCoeffs::highpass (sr, f, q); break;
                case FilterType::bandpass: biquads[0].c = BiquadCoeffs::bandpass (sr, f, q); break;
            }
            biquads[1].c = BiquadCoeffs();
        }

        biquads[0].reset();
        biquads[1].reset();
    }

    double sr = 44100.0;
    FrameSettings settings;

    Biquad biquads[2];
    std::vector<float> delayLine;
    int writePos = 0;
    float delaySamples = 0.0f;

    juce::LinearSmoothedValue<float> smoothedGain;
    std::vector<float> scratch;
    std::atomic<float> level { 0.0f };
};

} // namespace smt
