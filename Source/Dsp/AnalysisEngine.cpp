/*
  ------------------------------------------------------------------------------
    AnalysisEngine.cpp

    Author: Olivier Doaré, github.com/odoare
    Licenced under the GNU LGPL Version 3.0
    SPDX-License-Identifier: LGPL-3.0-or-later
  ------------------------------------------------------------------------------
*/

#include "AnalysisEngine.h"

namespace smt
{

void AnalysisEngine::setWindowSize (int sizePow2)
{
    sizePow2 = juce::nextPowerOfTwo (juce::jlimit (4096, 1 << 18, sizePow2));
    if (sizePow2 == windowSize)
        return;
    windowSize = sizePow2;
    // Window size changes invalidate the analysis; the GUI re-loads files.
    clear();
}

void AnalysisEngine::clear()
{
    curves.clear();
    average.clear();
    averageSmoothed.clear();
    correction.clear();
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
    return (int) curves.size();
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
    smoothingLowFraction  = juce::jlimit (0.0f, 1.0f, lowFraction);
    smoothingHighFraction = juce::jlimit (0.0f, 1.0f, highFraction);
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

    for (auto& c : curves)
        c.Hs = on ? smooth (c.H) : c.H;
    averageSmoothed    = on && ! average.empty()    ? smooth (average)    : average;
    subAverageSmoothed = on && ! subAverage.empty() ? smooth (subAverage) : subAverage;
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

    std::vector<float> bufX ((size_t) (2 * W)), bufY ((size_t) (2 * W));
    std::vector<double> pxx ((size_t) numBins, 0.0);
    std::vector<std::complex<double>> pxy ((size_t) numBins, { 0.0, 0.0 });

    const float* x = data.getReadPointer (0);   // sent
    const float* y = data.getReadPointer (1);   // recorded

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

void AnalysisEngine::computeAverage()
{
    average.clear();
    if (curves.empty())
        return;

    const auto numBins = curves[0].H.size();
    average.assign (numBins, { 0.0f, 0.0f });

    for (const auto& c : curves)
        for (size_t k = 0; k < numBins; ++k)
            average[k] += c.H[k];

    const float inv = 1.0f / (float) curves.size();
    for (auto& v : average)
        v *= inv;
}

void AnalysisEngine::computeSubAverage()
{
    subAverage.clear();
    if (subCurves.empty())
        return;

    const auto numBins = subCurves[0].H.size();
    subAverage.assign (numBins, { 0.0f, 0.0f });

    for (const auto& c : subCurves)
        for (size_t k = 0; k < numBins; ++k)
            subAverage[k] += c.H[k];

    const float inv = 1.0f / (float) subCurves.size();
    for (auto& v : subAverage)
        v *= inv;
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
    correctionLevel = juce::jlimit (0.0f, 1.0f, level01);
    recomputeCorrection();
}

void AnalysisEngine::setAnalysisRange (float lowHz, float highHz)
{
    analysisLowHz  = juce::jlimit (5.0f, 5000.0f, lowHz);
    analysisHighHz = juce::jlimit (juce::jmax (analysisLowHz * 1.1f, 500.0f),
                                   30000.0f, highHz);
    recomputeCorrection();
}

// 1 inside [analysisLowHz, analysisHighHz], rolling smoothly to 0 over a
// half-octave raised-cosine skirt below the low edge and above the high edge.
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

    const double dl = std::log2 (f / (double) analysisLowHz);    // >= 0 in band
    const double dh = std::log2 (f / (double) analysisHighHz);   // <= 0 in band
    const double wl = rc ((dl + tw) / tw);                       // low edge
    const double wh = rc ((tw - dh) / tw);                       // high edge
    return (float) (wl * wh);
}

void AnalysisEngine::setCrossoverHz (float hz)
{
    crossoverHz = juce::jlimit (20.0f, 1000.0f, hz);
    recomputeCorrection();
}

void AnalysisEngine::setSubPolarityInverted (bool inverted)
{
    subInverted = inverted;
    recomputeCorrection();
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
    for (size_t i = 0; i < freqs.size(); ++i)
        v[i] = argDeg (interpComplex (subAverageSmoothed, freqs[i]));
    return v;
}

std::vector<float> AnalysisEngine::getCorrectionPhaseDeg (const std::vector<float>& freqs) const
{
    std::vector<float> v (freqs.size(), 0.0f);
    for (size_t i = 0; i < freqs.size(); ++i)
        v[i] = argDeg (interpComplex (correction, freqs[i]));
    return v;
}

std::vector<float> AnalysisEngine::getCorrectedPhaseDeg (const std::vector<float>& freqs) const
{
    std::vector<float> v (freqs.size(), 0.0f);
    if (averageSmoothed.empty() || correction.empty())
        return v;
    for (size_t i = 0; i < freqs.size(); ++i)
        v[i] = argDeg (interpComplex (averageSmoothed, freqs[i])
                       * interpComplex (correction, freqs[i]));
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

bool AnalysisEngine::exportCorrectionIR (const juce::File& file, int firLength) const
{
    auto ir = renderCorrectionIR (firLength);
    if (ir.getNumSamples() == 0)
        return false;

    file.deleteFile();
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::FileOutputStream> stream (file.createOutputStream());
    if (stream == nullptr)
        return false;

    std::unique_ptr<juce::AudioFormatWriter> writer (
        wav.createWriterFor (stream.get(), sampleRate, 1, 32, {}, 0));
    if (writer == nullptr)
        return false;

    stream.release();
    return writer->writeFromAudioSampleBuffer (ir, 0, ir.getNumSamples());
}

bool AnalysisEngine::exportMeasuredIR (const juce::File& file, int firLength) const
{
    auto ir = renderMeasuredIR (firLength);
    if (ir.getNumSamples() == 0)
        return false;

    file.deleteFile();
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::FileOutputStream> stream (file.createOutputStream());
    if (stream == nullptr)
        return false;

    std::unique_ptr<juce::AudioFormatWriter> writer (
        wav.createWriterFor (stream.get(), sampleRate, 1, 32, {}, 0));
    if (writer == nullptr)
        return false;

    stream.release();
    return writer->writeFromAudioSampleBuffer (ir, 0, ir.getNumSamples());
}

} // namespace smt
