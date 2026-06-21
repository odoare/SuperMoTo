/*
  ------------------------------------------------------------------------------

    ConfigModel.h

    Data model for the SuperMoTo monitoring matrix.

    The plugin holds 6 matrix configurations (A..F). Each configuration is a
    full 16x16 routing matrix where every frame (crosspoint) carries its own
    gain, IIR filter, phase and delay settings. Output settings (FIR speaker
    correction, trim) describe the physical speaker attached to an output and
    are therefore global, shared by all configurations.

    Frame/output settings are deliberately NOT host-automatable parameters
    (6 x 256 x ~10 fields would swamp any host); they live here and are
    serialized with the plugin state. Host parameters (config buttons A..F,
    exclusive mode, master level, mute/dim/mono) live in the APVTS.

    Threading: setters are called from the message thread. The audio engine
    copies settings when the version counter changes, under a short lock.

    Author: Olivier Doaré, github.com/odoare
    Licenced under the GNU LGPL Version 3.0
    SPDX-License-Identifier: LGPL-3.0-or-later

  ------------------------------------------------------------------------------
*/

#pragma once

#include <JuceHeader.h>
#include <array>
#include <atomic>

namespace smt
{

constexpr int numChannels = 32;   // maximum inputs and outputs (matrix dimension)
constexpr int defaultChannels = 8; // active in/out count on a fresh instance
constexpr int numConfigs  = 6;    // A..F
constexpr float maxDelayMs = 100.0f;
constexpr int maxFrameTaps = 8;   // simultaneous frame traces on the analyzer

// What the measurement / SPL test signal exercises:
//   dryOutput  – stimulus straight to an output (raw speaker, to design FIRs)
//   outputFir  – stimulus to an output through its trim + FIR (verify the FIR)
//   fullSystem – stimulus into a plugin INPUT, through the whole engine
//                (matrix, crossover, FIRs, delay compensation) — the complete
//                system as heard. Channel selection means inputs in this mode.
enum class MeasureMode : int { dryOutput = 0, outputFir = 1, fullSystem = 2 };

inline juce::String configName (int c)      { return juce::String::charToString ((juce::juce_wchar) ('A' + c)); }

//==============================================================================
enum class FilterType : int { lowpass = 0, highpass = 1, bandpass = 2, peaking = 3 };

constexpr int numFrameBands = 4;        // EQ bands per matrix frame

// One EQ band of a frame's filter chain (the bands are cascaded in series).
struct FrameBand
{
    bool  on     = false;
    int   type   = (int) FilterType::peaking;
    int   order  = 2;             // 2 or 4 (ignored for peaking)
    float freq   = 1000.0f;       // Hz
    float q      = 0.707f;
    float gainDb = 0.0f;          // peaking only

    bool operator== (const FrameBand& o) const
    {
        return on == o.on && type == o.type && order == o.order
            && freq == o.freq && q == o.q && gainDb == o.gainDb;
    }
    bool operator!= (const FrameBand& o) const { return ! (*this == o); }
};

// A matrix frame is just a routing cell now: how much of the input reaches the
// output, with an optional polarity flip and an analyzer tap. The speaker
// processing (EQ, delay, FIR, trim) lives on the output.
struct FrameSettings
{
    bool  active      = false;
    float gainDb      = 0.0f;     // overall frame level
    bool  phaseInvert = false;
    bool  spectrum    = false;    // show this frame's signal on the analyzer

    bool isDefault() const
    {
        const FrameSettings d;
        return active == d.active && gainDb == d.gainDb && phaseInvert == d.phaseInvert
            && spectrum == d.spectrum;
    }
};

// One physical speaker output: trim, a 4-band EQ (e.g. the bass-management
// crossover), a time-alignment delay, an FIR correction, and an analyzer tap.
struct OutputSettings
{
    float        gainDb   = 0.0f;
    float        delayMs  = 0.0f;  // time-alignment delay
    bool         firOn    = false;
    juce::String firPath;          // impulse response wav file
    bool         spectrum = false; // show this output's sum on the analyzer
    std::array<FrameBand, (size_t) numFrameBands> bands {};

    bool anyBandOn() const
    {
        for (const auto& b : bands)
            if (b.on)
                return true;
        return false;
    }

    bool isDefault() const
    {
        const OutputSettings d;
        return gainDb == 0.0f && delayMs == 0.0f && ! firOn && firPath.isEmpty()
            && ! spectrum && bands == d.bands;
    }
};

//==============================================================================
class ConfigModel
{
public:
    ConfigModel() = default;

    //==========================================================================
    // Listener interface for GUI refresh (message thread only).
    struct Listener
    {
        virtual ~Listener() = default;
        virtual void modelChanged() = 0;
    };

    void addListener (Listener* l)      { listeners.add (l); }
    void removeListener (Listener* l)   { listeners.remove (l); }

    //==========================================================================
    FrameSettings getFrame (int config, int in, int out) const
    {
        const juce::SpinLock::ScopedLockType sl (lock);
        return frames[(size_t) config][(size_t) in][(size_t) out];
    }

    void setFrame (int config, int in, int out, const FrameSettings& s)
    {
        {
            const juce::SpinLock::ScopedLockType sl (lock);
            frames[(size_t) config][(size_t) in][(size_t) out] = s;
        }
        bumpAndNotify();
    }

    //==========================================================================
    // Visible/processed matrix size. Global like the output settings: it
    // describes the physical installation. Hidden frames keep their settings.
    int getNumIns() const noexcept      { return numIns.load(); }
    int getNumOuts() const noexcept     { return numOuts.load(); }

    void setMatrixSize (int ins, int outs)
    {
        ins  = juce::jlimit (1, numChannels, ins);
        outs = juce::jlimit (1, numChannels, outs);
        if (ins == numIns.load() && outs == numOuts.load())
            return;
        numIns.store (ins);
        numOuts.store (outs);
        bumpAndNotify();
    }

    //==========================================================================
    OutputSettings getOutput (int out) const
    {
        const juce::SpinLock::ScopedLockType sl (lock);
        return outputs[(size_t) out];
    }

    void setOutput (int out, const OutputSettings& s)
    {
        {
            const juce::SpinLock::ScopedLockType sl (lock);
            outputs[(size_t) out] = s;
        }
        bumpAndNotify();
    }

    void clearConfig (int config)
    {
        {
            const juce::SpinLock::ScopedLockType sl (lock);
            for (auto& row : frames[(size_t) config])
                for (auto& f : row)
                    f = FrameSettings();
        }
        bumpAndNotify();
    }

    void copyConfig (int from, int to)
    {
        {
            const juce::SpinLock::ScopedLockType sl (lock);
            frames[(size_t) to] = frames[(size_t) from];
        }
        bumpAndNotify();
    }

    /** Copy of a whole configuration matrix, used by the engine. */
    void copyAll (std::array<std::array<std::array<FrameSettings, numChannels>, numChannels>, numConfigs>& dstFrames,
                  std::array<OutputSettings, numChannels>& dstOutputs) const
    {
        const juce::SpinLock::ScopedLockType sl (lock);
        dstFrames = frames;
        dstOutputs = outputs;
    }

    int getVersion() const noexcept     { return version.load(); }

    //==========================================================================
    // Frame spectrum taps: the analyzer shows at most maxFrameTaps frame
    // traces. Returns the list of {config, in, out} of checked frames in
    // scan order, capped to maxFrameTaps.
    struct FrameRef { int config = -1, in = -1, out = -1;
                      bool isValid() const { return config >= 0; } };

    std::vector<FrameRef> getSpectrumFrames() const
    {
        const juce::SpinLock::ScopedLockType sl (lock);
        std::vector<FrameRef> refs;
        for (int c = 0; c < numConfigs; ++c)
            for (int i = 0; i < numIns.load(); ++i)
                for (int o = 0; o < numOuts.load(); ++o)
                    if (frames[(size_t) c][(size_t) i][(size_t) o].spectrum
                        && (int) refs.size() < maxFrameTaps)
                        refs.push_back ({ c, i, o });
        return refs;
    }

    //==========================================================================
    // Serialization (message thread).
    juce::ValueTree toValueTree() const;
    void restoreFromValueTree (const juce::ValueTree& tree);

private:
    void bumpAndNotify()
    {
        version.fetch_add (1);
        listeners.call ([] (Listener& l) { l.modelChanged(); });
    }

    mutable juce::SpinLock lock;
    std::array<std::array<std::array<FrameSettings, numChannels>, numChannels>, numConfigs> frames {};
    std::array<OutputSettings, numChannels> outputs {};
    std::atomic<int> numIns { defaultChannels }, numOuts { defaultChannels };
    std::atomic<int> version { 1 };

    juce::ListenerList<Listener> listeners;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ConfigModel)
};

} // namespace smt
