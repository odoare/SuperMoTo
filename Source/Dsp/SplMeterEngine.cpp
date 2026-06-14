/*
  ------------------------------------------------------------------------------
    SplMeterEngine.cpp

    Author: Olivier Doaré, github.com/odoare
    Licenced under the GNU LGPL Version 3.0
    SPDX-License-Identifier: LGPL-3.0-or-later
  ------------------------------------------------------------------------------
*/

#include "SplMeterEngine.h"

namespace smt
{

void SplMeterEngine::prepare (double sampleRate, int)
{
    sr = sampleRate > 0.0 ? sampleRate : 44100.0;
    ringCap = juce::jmax (1, (int) (maxWindowSec * sr));
    sq.assign ((size_t) ringCap, 0.0f);
    writePos = 0;
    valid = 0;
    curWindowLen = 0;
    runningSum = 0.0;
    sinePhase = 0.0;
    rmsDbFs.store (-120.0f);
}

void SplMeterEngine::updateRms (const float* mic, int n)
{
    const int L = juce::jlimit (1, ringCap, (int) (rmsWindowSec.load() * sr));
    if (L != curWindowLen)
    {
        // Window length changed: restart the accumulation cleanly.
        std::fill (sq.begin(), sq.end(), 0.0f);
        writePos = 0;
        valid = 0;
        runningSum = 0.0;
        curWindowLen = L;
    }

    for (int i = 0; i < n; ++i)
    {
        const float s = mic != nullptr ? mic[i] : 0.0f;
        const float sqv = s * s;

        // Once the window is full, drop the sample leaving it (L samples ago).
        if (valid >= L)
            runningSum -= sq[(size_t) ((writePos - L + ringCap) % ringCap)];

        sq[(size_t) writePos] = sqv;
        runningSum += sqv;
        writePos = (writePos + 1) % ringCap;
        if (valid < L)
            ++valid;
    }

    const int denom = juce::jmax (1, juce::jmin (valid, L));
    const double meanSq = juce::jmax (0.0, runningSum) / (double) denom;
    rmsDbFs.store (juce::Decibels::gainToDecibels ((float) std::sqrt (meanSq), -120.0f));
}

bool SplMeterEngine::process (const float* micInput, juce::AudioBuffer<float>& output,
                              int n, MatrixEngine& engine)
{
    const bool meter = meterOn.load();
    const bool sine  = sineOn.load();
    const bool noise = noiseOn.load();

    if (meter)
    {
        updateRms (micInput, n);
        if (micInput != nullptr)
            micTap.push (micInput, n);  // feed the spectrum analyzer
    }
    else
        rmsDbFs.store (-120.0f);

    if (! sine && ! noise)
        return false;       // metering only: the matrix keeps running

    // Own the audio: clear every output, render the stimulus on the selected
    // ones, then optionally pass each through its trim + FIR correction.
    for (int c = 0; c < output.getNumChannels(); ++c)
        output.clear (c, 0, n);

    const float sineGain  = juce::Decibels::decibelsToGain (sineAmpDb.load());
    const float noiseGain = juce::Decibels::decibelsToGain (noiseAmpDb.load());
    const double phaseInc = 2.0 * juce::MathConstants<double>::pi
                                * (double) sineFreq.load() / sr;
    const juce::uint32 mask = outputsMask.load();
    const int maxOut = juce::jmin (output.getNumChannels(), numChannels);

    for (int i = 0; i < n; ++i)
    {
        float v = 0.0f;
        if (sine)
        {
            v += sineGain * (float) std::sin (sinePhase);
            sinePhase += phaseInc;
            if (sinePhase >= juce::MathConstants<double>::twoPi)
                sinePhase -= juce::MathConstants<double>::twoPi;
        }
        if (noise)
            v += noiseGain * (random.nextFloat() * 2.0f - 1.0f);

        for (int o = 0; o < maxOut; ++o)
            if ((mask & (1u << (juce::uint32) o)) != 0)
                output.getWritePointer (o)[i] = v;
    }

    if (throughCorrection.load())
        for (int o = 0; o < maxOut; ++o)
            if ((mask & (1u << (juce::uint32) o)) != 0)
                engine.processOutputChainOnly (output, o, n, true);

    return true;
}

} // namespace smt
