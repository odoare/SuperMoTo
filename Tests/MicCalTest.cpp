/*
  ------------------------------------------------------------------------------
    MicCalTest.cpp

    Offline check of the microphone-calibration logic (fxme::MicCalibration):
      - parsing the standard text format (comments, whitespace/comma columns),
      - exact values at the file's points and log-frequency interpolation,
      - endpoint clamping outside the file's range,
      - phase unwrapping across a +/-180 deg wrap,
      - the complex "correction" being the exact inverse of the mic response,
      - applyToSpectrum() flattening a measured = mic-response spectrum,
      - magnitude-only files (no phase column) and invalid / empty input.

    The realistic file case is loaded from Tests/mic_cal_synthetic.txt (path
    passed by CMake as MICCAL_TEST_FILE); format edge cases use inline text.

    Run: build target SuperMoToMicCalTests and execute it; exits 0 on success.
  ------------------------------------------------------------------------------
*/

#include <JuceHeader.h>
#include <FxmeTools/dsp/MicCalibration.h>

static bool g_ok = true;

static bool approx (float a, float b, float tol, const char* what)
{
    const bool good = std::abs (a - b) <= tol;
    std::cout << (good ? "  OK  " : "  FAIL ") << what << ": " << a
              << " (expected " << b << " +/- " << tol << ")\n";
    g_ok = g_ok && good;
    return good;
}

static bool check (bool cond, const char* what)
{
    std::cout << (cond ? "  OK  " : "  FAIL ") << what << "\n";
    g_ok = g_ok && cond;
    return cond;
}

static float argDeg (std::complex<float> c)
{
    return std::arg (c) * 180.0f / juce::MathConstants<float>::pi;
}

static float gainDb (float gain)
{
    return 20.0f * std::log10 (gain);
}

int main()
{
    // ── 1. Load the realistic synthetic file ─────────────────────────────────
    std::cout << "Loading " << MICCAL_TEST_FILE << "\n";
    fxme::MicCalibration cal;
    check (cal.loadFromFile (juce::File (MICCAL_TEST_FILE)), "loadFromFile succeeds");
    check (cal.isValid(),                  "calibration is valid");
    check (cal.hasPhase(),                 "phase column detected");
    check (cal.getNumPoints() == 5,        "5 points parsed (comments skipped)");
    check (cal.getName() == "mic_cal_synthetic.txt", "source name remembered");
    approx (cal.getMinFreqHz(), 20.0f,     0.01f, "min freq");
    approx (cal.getMaxFreqHz(), 20000.0f,  0.01f, "max freq");

    // Exact magnitudes at the file's points.
    approx (cal.magnitudeDbAt (100.0),    2.0f, 1.0e-3f, "mag @ 100 Hz (exact)");
    approx (cal.magnitudeDbAt (1000.0),  -4.0f, 1.0e-3f, "mag @ 1 kHz (exact)");
    approx (cal.magnitudeDbAt (10000.0),  6.0f, 1.0e-3f, "mag @ 10 kHz (exact)");

    // Log-frequency interpolation: 316.23 Hz is the geometric mean of 100 and
    // 1000, so the value is the average of 2 dB and -4 dB = -1 dB. Likewise the
    // phase interpolates from +10 to -20 deg -> -5 deg.
    approx (cal.magnitudeDbAt (316.2278),  -1.0f, 1.0e-2f, "mag interp @ 316 Hz");
    approx (juce::radiansToDegrees (cal.phaseRadAt (316.2278)), -5.0f, 0.05f,
            "phase interp @ 316 Hz");

    // Endpoints held flat outside the file range.
    approx (cal.magnitudeDbAt (5.0),    1.0f, 1.0e-3f, "mag clamp below range");
    approx (cal.magnitudeDbAt (40000.0), -2.0f, 1.0e-3f, "mag clamp above range");

    // ── 2. Correction is the exact inverse of the mic response ───────────────
    // At 1 kHz the mic reads -4 dB / -20 deg, so the correction must be
    // +4 dB / +20 deg.
    const auto c1k = cal.correctionAt (1000.0);
    approx (gainDb (std::abs (c1k)), 4.0f, 1.0e-2f, "correction gain @ 1 kHz");
    approx (argDeg (c1k), 20.0f, 0.05f, "correction phase @ 1 kHz");

    // mic response * correction == 1 (flat) at several frequencies.
    for (double f : { 60.0, 300.0, 2000.0, 8000.0 })
    {
        const auto mic = std::polar (std::pow (10.0f, cal.magnitudeDbAt (f) / 20.0f),
                                     cal.phaseRadAt (f));
        const auto flat = mic * cal.correctionAt (f);
        approx (std::abs (flat - std::complex<float> (1.0f, 0.0f)), 0.0f, 1.0e-4f,
                ("mic * correction == 1 @ " + juce::String (f) + " Hz").toRawUTF8());
    }

    // ── 3. applyToSpectrum flattens a measured = mic-response spectrum ────────
    {
        const double sr = 48000.0;
        const int fftSize = 48000;                 // bin k maps to exactly k Hz
        std::vector<std::complex<float>> spec ((size_t) (fftSize / 2 + 1), { 0.0f, 0.0f });
        for (int k : { 100, 1000, 5000, 10000 })
        {
            const double f = (double) k;           // = k*sr/fftSize
            spec[(size_t) k] = std::polar (std::pow (10.0f, cal.magnitudeDbAt (f) / 20.0f),
                                           cal.phaseRadAt (f));
        }
        cal.applyToSpectrum (spec, sr, fftSize);
        for (int k : { 100, 1000, 5000, 10000 })
            approx (std::abs (spec[(size_t) k] - std::complex<float> (1.0f, 0.0f)), 0.0f, 1.0e-4f,
                    ("applyToSpectrum flattens bin " + juce::String (k)).toRawUTF8());
    }

    // ── 4. Format edge cases (inline text) ───────────────────────────────────
    // Comma separators, mixed comment markers, leading header text.
    {
        fxme::MicCalibration c;
        const juce::String txt =
            "# a hash comment\n"
            "; a semicolon comment\n"
            "Frequency  SPL  Phase\n"        // stray header (non-numeric first token)
            "100,  1.0,  0.0\n"
            "1000, 3.0,  0.0\n"
            "10000, 5.0, 0.0\n";
        check (c.loadFromText (txt, "commas.txt"), "comma/comment file parses");
        check (c.getNumPoints() == 3, "3 numeric rows kept, header skipped");
        approx (c.magnitudeDbAt (1000.0), 3.0f, 1.0e-3f, "comma file value @ 1 kHz");
    }

    // Magnitude-only file: no phase column -> hasPhase() false, zero phase.
    {
        fxme::MicCalibration c;
        check (c.loadFromText ("100 1.0\n1000 -2.0\n10000 4.0\n", "magonly.txt"),
               "magnitude-only file parses");
        check (! c.hasPhase(), "no phase column reported");
        approx (juce::radiansToDegrees (c.phaseRadAt (1000.0)), 0.0f, 1.0e-3f,
                "phase is zero without a phase column");
        approx (argDeg (c.correctionAt (1000.0)), 0.0f, 1.0e-3f,
                "correction phase zero without a phase column");
        approx (gainDb (std::abs (c.correctionAt (1000.0))),
                2.0f, 1.0e-2f, "magnitude-only correction gain @ 1 kHz");
    }

    // Phase unwrapping across a +/-180 deg wrap: 170 -> -170 in the file is a
    // continuous +20 deg step, so the unwrapped value at 200 Hz is +190 deg and
    // the midpoint (141.42 Hz) is +180 deg.
    {
        fxme::MicCalibration c;
        check (c.loadFromText ("100 0 170\n200 0 -170\n", "wrap.txt"), "wrap file parses");
        approx (juce::radiansToDegrees (c.phaseRadAt (200.0)), 190.0f, 0.1f,
                "phase unwrapped @ 200 Hz");
        approx (juce::radiansToDegrees (c.phaseRadAt (141.4214)), 180.0f, 0.2f,
                "phase unwrapped midpoint @ 141 Hz");
    }

    // ── 5. Invalid input and the default (unloaded) object ───────────────────
    {
        fxme::MicCalibration c;
        check (! c.loadFromText ("* only comments\n42\n", "bad.txt"),
               "fewer than 2 points rejected");
        check (! c.isValid(), "rejected file leaves it invalid");
    }
    {
        fxme::MicCalibration none;                 // never loaded
        check (! none.isValid(), "default calibration is invalid");
        approx (none.magnitudeDbAt (1000.0), 0.0f, 1.0e-6f, "default mag is 0 dB");
        const auto unity = none.correctionAt (1000.0);
        approx (std::abs (unity - std::complex<float> (1.0f, 0.0f)), 0.0f, 1.0e-6f,
                "default correction is unity");

        std::vector<std::complex<float>> spec (16, { 2.0f, -1.0f });
        none.applyToSpectrum (spec, 48000.0, 32);          // must be a no-op
        approx (std::abs (spec[8] - std::complex<float> (2.0f, -1.0f)), 0.0f, 1.0e-6f,
                "default applyToSpectrum is a no-op");
    }

    std::cout << (g_ok ? "ALL TESTS PASSED\n" : "TESTS FAILED\n");
    return g_ok ? 0 : 1;
}
