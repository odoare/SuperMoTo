/*
  ------------------------------------------------------------------------------
    AmbisonicsDecode.h

    Periphonic (3D) Ambisonics decoding for the configuration tool. Builds the
    decode matrix that maps a B-format input (ACN channel order, SN3D
    normalisation — the AmbiX convention) onto an arbitrary set of loudspeaker
    directions, using a sampling (projection) decoder with max-rE weighting.

    The decoder is the "basic + max-rE" sampling decoder
        D[s][j] = (1/L) * a_{n(j)} * Y_j(Omega_s),
    with Y_j the real SN3D spherical harmonic of ACN index j evaluated at the
    direction of speaker s, a_n the per-order max-rE gains, and L the number of
    speakers. The whole matrix is then scaled so an on-axis plane wave decodes
    to about unity at the nearest speaker. Coefficients are signed; the caller
    turns them into a gain magnitude plus a polarity flip.

    Orders 1..3 are supported (4 / 9 / 16 harmonics).

    Author: Olivier Doaré, github.com/odoare
    Licenced under the GNU LGPL Version 3.0
    SPDX-License-Identifier: LGPL-3.0-or-later
  ------------------------------------------------------------------------------
*/

#pragma once

#include <algorithm>
#include <cmath>
#include <vector>

namespace smt
{

struct SpeakerDir
{
    float azimuthDeg;     // counter-clockwise, 0 = front
    float elevationDeg;   // 0 = horizon, +90 = up
};

namespace ambisonics
{
    inline int numHarmonics (int order) noexcept { return (order + 1) * (order + 1); }

    // ACN degree n for a flat harmonic index j (j = n^2 + n + m).
    inline int degreeForAcn (int j) noexcept { return (int) std::floor (std::sqrt ((double) j)); }

    // Real SN3D spherical harmonics, ACN ordered, evaluated from the unit
    // direction vector (x,y,z). Indices 0..15 cover orders 0..3.
    inline double sn3d (int acn, double x, double y, double z) noexcept
    {
        switch (acn)
        {
            case 0:  return 1.0;                                 // (0, 0)
            // order 1
            case 1:  return y;                                   // (1,-1)
            case 2:  return z;                                   // (1, 0)
            case 3:  return x;                                   // (1, 1)
            // order 2
            case 4:  return std::sqrt (3.0) * x * y;             // (2,-2)
            case 5:  return std::sqrt (3.0) * y * z;             // (2,-1)
            case 6:  return 0.5 * (3.0 * z * z - 1.0);           // (2, 0)
            case 7:  return std::sqrt (3.0) * x * z;             // (2, 1)
            case 8:  return 0.5 * std::sqrt (3.0) * (x * x - y * y); // (2, 2)
            // order 3
            case 9:  return std::sqrt (5.0 / 8.0) * y * (3.0 * x * x - y * y);  // (3,-3)
            case 10: return std::sqrt (15.0) * x * y * z;                       // (3,-2)
            case 11: return std::sqrt (3.0 / 8.0) * y * (5.0 * z * z - 1.0);    // (3,-1)
            case 12: return 0.5 * z * (5.0 * z * z - 3.0);                      // (3, 0)
            case 13: return std::sqrt (3.0 / 8.0) * x * (5.0 * z * z - 1.0);    // (3, 1)
            case 14: return 0.5 * std::sqrt (15.0) * z * (x * x - y * y);       // (3, 2)
            case 15: return std::sqrt (5.0 / 8.0) * x * (x * x - 3.0 * y * y);  // (3, 3)
            default: return 0.0;
        }
    }

    // Per-order max-rE gains (normalised so a_0 = 1), from the largest root of
    // the Legendre polynomial P_{order+1}. Index by ACN degree n.
    inline double maxRe (int order, int n) noexcept
    {
        static const double o1[] = { 1.0, 0.577350 };
        static const double o2[] = { 1.0, 0.774597, 0.400000 };
        static const double o3[] = { 1.0, 0.861136, 0.612334, 0.304657 };
        switch (order)
        {
            case 1:  return n < 2 ? o1[n] : 0.0;
            case 2:  return n < 3 ? o2[n] : 0.0;
            case 3:  return n < 4 ? o3[n] : 0.0;
            default: return 1.0;
        }
    }

    // Builds the decode matrix D (row-major, L speakers x H harmonics).
    inline std::vector<std::vector<double>> decodeMatrix (int order,
                                                          const std::vector<SpeakerDir>& spk)
    {
        const int H = numHarmonics (order);
        const int L = (int) spk.size();
        std::vector<std::vector<double>> D ((size_t) L, std::vector<double> ((size_t) H, 0.0));
        if (L == 0)
            return D;

        constexpr double deg2rad = 3.14159265358979323846 / 180.0;

        // Raw sampling decoder, and the on-axis gain of each speaker for a plane
        // wave from its own direction (used to normalise the level).
        double peak = 0.0;
        for (int s = 0; s < L; ++s)
        {
            const double az = spk[(size_t) s].azimuthDeg * deg2rad;
            const double el = spk[(size_t) s].elevationDeg * deg2rad;
            const double x = std::cos (el) * std::cos (az);
            const double y = std::cos (el) * std::sin (az);
            const double z = std::sin (el);

            double onAxis = 0.0;
            for (int j = 0; j < H; ++j)
            {
                const double Y = sn3d (j, x, y, z);
                const double a = maxRe (order, degreeForAcn (j));
                const double d = a * Y / (double) L;
                D[(size_t) s][(size_t) j] = d;
                onAxis += d * Y;
            }
            peak = std::max (peak, onAxis);
        }

        if (peak > 1.0e-12)
            for (auto& row : D)
                for (auto& d : row)
                    d /= peak;

        return D;
    }
}

} // namespace smt
