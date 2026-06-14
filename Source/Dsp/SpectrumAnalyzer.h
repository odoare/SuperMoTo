/*
  ------------------------------------------------------------------------------
    SpectrumAnalyzer.h

    Spectrum analysis logic shared by every spectrum view (the monitoring
    matrix analyzer and the calibration mic analyzer). Given a SpectrumTap, it
    snapshots the latest fftSize samples, windows and FFTs them, maps the
    magnitude onto a log-frequency grid and exponentially averages the result
    into a dB curve. Pure DSP/maths: no GUI, no ownership of the taps.

    Author: Olivier Doaré, github.com/odoare
    Licenced under the GNU LGPL Version 3.0
    SPDX-License-Identifier: LGPL-3.0-or-later
  ------------------------------------------------------------------------------
*/

#pragma once

#include <JuceHeader.h>
#include "SpectrumTap.h"

namespace smt
{

class SpectrumAnalyzer
{
public:
    static constexpr int   numPoints = 512;             // points along the log-f axis
    static constexpr float fMin = 20.0f, fMax = 20000.0f;

    /** How the FFT bins falling on one display point are collapsed.
        - average: RMS (mean power) of the bins — broadband noise reads flat.
        - peak:    loudest bin — better for spotting narrow tones, but biases
                   broadband noise upward toward HF (more bins per point). */
    enum class Mode { average, peak };

    SpectrumAnalyzer();

    /** Display frequency (Hz) of grid point p. */
    static float pointFreq (int p);

    /** Windowed FFT of the tap's latest snapshot, mapped onto the log grid and
        exponentially smoothed into smoothedDb (read-modified in place). */
    void update (SpectrumTap& tap, std::array<float, numPoints>& smoothedDb,
                 double sampleRate, Mode mode);

private:
    juce::dsp::FFT fft;
    std::array<float, (size_t) spectrumFftSize> window;
    std::array<float, (size_t) (2 * spectrumFftSize)> fftData {};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SpectrumAnalyzer)
};

} // namespace smt
