/*
  ------------------------------------------------------------------------------
    AmbisonicsDecode.h

    Periphonic (3D) Ambisonics decoding for the configuration tool.

    The maths lives in FxmeTools (<FxmeTools/dsp/Ambisonics.h>): the ACN/SN3D
    spherical harmonics, the per-degree max-rE weights and the sampling decoder
    itself are all generic and shared with every other FX-Mechanics plugin. What
    stays here is only the part specific to this project's configuration tool: a
    loudspeaker direction expressed the way the GUI collects it, in degrees.

    Use fxme::ambi directly for anything else — channelsForOrder() for the
    harmonic count, orderOfChannel() for a harmonic's degree, maxREGain() for the
    decoder weights.

    Author: Olivier Doaré, github.com/odoare
    Licenced under the GNU LGPL Version 3.0
    SPDX-License-Identifier: LGPL-3.0-or-later
  ------------------------------------------------------------------------------
*/

#pragma once

#include <vector>

// Included directly rather than through the module umbrella (JuceHeader.h) so
// this header stays JUCE-free, as Ambisonics.h itself is.
#include <FxmeTools/dsp/Ambisonics.h>

namespace smt
{

// One loudspeaker direction as the config tool's GUI holds it: degrees, azimuth
// counter-clockwise with 0 at the front, elevation 0 at the horizon.
struct SpeakerDir
{
    float azimuthDeg;     // counter-clockwise, 0 = front
    float elevationDeg;   // 0 = horizon, +90 = up
};

namespace ambisonics
{
    /** Sampling decode matrix (L speakers x (order+1)^2 harmonics, signed) for
        loudspeakers given in degrees. Thin adapter over
        fxme::ambi::samplingDecodeMatrix — see that function for the decoder's
        definition and its caveat about irregular rigs. */
    inline std::vector<std::vector<float>> decodeMatrix (int order,
                                                         const std::vector<SpeakerDir>& spk)
    {
        constexpr float deg2rad = 3.14159265358979323846f / 180.0f;

        std::vector<fxme::ambi::Vec3> dirs;
        dirs.reserve (spk.size());

        for (const auto& s : spk)
            dirs.push_back (fxme::ambi::directionFromAngles (s.azimuthDeg * deg2rad,
                                                             s.elevationDeg * deg2rad));

        return fxme::ambi::samplingDecodeMatrix (order, dirs);
    }
}

} // namespace smt
