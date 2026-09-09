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
#include <FxmeTools/dsp/MicCalibration.h>
#include <algorithm>
#include <complex>
#include <limits>
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

    /** Transfer-function estimation method.

        welch — cross/auto spectrum averaged over Hann windows (any stimulus).
        sweep — deconvolution by the synchronized sweep's analytic spectral
                inverse (Novak et al., JAES 2015): needs the sweep identity
                (setSweepInfo, read from the measurement manifest). Gives the
                full-band response with true phase in one shot and separates
                the harmonic-distortion impulse responses, exposed as curves
                via getHarmonicDb(). Falls back to Welch for a file whose
                recording is shorter than the sweep, or when no valid sweep
                info is set.

        Changing the method invalidates the analysis (like setWindowSize);
        the GUI re-loads the files. */
    enum class TfMethod { welch, sweep };
    void setTfMethod (TfMethod m);
    TfMethod getTfMethod() const noexcept       { return tfMethod; }

    /** Sweep identity of the loaded files' measurement run:
        phase(t) = 2*pi*f1*L*(exp(t/L) - 1), L in seconds — the attributes
        MeasurementEngine writes per <Run> in measurement.xml. Takes effect
        at the next loadFiles()/loadSubFiles(). */
    struct SweepInfo
    {
        double f1 = 0.0, f2 = 0.0, L = 0.0;
        bool isValid() const noexcept { return L > 0.0 && f1 > 0.0 && f2 > f1; }
    };
    void setSweepInfo (const SweepInfo& s) noexcept { sweepInfo = s; }
    const SweepInfo& getSweepInfo() const noexcept  { return sweepInfo; }

    /** Harmonic-distortion magnitude curves (sweep method only): index i is
        harmonic order i + 2, evaluated at the given (output) frequencies in
        dB, normalized to the same mid-band reference as the measured curves
        — so the values read directly as distortion level below the
        fundamental. Empty when the Welch method was used. */
    int getNumHarmonics() const noexcept        { return (int) harmonicAvgSmoothed.size(); }
    std::vector<float> getHarmonicDb (int index, const std::vector<float>& freqs) const;

    /** Loads measurement files and computes their transfer functions.
        Returns the number of successfully analyzed files. */
    int loadFiles (const juce::Array<juce::File>& files);

    void clear();

    int getNumCurves() const noexcept           { return (int) curves.size(); }
    juce::String getCurveName (int i) const     { return curves[(size_t) i].name; }
    double getSampleRate() const noexcept       { return sampleRate; }

    /** Aggregate measured propagation delay of the loaded set, in ms: the
        median of the per-file delaySamples (so one outlier mic position
        can't skew it), converted via getSampleRate(). 0 if no data. */
    float getPropagationDelayMs() const;

    /** Absolute in-band level, in dB, of the CORRECTED response: the mean
        power of |smoothed average x correction| over [lowHz, highHz],
        i.e. the level this speaker will actually play at once its correction
        is applied (with no correction designed yet it degrades to the raw
        measured level). Unlike the plot getters this is NOT normalized to
        the mid-band reference, so levels of different engines/speakers can
        be compared directly — used by Group analysis's level matching.
        Returns -120 with no data. */
    /** Mean power of the response over [lowHz, highHz], in dB.

        `corrected` = true gives the level the speaker will play at once its
        correction FIR is applied, which is what level-matching several
        corrected speakers needs. Pass false for a source that will NOT receive
        a correction (the group's subwoofer never gets one), where the level
        that matters is the one it already has. */
    float getBandLevelDb (float lowHz, float highHz, bool corrected = true) const;

    /** The mid-band reference level, in dB: 20*log10 of the 200 Hz .. 2 kHz
        mean magnitude of the smoothed average, i.e. the 0 dB line the plot
        getters normalize to. Add it back to a normalized plot value to get
        the absolute |H| in dB (recorded level per unit of stimulus level).
        0 with no data. */
    float getReferenceDb() const noexcept
    {
        return curves.empty() ? 0.0f
             : juce::Decibels::gainToDecibels ((float) referenceGain, -120.0f);
    }

    //==========================================================================
    // Optional subwoofer integration (phase-only alignment). A second set of
    // measurements taken at the SAME positions as the main set (paired by load
    // order) is loaded; each is delay-anchored on the corresponding main
    // measurement so the main-vs-sub relative phase survives the per-position
    // delay removal. Around the crossover the correction then carries an
    // all-pass that steers the corrected main's phase onto the sub's, so the
    // two sum coherently there. The main's magnitude correction is unchanged.
    /** Loads the sub measurements (main set must be loaded first). Returns the
        number of successfully analyzed files. */
    int loadSubFiles (const juce::Array<juce::File>& files);
    void clearSub();
    bool hasSub() const noexcept                { return ! subAverageSmoothed.empty(); }

    void setCrossoverHz (float hz);
    float getCrossoverHz() const noexcept       { return crossoverHz; }
    void setSubPolarityInverted (bool inverted);
    bool getSubPolarityInverted() const noexcept { return subInverted; }

    /** Largest assumed main-vs-sub offset, in ms. Matches smt::maxDelayMs (the
        output delay range), since the offset is a difference of two delays that
        each live in [0, maxDelayMs]. */
    static constexpr float maxTimeAlignMs = 100.0f;

    /** Bulk time-alignment delay (ms) ASSUMED to be applied physically to the
        main output(s). The all-pass then only corrects the residual phase, so a
        short correction FIR can integrate the sub. Positive = main is delayed
        (the usual case: the sub lags); negative = the sub is delayed instead. */
    void setTimeAlignMs (float ms);
    float getTimeAlignMs() const noexcept       { return timeAlignMs; }

    /** Recommended main delay (ms) from the measured sub group delay around the
        crossover: a linear fit of the (delay-anchored) sub phase. */
    float estimateMainSubOffsetMs() const;

    std::vector<float> getSubDb (const std::vector<float>& freqs) const;
    std::vector<float> getSubPhaseDeg (const std::vector<float>& freqs) const;

    //==========================================================================
    /** Correction level: 0 = no correction, 1 = flat (except LF slope). */
    void setCorrectionLevel (float level01);
    float getCorrectionLevel() const noexcept   { return correctionLevel; }

    /** Maximum correction boost in dB (regularization of deep notches). */
    void setMaxBoostDb (float db)
    {
        if (db == maxBoostDb) return;
        maxBoostDb = db;
        recomputeCorrection();
    }
    float getMaxBoostDb() const noexcept        { return maxBoostDb; }

    /** Phase type of the rendered correction IR. Affects only
        exportCorrectionIR / renderCorrectionIR, not the displayed design.

        linear  - mixed/linear-phase: the IR is centred at firLength/2, so it
                  corrects magnitude AND phase (including the subwoofer
                  alignment) but adds ~firLength/2 samples of latency.
        minimum - minimum-phase, built from the correction MAGNITUDE only: the
                  IR is causal and front-loaded, adding ~no latency, at the
                  cost of the phase correction and the subwoofer phase
                  alignment. Use it for low-latency monitoring while tracking. */
    enum class PhaseType { linear, minimum };
    void setPhaseType (PhaseType t);
    PhaseType getPhaseType() const noexcept     { return phaseType; }

    /** The correction as it will actually be REALISED, which under minimum
        phase is not the one that was designed.

        renderIR() rebuilds a minimum-phase impulse from |correction| alone, so
        the designed phase (crossover all-pass included) is thrown away and
        replaced by the minimum-phase equivalent of the same magnitude. Plotting
        `correction` therefore shows a phase correction that will never be
        exported. The phase read-outs go through here instead; in linear-phase
        mode it simply returns `correction`.

        Magnitude is unchanged by the reconstruction (to within the -120 dB log
        floor), so the dB curves are the same either way.

        Derived eagerly by recomputeCorrection() and setPhaseType() rather than
        on demand, so that every accessor on this class stays a pure read once
        the analysis is done. That matters: the report figures call the phase
        read-outs from a background export job while the message thread may be
        plotting the same engine, and concurrent reads are safe where a lazily
        filled cache would not be. */
    const std::vector<std::complex<float>>& effectiveCorrection() const noexcept
    {
        return phaseType == PhaseType::minimum && ! correctionMinPhase.empty()
                   ? correctionMinPhase : correction;
    }

    /** Octave-fraction smoothing applied to the displayed transfer functions
        AND to the average the correction is derived from (complex smoothing,
        so magnitude and phase together). 1/6 octave = 1.0f/6.0f; 0 = off.

        The smoothing is frequency dependent: the octave fraction is
        interpolated (log-frequency) between lowFraction at/below
        smoothLowAnchorHz and highFraction at/above smoothHighAnchorHz. This
        lets the correction stay fine in the bass (resolving room modes) while
        only following broad trends in the treble. Pass the same value for both
        to get uniform smoothing. */
    void setSmoothing (float lowFraction, float highFraction);
    float getSmoothingLow() const noexcept      { return smoothingLowFraction; }
    float getSmoothingHigh() const noexcept     { return smoothingHighFraction; }

    /** The octave fraction actually used at frequency f, log-interpolated
        between the LF and HF settings across smoothLowAnchorHz ..
        smoothHighAnchorHz. The two settings are end points, not the values in
        force anywhere in between: at 170 Hz the fraction is still 88 % of the
        LF setting, so an HF setting says almost nothing about what happens
        around a subwoofer crossover. */
    float smoothingFractionAt (double f) const;

    /** Microphone calibration applied to the measured transfer functions (and
        thus to the displayed curves, the correction and the exported measured
        IR). The mic response is divided out per bin; pass a default-constructed
        (invalid) calibration to disable. Recomputes the smoothed spectra and
        the correction. */
    void setMicCalibration (const fxme::MicCalibration& cal);
    const fxme::MicCalibration& getMicCalibration() const noexcept { return micCal; }

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
        std::vector<std::vector<float>> harmonics;  // |H_m|, m = 2.. (sweep method)
        float delaySamples = 0.0f;
    };

    bool analyzeFile (const juce::File& file, Curve& out,
                      float forcedDelaySamples = std::numeric_limits<float>::quiet_NaN());
    bool estimateSweepTf (const float* recorded, int numSamples,
                          juce::dsp::FFT& fft, Curve& out);
    juce::AudioBuffer<float> renderIR (const std::vector<std::complex<float>>& spec,
                                       int firLength, bool minimumPhase = false) const;
    float bandWeight (double freqHz) const;     // 1 in band, raised-cosine skirts
    float alignWeight (double freqHz) const;    // 1 at/below crossover, 0 above
    void computeAverage();
    void computeSubAverage();
    void applySmoothing();
    void recomputeCorrection();

    // Complex fractional-octave smoothing with a frequency-dependent fraction
    // (lowFraction at LF, highFraction at HF, log-interpolated between the
    // anchors). sampleRate/windowSize map bins to frequency.
    static std::vector<std::complex<float>> smoothVariableOctave (
        const std::vector<std::complex<float>>& in,
        float lowFraction, float highFraction,
        double sampleRate, int windowSize);

    float interpDb (const std::vector<std::complex<float>>& spec, float freq) const;
    std::complex<float> interpComplex (const std::vector<std::complex<float>>& spec, float freq) const;

    int windowSize = 65536;
    double sampleRate = 0.0;
    double referenceGain = 1.0;                 // mid-band mean of the average
    float correctionLevel = 1.0f;
    float maxBoostDb = 12.0f;
    PhaseType phaseType = PhaseType::linear;     // rendered correction IR phase
    fxme::MicCalibration micCal;                 // divided out of the measurements
    float smoothingLowFraction  = 1.0f / 6.0f;  // octave fraction at LF (0 = off)
    float smoothingHighFraction = 1.0f / 6.0f;  // octave fraction at HF (0 = off)
    static constexpr double smoothLowAnchorHz  = 100.0;    // <= here: lowFraction
    static constexpr double smoothHighAnchorHz = 10000.0;  // >= here: highFraction
    float lfCornerHz = 30.0f;                   // slope towards low frequency
    float analysisLowHz  = 20.0f;               // band the analysis acts on
    float analysisHighHz = 20000.0f;
    float crossoverHz    = 80.0f;               // main/sub crossover
    float alignWidthOct  = 1.0f;                // phase-align release width above it
    bool  subInverted    = false;
    float timeAlignMs    = 0.0f;                // assumed physical main delay

    TfMethod tfMethod = TfMethod::welch;
    SweepInfo sweepInfo;                        // used when tfMethod == sweep
    static constexpr int maxHarmonicOrder = 5;  // orders 2..5 extracted

    std::vector<Curve> curves;
    // Minimum-phase equivalent of `correction`, for effectiveCorrection().
    // Empty unless the phase type is minimum. Kept in step by
    // updateEffectiveCorrection(), never derived on the fly (see the accessor).
    std::vector<std::complex<float>> correctionMinPhase;
    void updateEffectiveCorrection();

    std::vector<std::complex<float>> average;           // delay-aligned complex average
    std::vector<std::complex<float>> averageSmoothed;   // what plots & correction use
    std::vector<std::complex<float>> correction;        // designed correction

    // Harmonic magnitudes, power-averaged across mic positions (stored as
    // real-valued complex so the smoothing / mic-cal / interpDb machinery of
    // the main curves applies unchanged). Index 0 = order 2.
    std::vector<std::vector<std::complex<float>>> harmonicAvg;
    std::vector<std::vector<std::complex<float>>> harmonicAvgSmoothed;

    std::vector<Curve> subCurves;                       // sub set, anchored on main
    std::vector<std::complex<float>> subAverage;
    std::vector<std::complex<float>> subAverageSmoothed; // alignment + display

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AnalysisEngine)
};

} // namespace smt
