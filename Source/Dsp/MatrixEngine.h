/*
  ------------------------------------------------------------------------------
    MatrixEngine.h

    The SuperMoTo audio engine: six 16x16 matrix configurations (A..F) of
    FrameProcessors, plus one FIR correction filter and trim per physical
    output. Active configurations are summed (non-exclusive mode); the
    exclusive behaviour is enforced at the parameter level.

    The engine pulls its settings from the ConfigModel when the model's version
    counter changes, with a try-lock and no allocation on the audio thread: if
    the model is busy the pull is simply deferred to the next block, so a GUI
    edit or a state restore can never stall the audio callback. FIR impulse
    files are loaded on the message thread via updateFirFiles().

    Author: Olivier Doaré, github.com/odoare
    Licenced under the GNU LGPL Version 3.0
    SPDX-License-Identifier: LGPL-3.0-or-later
  ------------------------------------------------------------------------------
*/

#pragma once

#include <JuceHeader.h>
#include "../Model/ConfigModel.h"
#include "FrameProcessor.h"
#include "OutputProcessor.h"
#include <FxmeTools/dsp/FirFilter.h>     // fxme::FirFilter (WDL-backed, not in module umbrella)
#include "SpectrumBus.h"     // smt::SpectrumBus over fxme::SpectrumTap

namespace smt
{

class MatrixEngine
{
public:
    explicit MatrixEngine (ConfigModel& m) : model (m) {}

    void prepare (double sampleRate, int maxBlockSize);

    /** Mixes inputs into outputs through every active configuration.
        @param inputs       16 input channel pointers
        @param output       16-channel output buffer (overwritten)
        @param n            number of samples
        @param configActive which of the 6 configurations are engaged
        @param masterGain   linear master gain (level + mute + dim)        */
    void process (const float* const* inputs, juce::AudioBuffer<float>& output,
                  int n, const std::array<bool, numConfigs>& configActive,
                  float masterGain);

    /** Applies only the per-output trim + FIR chain to one channel of the
        buffer — used by the measurement part to verify correction curves. */
    void processOutputChainOnly (juce::AudioBuffer<float>& buffer, int channel, int n,
                                 bool applyFir);

    //==========================================================================
    float getFrameLevelDb (int config, int in, int out) const
    {
        return frames[(size_t) config][(size_t) in][(size_t) out].getLevelDb();
    }

    float getOutputLevelDb (int out) const
    {
        return juce::Decibels::gainToDecibels (outputLevels[(size_t) out].load(), -90.0f);
    }

    SpectrumBus& getSpectrumBus()           { return spectrumBus; }
    fxme::FirFilter& getFir (int out)       { return *firs[(size_t) out]; }

    /** Extra delay (samples / ms) the engine added to this output to align it
        with the longest output FIR's latency. 0 = no compensation. */
    int getOutputLatencyCompSamples (int out) const { return compDelay[(size_t) out].load(); }
    float getOutputLatencyCompMs (int out) const
    {
        return (float) (1000.0 * (double) compDelay[(size_t) out].load() / (sr > 0.0 ? sr : 48000.0));
    }

    /** How many samples/ms of THIS output's own manual delay were "spent"
        absorbing its own FIR's latency instead of being applied as an actual
        delay — see MatrixEngine.cpp's recomputeLatencyComp() doc comment.
        0 = the FIR is off, unfed, or the manual delay had no slack to give. */
    int getOutputSelfAbsorbedSamples (int out) const { return selfAbsorbDelay[(size_t) out].load(); }
    float getOutputSelfAbsorbedMs (int out) const
    {
        return (float) (1000.0 * (double) selfAbsorbDelay[(size_t) out].load() / (sr > 0.0 ? sr : 48000.0));
    }

    /** Message thread: (re)load FIR impulses whose path changed in the model.
        With force, every output reloads regardless (used after a preset /
        session restore, where the embedded audio may differ under an
        unchanged path). Latency compensation is *not* recomputed synchronously
        here (that touches OutputProcessor state, which must stay audio-thread
        only) — it's flagged dirty and picked up by the next process() block. */
    void updateFirFiles (bool force = false);

    /** Optional source of state-embedded IRs (fxme::EmbeddedAudio), set by the
        processor. When it yields a reader for an output, that impulse is used
        instead of the model's firPath, so presets/sessions stay portable even
        when the original wav files are gone. Message thread. */
    std::function<std::unique_ptr<juce::AudioFormatReader> (int out)> embeddedIrProvider;

private:
    void pullModelIfChanged();
    void computeFedMask();          // which outputs the engaged presets feed
    void recomputeLatencyComp();

    ConfigModel& model;

    std::array<std::array<std::array<FrameProcessor, numChannels>, numChannels>, numConfigs> frames;
    std::array<std::unique_ptr<fxme::FirFilter>, numChannels> firs;
    std::array<OutputProcessor, numChannels> outputProc;   // per-output EQ + delay
    std::array<juce::LinearSmoothedValue<float>, numChannels> outputGains;
    std::array<std::atomic<float>, numChannels> outputLevels {};
    juce::LinearSmoothedValue<float> smoothedMaster;

    // Inter-output latency compensation: each output that the engaged presets
    // actually feed is delayed so those outputs share the longest fed output
    // FIR's bulk latency (keeps multi-way / sub setups time-aligned). Outputs no
    // active frame routes to are ignored. Power-of-two ring, integer-sample delay.
    // Before adding this post-FIR delay, each output's own manual delay first
    // self-absorbs as much of its own FIR's latency as it has slack for (see
    // recomputeLatencyComp()); selfAbsorbDelay records how much for the UI.
    static constexpr int compCap = 1 << 16;     // max compensable latency
    std::array<std::vector<float>, numChannels> compBuf;
    std::array<int, numChannels> compWrite {};
    std::array<std::atomic<int>, numChannels> compDelay {};
    std::array<std::atomic<int>, numChannels> selfAbsorbDelay {};
    std::array<bool, numConfigs> activeConfigs {};   // last engaged presets
    std::atomic<juce::uint32> fedOutputsMask { 0 };  // outputs fed by them
    // Set by updateFirFiles() (message thread) when a FIR's latency may have
    // changed; consumed by process() (audio thread), which is the only thread
    // allowed to call recomputeLatencyComp() since it mutates OutputProcessor.
    std::atomic<bool> latencyCompDirty { false };

    // The engine's own copy of the model, refreshed by pullModelIfChanged().
    // Only the visible sub-range of frameSettings is kept current (that is all
    // the engine processes), and outputSettings carries no firPath — see
    // ConfigModel::tryCopyForEngine.
    ConfigModel::FrameSettingsArray frameSettings {};
    std::array<OutputAudioSettings, numChannels> outputSettings {};
    std::array<juce::String, numChannels> loadedFirPaths;   // message thread only

    juce::AudioBuffer<float> outScratch;
    int visIns = numChannels, visOuts = numChannels;   // processed matrix size
    int lastModelVersion = 0;
    double sr = 44100.0;
    bool prepared = false;

    SpectrumBus spectrumBus;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MatrixEngine)
};

} // namespace smt
