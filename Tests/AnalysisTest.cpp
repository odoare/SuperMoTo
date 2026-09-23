/*
  ------------------------------------------------------------------------------
    AnalysisTest.cpp

    Offline check of the analysis part: synthesizes measurement files whose
    "recorded" channel is the stimulus through a KNOWN filter (2nd order
    lowpass, 8 kHz) plus a propagation delay, runs the Welch analysis and
    verifies that
      - the estimated transfer function matches the known filter,
      - the corrected response (average x correction) is flat in band,
      - the exported impulse response actually applies that correction.

    Run: build target SuperMoToTests and execute it; exits 0 on success.

    Author: Olivier Doaré, github.com/odoare
    (c) 2023-2026 Olivier Doaré
    Licenced under the GNU AGPL Version 3.0, or commercial terms (LICENSE.md)
    SPDX-License-Identifier: AGPL-3.0-or-later OR LicenseRef-FXME-Commercial
  ------------------------------------------------------------------------------
*/

#include <JuceHeader.h>
#include "../Source/Dsp/AnalysisEngine.h"
#include <FxmeTools/dsp/Biquad.h>

static bool approx (float a, float b, float tol, const char* what)
{
    if (std::abs (a - b) <= tol)
    {
        std::cout << "  OK  " << what << ": " << a << " (expected " << b << " +/- " << tol << ")\n";
        return true;
    }
    std::cout << "  FAIL " << what << ": " << a << " (expected " << b << " +/- " << tol << ")\n";
    return false;
}

int main()
{
    const double sr = 48000.0;
    const float f1 = 10.0f, f2 = 20000.0f, T = 10.0f;
    const int n = (int) (T * sr);
    const int delaySamples = 240;       // 5 ms propagation
    const float lpFreq = 8000.0f;

    juce::TemporaryFile temp1 (".wav"), temp2 (".wav");
    juce::Array<juce::File> files { temp1.getFile(), temp2.getFile() };

    juce::Random rng (42);

    for (int fileIdx = 0; fileIdx < files.size(); ++fileIdx)
    {
        // Log sweep stimulus
        juce::AudioBuffer<float> buf (2, n);
        auto* x = buf.getWritePointer (0);
        auto* y = buf.getWritePointer (1);

        const double L = T / std::log (f2 / f1);
        const double K = 2.0 * juce::MathConstants<double>::pi * f1 * L;
        for (int i = 0; i < n; ++i)
        {
            const double t = i / sr;
            x[i] = 0.25f * (float) std::sin (K * (std::exp (t / L) - 1.0));
        }

        // "Recorded" = stimulus through known LP + delay (+ tiny noise),
        // with a slightly different delay per file (different mic position).
        fxme::Biquad lp;
        lp.c = fxme::BiquadCoeffs::lowpass (sr, lpFreq, 0.707f);
        const int d = delaySamples + 7 * fileIdx;
        for (int i = n - 1; i >= 0; --i)
            y[i] = i - d >= 0 ? x[i - d] : 0.0f;
        lp.processBlock (y, n);
        for (int i = 0; i < n; ++i)
            y[i] += 1.0e-5f * (rng.nextFloat() * 2.0f - 1.0f);

        juce::WavAudioFormat wav;
        // unique_ptr<OutputStream>, not the concrete type that createOutputStream
        // returns: createWriterFor() binds it by reference to move ownership out.
        std::unique_ptr<juce::OutputStream> stream = files[fileIdx].createOutputStream();
        auto writer = wav.createWriterFor (stream,
                                          juce::AudioFormatWriterOptions{}
                                              .withSampleRate    (sr)
                                              .withNumChannels   (2)
                                              .withBitsPerSample (32));
        if (writer == nullptr)
        {
            std::cout << "FAIL: cannot write test wav\n";
            return 1;
        }
        writer->writeFromAudioSampleBuffer (buf, 0, n);
    }

    smt::AnalysisEngine engine;
    engine.setWindowSize (16384);
    const int analyzed = engine.loadFiles (files);
    std::cout << "Analyzed " << analyzed << " files\n";
    if (analyzed != 2)
        return 1;

    bool ok = true;

    // The estimated response must match the known lowpass (relative levels).
    auto theoretical = [&] (float freq)
    {
        const auto w = freq / lpFreq;
        return -10.0f * std::log10 ((1.0f - w * w) * (1.0f - w * w) + 2.0f * 0.707f * 0.707f * w * w * 2.0f);
    };

    const std::vector<float> freqs { 100.0f, 1000.0f, 4000.0f, 8000.0f, 16000.0f };
    const auto avg = engine.getAverageDb (freqs);
    const float ref = avg[0];   // dB at 100 Hz, where the LP is flat

    ok &= approx (avg[1] - ref, theoretical (1000.0f) - theoretical (100.0f), 0.5f, "H(1k)/H(100)");
    ok &= approx (avg[2] - ref, theoretical (4000.0f) - theoretical (100.0f), 0.7f, "H(4k)/H(100)");
    ok &= approx (avg[3] - ref, theoretical (8000.0f) - theoretical (100.0f), 1.0f, "H(8k)/H(100)");

    // Full correction: average x correction must be flat where the needed
    // boost stays below the +12 dB cap (LP(8k) at 16 kHz is about -13 dB).
    engine.setCorrectionLevel (1.0f);
    const std::vector<float> band { 100.0f, 300.0f, 1000.0f, 3000.0f, 6000.0f, 10000.0f };
    const auto corrected = engine.getCorrectedDb (band);
    for (size_t i = 1; i < band.size(); ++i)
        ok &= approx (corrected[i] - corrected[0], 0.0f, 1.0f,
                      ("corrected flat @ " + juce::String (band[i]) + " Hz").toRawUTF8());

    // Phase. The delay estimator removes the linear phase at the IR peak
    // (integer sample, absorbing some filter group delay), so the average
    // phase must match the known lowpass UP TO a term linear in f.
    auto theoreticalPhaseDeg = [&] (float freq)
    {
        const float w = freq / lpFreq;
        return -std::atan2 (2.0f * 0.707f * w, 1.0f - w * w) * 180.0f
               / juce::MathConstants<float>::pi;
    };

    const std::vector<float> phFreqs { 1000.0f, 4000.0f };
    const auto avgPhase = engine.getAveragePhaseDeg (phFreqs);
    const float residual1k = avgPhase[0] - theoreticalPhaseDeg (1000.0f);
    const float residual4k = avgPhase[1] - theoreticalPhaseDeg (4000.0f);
    ok &= approx (residual4k, 4.0f * residual1k, 3.0f,
                  "phase matches filter up to a linear (delay) term");

    // The corrected phase must follow the LF slope target (2nd order
    // highpass at 30 Hz) — that IS the designed response, phase included.
    auto targetPhaseDeg = [] (float freq)
    {
        const float w = freq / 30.0f;
        return std::arg (std::complex<float> (-w * w, 0.0f)
                         / std::complex<float> (1.0f - w * w, std::sqrt (2.0f) * w))
               * 180.0f / juce::MathConstants<float>::pi;
    };

    // Checked with the phase unlimited: that is the full inversion. The
    // default limits it to the crossover region, above which the filter is
    // the minimum-phase one (tested at the end) and the corrected phase is
    // therefore whatever minimum phase leaves: here the fraction of a sample
    // the integer peak alignment left in the average, and the lag of the
    // roll-off above 11 kHz that the boost ceiling does not correct.
    const std::vector<float> phBand { 300.0f, 1000.0f, 3000.0f, 6000.0f };
    engine.setPhaseLimited (false);
    const auto corrPhase = engine.getCorrectedPhaseDeg (phBand);
    for (size_t i = 0; i < phBand.size(); ++i)
        ok &= approx (corrPhase[i], targetPhaseDeg (phBand[i]), 3.0f,
                      ("corrected phase = LF target @ " + juce::String (phBand[i]) + " Hz").toRawUTF8());
    engine.setPhaseLimited (true);

    // With smoothing off the inverse is exact: corrected even flatter.
    engine.setSmoothing (0.0f, 0.0f);
    const auto correctedRaw = engine.getCorrectedDb ({ 300.0f, 1000.0f, 6000.0f });
    for (auto v : correctedRaw)
        ok &= approx (v - correctedRaw[0], 0.0f, 0.5f, "corrected flat (smoothing off)");
    engine.setSmoothing (1.0f / 6.0f, 1.0f / 6.0f);

    // Zero correction level must leave a flat (0 dB) correction curve.
    engine.setCorrectionLevel (0.0f);
    const auto corrZero = engine.getCorrectionDb ({ 100.0f, 1000.0f, 10000.0f });
    for (auto v : corrZero)
        ok &= approx (v, 0.0f, 0.6f, "correction = 0 dB at level 0");

    // Exported IR: convolving the known filter response with the IR must
    // flatten it. Check the IR applies the same magnitude as the designed
    // correction at a few frequencies (FFT of the IR).
    engine.setCorrectionLevel (1.0f);
    const int firLen = 4096;
    auto ir = engine.renderCorrectionIR (firLen);
    if (ir.getNumSamples() != firLen)
    {
        std::cout << "  FAIL IR length " << ir.getNumSamples() << "\n";
        ok = false;
    }
    else
    {
        juce::dsp::FFT fft ((int) std::log2 ((double) firLen));
        std::vector<float> spec ((size_t) (2 * firLen), 0.0f);
        std::copy (ir.getReadPointer (0), ir.getReadPointer (0) + firLen, spec.begin());
        fft.performRealOnlyForwardTransform (spec.data(), true);

        const auto corrDb = engine.getCorrectionDb ({ 500.0f, 2000.0f, 6000.0f });
        const float testFreqs[] = { 500.0f, 2000.0f, 6000.0f };
        for (int i = 0; i < 3; ++i)
        {
            const int bin = (int) (testFreqs[i] * firLen / sr);
            const float mag = std::hypot (spec[(size_t) (2 * bin)], spec[(size_t) (2 * bin + 1)]);
            ok &= approx (juce::Decibels::gainToDecibels (mag), corrDb[(size_t) i], 1.0f,
                          ("IR magnitude @ " + juce::String (testFreqs[i]) + " Hz").toRawUTF8());
        }
    }

    // ── Positions that disagree must not produce a phase the render cannot
    //    realise ───────────────────────────────────────────────────────────
    //
    // The average takes its magnitude from a power mean and its phase from the
    // complex one. Where the positions disagree, that complex sum nearly
    // cancels and its argument becomes the direction of a residual between
    // near-random phasors: it can turn 180 degrees between neighbouring bins
    // while the magnitude walks smoothly through. A linear-phase render turns
    // such a step into a near-zero on the unit circle -- on a real campaign,
    // Q 518 and -25 dB at 697 Hz, which rings audibly on an F, and which the
    // minimum-phase render never showed because it uses the magnitude alone.
    //
    // So the invariant is exactly that: ONE design rendered two ways must have
    // ONE magnitude. Six positions spread over 2.6 ms decorrelate above a few
    // hundred hertz, which is what makes the average's phase worth testing.
    {
        std::cout << "\nPhase renderings of one design, positions that disagree\n";

        juce::Array<juce::File> spread;
        juce::OwnedArray<juce::TemporaryFile> keep;
        juce::Random rng2 (7);
        constexpr int numPos = 6;

        for (int fileIdx = 0; fileIdx < numPos; ++fileIdx)
        {
            auto* t = keep.add (new juce::TemporaryFile (".wav"));
            spread.add (t->getFile());

            juce::AudioBuffer<float> buf (2, n);
            auto* x = buf.getWritePointer (0);
            auto* y = buf.getWritePointer (1);

            const double L = T / std::log (f2 / f1);
            const double K = 2.0 * juce::MathConstants<double>::pi * f1 * L;
            for (int i = 0; i < n; ++i)
                x[i] = 0.25f * (float) std::sin (K * (std::exp (i / sr / L) - 1.0));

            // 23 samples apart: far enough that the positions stop agreeing on
            // phase well inside the band, which is the condition being tested.
            const int d = delaySamples + 23 * fileIdx;
            for (int i = n - 1; i >= 0; --i)
                y[i] = i - d >= 0 ? x[i - d] : 0.0f;

            fxme::Biquad lp2;
            lp2.c = fxme::BiquadCoeffs::lowpass (sr, lpFreq, 0.707f);
            lp2.processBlock (y, n);
            for (int i = 0; i < n; ++i)
                y[i] += 1.0e-5f * (rng2.nextFloat() * 2.0f - 1.0f);

            juce::WavAudioFormat wav2;
            std::unique_ptr<juce::OutputStream> os = spread[fileIdx].createOutputStream();
            auto w2 = wav2.createWriterFor (os, juce::AudioFormatWriterOptions{}
                                                    .withSampleRate    (sr)
                                                    .withNumChannels   (2)
                                                    .withBitsPerSample (32));
            if (w2 == nullptr)
            {
                std::cout << "  FAIL cannot write test wav\n";
                ok = false;
                break;
            }
            w2->writeFromAudioSampleBuffer (buf, 0, n);
        }

        smt::AnalysisEngine e2;
        e2.setWindowSize (16384);

        if (ok && e2.loadFiles (spread) == numPos)
        {
            e2.setCorrectionLevel (1.0f);
            constexpr int len = 8192;

            auto magnitudes = [&] (smt::AnalysisEngine::PhaseType pt)
            {
                e2.setPhaseType (pt);
                auto rendered = e2.renderCorrectionIR (len);
                juce::dsp::FFT fft2 ((int) std::log2 ((double) len));
                std::vector<float> sp ((size_t) (2 * len), 0.0f);
                std::copy (rendered.getReadPointer (0),
                           rendered.getReadPointer (0) + len, sp.begin());
                fft2.performRealOnlyForwardTransform (sp.data(), true);

                std::vector<float> db ((size_t) (len / 2 + 1));
                for (size_t k = 0; k < db.size(); ++k)
                    db[k] = juce::Decibels::gainToDecibels (
                                std::hypot (sp[2 * k], sp[2 * k + 1]), -120.0f);
                return db;
            };

            const auto linear = magnitudes (smt::AnalysisEngine::PhaseType::linear);
            const auto minimum = magnitudes (smt::AnalysisEngine::PhaseType::minimum);

            float worst = 0.0f;
            double worstHz = 0.0;
            for (size_t k = 0; k < linear.size(); ++k)
            {
                const double hz = (double) k * sr / len;
                if (hz < 200.0 || hz > 6000.0)
                    continue;

                const float d = std::abs (linear[k] - minimum[k]);
                if (d > worst)
                {
                    worst = d;
                    worstHz = hz;
                }
            }

            // Both renderings carry the same magnitude by construction, so the
            // tolerance is for the rendering, not for the design: before the
            // phase was faded by the positions' agreement this reached 27 dB.
            ok &= approx (worst, 0.0f, 3.0f,
                          ("linear vs minimum |H|, worst over 200 Hz - 6 kHz (at "
                           + juce::String (worstHz, 0) + " Hz)").toRawUTF8());
        }
        else if (ok)
        {
            std::cout << "  FAIL could not analyze the spread set\n";
            ok = false;
        }
    }

    // ── Linear phase limited to the crossover region ─────────────────────────
    //
    // Above the crossover region a linear-phase inversion of the room's phase
    // buys no audible timing and puts each seat's unshared part of it ahead of
    // the direct sound -- heard on a real campaign as a short pre-reverberation
    // on impacts. So, limited, the linear design must BE the minimum-phase one
    // above four times the crossover, and its render must hold no energy ahead
    // of the peak there; unlimited, it inverts whatever the positions agree on.
    //
    // Three positions that agree exactly on a 2nd-order all-pass at 1 kHz
    // (Q 2): the magnitude is flat, so the only thing a correction can do there
    // is phase, and a linear-phase inverse of an all-pass rings BEFORE its
    // peak. The crossover is the engine's 80 Hz, so the all-pass sits well
    // above 320 Hz.
    {
        std::cout << "\nLinear phase limited to the crossover region\n";

        juce::Array<juce::File> agree;
        juce::OwnedArray<juce::TemporaryFile> keep;
        juce::Random rng3 (11);
        constexpr int numPos = 3;

        for (int fileIdx = 0; fileIdx < numPos && ok; ++fileIdx)
        {
            auto* t = keep.add (new juce::TemporaryFile (".wav"));
            agree.add (t->getFile());

            juce::AudioBuffer<float> buf (2, n);
            auto* x = buf.getWritePointer (0);
            auto* y = buf.getWritePointer (1);

            const double L = T / std::log (f2 / f1);
            const double K = 2.0 * juce::MathConstants<double>::pi * f1 * L;
            for (int i = 0; i < n; ++i)
                x[i] = 0.25f * (float) std::sin (K * (std::exp (i / sr / L) - 1.0));

            for (int i = n - 1; i >= 0; --i)
                y[i] = i - delaySamples >= 0 ? x[i - delaySamples] : 0.0f;

            // RBJ all-pass: unit magnitude, 360 degrees of phase across 1 kHz.
            const double w0 = 2.0 * juce::MathConstants<double>::pi * 1000.0 / sr;
            const double alpha = std::sin (w0) / (2.0 * 2.0);
            const double a0 = 1.0 + alpha;
            fxme::Biquad ap;
            ap.c.b0 = (float) ((1.0 - alpha) / a0);
            ap.c.b1 = (float) (-2.0 * std::cos (w0) / a0);
            ap.c.b2 = 1.0f;
            ap.c.a1 = ap.c.b1;
            ap.c.a2 = ap.c.b0;
            ap.processBlock (y, n);
            for (int i = 0; i < n; ++i)
                y[i] += 1.0e-5f * (rng3.nextFloat() * 2.0f - 1.0f);

            juce::WavAudioFormat wav3;
            std::unique_ptr<juce::OutputStream> os = agree[fileIdx].createOutputStream();
            auto w3 = wav3.createWriterFor (os, juce::AudioFormatWriterOptions{}
                                                    .withSampleRate    (sr)
                                                    .withNumChannels   (2)
                                                    .withBitsPerSample (32));
            if (w3 == nullptr)
            {
                std::cout << "  FAIL cannot write test wav\n";
                ok = false;
                break;
            }
            w3->writeFromAudioSampleBuffer (buf, 0, n);
        }

        smt::AnalysisEngine e3;
        e3.setWindowSize (16384);

        if (ok && e3.loadFiles (agree) == numPos)
        {
            e3.setCorrectionLevel (1.0f);
            const float fx = e3.getCrossoverHz();

            // Worst disagreement between the linear design's phase and the
            // phase the minimum-phase render realises, above 4 x crossover.
            auto phaseGap = [&]
            {
                std::vector<float> fr;
                for (float hz = 4.0f * fx; hz <= 3000.0f; hz *= 1.01f)
                    fr.push_back (hz);
                e3.setPhaseType (smt::AnalysisEngine::PhaseType::linear);
                const auto lin = e3.getCorrectionPhaseDeg (fr);
                e3.setPhaseType (smt::AnalysisEngine::PhaseType::minimum);
                const auto mp = e3.getCorrectionPhaseDeg (fr);
                e3.setPhaseType (smt::AnalysisEngine::PhaseType::linear);

                float worst = 0.0f;
                for (size_t i = 0; i < fr.size(); ++i)
                {
                    float d = std::fmod (std::abs (lin[i] - mp[i]), 360.0f);
                    worst = std::max (worst, std::min (d, 360.0f - d));
                }
                return worst;
            };

            // Energy of the linear render ahead of its peak, 0.3 to 20 ms
            // before it, relative to the whole render: the pre-echo itself.
            auto preEcho = [&]
            {
                constexpr int len = 8192;
                e3.setPhaseType (smt::AnalysisEngine::PhaseType::linear);
                const auto ir = e3.renderCorrectionIR (len);
                const float* d = ir.getReadPointer (0);
                int peak = 0;
                for (int i = 1; i < len; ++i)
                    if (std::abs (d[i]) > std::abs (d[peak]))
                        peak = i;

                double pre = 0.0, all = 0.0;
                const int from = juce::jmax (0, peak - (int) (0.020 * sr));
                const int to   = peak - (int) (0.0003 * sr);
                for (int i = 0; i < len; ++i)
                {
                    const double v = (double) d[i] * (double) d[i];
                    all += v;
                    if (i >= from && i < to)
                        pre += v;
                }
                return (float) (10.0 * std::log10 (pre / juce::jmax (all, 1.0e-30) + 1.0e-30));
            };

            e3.setPhaseLimited (true);
            const float gapOn = phaseGap(), preOn = preEcho();
            e3.setPhaseLimited (false);
            const float gapOff = phaseGap(), preOff = preEcho();
            e3.setPhaseLimited (true);

            std::cout << "  limited:   worst phase gap to minimum phase " << gapOn
                      << " deg, pre-echo " << preOn << " dB\n"
                      << "  unlimited: worst phase gap to minimum phase " << gapOff
                      << " deg, pre-echo " << preOff << " dB\n";

            // Limited, the design above 4 x crossover IS the minimum-phase one...
            ok &= approx (gapOn, 0.0f, 3.0f, "limited: linear vs minimum phase above 4 x crossover (deg)");
            // ...and unlimited it is not, which is what makes the check mean something.
            if (gapOff < 90.0f)
            {
                std::cout << "  FAIL unlimited design did not invert the all-pass (" << gapOff << " deg)\n";
                ok = false;
            }
            // The render follows: the ring ahead of the peak is gone.
            if (preOn > preOff - 20.0f)
            {
                std::cout << "  FAIL pre-echo not reduced by 20 dB (" << preOn << " vs "
                          << preOff << " dB)\n";
                ok = false;
            }
        }
        else if (ok)
        {
            std::cout << "  FAIL could not analyze the agreeing set\n";
            ok = false;
        }
    }

    std::cout << (ok ? "ALL TESTS PASSED\n" : "TESTS FAILED\n");
    return ok ? 0 : 1;
}
