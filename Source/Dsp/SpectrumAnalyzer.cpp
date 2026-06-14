/*
  ------------------------------------------------------------------------------
    SpectrumAnalyzer.cpp

    Author: Olivier Doaré, github.com/odoare
    Licenced under the GNU LGPL Version 3.0
    SPDX-License-Identifier: LGPL-3.0-or-later
  ------------------------------------------------------------------------------
*/

#include "SpectrumAnalyzer.h"

namespace smt
{

SpectrumAnalyzer::SpectrumAnalyzer()
    : fft (spectrumFftOrder)
{
    for (int i = 0; i < spectrumFftSize; ++i)
        window[(size_t) i] = 0.5f * (1.0f - std::cos (
            2.0f * juce::MathConstants<float>::pi * (float) i
            / (float) (spectrumFftSize - 1)));
}

float SpectrumAnalyzer::pointFreq (int p)
{
    return fMin * std::pow (fMax / fMin, (float) p / (float) (numPoints - 1));
}

void SpectrumAnalyzer::update (SpectrumTap& tap, std::array<float, numPoints>& smoothedDb,
                               double sampleRate, Mode mode)
{
    tap.snapshot (fftData.data());
    for (int i = 0; i < spectrumFftSize; ++i)
        fftData[(size_t) i] *= window[(size_t) i];
    std::fill (fftData.begin() + spectrumFftSize, fftData.end(), 0.0f);

    fft.performFrequencyOnlyForwardTransform (fftData.data());

    const float binHz = (float) ((sampleRate > 0.0 ? sampleRate : 48000.0)
                                 / (double) spectrumFftSize);

    for (int p = 0; p < numPoints; ++p)
    {
        // Map this display point (and the span up to the next one) to FFT bins.
        // At HF many bins fall on one point: average their power (so broadband
        // noise stays flat) or take their peak, per the selected mode.
        const float f  = pointFreq (p);
        const int   b0 = juce::jlimit (1, spectrumFftSize / 2 - 1, (int) (f / binHz));
        const float f1 = pointFreq (juce::jmin (p + 1, numPoints - 1));
        const int   b1 = juce::jlimit (b0, spectrumFftSize / 2 - 1, (int) (f1 / binHz));

        float mag;
        if (mode == Mode::peak)
        {
            mag = 0.0f;
            for (int b = b0; b <= b1; ++b)
                mag = juce::jmax (mag, fftData[(size_t) b]);
        }
        else
        {
            double power = 0.0;
            for (int b = b0; b <= b1; ++b)
                power += (double) fftData[(size_t) b] * (double) fftData[(size_t) b];
            mag = (float) std::sqrt (power / (double) (b1 - b0 + 1));
        }

        const float db = juce::Decibels::gainToDecibels (
            mag * 2.0f / (float) spectrumFftSize, -120.0f);

        auto& s = smoothedDb[(size_t) p];
        s = s * 0.7f + db * 0.3f;
    }
}

} // namespace smt
