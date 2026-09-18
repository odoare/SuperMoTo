/*
  ------------------------------------------------------------------------------
    SplMeterEngine.cpp

    Author: Olivier Doaré, github.com/odoare
    Licenced under the GNU AGPL Version 3.0, or commercial terms (LICENSE.md)
    SPDX-License-Identifier: AGPL-3.0-or-later OR LicenseRef-FXME-Commercial
  ------------------------------------------------------------------------------
*/

#include "SplMeterEngine.h"

namespace smt
{

void SplMeterEngine::prepare (double sampleRate, int maxBlockSize)
{
    const double sr = sampleRate > 0.0 ? sampleRate : 44100.0;
    inScratch.setSize (numChannels, juce::jmax (1, maxBlockSize));
    rms.prepare (sr);
    generator.prepare (sr);
}

bool SplMeterEngine::process (const float* micInput, juce::AudioBuffer<float>& output,
                              int n, MatrixEngine& engine,
                              const std::array<bool, numConfigs>& configActive)
{
    if (meterOn.load())
    {
        rms.process (micInput, n);
        if (micInput != nullptr)
            micTap.push (micInput, n);  // feed the spectrum analyzer
    }
    else
        rms.clearReading();

    if (! generator.isGenerating())
        return false;       // metering only: the matrix keeps running

    const auto m = mode.load();
    const bool full = m == MeasureMode::fullSystem;

    // Own the audio. Output modes render the stimulus straight onto the selected
    // outputs; fullSystem injects it into the selected inputs.
    for (int c = 0; c < output.getNumChannels(); ++c)
        output.clear (c, 0, n);
    if (full)
        inScratch.clear();

    const juce::uint32 mask = channelsMask.load();
    const int maxCh = full ? juce::jmin (inScratch.getNumChannels(), numChannels)
                           : juce::jmin (output.getNumChannels(), numChannels);
    auto& target = full ? inScratch : output;

    generator.beginBlock();
    for (int i = 0; i < n; ++i)
    {
        const float v = generator.nextSample();
        for (int o = 0; o < maxCh; ++o)
            if ((mask & (1u << (juce::uint32) o)) != 0)
                target.getWritePointer (o)[i] = v;
    }

    if (full)
        // Run the whole engine (matrix, crossover, FIRs, latency comp), unity
        // master so the SPL reading reflects the stimulus level only.
        engine.process (inScratch.getArrayOfReadPointers(), output, n, configActive, 1.0f);
    else if (m == MeasureMode::outputFir)
        for (int o = 0; o < maxCh; ++o)
            if ((mask & (1u << (juce::uint32) o)) != 0)
                engine.processOutputChainOnly (output, o, n, true);

    return true;
}

} // namespace smt
