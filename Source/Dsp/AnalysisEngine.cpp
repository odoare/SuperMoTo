/*
  ------------------------------------------------------------------------------
    AnalysisEngine.cpp

    Author: Olivier Doaré, github.com/odoare
    Licenced under the GNU AGPL Version 3.0, or commercial terms (LICENSE.md)
    SPDX-License-Identifier: AGPL-3.0-or-later OR LicenseRef-FXME-Commercial
  ------------------------------------------------------------------------------
*/

#include "AnalysisEngine.h"
#include "ReverbTime.h"
#include <FxmeTools/dsp/SynchronizedSweep.h>

namespace smt
{

namespace
{
    // ISO 266 octave centres. Below 125 Hz an octave is too narrow to hold a
    // readable decay in a small room's measurement, and above 8 kHz the air
    // and the microphone have taken most of it.
    constexpr float octaveCentres[] = { 125.0f, 250.0f, 500.0f, 1000.0f, 2000.0f, 4000.0f, 8000.0f };

    /** An octave band with raised-cosine skirts, flat across the octave and
        fading out over the half-octave beyond each edge. A brick wall would
        ring on for as long as the decay it is supposed to be measuring. */
    float octaveWeight (double f, double centreHz) noexcept
    {
        if (f <= 0.0 || centreHz <= 0.0)
            return 0.0f;

        const double a = std::abs (std::log2 (f / centreHz));
        if (a <= 0.5) return 1.0f;
        if (a >= 1.0) return 0.0f;
        return (float) (0.5 * (1.0 + std::cos (juce::MathConstants<double>::pi * (a - 0.5) * 2.0)));
    }

    /** The minimum-phase unit phasors of a magnitude spectrum, by the real
        cepstrum -- the same construction renderIR() uses, returned as bare
        directions because that is all the caller does with them. False, and
        `out` left alone, when the grid is not one an FFT can take. */
    bool minimumPhaseDirections (const std::vector<double>& mag,
                                 std::vector<std::complex<double>>& out)
    {
        const int numBins = (int) mag.size();
        if (numBins < 2)
            return false;

        const int N = 2 * (numBins - 1);
        if (! juce::isPowerOfTwo (N))
            return false;

        using Cplx = std::complex<float>;
        juce::dsp::FFT fft ((int) std::log2 ((double) N));
        std::vector<Cplx> a ((size_t) N), b ((size_t) N);
        constexpr float floorMag = 1.0e-6f;         // -120 dB: avoids log(0)

        for (int k = 0; k < numBins; ++k)
            a[(size_t) k] = Cplx (std::log (juce::jmax (floorMag, (float) mag[(size_t) k])), 0.0f);
        for (int k = 1; k < N / 2; ++k)             // real, even spectrum: mirror
            a[(size_t) (N - k)] = a[(size_t) k];

        fft.perform (a.data(), b.data(), true);      // b = real cepstrum

        for (int n = 1; n < N / 2; ++n)      b[(size_t) n] *= 2.0f;
        for (int n = N / 2 + 1; n < N; ++n)  b[(size_t) n]  = Cplx();

        fft.perform (b.data(), a.data(), false);     // a = minimum-phase log spectrum

        out.resize ((size_t) numBins);
        for (int k = 0; k < numBins; ++k)
        {
            const auto z = std::exp (std::complex<double> (0.0, a[(size_t) k].imag()));
            out[(size_t) k] = z;                     // unit modulus by construction
        }
        return true;
    }

    /** Magnitude from the power mean, phase from the complex mean, and the
        phase faded toward the minimum-phase equivalent of that same magnitude
        by how much the positions actually agree about it.

        The fade is the part that is not obvious. Where the positions disagree
        the complex sum nearly cancels, and its ARGUMENT is then the direction
        of a residual between near-random phasors: it can turn 180 degrees
        between neighbouring bins while the power-mean magnitude walks smoothly
        through, so nothing in the magnitude shows it up. A linear-phase render
        realises that step as a near-zero on the unit circle -- measured at
        Q 518, -25 dB at 697 Hz on a real ten-position campaign, which rings
        audibly on an F. A minimum-phase render, using the magnitude alone,
        never sees it.

        `w` is the agreement, debiased: N unit phasors that agree on nothing
        still sum to 1/sqrt(N), so that much is taken out first and w = 0 means
        "no better than chance". The two directions are blended as phasors on
        the short arc, as the subwoofer alignment blends its own, so there is
        no unwrapping anywhere and nothing can jump. At w = 0 what is left is
        the minimum-phase response of a magnitude that is still exactly right.

        Measured on the campaign above: the magnitude correction does not move
        at all (0.000 dB RMS over 200 Hz - 10 kHz), the crossover delay
        estimate moves 0.03 ms and the summation efficiency 0.006 dB, while the
        notch goes from -19 dB to +1.7. */
    void blendAveragePhase (const std::vector<std::complex<double>>& sum,
                            const std::vector<double>& power, int numPositions,
                            std::vector<std::complex<float>>& average)
    {
        const size_t numBins = power.size();
        const double inv = 1.0 / (double) juce::jmax (1, numPositions);

        std::vector<double> mag (numBins);
        for (size_t k = 0; k < numBins; ++k)
            mag[k] = std::sqrt (power[k] * inv);

        std::vector<std::complex<double>> minPhase;
        const bool haveMinPhase = numPositions > 1 && minimumPhaseDirections (mag, minPhase);

        for (size_t k = 0; k < numBins; ++k)
        {
            const double a = std::abs (sum[k]);

            if (a <= 1.0e-30 || mag[k] <= 1.0e-30)
            {
                average[k] = std::complex<float> ((float) mag[k], 0.0f);
                continue;
            }

            const auto measured = sum[k] / a;       // the direction the positions voted for

            if (! haveMinPhase)
            {
                average[k] = std::complex<float> (measured * mag[k]);
                continue;
            }

            const double coh = a * inv / mag[k];    // |mean H| / sqrt(mean |H|^2)
            const double n = (double) numPositions;
            const double w = juce::jlimit (0.0, 1.0, (n * coh * coh - 1.0) / (n - 1.0));

            auto blend = w * measured + (1.0 - w) * minPhase[k];
            const double len = std::abs (blend);

            // Opposed directions at w = 1/2 leave nothing to normalise; the
            // minimum-phase one is the safe half of that pair.
            average[k] = std::complex<float> (len > 1.0e-6 ? blend * (mag[k] / len)
                                                           : minPhase[k] * mag[k]);
        }
    }

    /** Back to the time domain from a half-spectrum, optionally through one
        octave band. windowSize samples, the direct sound at 0 (the curves
        carry no propagation delay by the time they are stored). */
    juce::AudioBuffer<float> inverseTransform (const std::vector<std::complex<float>>& H,
                                               int windowSize, double sampleRate,
                                               double bandCentreHz = 0.0)
    {
        juce::AudioBuffer<float> out;
        const int W = windowSize;
        const int numBins = W / 2 + 1;

        if (W < 64 || (int) H.size() < numBins || sampleRate <= 0.0)
            return out;

        // Only the first numBins complex values are read, but the buffer has to
        // be 2 * W floats all the same (juce::dsp::FFT's contract).
        std::vector<float> buf ((size_t) (2 * W), 0.0f);
        for (int k = 0; k < numBins; ++k)
        {
            auto v = H[(size_t) k];
            if (bandCentreHz > 0.0)
                v *= octaveWeight ((double) k * sampleRate / (double) W, bandCentreHz);

            buf[(size_t) (2 * k)]     = v.real();
            buf[(size_t) (2 * k + 1)] = v.imag();
        }

        juce::dsp::FFT fft ((int) std::log2 ((double) W));
        fft.performRealOnlyInverseTransform (buf.data());

        out.setSize (1, W);
        out.copyFrom (0, 0, buf.data(), W);
        return out;
    }
}

void AnalysisEngine::setWindowSize (int sizePow2)
{
    sizePow2 = juce::nextPowerOfTwo (juce::jlimit (4096, 1 << 18, sizePow2));
    if (sizePow2 == windowSize)
        return;
    windowSize = sizePow2;
    // Window size changes invalidate the analysis; the GUI re-loads files.
    clear();
}

void AnalysisEngine::setTfMethod (TfMethod m)
{
    if (m == tfMethod)
        return;
    tfMethod = m;
    // Method changes invalidate the analysis; the GUI re-loads files.
    clear();
}

void AnalysisEngine::clear()
{
    curves.clear();
    reverbTime = {};
    average.clear();
    averageSmoothed.clear();
    correction.clear();
    harmonicAvg.clear();
    harmonicAvgSmoothed.clear();
    sampleRate = 0.0;
    referenceGain = 1.0;
    clearSub();                 // reloading the main set invalidates the pairing
}

void AnalysisEngine::clearSub()
{
    subCurves.clear();
    subAverage.clear();
    subAverageSmoothed.clear();
}

int AnalysisEngine::loadFiles (const juce::Array<juce::File>& files)
{
    clear();

    for (const auto& f : files)
    {
        Curve c;
        if (analyzeFile (f, c))
            curves.push_back (std::move (c));
    }

    computeAverage();
    applySmoothing();
    recomputeCorrection();
    computeReverbTime();        // from the raw curves, so none of the above touches it
    return (int) curves.size();
}

float AnalysisEngine::getPropagationDelayMs() const
{
    if (curves.empty() || sampleRate <= 0.0)
        return 0.0f;

    std::vector<float> delays;
    delays.reserve (curves.size());
    for (const auto& c : curves)
        delays.push_back (c.delaySamples);

    auto mid = delays.begin() + (long) (delays.size() / 2);
    std::nth_element (delays.begin(), mid, delays.end());
    return (float) (1000.0 * (double) *mid / sampleRate);
}

float AnalysisEngine::getBandLevelDb (float lowHz, float highHz, bool corrected) const
{
    if (averageSmoothed.empty() || sampleRate <= 0.0)
        return -120.0f;

    // Mean POWER (not mean dB) of the corrected response over the band —
    // energy is the right quantity to average for level matching. The
    // correction already carries the referenceGain normalization, so this is
    // the absolute level the corrected speaker will actually play at.
    double sum = 0.0;
    int count = 0;
    for (size_t k = 0; k < averageSmoothed.size(); ++k)
    {
        const double f = (double) k * sampleRate / (double) windowSize;
        if (f < (double) lowHz || f > (double) highHz)
            continue;
        auto h = std::complex<double> (averageSmoothed[k]);
        if (corrected && k < correction.size())
            h *= std::complex<double> (correction[k]);
        sum += std::norm (h);
        ++count;
    }
    if (count == 0)
        return -120.0f;

    return (float) (10.0 * std::log10 (juce::jmax (1.0e-12, sum / (double) count)));
}

int AnalysisEngine::loadSubFiles (const juce::Array<juce::File>& files)
{
    clearSub();
    if (curves.empty())
        return 0;               // the main set defines the per-position anchors

    int i = 0;
    for (const auto& f : files)
    {
        Curve c;
        // Anchor each sub measurement on the delay of the main measurement at
        // the same position (paired by load order), so the main-vs-sub relative
        // phase is preserved instead of being zeroed out per file.
        const float anchor = curves[(size_t) juce::jmin (i, (int) curves.size() - 1)].delaySamples;
        if (analyzeFile (f, c, anchor))
            subCurves.push_back (std::move (c));
        ++i;
    }

    computeSubAverage();
    applySmoothing();
    recomputeCorrection();
    return (int) subCurves.size();
}

void AnalysisEngine::setSmoothing (float lowFraction, float highFraction)
{
    lowFraction  = juce::jlimit (0.0f, 1.0f, lowFraction);
    highFraction = juce::jlimit (0.0f, 1.0f, highFraction);
    if (lowFraction == smoothingLowFraction && highFraction == smoothingHighFraction)
        return;          // avoid a full re-smoothing pass when nothing changed
    smoothingLowFraction  = lowFraction;
    smoothingHighFraction = highFraction;
    applySmoothing();
    recomputeCorrection();
}

void AnalysisEngine::applySmoothing()
{
    const bool on = smoothingLowFraction > 0.0f || smoothingHighFraction > 0.0f;
    auto smooth = [this] (const std::vector<std::complex<float>>& in)
    {
        return smoothVariableOctave (in, smoothingLowFraction, smoothingHighFraction,
                                     sampleRate, windowSize);
    };

    // The averages are smoothed as magnitude and phase SEPARATELY, or the
    // complex moving average puts straight back the cancellation
    // computeAverage() just took out: at 13 kHz a 1/3-octave window spans
    // 3 kHz, and the residual phase turns several times across it. Measured
    // before this: 1.7 dB of the displayed treble level moved with the
    // smoothing fraction alone, on a quantity that should not depend on it.
    // The individual curves keep the plain complex smoothing — per position
    // the two differ by 0.1 to 0.6 dB, and the complex form keeps that
    // position's own phase detail.
    auto smoothMagPhase = [&smooth] (const std::vector<std::complex<float>>& in)
    {
        std::vector<std::complex<float>> mag (in.size());
        for (size_t k = 0; k < in.size(); ++k)
            mag[k] = { std::abs (in[k]), 0.0f };

        const auto m = smooth (mag);        // a real moving average: no cancellation
        const auto p = smooth (in);         // taken for its argument only

        std::vector<std::complex<float>> out (in.size());
        for (size_t k = 0; k < in.size(); ++k)
        {
            const float a = std::abs (p[k]);
            out[k] = a > 1.0e-30f ? p[k] * (m[k].real() / a)
                                  : std::complex<float> (m[k].real(), 0.0f);
        }
        return out;
    };

    // The microphone calibration is divided out of every spectrum used for
    // display / correction / export (a no-op when none is loaded). The raw
    // H / average stay untouched, so this stays idempotent across re-smoothing.
    for (auto& c : curves)
    {
        c.Hs = on ? smooth (c.H) : c.H;
        micCal.applyToSpectrum (c.Hs, sampleRate, windowSize);
    }
    averageSmoothed = on && ! average.empty() ? smoothMagPhase (average) : average;
    micCal.applyToSpectrum (averageSmoothed, sampleRate, windowSize);
    subAverageSmoothed = on && ! subAverage.empty() ? smoothMagPhase (subAverage) : subAverage;
    micCal.applyToSpectrum (subAverageSmoothed, sampleRate, windowSize);

    harmonicAvgSmoothed.clear();
    for (const auto& h : harmonicAvg)
    {
        auto hs = on ? smooth (h) : h;
        micCal.applyToSpectrum (hs, sampleRate, windowSize);
        harmonicAvgSmoothed.push_back (std::move (hs));
    }
}

void AnalysisEngine::setMicCalibration (const fxme::MicCalibration& cal)
{
    // Identity check (name + validity), not a deep comparison: the shared mic
    // cal only actually changes via an explicit load/clear in the Calibration
    // pane, so this is enough to skip a full re-smoothing pass on every
    // incidental re-push (e.g. from an unrelated control's onChange).
    if (cal.isValid() == micCal.isValid() && cal.getName() == micCal.getName())
        return;
    micCal = cal;
    applySmoothing();           // re-derives the cal-corrected smoothed spectra
    recomputeCorrection();
}

bool AnalysisEngine::analyzeFile (const juce::File& file, Curve& out, float forcedDelaySamples)
{
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (fm.createReaderFor (file));

    if (reader == nullptr || reader->numChannels < 2
        || reader->lengthInSamples < (juce::int64) windowSize)
        return false;

    if (sampleRate <= 0.0)
        sampleRate = reader->sampleRate;
    else if (std::abs (reader->sampleRate - sampleRate) > 1.0)
        return false;       // all files must share the same rate

    const int n = (int) std::min (reader->lengthInSamples, (juce::int64) (1 << 24));
    juce::AudioBuffer<float> data (2, n);
    reader->read (&data, 0, n, 0, true, true);

    const int W = windowSize;
    const int hop = W / 2;                       // 50% overlap
    const int numBins = W / 2 + 1;
    const int fftOrder = (int) std::log2 ((double) W);

    juce::dsp::FFT fft (fftOrder);
    std::vector<float> window ((size_t) W);
    for (int i = 0; i < W; ++i)
        window[(size_t) i] = 0.5f * (1.0f - std::cos (2.0f * juce::MathConstants<float>::pi
                                                      * (float) i / (float) (W - 1)));

    const float* x = data.getReadPointer (0);   // sent
    const float* y = data.getReadPointer (1);   // recorded

    // Sweep deconvolution (Novak): one-shot full-band H with true phase plus
    // the harmonic-distortion IRs. Falls back to Welch when not applicable
    // (no/invalid sweep info, or a recording shorter than the sweep).
    out.harmonics.clear();
    const bool haveSweepH = tfMethod == TfMethod::sweep && sweepInfo.isValid()
                         && estimateSweepTf (y, n, fft, out);

    if (! haveSweepH)
    {
        std::vector<float> bufX ((size_t) (2 * W)), bufY ((size_t) (2 * W));
        std::vector<double> pxx ((size_t) numBins, 0.0);
        std::vector<std::complex<double>> pxy ((size_t) numBins, { 0.0, 0.0 });

        int numSegments = 0;
        for (int start = 0; start + W <= n; start += hop)
        {
            std::fill (bufX.begin(), bufX.end(), 0.0f);
            std::fill (bufY.begin(), bufY.end(), 0.0f);
            for (int i = 0; i < W; ++i)
            {
                bufX[(size_t) i] = x[start + i] * window[(size_t) i];
                bufY[(size_t) i] = y[start + i] * window[(size_t) i];
            }

            fft.performRealOnlyForwardTransform (bufX.data(), true);
            fft.performRealOnlyForwardTransform (bufY.data(), true);

            for (int k = 0; k < numBins; ++k)
            {
                const std::complex<double> X (bufX[(size_t) (2 * k)], bufX[(size_t) (2 * k + 1)]);
                const std::complex<double> Y (bufY[(size_t) (2 * k)], bufY[(size_t) (2 * k + 1)]);
                pxx[(size_t) k] += std::norm (X);
                pxy[(size_t) k] += std::conj (X) * Y;
            }
            ++numSegments;
        }

        if (numSegments == 0)
            return false;

        // H = Pxy / Pxx, regularized against silent bins.
        double pxxMax = 0.0;
        for (auto v : pxx)
            pxxMax = std::max (pxxMax, v);
        const double eps = pxxMax * 1.0e-10 + 1.0e-30;

        out.H.resize ((size_t) numBins);
        for (int k = 0; k < numBins; ++k)
            out.H[(size_t) k] = std::complex<float> (pxy[(size_t) k] / (pxx[(size_t) k] + eps));
    }

    // --- Delay: either estimated from the impulse response (IFFT of H), or,
    // for sub measurements, forced to the paired main measurement's delay so
    // the relative main-vs-sub timing is kept. ---
    float delay;
    if (std::isnan (forcedDelaySamples))
    {
        std::vector<float> ir ((size_t) (2 * W), 0.0f);
        for (int k = 0; k < numBins; ++k)
        {
            ir[(size_t) (2 * k)]     = out.H[(size_t) k].real();
            ir[(size_t) (2 * k + 1)] = out.H[(size_t) k].imag();
        }
        fft.performRealOnlyInverseTransform (ir.data());

        int peakIdx = 0;
        float peakVal = 0.0f;
        for (int i = 0; i < W; ++i)
        {
            const float a = std::abs (ir[(size_t) i]);
            if (a > peakVal) { peakVal = a; peakIdx = i; }
        }
        // A peak in the second half is a (small) negative delay wrapped around.
        delay = peakIdx <= W / 2 ? (float) peakIdx : (float) (peakIdx - W);
    }
    else
    {
        delay = forcedDelaySamples;
    }
    out.delaySamples = delay;

    // Remove the linear phase so curves from different mic positions can be
    // averaged as complex values.
    for (int k = 0; k < numBins; ++k)
    {
        const float phi = 2.0f * juce::MathConstants<float>::pi * (float) k * delay / (float) W;
        out.H[(size_t) k] *= std::polar (1.0f, phi);
    }

    out.name = file.getFileName();
    return true;
}

// Deconvolution path (Novak et al. 2015): the recorded channel is deconvolved
// by the synchronized sweep's analytic spectral inverse. The first windowSize
// samples of the full response hold the (propagation-delayed) linear IR —
// FFT'd onto the same windowSize/2+1 bin grid the Welch path uses, so
// everything downstream (delay removal, complex averaging, correction design)
// is method-agnostic. The harmonic IRs, wrapped towards the end of the full
// response at L*ln(m)*fs before the linear one, are windowed out and kept as
// magnitude spectra (their phase is not used across mic positions).
bool AnalysisEngine::estimateSweepTf (const float* recorded, int numSamples,
                                      juce::dsp::FFT& fft, Curve& out)
{
    const int W = windowSize;
    const int numBins = W / 2 + 1;

    fxme::SynchronizedSweep sweep;
    sweep.prepareExact (sweepInfo.f1, sweepInfo.f2, sampleRate, sweepInfo.L);
    if (sweep.getNumSamples() > numSamples)
        return false;               // not this sweep's recording

    const auto full = sweep.deconvolve (recorded, numSamples);
    const int N = (int) full.size();
    if (N < 2 * W)
        return false;               // harmonics would not stay clear of the linear IR

    std::vector<float> buf ((size_t) (2 * W), 0.0f);
    std::copy (full.begin(), full.begin() + W, buf.begin());
    fft.performRealOnlyForwardTransform (buf.data(), true);

    out.H.resize ((size_t) numBins);
    for (int k = 0; k < numBins; ++k)
        out.H[(size_t) k] = { buf[(size_t) (2 * k)], buf[(size_t) (2 * k + 1)] };

    // Harmonic IRs, centred on (linear peak - L*ln(m)*fs). The extraction
    // half-width is bounded by the shrinking spacing to the NEXT order, so
    // neighbouring orders never leak into the window.
    int linPos = 0;
    float linPeak = 0.0f;
    for (int i = 0; i < W; ++i)
        if (std::abs (full[(size_t) i]) > linPeak)
        {
            linPeak = std::abs (full[(size_t) i]);
            linPos = i;
        }

    for (int m = 2; m <= maxHarmonicOrder; ++m)
    {
        const double off = sweep.harmonicOffsetSamples (m);
        const double gapNext = sweep.harmonicOffsetSamples (m + 1) - off;
        const int half = (int) std::min ((double) (W / 2), 0.45 * gapNext);
        if (half < 128 || off + (double) half >= (double) (N - W))
            break;                  // too short a sweep to separate this order

        const auto ir = fxme::SynchronizedSweep::extractCircular (
                            full, (double) linPos - off, 2 * half);

        std::fill (buf.begin(), buf.end(), 0.0f);
        std::copy (ir.begin(), ir.end(), buf.begin());
        fft.performRealOnlyForwardTransform (buf.data(), true);

        std::vector<float> mag ((size_t) numBins);
        for (int k = 0; k < numBins; ++k)
            mag[(size_t) k] = std::abs (std::complex<float> (buf[(size_t) (2 * k)],
                                                             buf[(size_t) (2 * k + 1)]));
        out.harmonics.push_back (std::move (mag));
    }

    return true;
}

void AnalysisEngine::computeAverage()
{
    average.clear();
    harmonicAvg.clear();
    if (curves.empty())
        return;

    const auto numBins = curves[0].H.size();
    average.assign (numBins, { 0.0f, 0.0f });

    // Magnitude from a POWER average across the positions, phase from the
    // complex one. |mean H| is not mean |H|, and the two part company as soon
    // as the positions stop agreeing on phase: above the room's transition
    // frequency they do not, and a complex average of ten near-random phasors
    // reads about 1/sqrt(10) low. Measured on a 10-position set, the loss is
    // 0.2 dB at 200 Hz, 5 dB at 8 kHz and 12 dB at 13 kHz -- at bins where
    // every individual curve is flat. Inverting that would boost a
    // cancellation that exists at no microphone position, which is exactly
    // what the correction used to do. The power average is what the spatial
    // average of the sound field is (and is already what this function does
    // for the harmonic curves below, for the same reason); the complex average
    // still supplies the phase, which stays meaningful exactly where the
    // positions agree about it -- the bass, where the phase correction and the
    // subwoofer alignment do their work. Where they do not agree, that phase
    // is faded out rather than trusted: see blendAveragePhase().
    std::vector<std::complex<double>> sum (numBins, { 0.0, 0.0 });
    std::vector<double> power (numBins, 0.0);

    for (const auto& c : curves)
        for (size_t k = 0; k < numBins; ++k)
        {
            const std::complex<double> h (c.H[k]);
            sum[k] += h;
            power[k] += std::norm (h);
        }

    blendAveragePhase (sum, power, (int) curves.size(), average);

    // Harmonic magnitudes: POWER average across mic positions (distortion
    // phase is not coherent between positions, so a complex average would
    // cancel). Stored as real-valued complex so smoothing / mic-cal /
    // interpolation reuse the main-curve machinery.
    size_t maxOrders = 0;
    for (const auto& c : curves)
        maxOrders = std::max (maxOrders, c.harmonics.size());

    for (size_t o = 0; o < maxOrders; ++o)
    {
        std::vector<double> acc (numBins, 0.0);
        int cnt = 0;
        for (const auto& c : curves)
            if (o < c.harmonics.size())
            {
                for (size_t k = 0; k < numBins; ++k)
                    acc[k] += (double) c.harmonics[o][k] * (double) c.harmonics[o][k];
                ++cnt;
            }

        std::vector<std::complex<float>> avg (numBins);
        for (size_t k = 0; k < numBins; ++k)
            avg[k] = { (float) std::sqrt (acc[k] / (double) cnt), 0.0f };
        harmonicAvg.push_back (std::move (avg));
    }
}

void AnalysisEngine::computeSubAverage()
{
    subAverage.clear();
    if (subCurves.empty())
        return;

    const auto numBins = subCurves[0].H.size();
    subAverage.assign (numBins, { 0.0f, 0.0f });

    // Power magnitude, complex phase, as computeAverage() — see the comment
    // there, and blendAveragePhase() for what happens to the phase where the
    // positions disagree. The subwoofer's own band is the coherent one (0.93
    // at 80 Hz on the set measured there), so the fade barely acts below the
    // crossover, which is the only place the alignment reads this phase: on
    // the campaign it moved the crossover delay estimate by 0.03 ms. What the
    // power magnitude stops is the sub's out-of-band reading collapsing to a
    // level no position shows.
    std::vector<std::complex<double>> sum (numBins, { 0.0, 0.0 });
    std::vector<double> power (numBins, 0.0);

    for (const auto& c : subCurves)
        for (size_t k = 0; k < numBins; ++k)
        {
            const std::complex<double> h (c.H[k]);
            sum[k] += h;
            power[k] += std::norm (h);
        }

    blendAveragePhase (sum, power, (int) subCurves.size(), subAverage);
}

std::vector<std::complex<float>> AnalysisEngine::smoothVariableOctave (
    const std::vector<std::complex<float>>& in,
    float lowFraction, float highFraction, double sampleRate, int windowSize)
{
    // Moving complex average over a +/- (fraction/2) octave band around each
    // bin, computed with prefix sums for O(n). The octave fraction is
    // frequency dependent: log-interpolated from lowFraction at/below
    // smoothLowAnchorHz to highFraction at/above smoothHighAnchorHz, so the
    // bass can stay finely resolved while the treble is smoothed broadly.
    const int n = (int) in.size();
    std::vector<std::complex<double>> prefix ((size_t) n + 1, { 0.0, 0.0 });
    for (int k = 0; k < n; ++k)
        prefix[(size_t) k + 1] = prefix[(size_t) k] + std::complex<double> (in[(size_t) k]);

    const double logLo = std::log2 (smoothLowAnchorHz);
    const double logHi = std::log2 (smoothHighAnchorHz);
    const double binToHz = (windowSize > 0 && sampleRate > 0.0)
                               ? sampleRate / (double) windowSize : 0.0;

    std::vector<std::complex<float>> outv ((size_t) n);

    for (int k = 0; k < n; ++k)
    {
        // Interpolate the octave fraction for this bin's frequency.
        const double f = (double) k * binToHz;
        double t = 0.0;
        if (f > 0.0 && logHi > logLo)
            t = juce::jlimit (0.0, 1.0, (std::log2 (f) - logLo) / (logHi - logLo));
        const double fraction = (double) lowFraction + t * ((double) highFraction - (double) lowFraction);

        if (fraction <= 0.0)        // no smoothing at this bin
        {
            outv[(size_t) k] = in[(size_t) k];
            continue;
        }

        const double r  = std::pow (2.0, fraction * 0.5);
        const int    lo = juce::jlimit (0, n - 1, (int) std::floor ((double) k / r));
        const int    hi = juce::jlimit (0, n - 1, (int) std::ceil  ((double) k * r));
        const auto sum  = prefix[(size_t) hi + 1] - prefix[(size_t) lo];
        outv[(size_t) k] = std::complex<float> (sum / (double) (hi - lo + 1));
    }
    return outv;
}

void AnalysisEngine::setCorrectionLevel (float level01)
{
    level01 = juce::jlimit (0.0f, 1.0f, level01);
    if (level01 == correctionLevel)
        return;
    correctionLevel = level01;
    recomputeCorrection();
}

void AnalysisEngine::setAnalysisRange (float lowHz, float highHz)
{
    lowHz  = juce::jlimit (5.0f, 5000.0f, lowHz);
    highHz = juce::jlimit (juce::jmax (lowHz * 1.1f, 500.0f), 30000.0f, highHz);
    if (lowHz == analysisLowHz && highHz == analysisHighHz)
        return;
    analysisLowHz  = lowHz;
    analysisHighHz = highHz;
    recomputeCorrection();
}

// 1 inside [analysisLowHz, analysisHighHz], rolling smoothly to 0 over a
// raised-cosine skirt below the low edge and above the high edge.
//
// The low skirt is always half an octave: there is room for it below any
// sensible low edge. The high one is COMPRESSED to whatever fits under
// Nyquist. A half octave above a 20 kHz edge would end at 28.3 kHz, past
// Nyquist at 44.1 and 48 kHz, so the skirt never completed and the correction
// was still applying real boost at the last bin — 0.82 of full weight at
// 22.05 kHz, 0.46 at 24 kHz. That boost is aimed at the tweeter's roll-off and
// the converter's anti-alias filter, neither of which an FIR can undo, and it
// is what lengthens the rendered correction. Measured on a 4096-tap
// linear-phase render at 48 kHz, letting the skirt finish at Nyquist moved the
// pre-echo from -42 to -64 dB and cut the taps above -60 dB from 1795 to 286,
// leaving the gain at and below the high edge untouched. The narrower skirt
// does not ring more: what rings is a correction that still has structure in
// the last bin.
float AnalysisEngine::bandWeight (double f) const
{
    if (f <= 0.0)
        return 0.0f;

    constexpr double tw = 0.5;      // skirt width in octaves
    auto rc = [] (double x)         // raised cosine, x clamped to 0..1
    {
        x = juce::jlimit (0.0, 1.0, x);
        return 0.5 - 0.5 * std::cos (juce::MathConstants<double>::pi * x);
    };

    // As much of the half octave above the high edge as fits below Nyquist.
    // Unchanged (the full tw) at sample rates with room for it, e.g. 96 kHz,
    // and while the sample rate is still unknown.
    double twHigh = tw;
    if (sampleRate > 0.0)
    {
        const double end = std::min ((double) analysisHighHz * std::pow (2.0, tw),
                                     0.5 * sampleRate);
        twHigh = std::log2 (end / (double) analysisHighHz);
    }

    const double dl = std::log2 (f / (double) analysisLowHz);    // >= 0 in band
    const double dh = std::log2 (f / (double) analysisHighHz);   // <= 0 in band
    const double wl = rc ((dl + tw) / tw);                       // low edge

    // A high edge at or above Nyquist leaves no bins to taper: everything the
    // spectrum actually holds is in band, so weight it fully rather than
    // dividing by a zero skirt width.
    const double wh = twHigh > 1.0e-6 ? rc ((twHigh - dh) / twHigh)
                                      : (dh <= 0.0 ? 1.0 : 0.0);
    return (float) (wl * wh);
}

void AnalysisEngine::setCrossoverHz (float hz)
{
    hz = juce::jlimit (20.0f, 1000.0f, hz);
    if (hz == crossoverHz)
        return;
    crossoverHz = hz;
    recomputeCorrection();
}

void AnalysisEngine::setSubPolarityInverted (bool inverted)
{
    if (inverted == subInverted)
        return;
    subInverted = inverted;
    recomputeCorrection();
}

void AnalysisEngine::setTimeAlignMs (float ms)
{
    // maxTimeAlignMs, not the single-speaker pane's +/-40 ms slider range: in a
    // group the assumed offset is a_i - a_sub, and a spread-out set with a
    // trimmed subwoofer reaches well past 40 ms. Clamping at 40 here made the
    // group's sub trim silently stop having any effect once the offset
    // saturated, which on a set with a 30.6 ms arrival difference happened at a
    // trim of only -9.4 ms.
    ms = juce::jlimit (-maxTimeAlignMs, maxTimeAlignMs, ms);
    if (ms == timeAlignMs)
        return;
    timeAlignMs = ms;
    recomputeCorrection();
}

// Recommended main delay: the sub's group delay near the crossover, from a
// linear fit of the (delay-anchored) sub phase over [crossover/2 .. crossover*2].
float AnalysisEngine::estimateMainSubOffsetMs() const
{
    if (subAverageSmoothed.empty() || sampleRate <= 0.0)
        return 0.0f;

    const double fLo = juce::jmax (1.0, (double) crossoverHz * 0.5);
    const double fHi = (double) crossoverHz * 2.0;
    constexpr int N = 48;

    // Sample and unwrap the phase, then WEIGHTED least-squares slope dphi/df.
    //
    // The weight |S|^2 is essential, not a refinement. The fit band reaches an
    // octave above the crossover, and a subwoofer is typically 40-70 dB down by
    // then: for a real measurement crossed at 85 Hz the band spans some 46 dB,
    // and above roughly 120 Hz the phase is noise. An unweighted fit gives that
    // noise the same say as the passband, so the answer walks with the
    // crossover setting (26 ms at 60 Hz down to 11 ms at 150 Hz on one such
    // set) and reports a disagreement with the arrival-time estimate that is
    // pure artefact. Weighting by power holds the same measurement at 26-35 ms
    // across every crossover setting, in agreement with the arrival times.
    double sw = 0, sf = 0, sp = 0, sff = 0, sfp = 0, prev = 0, unwrapped = 0;
    for (int i = 0; i < N; ++i)
    {
        const double f = fLo * std::pow (fHi / fLo, (double) i / (double) (N - 1));
        const auto   S = std::complex<double> (interpComplex (subAverageSmoothed, (float) f));
        double p = std::arg (S);
        if (i == 0)
        {
            unwrapped = p;
        }
        else
        {
            double d = p - prev;
            while (d >  juce::MathConstants<double>::pi) d -= juce::MathConstants<double>::twoPi;
            while (d < -juce::MathConstants<double>::pi) d += juce::MathConstants<double>::twoPi;
            unwrapped += d;
        }
        prev = p;

        // Unwrapping still has to walk the whole band in order, so every point
        // is visited; only its influence on the slope is weighted.
        const double w = std::norm (S);
        sw += w; sf += w * f; sp += w * unwrapped;
        sff += w * f * f; sfp += w * f * unwrapped;
    }

    const double denom = sw * sff - sf * sf;
    if (! (denom > 1.0e-30))
        return 0.0f;                // no usable level anywhere in the band
    const double slope = (sw * sfp - sf * sp) / denom;   // dphi/df
    const double tauMs = -slope / juce::MathConstants<double>::twoPi * 1000.0;  // phi = -2pi f tau
    return juce::jlimit (-maxTimeAlignMs, maxTimeAlignMs, (float) tauMs);
}

float AnalysisEngine::smoothingFractionAt (double f) const
{
    // Same log interpolation smoothVariableOctave applies per bin: lowFraction
    // at or below smoothLowAnchorHz, highFraction at or above
    // smoothHighAnchorHz. Exposed so callers can ask what smoothing actually
    // applies at a frequency of interest instead of guessing from the two
    // end-point settings — at 170 Hz the answer is 88% of the LF setting.
    const double logLo = std::log2 (smoothLowAnchorHz);
    const double logHi = std::log2 (smoothHighAnchorHz);
    double t = 0.0;
    if (f > 0.0 && logHi > logLo)
        t = juce::jlimit (0.0, 1.0, (std::log2 (f) - logLo) / (logHi - logLo));
    return (float) ((double) smoothingLowFraction
                    + t * ((double) smoothingHighFraction - (double) smoothingLowFraction));
}

// Phase-alignment weight: full (1) at and below the crossover, released to 0
// over `alignWidthOct` octaves above it (where the main dominates and should
// keep its own flat-phase correction rather than inherit the sub's phase).
float AnalysisEngine::alignWeight (double f) const
{
    if (f <= 0.0)
        return 0.0f;
    const double d = std::log2 (f / (double) crossoverHz);       // 0 at crossover
    const double x = juce::jlimit (0.0, 1.0, d / (double) alignWidthOct);
    return (float) (0.5 + 0.5 * std::cos (juce::MathConstants<double>::pi * x));
}

void AnalysisEngine::recomputeCorrection()
{
    correction.clear();
    correctionMinPhase.clear();
    if (averageSmoothed.empty() || sampleRate <= 0.0)
        return;

    const int numBins = (int) averageSmoothed.size();
    const int W = windowSize;

    // The inverse is taken on the (octave-fraction smoothed) average so it
    // does not chase every interference notch of the room.
    const auto& smoothed = averageSmoothed;

    // Normalization: the correction should not change the overall level.
    // Use the mean magnitude in the 200 Hz .. 2 kHz band as the reference.
    double ref = 0.0;
    int refCount = 0;
    for (int k = 0; k < numBins; ++k)
    {
        const double f = (double) k * sampleRate / (double) W;
        if (f >= 200.0 && f <= 2000.0)
        {
            ref += std::abs (smoothed[(size_t) k]);
            ++refCount;
        }
    }
    ref = refCount > 0 ? ref / (double) refCount : 1.0;
    if (ref <= 0.0)
        ref = 1.0;
    referenceGain = ref;        // also the 0 dB reference of the plots

    const float maxBoost = juce::Decibels::decibelsToGain (maxBoostDb);
    correction.resize ((size_t) numBins);

    for (int k = 0; k < numBins; ++k)
    {
        const double f = (double) k * sampleRate / (double) W;

        // Exact inverse of the normalized response (modulus AND phase),
        // with the boost soft-limited so deep notches are not over-corrected.
        const auto h = std::complex<double> (smoothed[(size_t) k]) / ref;
        const double normH = std::norm (h);
        std::complex<double> c = normH > 1.0e-12 ? std::conj (h) / normH
                                                 : std::complex<double> ((double) maxBoost, 0.0);

        // Soft-knee boost limit: rather than clamping the magnitude hard at
        // maxBoostDb (which leaves a kink at every deep notch), the gain in dB
        // is bent towards the ceiling with a tanh knee over its last `knee` dB.
        // Below the knee the boost passes untouched; it then saturates smoothly
        // and asymptotes to maxBoostDb. Cuts (mag < 1) are left alone.
        const double mag = std::abs (c);
        const double magDb = juce::Decibels::gainToDecibels (mag, -120.0);
        if (magDb > 0.0)
        {
            const double L    = (double) maxBoostDb;        // ceiling
            const double knee = juce::jmin (6.0, L);        // soft region below it
            const double T    = L - knee;                   // linear up to here
            const double limDb = (knee > 1.0e-6 && magDb > T)
                                     ? T + knee * std::tanh ((magDb - T) / knee)
                                     : juce::jmin (magDb, L);
            c *= juce::Decibels::decibelsToGain (limDb - magDb);
        }

        // Slope towards low frequency: 2nd order highpass target keeps the
        // correction from boosting subsonics.
        const std::complex<double> jw (0.0, f / (double) lfCornerHz);
        const std::complex<double> hp = (jw * jw) / (jw * jw + std::sqrt (2.0) * jw + 1.0);
        c *= hp;

        // Correction level: log-domain interpolation between bypass and full.
        if (correctionLevel < 1.0f)
        {
            const double mag = std::abs (c);
            const double ph  = std::arg (c);
            c = std::polar (std::pow (mag, (double) correctionLevel),
                            ph * (double) correctionLevel);
        }

        // Subwoofer phase alignment (all-pass, magnitude untouched): around the
        // crossover, steer the corrected main's phase onto the sub's so the two
        // sum coherently. Built from the sub's unit-magnitude (pure-phase)
        // response, complex-blended towards no change above the crossover.
        // (The blend interpolates the wrapped phasor along the short arc, so it
        // applies only the minimal rotation; scaling the unwrapped phase instead
        // forces large accumulated phase to unwind across the band.)
        if (! subAverageSmoothed.empty())
        {
            std::complex<double> S = subAverageSmoothed[(size_t) k];
            if (subInverted)
                S = -S;

            // Assume the main is physically delayed by timeAlignMs: align to the
            // sub phase advanced by that delay, so the all-pass only carries the
            // residual (the bulk delay lands in the matrix, keeping the FIR short).
            if (timeAlignMs != 0.0f)
                S *= std::polar (1.0, 2.0 * juce::MathConstants<double>::pi
                                          * f * (double) timeAlignMs / 1000.0);

            const double aS = std::abs (S);
            const std::complex<double> ph = aS > 1.0e-20 ? S / aS
                                                         : std::complex<double> (1.0, 0.0);
            const double wx = (double) alignWeight (f);
            std::complex<double> A = (1.0 - wx) + wx * ph;
            const double aA = std::abs (A);
            if (aA > 1.0e-12)
                A /= aA;                // renormalize: phase-only, no level change
            c *= A;
        }

        // Outside the analysis band, fade the correction back to unity so we do
        // not try to compensate content the speaker cannot reproduce.
        const double w = (double) bandWeight (f);
        c = std::complex<double> (1.0 - w, 0.0) + c * w;

        correction[(size_t) k] = std::complex<float> (c);
    }

    // Derive the realised spectrum now, while we are on a writing thread: the
    // read-outs must never have to build it themselves (see effectiveCorrection).
    updateEffectiveCorrection();
}

//==============================================================================
float AnalysisEngine::interpDb (const std::vector<std::complex<float>>& spec, float freq) const
{
    if (spec.empty() || sampleRate <= 0.0)
        return -120.0f;

    const double bin = (double) freq * (double) windowSize / sampleRate;
    const int k0 = juce::jlimit (0, (int) spec.size() - 1, (int) bin);
    const int k1 = juce::jlimit (0, (int) spec.size() - 1, k0 + 1);
    const float frac = (float) (bin - (double) k0);

    const float m0 = std::abs (spec[(size_t) k0]);
    const float m1 = std::abs (spec[(size_t) k1]);
    return juce::Decibels::gainToDecibels (m0 + (m1 - m0) * frac, -120.0f);
}

std::complex<float> AnalysisEngine::interpComplex (const std::vector<std::complex<float>>& spec,
                                                   float freq) const
{
    if (spec.empty() || sampleRate <= 0.0)
        return { 0.0f, 0.0f };

    const double bin = (double) freq * (double) windowSize / sampleRate;
    const int k0 = juce::jlimit (0, (int) spec.size() - 1, (int) bin);
    const int k1 = juce::jlimit (0, (int) spec.size() - 1, k0 + 1);
    const float frac = (float) (bin - (double) k0);
    return spec[(size_t) k0] * (1.0f - frac) + spec[(size_t) k1] * frac;
}

static float argDeg (std::complex<float> c)
{
    return std::arg (c) * 180.0f / juce::MathConstants<float>::pi;
}

std::vector<float> AnalysisEngine::getCurvePhaseDeg (int curve, const std::vector<float>& freqs) const
{
    std::vector<float> v (freqs.size(), 0.0f);
    if (curve < 0 || curve >= (int) curves.size())
        return v;
    for (size_t i = 0; i < freqs.size(); ++i)
        v[i] = argDeg (interpComplex (curves[(size_t) curve].Hs, freqs[i]));
    return v;
}

std::vector<float> AnalysisEngine::getAveragePhaseDeg (const std::vector<float>& freqs) const
{
    std::vector<float> v (freqs.size(), 0.0f);
    for (size_t i = 0; i < freqs.size(); ++i)
        v[i] = argDeg (interpComplex (averageSmoothed, freqs[i]));
    return v;
}

std::vector<float> AnalysisEngine::getSubDb (const std::vector<float>& freqs) const
{
    std::vector<float> v (freqs.size(), -120.0f);
    if (subAverageSmoothed.empty())
        return v;
    // Place the sub on the same 0 dB reference as the main average for context.
    const float refDb = juce::Decibels::gainToDecibels ((float) referenceGain, -120.0f);
    for (size_t i = 0; i < freqs.size(); ++i)
        v[i] = interpDb (subAverageSmoothed, freqs[i]) - refDb;
    return v;
}

std::vector<float> AnalysisEngine::getSubPhaseDeg (const std::vector<float>& freqs) const
{
    std::vector<float> v (freqs.size(), 0.0f);
    if (subAverageSmoothed.empty())
        return v;

    // The sub AS THE CORRECTION SEES IT: polarity flipped if asked, and
    // advanced by the assumed bulk delay. This is exactly the S' that
    // recomputeCorrection() steers the corrected main onto.
    //
    // Plotting the raw anchored average instead put this curve in a different
    // time reference from the corrected-main curve drawn beside it. With the
    // 30 ms main/sub offset of a real room the raw sub sweeps through many
    // turns across the crossover while the corrected main, steered onto the
    // post-delay version, comes out nearly flat: the two looked maximally
    // misaligned at precisely the moment they were correctly aligned, and
    // identical when the alignment had not been computed at all. Neither the
    // Invert sub switch nor the Mains-delay slider moved this curve, though
    // both changed what the correction was built from.
    const double tau = (double) timeAlignMs / 1000.0;
    for (size_t i = 0; i < freqs.size(); ++i)
    {
        auto S = std::complex<double> (interpComplex (subAverageSmoothed, freqs[i]));
        if (subInverted)
            S = -S;
        if (timeAlignMs != 0.0f)
            S *= std::polar (1.0, 2.0 * juce::MathConstants<double>::pi
                                      * (double) freqs[i] * tau);
        v[i] = (float) (std::arg (S) * 180.0 / juce::MathConstants<double>::pi);
    }
    return v;
}

void AnalysisEngine::updateEffectiveCorrection()
{
    correctionMinPhase.clear();
    if (phaseType != PhaseType::minimum || correction.empty())
        return;

    // Same real-cepstrum construction renderIR() uses, but evaluated on the
    // correction's own bin grid so the plots can read it directly. The FIR is
    // rendered at its own (shorter) length, so the two agree in shape rather
    // than sample for sample; what matters here is that the phase shown is the
    // minimum-phase one that will be exported, not the designed one that will
    // not.
    const int numBins = (int) correction.size();
    if (numBins < 2)
        return;

    const int N = 2 * (numBins - 1);            // == windowSize
    if (! juce::isPowerOfTwo (N))
        return;

    using Cplx = std::complex<float>;
    juce::dsp::FFT fft ((int) std::log2 ((double) N));
    std::vector<Cplx> a ((size_t) N), b ((size_t) N);
    constexpr float floorMag = 1.0e-6f;         // -120 dB: avoids log(0)

    for (int k = 0; k < numBins; ++k)
        a[(size_t) k] = Cplx (std::log (juce::jmax (floorMag,
                                                    std::abs (correction[(size_t) k]))), 0.0f);
    for (int k = 1; k < N / 2; ++k)             // real, even spectrum: mirror
        a[(size_t) (N - k)] = a[(size_t) k];

    fft.perform (a.data(), b.data(), true);      // b = real cepstrum

    for (int n = 1; n < N / 2; ++n)      b[(size_t) n] *= 2.0f;
    for (int n = N / 2 + 1; n < N; ++n)  b[(size_t) n]  = Cplx();

    fft.perform (b.data(), a.data(), false);     // a = minimum-phase log spectrum

    correctionMinPhase.resize ((size_t) numBins);
    for (int k = 0; k < numBins; ++k)
        correctionMinPhase[(size_t) k] = std::exp (a[(size_t) k]);
}

void AnalysisEngine::setPhaseType (PhaseType t)
{
    if (t == phaseType)
        return;
    phaseType = t;
    // Which spectrum is actually realised depends on this, so the cached
    // minimum-phase version has to follow. Cheap: it clears in linear mode.
    updateEffectiveCorrection();
}

std::vector<float> AnalysisEngine::getCorrectionPhaseDeg (const std::vector<float>& freqs) const
{
    std::vector<float> v (freqs.size(), 0.0f);
    for (size_t i = 0; i < freqs.size(); ++i)
        v[i] = argDeg (interpComplex (effectiveCorrection(), freqs[i]));
    return v;
}

std::vector<float> AnalysisEngine::getCorrectedPhaseDeg (const std::vector<float>& freqs) const
{
    std::vector<float> v (freqs.size(), 0.0f);
    if (averageSmoothed.empty() || correction.empty())
        return v;
    for (size_t i = 0; i < freqs.size(); ++i)
        v[i] = argDeg (interpComplex (averageSmoothed, freqs[i])
                       * interpComplex (effectiveCorrection(), freqs[i]));
    return v;
}

std::vector<float> AnalysisEngine::getCurveDb (int curve, const std::vector<float>& freqs) const
{
    std::vector<float> v (freqs.size(), -120.0f);
    if (curve < 0 || curve >= (int) curves.size())
        return v;
    const float refDb = juce::Decibels::gainToDecibels ((float) referenceGain, -120.0f);
    for (size_t i = 0; i < freqs.size(); ++i)
        v[i] = interpDb (curves[(size_t) curve].Hs, freqs[i]) - refDb;
    return v;
}

std::vector<float> AnalysisEngine::getAverageDb (const std::vector<float>& freqs) const
{
    std::vector<float> v (freqs.size(), -120.0f);
    const float refDb = juce::Decibels::gainToDecibels ((float) referenceGain, -120.0f);
    for (size_t i = 0; i < freqs.size(); ++i)
        v[i] = interpDb (averageSmoothed, freqs[i]) - refDb;
    return v;
}

std::vector<float> AnalysisEngine::getCorrectionDb (const std::vector<float>& freqs) const
{
    std::vector<float> v (freqs.size(), -120.0f);
    for (size_t i = 0; i < freqs.size(); ++i)
        v[i] = interpDb (correction, freqs[i]);
    return v;
}

std::vector<float> AnalysisEngine::getHarmonicDb (int index, const std::vector<float>& freqs) const
{
    std::vector<float> v (freqs.size(), -120.0f);
    if (index < 0 || index >= (int) harmonicAvgSmoothed.size())
        return v;
    const float refDb = juce::Decibels::gainToDecibels ((float) referenceGain, -120.0f);
    for (size_t i = 0; i < freqs.size(); ++i)
        v[i] = interpDb (harmonicAvgSmoothed[(size_t) index], freqs[i]) - refDb;
    return v;
}

std::vector<float> AnalysisEngine::getCorrectedDb (const std::vector<float>& freqs) const
{
    std::vector<float> v (freqs.size(), -120.0f);
    if (averageSmoothed.empty() || correction.empty())
        return v;

    std::vector<std::complex<float>> prod (averageSmoothed.size());
    for (size_t k = 0; k < averageSmoothed.size(); ++k)
        prod[k] = averageSmoothed[k] * correction[k];

    const float refDb = juce::Decibels::gainToDecibels ((float) referenceGain, -120.0f);
    for (size_t i = 0; i < freqs.size(); ++i)
        v[i] = interpDb (prod, freqs[i]) - refDb;
    return v;
}

//==============================================================================
juce::AudioBuffer<float> AnalysisEngine::renderIR (const std::vector<std::complex<float>>& spec,
                                                   int firLength, bool minimumPhase) const
{
    juce::AudioBuffer<float> ir;
    if (spec.empty() || sampleRate <= 0.0)
        return ir;

    const int N = juce::nextPowerOfTwo (juce::jlimit (256, 1 << 17, firLength));
    const int numBins = N / 2 + 1;
    const int order = (int) std::log2 ((double) N);

    // Resample the source spectrum onto this FIR's frequency grid (linear
    // interpolation between the nearest Welch bins).
    auto sampleSpec = [&] (int k) -> std::complex<float>
    {
        const double f   = (double) k * sampleRate / (double) N;
        const double bin = f * (double) windowSize / sampleRate;
        const int b0 = juce::jlimit (0, (int) spec.size() - 1, (int) bin);
        const int b1 = juce::jlimit (0, (int) spec.size() - 1, b0 + 1);
        const float frac = (float) (bin - (double) b0);
        return spec[(size_t) b0] * (1.0f - frac) + spec[(size_t) b1] * frac;
    };

    juce::dsp::FFT fft (order);
    ir.setSize (1, N);
    auto* d = ir.getWritePointer (0);

    if (minimumPhase)
    {
        // Minimum-phase rendering from the target MAGNITUDE only (the designed
        // phase, including the subwoofer alignment, is intentionally dropped).
        // Real-cepstrum method: the minimum-phase log spectrum is the causal
        // part of the real cepstrum of the log magnitude. The resulting IR is
        // front-loaded (peak near sample 0), so it adds essentially no bulk
        // latency and the inter-output compensation sees ~0 for it.
        using Cplx = std::complex<float>;
        std::vector<Cplx> a ((size_t) N), b ((size_t) N);
        const float floorMag = 1.0e-6f;             // -120 dB: avoids log(0)

        for (int k = 0; k <= N / 2; ++k)
            a[(size_t) k] = Cplx (std::log (juce::jmax (floorMag, std::abs (sampleSpec (k)))), 0.0f);
        for (int k = 1; k < N / 2; ++k)             // real, even spectrum: mirror
            a[(size_t) (N - k)] = a[(size_t) k];

        fft.perform (a.data(), b.data(), true);      // b = real cepstrum (1/N scaled)

        // Keep the causal part: double bins 1..N/2-1, zero the anti-causal
        // half, leave n = 0 and n = N/2 untouched.
        for (int n = 1; n < N / 2; ++n)        b[(size_t) n] *= 2.0f;
        for (int n = N / 2 + 1; n < N; ++n)    b[(size_t) n]  = Cplx();

        fft.perform (b.data(), a.data(), false);     // a = minimum-phase log spectrum
        for (int k = 0; k < N; ++k)
            a[(size_t) k] = std::exp (a[(size_t) k]);

        fft.perform (a.data(), b.data(), true);      // b = minimum-phase impulse (real)

        // One-sided taper: keep the front intact, fade only the tail so the
        // truncation at the end is clean.
        const int tn = juce::jmax (1, (int) ((float) N * 0.1f));
        for (int i = 0; i < N; ++i)
        {
            float w = 1.0f;
            if (i >= N - tn)
                w = 0.5f * (1.0f - std::cos (juce::MathConstants<float>::pi
                                              * (float) (N - 1 - i) / (float) tn));
            d[i] = b[(size_t) i].real() * w;
        }
        return ir;
    }

    // Linear / mixed-phase path: resample, add a half-length linear phase shift
    // so the (mixed phase) response stays causal, then inverse-transform.
    std::vector<float> buf ((size_t) (2 * N), 0.0f);
    for (int k = 0; k < numBins; ++k)
    {
        auto c = sampleSpec (k);

        // shift by N/2 samples: multiply by exp(-j*pi*k)  ( = (-1)^k )
        if ((k & 1) != 0)
            c = -c;

        buf[(size_t) (2 * k)]     = c.real();
        buf[(size_t) (2 * k + 1)] = c.imag();
    }

    fft.performRealOnlyInverseTransform (buf.data());

    // Tukey window centred on N/2 to clean up edges.
    const float taper = 0.1f;
    const int tn = juce::jmax (1, (int) ((float) N * taper));
    for (int i = 0; i < N; ++i)
    {
        float w = 1.0f;
        if (i < tn)
            w = 0.5f * (1.0f - std::cos (juce::MathConstants<float>::pi * (float) i / (float) tn));
        else if (i >= N - tn)
            w = 0.5f * (1.0f - std::cos (juce::MathConstants<float>::pi * (float) (N - 1 - i) / (float) tn));
        d[i] = buf[(size_t) i] * w;
    }

    return ir;
}

juce::AudioBuffer<float> AnalysisEngine::renderCorrectionIR (int firLength) const
{
    return renderIR (correction, firLength, phaseType == PhaseType::minimum);
}

juce::AudioBuffer<float> AnalysisEngine::renderRawIR (int curveIndex) const
{
    if (curveIndex < 0 || curveIndex >= (int) curves.size())
        return {};

    return inverseTransform (curves[(size_t) curveIndex].H, windowSize, sampleRate);
}

void AnalysisEngine::computeReverbTime()
{
    reverbTime = {};

    if (curves.empty() || sampleRate <= 0.0)
    {
        reverbTime.note = "no measurements loaded";
        return;
    }

    // Welch is refused rather than reported wrong. Its segmentation smears a
    // swept measurement in time: on a synthetic 0.40 s room it reads 0.63 s,
    // and a longer sweep does not improve it. See ReverbTime.h.
    if (tfMethod != TfMethod::sweep)
    {
        reverbTime.note = "needs the sweep method: a Welch estimate of a swept "
                          "measurement smears the decay and reads far too long";
        return;
    }

    // The raw transfer function is the only one that still has the room's tail
    // in it, and each position is estimated on its own: ISO 3382 averages
    // positions by taking the mean of the times, not by averaging responses.
    for (const float centre : octaveCentres)
    {
        if ((double) centre * 2.0 >= sampleRate * 0.5)
            continue;                       // the band runs past Nyquist

        ReverbTime::Band band;
        band.centreHz = centre;

        float sum = 0.0f, lo = 0.0f, hi = 0.0f;

        for (const auto& c : curves)
        {
            const auto ir = inverseTransform (c.H, windowSize, sampleRate, (double) centre);
            if (ir.getNumSamples() == 0)
                continue;

            const auto d = reverb::estimateDecay (ir.getReadPointer (0), ir.getNumSamples(), sampleRate);
            if (! d.ok())
                continue;

            const float t = d.t60();
            if (band.positions == 0)
                lo = hi = t;
            else
            {
                lo = juce::jmin (lo, t);
                hi = juce::jmax (hi, t);
            }

            sum += t;
            ++band.positions;
        }

        if (band.positions == 0)
            continue;

        band.t60 = sum / (float) band.positions;
        band.spread = hi - lo;
        reverbTime.bands.push_back (band);
    }

    // The headline figure is the usual mid-frequency one, the mean of the
    // 500 Hz and 1 kHz octaves — the band the critical-distance estimate of
    // the manual wants, and the band a room is normally described by.
    float mid = 0.0f;
    int midBands = 0;
    for (const auto& b : reverbTime.bands)
        if (juce::exactlyEqual (b.centreHz, 500.0f) || juce::exactlyEqual (b.centreHz, 1000.0f))
        {
            mid += b.t60;
            reverbTime.positions = juce::jmax (reverbTime.positions, b.positions);
            ++midBands;
        }

    if (midBands > 0)
        reverbTime.midT60 = mid / (float) midBands;
    else
        reverbTime.note = reverbTime.bands.empty()
                            ? "no band decayed far enough to fit a slope"
                            : "the 500 Hz and 1 kHz octaves did not decay far enough to fit";
}

juce::AudioBuffer<float> AnalysisEngine::renderMeasuredIR (int firLength) const
{
    // Export the smoothed/displayed average, band-limited to the analysis range
    // so out-of-band room and microphone noise does not leak into the IR.
    if (averageSmoothed.empty() || sampleRate <= 0.0)
        return {};

    std::vector<std::complex<float>> banded (averageSmoothed.size());
    for (int k = 0; k < (int) banded.size(); ++k)
    {
        const double f = (double) k * sampleRate / (double) windowSize;
        banded[(size_t) k] = averageSmoothed[(size_t) k] * bandWeight (f);
    }
    return renderIR (banded, firLength);
}

juce::AudioBuffer<float> AnalysisEngine::renderCorrectedIR (int firLength) const
{
    if (averageSmoothed.empty() || correction.empty() || sampleRate <= 0.0)
        return {};

    const auto& c = effectiveCorrection();
    std::vector<std::complex<float>> v (averageSmoothed.size());
    for (int k = 0; k < (int) v.size(); ++k)
    {
        const double f = (double) k * sampleRate / (double) windowSize;
        auto h = std::complex<double> (averageSmoothed[(size_t) k]);
        if (k < (int) c.size())
            h *= std::complex<double> (c[(size_t) k]);
        v[(size_t) k] = std::complex<float> (h * (double) bandWeight (f));
    }
    return renderIR (v, firLength);
}

juce::AudioBuffer<float> AnalysisEngine::renderSystemIR (int firLength, float subGainDb) const
{
    if (averageSmoothed.empty() || subAverageSmoothed.empty() || sampleRate <= 0.0)
        return {};

    const auto& c = effectiveCorrection();
    const double g   = juce::Decibels::decibelsToGain ((double) subGainDb);
    const double tau = (double) timeAlignMs / 1000.0;

    std::vector<std::complex<float>> v (averageSmoothed.size());
    for (int k = 0; k < (int) v.size(); ++k)
    {
        const double f = (double) k * sampleRate / (double) windowSize;

        auto main = std::complex<double> (averageSmoothed[(size_t) k]);
        if (k < (int) c.size())
            main *= std::complex<double> (c[(size_t) k]);

        // The sub as it will actually arrive: polarity, then advanced by the
        // bulk delay the main is assumed to receive. Same S' the correction is
        // designed against, so this trace and the corrected-main trace share
        // one time reference.
        auto S = std::complex<double> (subAverageSmoothed[(size_t) k]);
        if (subInverted)
            S = -S;
        if (timeAlignMs != 0.0f)
            S *= std::polar (1.0, 2.0 * juce::MathConstants<double>::pi * f * tau);

        v[(size_t) k] = std::complex<float> ((main + g * S) * (double) bandWeight (f));
    }
    return renderIR (v, firLength);
}

bool AnalysisEngine::exportCorrectionIR (const juce::File& file, int firLength) const
{
    auto ir = renderCorrectionIR (firLength);
    if (ir.getNumSamples() == 0)
        return false;

    file.deleteFile();
    juce::WavAudioFormat wav;

    // Must be declared as unique_ptr<OutputStream> rather than to the concrete
    // type: createWriterFor() binds it by reference and moves ownership out only
    // on success (so no manual release, and the stream is freed here on failure).
    std::unique_ptr<juce::OutputStream> stream = file.createOutputStream();
    if (stream == nullptr)
        return false;

    auto writer = wav.createWriterFor (stream,
                                       juce::AudioFormatWriterOptions{}
                                           .withSampleRate    (sampleRate)
                                           .withNumChannels   (1)
                                           .withBitsPerSample (32));
    if (writer == nullptr)
        return false;

    return writer->writeFromAudioSampleBuffer (ir, 0, ir.getNumSamples());
}

bool AnalysisEngine::exportMeasuredIR (const juce::File& file, int firLength) const
{
    auto ir = renderMeasuredIR (firLength);
    if (ir.getNumSamples() == 0)
        return false;

    file.deleteFile();
    juce::WavAudioFormat wav;

    // Must be declared as unique_ptr<OutputStream> rather than to the concrete
    // type: createWriterFor() binds it by reference and moves ownership out only
    // on success (so no manual release, and the stream is freed here on failure).
    std::unique_ptr<juce::OutputStream> stream = file.createOutputStream();
    if (stream == nullptr)
        return false;

    auto writer = wav.createWriterFor (stream,
                                       juce::AudioFormatWriterOptions{}
                                           .withSampleRate    (sampleRate)
                                           .withNumChannels   (1)
                                           .withBitsPerSample (32));
    if (writer == nullptr)
        return false;

    return writer->writeFromAudioSampleBuffer (ir, 0, ir.getNumSamples());
}

} // namespace smt
