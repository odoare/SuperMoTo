/*
  ------------------------------------------------------------------------------
    SweepTest.cpp

    Offline check of the synchronized swept-sine logic
    (fxme::SynchronizedSweep, after Novak et al., JAES 2015):
      - sweep-rate quantization: f1*L integer, duration = L*ln(f2/f1),
      - the identity system deconvolving to a clean pulse at t = 0,
      - a pure delay deconvolving to a pulse at that delay,
      - a cubic nonlinearity producing a 3rd-order harmonic IR at
        L*ln(3)*fs before the linear one (circularly wrapped),
      - extractCircular windowing (incl. wrap-around).

    A short sweep (2 s, 20 Hz .. 8 kHz at 24 kHz) keeps the FFTs quick.

    Run: build target SuperMoToSweepTests and execute it; exits 0 on success.
  ------------------------------------------------------------------------------
*/

#include <JuceHeader.h>
#include <FxmeTools/dsp/SynchronizedSweep.h>
#include <iostream>

static bool g_ok = true;

static bool check (bool cond, const char* what)
{
    std::cout << (cond ? "  OK  " : "  FAIL ") << what << "\n";
    g_ok = g_ok && cond;
    return cond;
}

static bool approx (double a, double b, double tol, const char* what)
{
    const bool good = std::abs (a - b) <= tol;
    std::cout << (good ? "  OK  " : "  FAIL ") << what << ": " << a
              << " (expected " << b << " +/- " << tol << ")\n";
    g_ok = g_ok && good;
    return good;
}

// Peak magnitude and its index within a circular window of half-width `hw`
// around `centre`.
static void peakAround (const std::vector<float>& h, double centre, int hw,
                        float& peak, int& peakIdx)
{
    const int n = (int) h.size();
    peak = 0.0f;
    peakIdx = 0;
    for (int i = -hw; i <= hw; ++i)
    {
        int idx = ((int) std::llround (centre) + i) % n;
        if (idx < 0)
            idx += n;
        if (std::abs (h[(size_t) idx]) > peak)
        {
            peak = std::abs (h[(size_t) idx]);
            peakIdx = idx;
        }
    }
}

int main()
{
    const double f1 = 20.0, f2 = 8000.0, fs = 24000.0, targetT = 2.0;

    // ── 1. Quantization ──────────────────────────────────────────────────────
    std::cout << "Sweep parameters\n";
    fxme::SynchronizedSweep sweep;
    sweep.prepare (f1, f2, fs, targetT);

    const double L = sweep.getL();
    approx (f1 * L, std::round (f1 * L), 1.0e-9, "f1*L is an integer");
    approx (sweep.getDurationS(), L * std::log (f2 / f1), 1.0e-12,
            "duration = L*ln(f2/f1)");
    check (std::abs (sweep.getDurationS() - targetT) < 0.5,
           "actual duration close to the target");
    check (sweep.getNumSamples() > 0, "positive length");
    approx (sweep.getSample (0), 0.0, 1.0e-9, "sweep starts at zero phase");

    fxme::SynchronizedSweep exact;
    exact.prepareExact (f1, f2, fs, L);
    approx (exact.getDurationS(), sweep.getDurationS(), 0.0, "prepareExact keeps L");

    const auto x = sweep.render();
    const int numS = (int) x.size();
    const int tail = (int) (0.25 * fs);

    // ── 2. Identity system: y = x -> pulse at t = 0 ─────────────────────────
    std::cout << "Identity deconvolution\n";
    std::vector<float> y (x.begin(), x.end());
    y.resize ((size_t) (numS + tail), 0.0f);

    const auto h = sweep.deconvolve (y.data(), (int) y.size());
    const int N = (int) h.size();
    check (N == sweep.getFftSizeFor ((int) y.size()), "deconvolved length = FFT size");

    float peak0;
    int peakIdx0;
    peakAround (h, 0.0, 8, peak0, peakIdx0);
    check (peakIdx0 <= 2 || peakIdx0 >= N - 2, "identity pulse at t = 0 (+/- 2)");
    check (peak0 > 0.3f && peak0 < 1.5f,
           "identity pulse amplitude sane (band-limited unit pulse)");

    // Background well below the pulse away from t = 0 and from the harmonic
    // wrap region.
    float bgPeak;
    int bgIdx;
    peakAround (h, N / 4.0, N / 8, bgPeak, bgIdx);
    check (bgPeak < 0.05f * peak0, "background < -26 dB of the pulse");

    // ── 3. Pure delay -> pulse at the delay ─────────────────────────────────
    std::cout << "Delay deconvolution\n";
    const int delay = 480;
    std::vector<float> yd ((size_t) (numS + tail + delay), 0.0f);
    for (int i = 0; i < numS; ++i)
        yd[(size_t) (i + delay)] = x[(size_t) i];

    const auto hd = sweep.deconvolve (yd.data(), (int) yd.size());
    float peakD;
    int peakIdxD;
    peakAround (hd, (double) delay, 8, peakD, peakIdxD);
    check (std::abs (peakIdxD - delay) <= 2, "delayed pulse at the delay (+/- 2)");
    check (peakD > 0.3f, "delayed pulse amplitude sane");

    // ── 4. Cubic nonlinearity -> 3rd-order harmonic IR ──────────────────────
    std::cout << "Harmonic separation\n";
    std::vector<float> yn (y.size(), 0.0f);
    for (size_t i = 0; i < y.size(); ++i)
        yn[i] = y[i] - 0.2f * y[i] * y[i] * y[i];

    const auto hn = sweep.deconvolve (yn.data(), (int) yn.size());
    const double off3 = sweep.harmonicOffsetSamples (3);
    check (sweep.harmonicOffsetSamples (1) == 0.0, "order 1 has zero offset");

    // The 3rd-order IR sits off3 samples before the linear one -> wrapped to
    // N - off3. Compare against the same window of the LINEAR-only response.
    const int hw3 = (int) (0.005 * fs);
    float peak3, peak3lin;
    int i3, i3lin;
    peakAround (hn, (double) N - off3, hw3, peak3, i3);
    peakAround (h,  (double) N - off3, hw3, peak3lin, i3lin);
    check (peak3 > 0.02f, "3rd-order harmonic IR present");
    check (peak3 > 10.0f * (peak3lin + 1.0e-6f),
           "harmonic energy absent from the linear-only response");

    // Even orders should stay silent for an odd nonlinearity.
    const double off2 = sweep.harmonicOffsetSamples (2);
    float peak2;
    int i2;
    peakAround (hn, (double) N - off2, hw3, peak2, i2);
    check (peak2 < 0.25f * peak3, "no 2nd-order energy from an odd nonlinearity");

    // ── 5. extractCircular ──────────────────────────────────────────────────
    std::cout << "extractCircular\n";
    std::vector<float> ramp (16);
    for (int i = 0; i < 16; ++i)
        ramp[(size_t) i] = (float) i;
    const auto win = fxme::SynchronizedSweep::extractCircular (ramp, 0.0, 5);
    check ((int) win.size() == 5, "requested length");
    check (win[0] == 14.0f && win[1] == 15.0f && win[2] == 0.0f
               && win[3] == 1.0f && win[4] == 2.0f,
           "circular wrap around index 0");

    std::cout << (g_ok ? "\nAll sweep tests passed.\n"
                       : "\nSOME SWEEP TESTS FAILED.\n");
    return g_ok ? 0 : 1;
}
