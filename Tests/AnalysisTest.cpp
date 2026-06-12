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
  ------------------------------------------------------------------------------
*/

#include <JuceHeader.h>
#include "../Source/Dsp/AnalysisEngine.h"
#include "../Source/Dsp/Biquad.h"

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
        smt::Biquad lp;
        lp.c = smt::BiquadCoeffs::lowpass (sr, lpFreq, 0.707f);
        const int d = delaySamples + 7 * fileIdx;
        for (int i = n - 1; i >= 0; --i)
            y[i] = i - d >= 0 ? x[i - d] : 0.0f;
        lp.processBlock (y, n);
        for (int i = 0; i < n; ++i)
            y[i] += 1.0e-5f * (rng.nextFloat() * 2.0f - 1.0f);

        juce::WavAudioFormat wav;
        auto stream = files[fileIdx].createOutputStream();
        std::unique_ptr<juce::AudioFormatWriter> writer (
            wav.createWriterFor (stream.get(), sr, 2, 32, {}, 0));
        if (writer == nullptr)
        {
            std::cout << "FAIL: cannot write test wav\n";
            return 1;
        }
        stream.release();
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

    std::cout << (ok ? "ALL TESTS PASSED\n" : "TESTS FAILED\n");
    return ok ? 0 : 1;
}
