/*
  ------------------------------------------------------------------------------
    ReverbTimeTest.cpp

    The decay estimator, against rooms whose T60 is known because they were
    built with it: exponentially decaying gaussian noise, which is the
    textbook diffuse tail and decays at exactly the rate it was given.

    What is actually being pinned here is not the arithmetic of a line fit but
    the two things that make a decay estimate wrong in practice — integrating
    into the noise floor, which reads far too long, and truncating without
    compensation, which reads short — plus the refusals: a measurement that
    never falls far enough should say so rather than extrapolate from nothing.

    No JUCE: ReverbTime.h has none, and this is the whole reason.

    Run: build target SuperMoToReverbTests and execute it; exits 0 on success.

    Author: Olivier Doaré, github.com/odoare
    (c) 2023-2026 Olivier Doaré
    Licenced under the GNU AGPL Version 3.0, or commercial terms (LICENSE.md)
    SPDX-License-Identifier: AGPL-3.0-or-later OR LicenseRef-FXME-Commercial
  ------------------------------------------------------------------------------
*/

#include "../Source/Dsp/ReverbTime.h"

#include <cmath>
#include <cstdio>
#include <iomanip>
#include <iostream>
#include <random>
#include <string>
#include <vector>

static bool g_ok = true;

static bool check (bool cond, const std::string& what)
{
    std::cout << (cond ? "  OK   " : "  FAIL ") << what << "\n";
    g_ok = g_ok && cond;
    return cond;
}

/** A room with the decay it is told to have: gaussian noise under a -60 dB /
    t60 envelope, a direct sound on top, and a noise floor under everything. */
static std::vector<float> makeRoom (double t60, int n, double sr,
                                    double noiseDb = -70.0, unsigned seed = 1)
{
    std::mt19937 rng (seed);
    std::normal_distribution<float> gauss (0.0f, 1.0f);

    std::vector<float> h ((size_t) n);
    const float noise = (float) std::pow (10.0, noiseDb / 20.0);

    for (int i = 0; i < n; ++i)
    {
        const double t = (double) i / sr;
        const float env = (float) std::pow (10.0, -3.0 * t / t60);
        h[(size_t) i] = gauss (rng) * env + gauss (rng) * noise;
    }

    h[0] += 4.0f;               // the direct sound
    return h;
}

static std::string secs (float v)
{
    char buf[32];
    std::snprintf (buf, sizeof (buf), "%.3f", (double) v);
    return buf;
}

int main()
{
    constexpr double sr = 48000.0;
    const int n = 65536;        // the analysis window, 1.37 s at 48 kHz

    std::cout << "\n== Rooms whose decay is known ==\n";
    {
        // The tolerance is 5 %: the estimator is fitting a line to one
        // realisation of a random tail, and a different seed moves the answer
        // by about a percent. Measured, these land within 2 %.
        for (const double t60 : { 0.25, 0.40, 0.60, 0.90 })
        {
            const auto h = makeRoom (t60, n, sr);
            const auto d = smt::reverb::estimateDecay (h.data(), n, sr);
            const float got = d.t60();
            const bool close = d.ok() && std::abs (got - (float) t60) < 0.05f * (float) t60;
            check (close, "T60 " + secs ((float) t60) + " s reads " + secs (got)
                              + " s (range " + secs (d.usableRangeDb) + " dB)");
        }
    }

    std::cout << "\n== The noise floor is not part of the room ==\n";
    {
        // A floor high enough to swallow the end of the decay. Integrated
        // blindly it would read far too long; truncated at the crossing it
        // should still read the room.
        const auto h = makeRoom (0.40, n, sr, -35.0, 7);
        const auto d = smt::reverb::estimateDecay (h.data(), n, sr);
        check (d.ok() && std::abs (d.t60() - 0.40f) < 0.06f,
               "a -35 dB floor still reads " + secs (d.t60()) + " s, not the floor's own tail");
        check (d.truncationS > 0.0f && d.truncationS < (float) ((double) n / sr),
               "integration stopped early, at " + secs (d.truncationS) + " s");
    }

    std::cout << "\n== What it refuses ==\n";
    {
        std::vector<float> silence ((size_t) n, 0.0f);
        check (! smt::reverb::estimateDecay (silence.data(), n, sr).ok(),
               "silence has no decay time");

        std::vector<float> click ((size_t) n, 0.0f);
        click[100] = 1.0f;
        check (! smt::reverb::estimateDecay (click.data(), n, sr).ok(),
               "a bare impulse has no decay time");

        const auto h = makeRoom (0.40, n, sr);
        check (! smt::reverb::estimateDecay (h.data(), 2000, sr).ok(),
               "40 ms of response is too short to fit");

        check (! smt::reverb::estimateDecay (nullptr, n, sr).ok()
               && ! smt::reverb::estimateDecay (h.data(), n, 0.0).ok(),
               "null data and a zero sample rate are refused");
    }

    std::cout << "\n== T20, T30 and EDT agree on a pure exponential ==\n";
    {
        // They measure different parts of the same straight line, so on a
        // synthetic room they must agree. On a real one they do not, which is
        // the whole reason ISO 3382 reports more than one.
        const auto h = makeRoom (0.50, n, sr, -80.0, 3);
        const auto d = smt::reverb::estimateDecay (h.data(), n, sr);
        check (d.hasT20 && d.hasT30 && std::abs (d.t20 - d.t30) < 0.03f,
               "T20 " + secs (d.t20) + " s and T30 " + secs (d.t30) + " s agree");
        check (d.hasEdt && std::abs (d.edt - 0.50f) < 0.06f,
               "EDT " + secs (d.edt) + " s matches too, the decay being one slope");
    }

    std::cout << "\n== The pre-response before the peak is ignored ==\n";
    {
        // An inverse transform wraps what precedes the direct sound to the end
        // of the buffer; rotating the room so its peak sits late must not
        // change the answer.
        const auto h = makeRoom (0.40, n, sr, -70.0, 11);
        const auto straight = smt::reverb::estimateDecay (h.data(), n, sr);

        std::vector<float> rotated ((size_t) n);
        const int shift = 500;
        for (int i = 0; i < n; ++i)
            rotated[(size_t) ((i + shift) % n)] = h[(size_t) i];

        const auto turned = smt::reverb::estimateDecay (rotated.data(), n, sr);
        check (turned.ok() && std::abs (turned.t60() - straight.t60()) < 0.02f,
               "peak at sample " + std::to_string (shift) + " reads "
                   + secs (turned.t60()) + " s against " + secs (straight.t60()));
    }

    std::cout << (g_ok ? "\nALL TESTS PASSED\n" : "\nTESTS FAILED\n");
    return g_ok ? 0 : 1;
}
