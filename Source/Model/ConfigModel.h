/*
  ------------------------------------------------------------------------------

    ConfigModel.h

    Data model for the SuperMoTo monitoring matrix.

    The plugin holds 6 matrix configurations (A..F). Each configuration is a
    full routing matrix (up to 32x32) where every frame (crosspoint) carries
    its own gain, phase and a small 2-band EQ. Output settings (trim, its own
    2-band EQ, time-alignment delay, FIR speaker correction) describe the
    physical speaker attached to an output and are therefore global, shared by
    all configurations.

    Frame/output settings are deliberately NOT host-automatable parameters
    (6 x 256 x ~10 fields would swamp any host); they live here and are
    serialized with the plugin state. Host parameters (config buttons A..F,
    exclusive mode, master level, mute/dim/mono) live in the APVTS.

    Threading: setters are called from the message thread. The audio engine
    copies the settings it needs when the version counter changes, via
    tryCopyForEngine() — a TRY-lock, so a GUI edit or a state restore holding
    the lock can never block the audio thread; the engine simply keeps last
    block's settings and retries. That copy also deliberately excludes
    OutputSettings::firPath (see OutputAudioSettings): assigning a juce::String
    on the audio thread can free the previous buffer, and the engine never needs
    the path. copyAll() is the full copy, message thread only (serialization).

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
// Active matrix size on a fresh instance, independent of the fixed 32-channel
// bus. Stereo in, because the common case is a stereo mixing room; eight out,
// because a monitor controller's job is feeding several destinations from it
// (two speaker pairs, a subwoofer and headphones is already seven).
constexpr int defaultIns  = 2;
constexpr int defaultOuts = 8;
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

constexpr int numOutputBands = 2;       // EQ bands per output
constexpr int numFrameBands  = 2;       // EQ bands per matrix frame (crosspoint)

// One EQ band of a filter chain (the bands are cascaded in series). Shared by
// both frames and outputs.
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

// A matrix frame: how much of the input reaches the output (gain + optional
// polarity flip), its own small 2-band EQ (e.g. a config-specific tonal
// tweak on just this route), and an analyzer tap. The speaker's own
// processing (trim, its own EQ, delay, FIR) lives on the output.
struct FrameSettings
{
    bool  active      = false;
    float gainDb      = 0.0f;     // overall frame level
    bool  phaseInvert = false;
    bool  spectrum    = false;    // show this frame's signal on the analyzer
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
        const FrameSettings d;
        return active == d.active && gainDb == d.gainDb && phaseInvert == d.phaseInvert
            && spectrum == d.spectrum && bands == d.bands;
    }
};

struct OutputAudioSettings;     // defined just below OutputSettings

// One physical speaker output: trim, a 2-band EQ (e.g. the bass-management
// crossover), a time-alignment delay, an FIR correction, and an analyzer tap.
struct OutputSettings
{
    float        gainDb   = 0.0f;
    float        delayMs  = 0.0f;  // time-alignment delay
    bool         firOn    = false;
    juce::String firPath;          // impulse response wav file
    bool         spectrum = false; // show this output's sum on the analyzer
    std::array<FrameBand, (size_t) numOutputBands> bands {};

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

    /** The subset the audio engine needs, as plain values. Defined below. */
    OutputAudioSettings audio() const noexcept;
};

// Everything the audio engine reads from an output, with firPath deliberately
// left out: it is a juce::String, and copy-assigning one on the audio thread
// releases the previous reference, which can call free(). The path is only ever
// needed by MatrixEngine::updateFirFiles(), which runs on the message thread and
// reads it straight from the model.
struct OutputAudioSettings
{
    float gainDb   = 0.0f;
    float delayMs  = 0.0f;
    bool  firOn    = false;
    bool  spectrum = false;
    std::array<FrameBand, (size_t) numOutputBands> bands {};

    bool anyBandOn() const
    {
        for (const auto& b : bands)
            if (b.on)
                return true;
        return false;
    }
};

inline OutputAudioSettings OutputSettings::audio() const noexcept
{
    return { gainDb, delayMs, firOn, spectrum, bands };
}

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

    using FrameSettingsArray =
        std::array<std::array<std::array<FrameSettings, numChannels>, numChannels>, numConfigs>;

    /** Full copy, including every hidden frame and each output's firPath.
        MESSAGE THREAD ONLY — it copies juce::Strings. Used by toValueTree(). */
    void copyAll (FrameSettingsArray& dstFrames,
                  std::array<OutputSettings, numChannels>& dstOutputs) const
    {
        const juce::SpinLock::ScopedLockType sl (lock);
        dstFrames = frames;
        dstOutputs = outputs;
    }

    /** Audio thread: copy what the engine needs, without ever blocking.
        Returns false if the lock was busy (a GUI edit or a state restore is in
        progress); the caller should keep the settings it already has and try
        again next block, rather than waiting on a message-thread critical
        section that may be holding a whole ValueTree parse.

        Only the visible `ins` x `outs` sub-range of each configuration is
        copied — those are the only frames the engine processes, and it keeps a
        default 8x8 matrix at ~24 KB per pull instead of ~384 KB for all 32x32x6.
        Frames outside the range are refreshed by the pull that follows the
        matrix growing, before they can be processed. */
    bool tryCopyForEngine (int ins, int outs,
                           FrameSettingsArray& dstFrames,
                           std::array<OutputAudioSettings, numChannels>& dstOutputs) const noexcept
    {
        const juce::SpinLock::ScopedTryLockType tl (lock);
        if (! tl.isLocked())
            return false;

        const int ni = juce::jlimit (0, numChannels, ins);
        const int no = juce::jlimit (0, numChannels, outs);

        for (int c = 0; c < numConfigs; ++c)
            for (int i = 0; i < ni; ++i)
                for (int o = 0; o < no; ++o)
                    dstFrames[(size_t) c][(size_t) i][(size_t) o]
                        = frames[(size_t) c][(size_t) i][(size_t) o];

        for (int o = 0; o < numChannels; ++o)
            dstOutputs[(size_t) o] = outputs[(size_t) o].audio();

        return true;
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
        // Reserve (i.e. allocate) BEFORE taking the lock. The loop is capped at
        // maxFrameTaps, so push_back can never reallocate inside the critical
        // section — the audio thread's try-lock should not be losing races to a
        // GUI query that is busy calling operator new.
        std::vector<FrameRef> refs;
        refs.reserve ((size_t) maxFrameTaps);

        const int ins = numIns.load(), outs = numOuts.load();

        const juce::SpinLock::ScopedLockType sl (lock);
        for (int c = 0; c < numConfigs; ++c)
            for (int i = 0; i < ins; ++i)
                for (int o = 0; o < outs; ++o)
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
    std::atomic<int> numIns { defaultIns }, numOuts { defaultOuts };
    std::atomic<int> version { 1 };

    juce::ListenerList<Listener> listeners;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ConfigModel)
};

} // namespace smt
