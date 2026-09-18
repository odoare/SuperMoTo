/*
  ------------------------------------------------------------------------------
    FractionalDelayTest.cpp

    Two things, because the project holds two:

    1. The delay OutputProcessor really applies. It rounds to a whole number of
       samples, so every setting comes out as an exact sample shift: a unit
       impulse in, a unit impulse out, nothing added and nothing coloured. The
       tests below pin the rounding (including that it rounds the same way
       MatrixEngine::recomputeLatencyComp does, so the two cannot disagree),
       the FIR self-absorption, and block-boundary continuity.

    2. The windowed-sinc interpolator of FractionalDelay.h, which is not wired
       in but is kept for a rig that ever needs sub-sample resolution. Testing
       it here keeps it from rotting: the kernels are checked for unit gain at
       DC, for symmetry, for reducing to an impulse at zero fraction, and for
       the flat magnitude that was the point of it.

    Run: build target SuperMoToDelayTests and execute it; exits 0 on success.

    Author: Olivier Doaré, github.com/odoare
    (c) 2023-2026 Olivier Doaré
    Licenced under the GNU AGPL Version 3.0, or commercial terms (LICENSE.md)
    SPDX-License-Identifier: AGPL-3.0-or-later OR LicenseRef-FXME-Commercial
  ------------------------------------------------------------------------------
*/

#include <JuceHeader.h>
#include "../Source/Dsp/OutputProcessor.h"
#include "../Source/Dsp/FractionalDelay.h"
#include <cmath>
#include <complex>
#include <iostream>
#include <vector>

static bool g_ok = true;

static bool check (bool cond, const juce::String& what)
{
    std::cout << (cond ? "  OK   " : "  FAIL ") << what << "\n";
    g_ok = g_ok && cond;
    return cond;
}

static constexpr double sr = 44100.0;
static constexpr int    len = 4096;

/** The delay path's impulse response, with the EQ left off. */
static std::vector<float> impulseResponse (double delaySamples, int firLatency = 0,
                                           int blockSize = 0)
{
    smt::OutputProcessor op;
    op.prepare (sr, 512);
    op.setFirLatencySamples (firLatency);

    smt::OutputAudioSettings s;
    s.delayMs = (float) (delaySamples * 1000.0 / sr);
    op.applySettings (s, true);

    std::vector<float> buf ((size_t) len, 0.0f);
    buf[0] = 1.0f;

    if (blockSize <= 0)
        op.process (buf.data(), len);
    else
        for (int i = 0; i < len; i += blockSize)
            op.process (buf.data() + i, juce::jmin (blockSize, len - i));

    return buf;
}

/** Where the unit impulse came out, or -1 if what came out was not one. */
static int impulseAt (const std::vector<float>& ir)
{
    int at = -1;
    for (int i = 0; i < len; ++i)
    {
        if (ir[(size_t) i] == 0.0f)
            continue;
        if (at >= 0 || std::abs (ir[(size_t) i] - 1.0f) > 1.0e-7f)
            return -1;              // a second non-zero sample, or not unity
        at = i;
    }
    return at;
}

/** |H(f)| of a bare kernel, in dB, by direct evaluation: 32 taps do not need
    an FFT, and this keeps the test free of one. */
static double kernelMagDb (const float* h, double freqHz)
{
    const double w = juce::MathConstants<double>::twoPi * freqHz / sr;
    std::complex<double> H {};
    for (int k = 0; k < smt::fracdelay::taps; ++k)
        H += (double) h[k] * std::polar (1.0, -w * (double) k);
    return 20.0 * std::log10 (std::abs (H));
}

int main()
{
    std::cout << "FractionalDelayTest\n\n--- the delay OutputProcessor applies ---\n";

    // ── every setting is an exact sample shift ───────────────────────────────
    for (double want : { 0.0, 1.0, 17.0, 1284.0, 1285.0, 4000.0 })
        check (impulseAt (impulseResponse (want)) == (int) want,
               juce::String ("a delay of ") + juce::String (want, 0)
                   + " samples comes out as a unit impulse there");

    // ── fractions round to the nearest sample, and stay exact ────────────────
    for (auto pair : { std::pair<double, int> { 1284.1, 1284 },
                       std::pair<double, int> { 1284.4, 1284 },
                       std::pair<double, int> { 1284.633, 1285 },
                       std::pair<double, int> { 1284.9, 1285 },
                       std::pair<double, int> { 0.4, 0 },
                       std::pair<double, int> { 0.6, 1 } })
    {
        const int at = impulseAt (impulseResponse (pair.first));
        check (at == pair.second,
               juce::String (pair.first, 3) + " samples rounds to " + juce::String (at)
                   + " (expected " + juce::String (pair.second)
                   + "), and is still a bare impulse, so the magnitude is flat by construction");
    }

    // ── rounded the same way MatrixEngine rounds it ──────────────────────────
    {
        // MatrixEngine::recomputeLatencyComp() budgets roundToInt(delay) for
        // this output; whatever the line then applies, plus the FIR latency it
        // self-absorbed, has to come to that same integer, or the compensation
        // of every other output is off by a sample. The rounding has to happen
        // once, on the user's delay: juce::roundToInt is round-half-to-even, so
        // rounding before and after subtracting the latency are not the same
        // thing (roundToInt(1284.5) - 17 is 1267, roundToInt(1267.5) is 1268).
        auto budgetedFor = [] (double samples)
        {
            const float ms = (float) (samples * 1000.0 / sr);   // as the model stores it
            return juce::roundToInt ((double) ms * 0.001 * sr);
        };

        bool agree = true;
        for (double raw : { 1284.1, 1284.5, 1284.633, 1283.5, 260.5, 17.25, 0.5 })
            for (int firLat : { 0, 17, 260, 1024 })
            {
                const int budgeted = budgetedFor (raw);
                const int applied = impulseAt (impulseResponse (raw, firLat));
                agree = agree && (applied >= 0)
                              && (applied + juce::jmin (firLat, budgeted) == budgeted);
            }
        check (agree, "what the line applies plus the self-absorbed FIR latency is "
                      "exactly the integer MatrixEngine budgeted");
    }

    // ── the FIR self-absorption still comes off the delay ────────────────────
    check (impulseAt (impulseResponse (1284.633, 1024)) == 261,
           "a FIR latency of 1024 leaves 261 samples on the delay line");
    check (impulseAt (impulseResponse (1284.633, 2048)) == 0,
           "a FIR latency longer than the delay leaves nothing for the line");

    // ── block boundaries do not show ─────────────────────────────────────────
    {
        const auto whole = impulseResponse (1284.633);
        const auto blocked = impulseResponse (1284.633, 0, 64);
        check (whole == blocked, "64-sample blocks give the same output as one long call");
    }

    std::cout << "\n--- FractionalDelay.h, kept but not wired in ---\n";

    // ── the kernels are still the kernels ────────────────────────────────────
    {
        double worstGain = 0.0;
        for (int p = 0; p < smt::fracdelay::phases; ++p)
        {
            const float* h = smt::fracdelay::kernel (p);
            double sum = 0.0;
            for (int k = 0; k < smt::fracdelay::taps; ++k)
                sum += (double) h[k];
            worstGain = std::max (worstGain, std::abs (sum - 1.0));
        }
        check (worstGain < 1.0e-6,
               juce::String ("every kernel has unit gain at DC, worst error ")
                   + juce::String (worstGain, 9));
    }
    {
        const float* h = smt::fracdelay::kernel (0);
        bool impulse = std::abs (h[smt::fracdelay::centre] - 1.0f) < 1.0e-6f;
        for (int k = 0; k < smt::fracdelay::taps; ++k)
            if (k != smt::fracdelay::centre)
                impulse = impulse && std::abs (h[k]) < 1.0e-6f;
        check (impulse, "a zero fraction reduces to a unit impulse on the centre tap");
    }
    {
        const float* h = smt::fracdelay::kernel (smt::fracdelay::phases / 2);
        double worst = 0.0;
        for (int k = 0; k < smt::fracdelay::taps / 2; ++k)
            worst = std::max (worst,
                              (double) std::abs (h[k] - h[smt::fracdelay::taps - 1 - k]));
        check (worst < 1.0e-6, "the half-sample kernel is symmetric");
    }
    {
        // The whole point of the thing: flat where the two-tap linear
        // interpolation it was written to replace loses 1.5 dB at 8 kHz and
        // 7.6 dB at 16 kHz.
        double worst8 = 0.0, worst16 = 0.0;
        for (int p = 1; p < smt::fracdelay::phases; ++p)
        {
            const float* h = smt::fracdelay::kernel (p);
            for (double f = 20.0; f <= 8000.0; f *= 1.2)
                worst8 = std::max (worst8, std::abs (kernelMagDb (h, f)));
            for (double f = 8000.0; f <= 16000.0; f *= 1.05)
                worst16 = std::max (worst16, std::abs (kernelMagDb (h, f)));
        }
        check (worst8 < 0.01 && worst16 < 0.05,
               juce::String ("the kernels are flat to ") + juce::String (worst8, 5)
                   + " dB up to 8 kHz and " + juce::String (worst16, 5) + " dB to 16 kHz");
    }

    std::cout << (g_ok ? "\nALL TESTS PASSED\n" : "\nTESTS FAILED\n");
    return g_ok ? 0 : 1;
}
