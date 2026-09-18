/*
  ------------------------------------------------------------------------------
    FractionalDelay.h

    Sub-sample interpolation for a delay line.

    NOT CURRENTLY WIRED IN. OutputProcessor rounds its time-alignment delay to
    a whole number of samples instead, because half a sample is 11 us at
    44.1 kHz, or 4 mm of path, and because a relative error of one sample
    between two outputs puts its first cancellation notch at fs/2 by
    construction and so can never comb inside the audio band. Rounding also
    makes a delay a pure sample shift, which is bit-exact, which means two
    configurations differing only in a filter differ only in that filter. That
    was worth more than the resolution. Kept here, and kept under test by
    Tests/FractionalDelayTest.cpp, in case a rig ever needs the sub-sample
    resolution back: a line array or a wavefield system, where relative
    distances matter to a few millimetres. To re-engage it, include this
    header from OutputProcessor.h and restore the fractional branch of
    updateDelaySamples() and process() (see git history).

    A delay of a whole number of samples is a delay-line read. A delay that
    lands between two samples has to interpolate, and the obvious two-tap
    linear interpolation is a low-pass: its response is |(1-a) + a e^{-jw}|,
    which for half a sample costs 1.5 dB at 8 kHz and 7.6 dB at 16 kHz at a
    44.1 kHz sample rate. Two outputs whose delays happen to have different
    fractional parts then differ from each other by that much in the treble,
    which is a real error in a controller whose job is aligning loudspeakers,
    and it is invisible in the delay read-out. It was found by measurement:
    two states of one rig that should have differed only in a filter differed
    by a systematic 0.5 dB RMS over 1-8 kHz, because one of them happened to
    bypass the delay line and the other ran 1284.63 samples through it.

    A windowed-sinc interpolator does not colour anything. Measured against
    the ideal fractional delay over every fraction, 32 taps of a
    Blackman-windowed sinc hold the magnitude within 0.002 dB to 16 kHz and
    the delay itself within 0.002 samples, against 7.6 dB and 0.4 samples for
    the two-tap. Shorter kernels are cheaper and worse: 16 taps give 0.04 dB
    to 16 kHz, 8 taps 1.7 dB, 4 taps 5.4 dB.

    The cost is a 32-tap dot product per sample on outputs whose delay is not
    a whole number of samples; outputs on a whole sample keep the single read.
    Measured, that is 13.5 ns a sample against 1.1 ns for the plain read, so
    about 2 % of one core for a full 32-output matrix at 48 kHz against 0.2 %.
    Real, but small: it was not the reason for rounding instead.

    The kernels are built once, for 256 sub-sample positions, so the audio
    thread only indexes a table. Quantizing the fraction to 1/256 of a sample
    is 0.09 us at 44.1 kHz, which is three orders of magnitude below the
    ~10 us at which an interaural time difference starts to shift an image.

    Author: Olivier Doaré, github.com/odoare
    Licenced under the GNU AGPL Version 3.0, or commercial terms (LICENSE.md)
    SPDX-License-Identifier: AGPL-3.0-or-later OR LicenseRef-FXME-Commercial
  ------------------------------------------------------------------------------
*/

#pragma once

#include <array>
#include <cmath>
#include <cstddef>

namespace smt::fracdelay
{

inline constexpr int taps   = 32;            // kernel length
inline constexpr int centre = taps / 2 - 1;  // tap the integer part of the delay lands on
inline constexpr int phases = 256;           // sub-sample positions the table holds

/** The interpolation table: `phases` kernels of `taps` coefficients, kernel p
    realising a delay of p/phases of a sample on top of the integer part. */
inline const std::array<float, (std::size_t) phases * taps>& table()
{
    static const auto built = []
    {
        constexpr double pi = 3.14159265358979323846;
        std::array<float, (std::size_t) phases * taps> t {};

        for (int p = 0; p < phases; ++p)
        {
            const double frac = (double) p / (double) phases;
            double h[taps], sum = 0.0;

            for (int k = 0; k < taps; ++k)
            {
                const double x = (double) (k - centre) - frac;
                const double s = std::abs (x) < 1.0e-12 ? 1.0
                                                        : std::sin (pi * x) / (pi * x);

                // Blackman over the interior of taps + 2 points, so that
                // neither end coefficient is windowed away to nothing.
                const double u = 2.0 * pi * (double) (k + 1) / (double) (taps + 1);
                const double w = 0.42 - 0.5 * std::cos (u) + 0.08 * std::cos (2.0 * u);

                h[k] = s * w;
                sum += h[k];
            }

            // Normalised, so the gain at DC is exactly one whatever the window
            // does to the tails.
            for (int k = 0; k < taps; ++k)
                t[(std::size_t) (p * taps + k)] = (float) (h[k] / sum);
        }
        return t;
    }();

    return built;
}

/** Kernel for sub-sample position `phase`, in [0, phases). Coefficient k
    multiplies the sample delayed by (integer part) + k - centre. */
inline const float* kernel (int phase) noexcept
{
    return table().data() + (std::size_t) phase * taps;
}

/** Build the table now rather than on whoever asks first. Call it from
    prepare(), so the cost is not paid on an audio callback. */
inline void prime()
{
    (void) table();
}

} // namespace smt::fracdelay
