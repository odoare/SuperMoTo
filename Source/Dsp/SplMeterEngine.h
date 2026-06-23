/*
  ------------------------------------------------------------------------------
    SplMeterEngine.h

    SPL meter / signal generator for the calibration part. On the audio
    thread it measures the RMS level of the selected microphone input over a
    configurable sliding window (reported in dBFS) and, when a generator is
    engaged, emits a test stimulus (sinusoid and/or white noise). The routing
    follows the shared measurement mode: onto the selected outputs (dry or
    through their trim + FIR), or — in fullSystem mode — into the selected inputs
    and through the whole engine (matrix, crossover, FIRs, latency comp).

    The calibration GUI turns the dBFS reading into dB SPL with a reference
    value the user enters from a real SPL meter.

    All controls are set from the message thread through atomics; process()
    runs on the audio thread and never allocates.

    Author: Olivier Doaré, github.com/odoare
    Licenced under the GNU LGPL Version 3.0
    SPDX-License-Identifier: LGPL-3.0-or-later
  ------------------------------------------------------------------------------
*/

#pragma once

#include <JuceHeader.h>
#include "MatrixEngine.h"

namespace smt
{

class SplMeterEngine
{
public:
    SplMeterEngine() = default;

    void prepare (double sampleRate, int maxBlockSize);

    //==========================================================================
    // Message-thread setters (realtime-safe atomics).
    void setMeterOn (bool on)              { meterOn.store (on); micTap.setEnabled (on); }
    void setMicChannel (int ch)            { micChannel.store (ch); }
    void setChannelsMask (juce::uint32 m)  { channelsMask.store (m); }  // outputs, or inputs
    void setMode (MeasureMode m)           { mode.store (m); }
    void setRmsWindowSeconds (float s)     { rmsWindowSec.store (juce::jlimit (0.005f, maxWindowSec, s)); }

    void setSineOn (bool on)               { sineOn.store (on); }
    void setSineAmpDb (float db)           { sineAmpDb.store (db); }
    void setSineFreq (float hz)            { sineFreq.store (hz); }
    void setNoiseOn (bool on)              { noiseOn.store (on); }
    void setNoiseAmpDb (float db)          { noiseAmpDb.store (db); }

    //==========================================================================
    // Message-thread getters.
    int  getMicChannel() const noexcept    { return micChannel.load(); }
    float getRmsDbFs() const noexcept      { return rmsDbFs.load(); }
    bool isGenerating() const noexcept     { return sineOn.load() || noiseOn.load(); }

    /** Tap feeding the calibration spectrum analyzer (enabled with the meter). */
    fxme::SpectrumTap& getMicSpectrumTap() noexcept { return micTap; }

    //==========================================================================
    /** Audio thread. Updates the mic RMS and, when a generator is engaged,
        renders the test signal: on the selected outputs (dry or through their
        FIR), or — in fullSystem mode — into the selected inputs and through the
        whole engine. Returns true when it owns the audio: the monitoring matrix
        must then stay silent for this block. */
    bool process (const float* micInput, juce::AudioBuffer<float>& output,
                  int n, MatrixEngine& engine, const std::array<bool, numConfigs>& configActive);

private:
    void updateRms (const float* mic, int n);

    static constexpr float maxWindowSec = 2.0f;

    std::atomic<bool>  meterOn { false };
    std::atomic<bool>  sineOn { false }, noiseOn { false };
    std::atomic<float> sineAmpDb { -12.0f }, sineFreq { 1000.0f }, noiseAmpDb { -12.0f };
    std::atomic<float> rmsWindowSec { 0.3f };
    std::atomic<int>   micChannel { 0 };
    std::atomic<juce::uint32> channelsMask { 0 };
    std::atomic<MeasureMode> mode { MeasureMode::dryOutput };

    std::atomic<float> rmsDbFs { -120.0f };

    fxme::SpectrumTap micTap;     // mic samples for the calibration spectrum analyzer
    juce::AudioBuffer<float> inScratch;     // synthetic inputs (fullSystem mode)

    double sr = 44100.0;

    // Sliding-window RMS: ring of squared samples with a running sum. The
    // window length can change at runtime; that simply restarts accumulation.
    std::vector<float> sq;
    int ringCap = 0, writePos = 0, valid = 0, curWindowLen = 0;
    double runningSum = 0.0;

    // Generators.
    double sinePhase = 0.0;
    juce::Random random;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SplMeterEngine)
};

} // namespace smt
