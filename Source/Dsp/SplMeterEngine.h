/*
  ------------------------------------------------------------------------------
    SplMeterEngine.h

    SPL meter / signal generator for the calibration part. On the audio thread
    it measures the RMS level of the selected microphone input (reported in
    dBFS) and, when a generator is engaged, emits a test stimulus (sinusoid
    and/or white noise). The routing follows the shared measurement mode: onto
    the selected outputs (dry or through their trim + FIR), or — in fullSystem
    mode — into the selected inputs and through the whole engine (matrix,
    crossover, FIRs, latency comp).

    The reusable DSP — the sliding-window RMS (fxme::RmsMeter) and the test
    generator (fxme::SignalGenerator) — lives in FxmeTools; this class is the
    SuperMoTo-specific wrapper holding the mic/channel routing and the engine
    integration. The calibration GUI turns the dBFS reading into dB SPL with a
    reference value the user enters from a real SPL meter.

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
    void setRmsWindowSeconds (float s)     { rms.setWindowSeconds (s); }

    void setSineOn (bool on)               { generator.setSineOn (on); }
    void setSineAmpDb (float db)           { generator.setSineAmpDb (db); }
    void setSineFreq (float hz)            { generator.setSineFreq (hz); }
    void setNoiseOn (bool on)              { generator.setNoiseOn (on); }
    void setNoiseAmpDb (float db)          { generator.setNoiseAmpDb (db); }

    //==========================================================================
    // Message-thread getters.
    int  getMicChannel() const noexcept    { return micChannel.load(); }
    float getRmsDbFs() const noexcept      { return rms.getRmsDbFs(); }
    bool isGenerating() const noexcept     { return generator.isGenerating(); }

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
    std::atomic<bool>  meterOn { false };
    std::atomic<int>   micChannel { 0 };
    std::atomic<juce::uint32> channelsMask { 0 };
    std::atomic<MeasureMode> mode { MeasureMode::dryOutput };

    fxme::RmsMeter        rms;          // sliding-window RMS of the mic input
    fxme::SignalGenerator generator;    // sine / white-noise stimulus
    fxme::SpectrumTap     micTap;       // mic samples for the calibration spectrum analyzer
    juce::AudioBuffer<float> inScratch; // synthetic inputs (fullSystem mode)

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SplMeterEngine)
};

} // namespace smt
