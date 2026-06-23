/*
  ------------------------------------------------------------------------------
    SpectrumBus.h

    The set of all analyzer taps plus the frame-tap routing table. This is the
    SuperMoTo-specific layout on top of the generic fxme::SpectrumTap ring
    buffer (which lives in FxmeTools):

      0 .. numChannels-1                        : output sums (post matrix + FIR)
      numChannels .. numChannels+maxFrameTaps-1 : selected matrix frames

    Author: Olivier Doaré, github.com/odoare
    Licenced under the GNU LGPL Version 3.0
    SPDX-License-Identifier: LGPL-3.0-or-later
  ------------------------------------------------------------------------------
*/

#pragma once

#include <JuceHeader.h>
#include <FxmeTools/dsp/SpectrumTap.h>
#include <array>
#include <atomic>
#include "../Model/ConfigModel.h"

namespace smt
{

constexpr int numSpectrumTaps = numChannels + maxFrameTaps;

/** The set of all analyzer taps plus the frame-tap routing table. */
class SpectrumBus
{
public:
    fxme::SpectrumTap& outputTap (int o)        { return taps[(size_t) o]; }
    fxme::SpectrumTap& frameTap (int slot)      { return taps[(size_t) (numChannels + slot)]; }
    fxme::SpectrumTap& tap (int index)          { return taps[(size_t) index]; }

    /** Frame-slot routing, set from the model on the audio thread. */
    struct FrameRoute { std::atomic<int> config { -1 }, in { -1 }, out { -1 }; };
    FrameRoute frameRoutes[maxFrameTaps];

    void setFrameRoute (int slot, int config, int in, int out)
    {
        frameRoutes[slot].config.store (config);
        frameRoutes[slot].in.store (in);
        frameRoutes[slot].out.store (out);
        frameTap (slot).setEnabled (config >= 0);
    }

    int findFrameSlot (int config, int in, int out) const
    {
        for (int s = 0; s < maxFrameTaps; ++s)
            if (frameRoutes[s].config.load() == config
                && frameRoutes[s].in.load() == in
                && frameRoutes[s].out.load() == out)
                return s;
        return -1;
    }

private:
    std::array<fxme::SpectrumTap, (size_t) numSpectrumTaps> taps;
};

} // namespace smt
