/*
  ------------------------------------------------------------------------------
    AnalysisEngine.h

    Part 3 of SuperMoTo: analysis of speaker measurements and design of
    correction impulse responses.

    A set of measurement files (stereo wav: ch1 = sent, ch2 = recorded,
    produced by the measurement part with the microphone at different
    positions around a reference point) is loaded. For each file the
    transfer function is estimated with the Welch method: the cross
    spectrum of sent/recorded divided by the auto spectrum of the sent
    signal, averaged over Hann windows of selectable size (default 65536).

    The propagation delay of each measurement is estimated and removed, so
    the complex responses can be averaged across microphone positions. A
    correction curve compensating modulus AND phase is then derived from
    the regularized inverse of the (octave-fraction smoothed) average, with
    an adjustable correction level from 0 (none) to 1 (flat, except a slope
    towards low frequencies), and exported as an impulse response wav that
    the monitoring part loads as an output FIR.

    All methods run on the message thread (file based, not real time).

    Author: Olivier Doaré, github.com/odoare
    Licenced under the GNU LGPL Version 3.0
    SPDX-License-Identifier: LGPL-3.0-or-later
  ------------------------------------------------------------------------------
*/

#pragma once

#include <JuceHeader.h>
#include <complex>
#include <vector>

namespace smt
{

class AnalysisEngine
{
public:
    AnalysisEngine() = default;

    //==========================================================================
    void setWindowSize (int sizePow2);          // e.g. 16384 .. 262144
    int getWindowSize() const noexcept          { return windowSize; }

    /** Loads measurement files and computes their transfer functions.
        Returns the number of successfully analyzed files. */
    int loadFiles (const juce::Array<juce::File>& files);

    void clear();

    int getNumCurves() const noexcept           { return (int) curves.size(); }
    juce::String getCurveName (int i) const     { return curves[(size_t) i].name; }
    double getSampleRate() const noexcept       { return sampleRate; }

    //==========================================================================
    /** Correction level: 0 = no correction, 1 = flat (except LF slope). */
    void setCorrectionLevel (float level01);
    float getCorrectionLevel() const noexcept   { return correctionLevel; }

    /** Maximum correction boost in dB (regularization of deep notches). */
    void setMaxBoostDb (float db)               { maxBoostDb = db; recomputeCorrection(); }

    /** Octave-fraction smoothing applied to the displayed transfer functions
        AND to the average the correction is derived from (complex smoothing,
        so magnitude and phase together). 1/6 octave = 1.0f/6.0f; 0 = off. */
    void setSmoothing (float octaveFraction);
    float getSmoothing() const noexcept         { return smoothingFraction; }

    /** Frequency band the analysis acts on. Outside [lowHz, highHz] (with a
        half-octave skirt) the correction is faded to unity and the exported
        measured IR is rolled off, so out-of-band room/mic noise and content
        beyond the speaker's range neither pollute the IR nor get "corrected". */
    void setAnalysisRange (float lowHz, float highHz);
    float getAnalysisLowHz() const noexcept     { return analysisLowHz; }
    float getAnalysisHighHz() const noexcept    { return analysisHighHz; }

    //==========================================================================
    // Plot data: |H| in dB evaluated at the given frequencies (Hz).
    // Transfer functions (curves, average, corrected) are normalized to the
    // 200 Hz .. 2 kHz mean of the average — the same reference the correction
    // design uses — so 0 dB is the mid-band level regardless of the absolute
    // measurement gain. The correction curve is an absolute gain in dB.
    bool hasData() const noexcept               { return ! curves.empty(); }
    std::vector<float> getCurveDb (int curve, const std::vector<float>& freqs) const;
    std::vector<float> getAverageDb (const std::vector<float>& freqs) const;
    std::vector<float> getCorrectionDb (const std::vector<float>& freqs) const;
    std::vector<float> getCorrectedDb (const std::vector<float>& freqs) const;

    // Phase in degrees (principal value, propagation delay already removed).
    // The correction conjugates the average's phase, so the corrected phase
    // should sit near 0 in band.
    std::vector<float> getCurvePhaseDeg (int curve, const std::vector<float>& freqs) const;
    std::vector<float> getAveragePhaseDeg (const std::vector<float>& freqs) const;
    std::vector<float> getCorrectionPhaseDeg (const std::vector<float>& freqs) const;
    std::vector<float> getCorrectedPhaseDeg (const std::vector<float>& freqs) const;

    //==========================================================================
    /** Renders the correction to an impulse response of firLength samples
        (linear-phase style, peak around firLength/2) and writes a mono
        float wav. */
    bool exportCorrectionIR (const juce::File& file, int firLength) const;

    /** Same but returns the IR buffer (e.g. to load it directly). */
    juce::AudioBuffer<float> renderCorrectionIR (int firLength) const;

    /** Renders the measured response itself (the delay-aligned complex average
        of the loaded measurements) to an impulse response of firLength samples
        and writes a mono float wav. */
    bool exportMeasuredIR (const juce::File& file, int firLength) const;

    /** Same but returns the IR buffer. */
    juce::AudioBuffer<float> renderMeasuredIR (int firLength) const;

private:
    struct Curve
    {
        juce::String name;
        std::vector<std::complex<float>> H;     // windowSize/2+1 bins, delay removed
        std::vector<std::complex<float>> Hs;    // smoothed version (display)
        float delaySamples = 0.0f;
    };

    bool analyzeFile (const juce::File& file, Curve& out);
    juce::AudioBuffer<float> renderIR (const std::vector<std::complex<float>>& spec,
                                       int firLength) const;
    float bandWeight (double freqHz) const;     // 1 in band, raised-cosine skirts
    void computeAverage();
    void applySmoothing();
    void recomputeCorrection();

    static std::vector<std::complex<float>> smoothOctaveFraction (
        const std::vector<std::complex<float>>& in, float fraction);

    float interpDb (const std::vector<std::complex<float>>& spec, float freq) const;
    std::complex<float> interpComplex (const std::vector<std::complex<float>>& spec, float freq) const;

    int windowSize = 65536;
    double sampleRate = 0.0;
    double referenceGain = 1.0;                 // mid-band mean of the average
    float correctionLevel = 1.0f;
    float maxBoostDb = 12.0f;
    float smoothingFraction = 1.0f / 6.0f;      // 0 = off
    float lfCornerHz = 30.0f;                   // slope towards low frequency
    float analysisLowHz  = 20.0f;               // band the analysis acts on
    float analysisHighHz = 20000.0f;

    std::vector<Curve> curves;
    std::vector<std::complex<float>> average;           // delay-aligned complex average
    std::vector<std::complex<float>> averageSmoothed;   // what plots & correction use
    std::vector<std::complex<float>> correction;        // designed correction

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AnalysisEngine)
};

} // namespace smt
