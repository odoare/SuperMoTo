/*
  ------------------------------------------------------------------------------
    TargetCurveTest.cpp

    The monitor target curve, both halves of it: the shape TargetCurveShape
    defines and plots, and the biquads buildTargetCascade() realises it with.
    The whole point of the design is that those two cannot drift apart, so
    that is what most of this checks — at three sample rates, since the only
    thing that separates them is the bilinear transform, and that is where a
    rate shows.

    Also pinned here: that the curve delivers the tilt it is asked for, that
    it is a straight line against log frequency, that it never has gain
    anywhere in the band (so the layer can never clip an output), and that a
    flat curve builds nothing at all.

    Run: build target SuperMoToTargetTests and execute it; exits 0 on success.

    Author: Olivier Doaré, github.com/odoare
    (c) 2023-2026 Olivier Doaré
    Licenced under the GNU AGPL Version 3.0, or commercial terms (LICENSE.md)
    SPDX-License-Identifier: AGPL-3.0-or-later OR LicenseRef-FXME-Commercial
  ------------------------------------------------------------------------------
*/

#include <JuceHeader.h>
#include "../Source/Dsp/TargetCurve.h"
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

/** |H| of a built cascade at f, in dB. */
static double cascadeDb (const fxme::BiquadCoeffs* c, int n, double f, double sr)
{
    const auto z = std::polar (1.0, -2.0 * juce::MathConstants<double>::pi * f / sr);
    std::complex<double> h (1.0, 0.0);
    for (int i = 0; i < n; ++i)
        h *= ((double) c[i].b0 + (double) c[i].b1 * z + (double) c[i].b2 * z * z)
           / (1.0 + (double) c[i].a1 * z + (double) c[i].a2 * z * z);
    return 20.0 * std::log10 (std::abs (h));
}

static std::vector<double> bandGrid (int n = 400)
{
    std::vector<double> f ((size_t) n);
    for (int i = 0; i < n; ++i)
        f[(size_t) i] = smt::targetLowHz * std::pow (smt::targetHighHz / smt::targetLowHz,
                                                     (double) i / (double) (n - 1));
    return f;
}

struct NamedCurve { const char* name; smt::TargetCurve curve; };

static const NamedCurve curves[] =
{
    { "tilt -3",              { -3.0f,    0.0f, 0.0f, 105.0f } },
    { "tilt -4 (default)",    { -4.0f,    0.0f, 0.0f, 105.0f } },
    { "tilt -6",              { -6.0f,    0.0f, 0.0f, 105.0f } },
    { "tilt -10 + bass +3",   { -10.0f,   0.0f, 3.0f, 105.0f } },
    { "flat below 1k, -6",    { -6.0f, 1000.0f, 0.0f, 105.0f } },
    { "bass +4 only",         {  0.0f,    0.0f, 4.0f, 105.0f } },
    { "rising +3",            {  3.0f,    0.0f, 0.0f, 105.0f } },
};

int main()
{
    std::cout << "TargetCurveTest\n";
    const auto f = bandGrid();

    // ── the shape delivers what it is asked for ─────────────────────────────
    std::cout << "\n--- the curve as defined ---\n";
    for (const auto& c : curves)
    {
        // A shelf adds a drop of its own, so the claim is about the tilt.
        if (c.curve.tiltDb == 0.0f || c.curve.bassDb != 0.0f)
            continue;

        const smt::TargetCurveShape shape (c.curve);
        const double lo = smt::targetcurve::tiltLowHz (c.curve);
        const double got = shape.gainDb (smt::targetHighHz) - shape.gainDb (lo);

        // A turnover's knee is rounded rather than square -- six gentle
        // sections cannot make a corner, and pretending otherwise is what the
        // definition was changed to avoid -- so the curve is already a little
        // below 0 dB at the turnover itself and the fall measured from there
        // comes out slightly short. 0.11 dB on the 1 kHz case.
        const double tol = c.curve.turnoverHz > 0.0f ? 0.15 : 0.06;
        check (std::abs (got - (double) c.curve.tiltDb) < tol,
               juce::String (c.name) + ": falls " + juce::String (got, 2)
                   + " dB from the turnover to 20 kHz");
    }

    // ── a plain tilt is a straight line against log f ───────────────────────
    for (double tilt : { -3.0, -6.0, -10.0 })
    {
        const smt::TargetCurve c { (float) tilt, 0.0f, 0.0f, 105.0f };
        const smt::TargetCurveShape shape (c);

        // Least-squares line through the curve, then the worst departure.
        double sx = 0.0, sy = 0.0, sxx = 0.0, sxy = 0.0;
        for (double freq : f)
        {
            const double x = std::log2 (freq), y = shape.gainDb (freq);
            sx += x; sy += y; sxx += x * x; sxy += x * y;
        }
        const double n = (double) f.size();
        const double slope = (n * sxy - sx * sy) / (n * sxx - sx * sx);
        const double intercept = (sy - slope * sx) / n;

        double worst = 0.0;
        for (double freq : f)
            worst = std::max (worst, std::abs (shape.gainDb (freq)
                                               - (slope * std::log2 (freq) + intercept)));

        check (worst < 0.05,
               juce::String ("a ") + juce::String (tilt, 0) + " dB tilt is straight to "
                   + juce::String (worst, 3) + " dB, at " + juce::String (slope, 3) + " dB/octave");
    }

    // ── the normalisation is where it says it is ────────────────────────────
    for (const auto& c : curves)
    {
        const smt::TargetCurveShape shape (c.curve);
        double sum = 0.0;
        constexpr int n = 65;
        for (int i = 0; i < n; ++i)
        {
            const double freq = smt::targetRefLowHz
                              * std::pow (smt::targetRefHighHz / smt::targetRefLowHz,
                                          (double) i / (double) (n - 1));
            sum += std::pow (10.0, shape.gainDb (freq) / 20.0);
        }
        const double meanDb = 20.0 * std::log10 (sum / (double) n);
        check (std::abs (meanDb) < 0.05,
               juce::String (c.name) + ": 0 dB on average over 200 Hz - 2 kHz ("
                   + juce::String (meanDb, 3) + ")");
    }

    // ── the filter is the curve ─────────────────────────────────────────────
    std::cout << "\n--- the filter against the curve ---\n";
    for (double sr : { 44100.0, 48000.0, 96000.0 })
    {
        double worstAll = 0.0;
        juce::String worstWhere;

        for (const auto& c : curves)
        {
            fxme::BiquadCoeffs bq[smt::maxTargetBiquads];
            const int n = smt::buildTargetCascade (c.curve, sr, bq, smt::maxTargetBiquads);
            const smt::TargetCurveShape shape (c.curve);

            double worst = 0.0;
            for (double freq : f)
                worst = std::max (worst, std::abs (cascadeDb (bq, n, freq, sr)
                                                   - (shape.gainDb (freq) + shape.offsetDb())));

            if (worst > worstAll)
            {
                worstAll = worst;
                worstWhere = c.name;
            }
        }

        check (worstAll < 0.45,
               juce::String ("at ") + juce::String (sr / 1000.0, 1) + " kHz the filter follows "
                   + "the curve to " + juce::String (worstAll, 3) + " dB (worst: " + worstWhere + ")");
    }

    // ── it only ever attenuates ─────────────────────────────────────────────
    std::cout << "\n--- what reaches the output ---\n";
    for (double sr : { 44100.0, 48000.0, 96000.0 })
    {
        double peak = -100.0;

        for (const auto& c : curves)
        {
            fxme::BiquadCoeffs bq[smt::maxTargetBiquads];
            const int n = smt::buildTargetCascade (c.curve, sr, bq, smt::maxTargetBiquads);
            if (n == 0)
                continue;

            for (double freq : f)
                peak = std::max (peak, cascadeDb (bq, n, freq, sr));
        }

        // The definition's own peak sits at 0 dB by construction, so anything
        // above that would be the filter overshooting it.
        check (peak < 0.02,
               juce::String ("at ") + juce::String (sr / 1000.0, 1)
                   + " kHz no curve has gain anywhere in the band (peak "
                   + juce::String (peak, 3) + " dB)");
    }

    // ── a flat curve builds nothing, and every curve fits its array ──────────
    {
        fxme::BiquadCoeffs bq[smt::maxTargetBiquads];
        const smt::TargetCurve flat { 0.0f, 0.0f, 0.0f, 105.0f };
        check (smt::buildTargetCascade (flat, 44100.0, bq, smt::maxTargetBiquads) == 0
                   && flat.isFlat(),
               "a flat target builds no biquads at all");

        bool fits = true;
        for (const auto& c : curves)
            if (! c.curve.isFlat())
            {
                const int n = smt::buildTargetCascade (c.curve, 44100.0, bq, smt::maxTargetBiquads);
                fits = fits && n > 0 && n <= smt::maxTargetBiquads;
            }
        check (fits, juce::String ("every curve fits in ") + juce::String (smt::maxTargetBiquads)
                         + " biquads");
    }

    // ── the values round-trip through the model's comparison ────────────────
    {
        smt::TargetCurve a { -4.0f, 0.0f, 0.0f, 105.0f };
        smt::TargetCurve b = a;
        const bool same = a == b;
        b.bassHz = 106.0f;
        check (same && a != b, "curves compare by value, which is what the engine rebuilds on");
    }

    std::cout << (g_ok ? "\nALL TESTS PASSED\n" : "\nTESTS FAILED\n");
    return g_ok ? 0 : 1;
}
