/*
  ------------------------------------------------------------------------------
    FrameProcessor.h

    Processing of one matrix frame (crosspoint): a smoothed gain with optional
    polarity flip. The result of process() is *added* into the destination
    output channel; the speaker processing (EQ, delay, FIR) lives downstream on
    the output (see OutputProcessor / MatrixEngine).

    Author: Olivier Doaré, github.com/odoare
    Licenced under the GNU LGPL Version 3.0
    SPDX-License-Identifier: LGPL-3.0-or-later
  ------------------------------------------------------------------------------
*/

#pragma once

#include <JuceHeader.h>
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
        smoothedGain.reset (sr, 0.05);
        reset();
        applySettings (settings, true);
    }

    void reset()
    {
        level.store (0.0f);
    }

    /** Called on the audio thread when the model changed (no allocation). */
    void applySettings (const FrameSettings& s, bool force = false)
    {
        const bool wasActive = settings.active;
        settings = s;

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

    /** Gains the input and adds it into dest. Returns a pointer to the frame's
        own (post-gain) signal for metering/spectrum use, valid until the next
        call. */
    const float* process (const float* input, float* dest, int n)
    {
        auto* tmp = scratch.data();

        float peak = 0.0f;
        for (int i = 0; i < n; ++i)
        {
            const float v = input[i] * smoothedGain.getNextValue();
            tmp[i] = v;
            dest[i] += v;
            peak = juce::jmax (peak, std::abs (v));
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
    double sr = 44100.0;
    FrameSettings settings;

    juce::LinearSmoothedValue<float> smoothedGain;
    std::vector<float> scratch;
    std::atomic<float> level { 0.0f };
};

} // namespace smt
