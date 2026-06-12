/*
  ------------------------------------------------------------------------------
    SpectrumTap.h

    Lock-free single-writer/single-reader signal taps feeding the global
    spectrum analyzer. The audio thread pushes samples into a ring buffer;
    the GUI thread snapshots the most recent fftSize samples and performs
    the FFT itself (windowing + averaging happen on the GUI side).

    Tap layout (fixed):
      0 .. numChannels-1                : output sums (post matrix + FIR)
      numChannels .. numChannels+maxFrameTaps-1 : selected matrix frames

    Author: Olivier Doaré, github.com/odoare
    Licenced under the GNU LGPL Version 3.0
    SPDX-License-Identifier: LGPL-3.0-or-later
  ------------------------------------------------------------------------------
*/

#pragma once

#include <JuceHeader.h>
#include <array>
#include <atomic>
#include "../Model/ConfigModel.h"

namespace smt
{

constexpr int spectrumFftOrder = 12;
constexpr int spectrumFftSize  = 1 << spectrumFftOrder;     // 4096
constexpr int numSpectrumTaps  = numChannels + maxFrameTaps;

class SpectrumTap
{
public:
    SpectrumTap()
    {
        buffer.fill (0.0f);
    }

    void setEnabled (bool e) noexcept       { enabled.store (e); }
    bool isEnabled() const noexcept         { return enabled.load(); }

    void push (const float* data, int n) noexcept
    {
        if (! enabled.load())
            return;

        int w = writePos.load (std::memory_order_relaxed);
        for (int i = 0; i < n; ++i)
        {
            buffer[(size_t) w] = data[i];
            if (++w >= size)
                w = 0;
        }
        writePos.store (w, std::memory_order_release);
    }

    /** Copies the most recent fftSize samples in chronological order. */
    void snapshot (float* dest) const noexcept
    {
        int w = writePos.load (std::memory_order_acquire);
        int r = w - spectrumFftSize;
        if (r < 0)
            r += size;
        for (int i = 0; i < spectrumFftSize; ++i)
        {
            dest[i] = buffer[(size_t) r];
            if (++r >= size)
                r = 0;
        }
    }

private:
    static constexpr int size = 2 * spectrumFftSize;
    std::array<float, (size_t) size> buffer;
    std::atomic<int> writePos { 0 };
    std::atomic<bool> enabled { false };
};

//==============================================================================
/** The set of all analyzer taps plus the frame-tap routing table. */
class SpectrumBus
{
public:
    SpectrumTap& outputTap (int o)              { return taps[(size_t) o]; }
    SpectrumTap& frameTap (int slot)            { return taps[(size_t) (numChannels + slot)]; }
    SpectrumTap& tap (int index)                { return taps[(size_t) index]; }

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
    std::array<SpectrumTap, (size_t) numSpectrumTaps> taps;
};

} // namespace smt
