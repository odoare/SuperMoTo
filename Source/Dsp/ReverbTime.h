/*
  ------------------------------------------------------------------------------
    ReverbTime.h

    Reverberation time from an impulse response, by Schroeder backward
    integration and the line fits of ISO 3382: EDT (0 to -10 dB), T20 (-5 to
    -25) and T30 (-5 to -35), each extrapolated to the 60 dB a room almost
    never gives in one measurement.

    The one part that is not a formula is where to stop integrating. Carried
    past the noise floor, the backward integral accumulates noise rather than
    room and the decay curve flattens into a tail that reads as a far longer
    T60 than the room has. The stopping point is found the usual way (Lundeby,
    compacted): smooth the energy into short intervals, estimate the noise from
    the end, fit the part that is still clear of it, take the crossing of the
    two, and iterate. The energy the truncation throws away is added back from
    the fitted slope, so the curve does not bend down into its own end.

    WHAT TO FEED IT. A real impulse response, which is not the same thing as
    an inverse transform of any frequency response lying around:

      - NOT a smoothed one. Fractional-octave smoothing is a convolution in
        frequency, so it is a multiplication in time by a window of about
        1/(bandwidth) -- 1/6 octave is 43 ms at 200 Hz and 4.3 ms at 2 kHz.
        Measured on a synthetic 0.40 s room, the smoothed response reads
        0.28-0.30 s broadband and gives no readable decay at all in any octave
        band. What is left to measure is the smoothing kernel.

      - NOT an average whose magnitude and phase come from different means.
        AnalysisEngine's average takes its magnitude from a power mean and its
        phase from a complex mean (which is right for what it is for), so the
        two do not describe one response and the inverse transform is not an
        impulse. Its energy does not even fall monotonically: on that same
        synthetic room it drops to -38 dB by 0.5 s and is back at -35 dB by
        1.0 s. ISO 3382 averages positions the other way round -- a decay per
        position, then the mean of the times -- and so does the caller.

      - NOT a Welch transfer function, when the excitation is a sweep. The
        segmentation smears the response in time: the same 0.40 s room reads
        0.63-0.66 s that way, and a longer sweep does not help. From a sweep
        deconvolution it reads 0.399.

    Free of JUCE so it can be tested on its own (Tests/ReverbTimeTest.cpp).

    Author: Olivier Doaré, github.com/odoare
    Licenced under the GNU AGPL Version 3.0, or commercial terms (LICENSE.md)
    SPDX-License-Identifier: AGPL-3.0-or-later OR LicenseRef-FXME-Commercial
  ------------------------------------------------------------------------------
*/

#pragma once

#include <algorithm>
#include <cmath>
#include <vector>

namespace smt::reverb
{

/** One position's decay, or as much of it as the measurement supports. The
    times are already extrapolated to 60 dB, so t20, t30 and edt are all
    estimates OF T60 and are directly comparable. */
struct Decay
{
    float edt = 0.0f;               ///< from the first 10 dB, x6
    float t20 = 0.0f;               ///< -5 to -25 dB, x3
    float t30 = 0.0f;               ///< -5 to -35 dB, x2
    bool  hasEdt = false, hasT20 = false, hasT30 = false;

    /** How far the decay curve fell before the noise floor stopped it. Under
        about 25 dB nothing but EDT can be fitted. */
    float usableRangeDb = 0.0f;

    /** Where the integration had to stop, in seconds from the direct sound. */
    float truncationS = 0.0f;

    bool ok() const noexcept        { return hasT20 || hasT30; }

    /** The best-supported estimate: T30 where the measurement reached -35 dB,
        T20 where it only reached -25, and 0 when neither. EDT is deliberately
        not a fallback -- it describes the first 10 dB, which in a small room
        is mostly the direct sound and the first reflections, not the decay. */
    float t60() const noexcept      { return hasT30 ? t30 : (hasT20 ? t20 : 0.0f); }
};

namespace detail
{
    /** Least-squares line through (x, y). Returns false for a flat or rising
        fit, which is not a decay whatever else it is. */
    inline bool fitLine (const float* x, const float* y, int n,
                         double& slope, double& intercept)
    {
        if (n < 4)
            return false;

        double sx = 0.0, sy = 0.0, sxx = 0.0, sxy = 0.0;
        for (int i = 0; i < n; ++i)
        {
            sx += x[i];  sy += y[i];
            sxx += (double) x[i] * x[i];
            sxy += (double) x[i] * y[i];
        }

        const double den = (double) n * sxx - sx * sx;
        if (std::abs (den) < 1.0e-12)
            return false;

        slope = ((double) n * sxy - sx * sy) / den;
        intercept = (sy - slope * sx) / (double) n;
        return slope < 0.0;
    }

    /** First index at which the (monotonically falling) curve has reached
        `level`, or -1 when it never does. */
    inline int firstAtOrBelow (const std::vector<float>& db, float level)
    {
        for (int i = 0; i < (int) db.size(); ++i)
            if (db[(size_t) i] <= level)
                return i;
        return -1;
    }

    /** One ISO 3382 fit between two levels on the decay curve, extrapolated to
        60 dB. Returns 0 when the curve never reached `to`. */
    inline float fitDecayTime (const std::vector<float>& db, double sampleRate,
                               float from, float to)
    {
        const int i0 = firstAtOrBelow (db, from);
        const int i1 = firstAtOrBelow (db, to);
        if (i0 < 0 || i1 <= i0)
            return 0.0f;

        const int n = i1 - i0;
        std::vector<float> t ((size_t) n), y ((size_t) n);
        for (int i = 0; i < n; ++i)
        {
            t[(size_t) i] = (float) ((double) (i0 + i) / sampleRate);
            y[(size_t) i] = db[(size_t) (i0 + i)];
        }

        double slope = 0.0, intercept = 0.0;
        if (! fitLine (t.data(), y.data(), n, slope, intercept))
            return 0.0f;

        return (float) (-60.0 / slope);
    }
}

/** The decay of one impulse response. `ir` may hold anything before the direct
    sound (an inverse transform wraps the pre-response to the end of the
    buffer); the search starts at the largest sample and works forward, so what
    sits before it is ignored rather than integrated. */
inline Decay estimateDecay (const float* ir, int numSamples, double sampleRate)
{
    Decay d;
    if (ir == nullptr || numSamples <= 0 || sampleRate <= 0.0)
        return d;

    // ── The direct sound, and the energy after it ────────────────────────────
    int peak = 0;
    float peakAbs = 0.0f;
    for (int i = 0; i < numSamples; ++i)
        if (const float a = std::abs (ir[i]); a > peakAbs)
        {
            peakAbs = a;
            peak = i;
        }

    const int n = numSamples - peak;
    if (peakAbs <= 0.0f || (double) n / sampleRate < 0.1)
        return d;                       // under 100 ms of response: nothing to fit

    std::vector<double> energy ((size_t) n);
    for (int i = 0; i < n; ++i)
        energy[(size_t) i] = (double) ir[peak + i] * ir[peak + i];

    // ── Where the room stops and the noise floor starts ──────────────────────
    // A short-interval mean first, so the crossing is found on a smooth curve
    // rather than on the sample-to-sample scatter of a decaying noise tail.
    const int interval = std::max (1, (int) (0.01 * sampleRate));       // 10 ms
    const int m = n / interval;
    if (m < 10)
        return d;

    std::vector<float> env ((size_t) m), envT ((size_t) m);
    for (int j = 0; j < m; ++j)
    {
        double s = 0.0;
        for (int i = 0; i < interval; ++i)
            s += energy[(size_t) (j * interval + i)];
        env[(size_t) j] = (float) (s / (double) interval);
        envT[(size_t) j] = (float) (((double) j + 0.5) * interval / sampleRate);
    }

    const double ref = std::max (1.0e-300, (double) env[0]);
    auto toDb = [ref] (double v) { return (float) (10.0 * std::log10 (std::max (1.0e-300, v) / ref)); };

    double noise = 0.0;
    {                                   // first guess: the last tenth
        const int from = std::max (1, (int) (0.9 * m));
        for (int j = from; j < m; ++j)
            noise += env[(size_t) j];
        noise /= (double) std::max (1, m - from);
    }

    double slope = 0.0, intercept = 0.0, crossS = 0.0;
    bool haveCross = false;

    std::vector<float> envDb ((size_t) m);
    for (int pass = 0; pass < 5; ++pass)
    {
        for (int j = 0; j < m; ++j)
            envDb[(size_t) j] = toDb (env[(size_t) j]);

        const float noiseDb = toDb (noise);

        // The leading stretch that is still clear of the noise by 10 dB. Only
        // the leading one: once the curve has reached the floor, later
        // intervals that happen to poke above it are floor, not room.
        int last = 1;
        while (last < m && envDb[(size_t) last] > noiseDb + 10.0f)
            ++last;

        if (last < 5)
            break;                      // nothing clear enough of the floor to fit

        if (! detail::fitLine (envT.data(), envDb.data(), last, slope, intercept))
            break;

        crossS = ((double) noiseDb - intercept) / slope;
        haveCross = crossS > 0.0;
        if (! haveCross)
            break;

        // Re-estimate the floor from beyond the crossing and go round again.
        const int from = (int) (crossS * 1.1 * sampleRate / interval);
        if (from >= m - 2)
            break;

        double s = 0.0;
        for (int j = from; j < m; ++j)
            s += env[(size_t) j];
        noise = s / (double) (m - from);
    }

    const int stop = haveCross
                       ? std::min (n, std::max (10 * interval, (int) (crossS * sampleRate)))
                       : n;

    // ── Schroeder backward integration ───────────────────────────────────────
    // Plus Lundeby's compensation: the energy beyond the truncation, taken
    // from the fitted slope. Without it the curve bends down into its own end
    // and every fit that reaches near it comes out short.
    double tail = 0.0;
    if (haveCross && slope < 0.0)
    {
        const double decayPerSecond = -slope / 10.0 * std::log (10.0);
        const double energyAtStop = std::pow (10.0, (intercept + slope * (double) stop / sampleRate) / 10.0) * ref;
        if (decayPerSecond > 0.0 && std::isfinite (energyAtStop))
            tail = energyAtStop / decayPerSecond * sampleRate;   // in sample-energy units
    }

    std::vector<float> db ((size_t) stop);
    {
        double sum = tail;
        for (int i = stop - 1; i >= 0; --i)
        {
            sum += energy[(size_t) i];
            db[(size_t) i] = (float) sum;           // energy for now, dB below
        }

        const double total = std::max (1.0e-300, (double) db[0]);
        for (int i = 0; i < stop; ++i)
            db[(size_t) i] = (float) (10.0 * std::log10 (std::max (1.0e-300, (double) db[(size_t) i]) / total));
    }

    d.truncationS = (float) ((double) stop / sampleRate);
    d.usableRangeDb = -db[(size_t) (stop - 1)];

    d.edt = detail::fitDecayTime (db, sampleRate,  0.0f, -10.0f);
    d.t20 = detail::fitDecayTime (db, sampleRate, -5.0f, -25.0f);
    d.t30 = detail::fitDecayTime (db, sampleRate, -5.0f, -35.0f);
    d.hasEdt = d.edt > 0.0f;
    d.hasT20 = d.t20 > 0.0f;
    d.hasT30 = d.t30 > 0.0f;
    return d;
}

} // namespace smt::reverb
