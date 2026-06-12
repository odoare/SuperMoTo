/*
  ------------------------------------------------------------------------------
    MatrixEngine.h

    The SuperMoTo audio engine: six 16x16 matrix configurations (A..F) of
    FrameProcessors, plus one FIR correction filter and trim per physical
    output. Active configurations are summed (non-exclusive mode); the
    exclusive behaviour is enforced at the parameter level.

    The engine pulls its settings from the ConfigModel when the model's
    version counter changes (short spinlock, no allocation on the audio
    thread). FIR impulse files are loaded on the message thread via
    updateFirFiles().

    Author: Olivier Doaré, github.com/odoare
    Licenced under the GNU LGPL Version 3.0
    SPDX-License-Identifier: LGPL-3.0-or-later
  ------------------------------------------------------------------------------
*/

#pragma once

#include <JuceHeader.h>
#include "../Model/ConfigModel.h"
#include "FrameProcessor.h"
#include "FirFilter.h"
#include "SpectrumTap.h"

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
    FirFilter& getFir (int out)             { return *firs[(size_t) out]; }

    /** Message thread: (re)load FIR impulse files whose path changed in the
        model, and enable/disable output spectrum taps. */
    void updateFirFiles();

private:
    void pullModelIfChanged();

    ConfigModel& model;

    std::array<std::array<std::array<FrameProcessor, numChannels>, numChannels>, numConfigs> frames;
    std::array<std::unique_ptr<FirFilter>, numChannels> firs;
    std::array<juce::LinearSmoothedValue<float>, numChannels> outputGains;
    std::array<std::atomic<float>, numChannels> outputLevels {};
    juce::LinearSmoothedValue<float> smoothedMaster;

    std::array<std::array<std::array<FrameSettings, numChannels>, numChannels>, numConfigs> frameSettings {};
    std::array<OutputSettings, numChannels> outputSettings {};
    std::array<juce::String, numChannels> loadedFirPaths;

    juce::AudioBuffer<float> outScratch;
    int lastModelVersion = 0;
    double sr = 44100.0;
    bool prepared = false;

    SpectrumBus spectrumBus;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MatrixEngine)
};

} // namespace smt
