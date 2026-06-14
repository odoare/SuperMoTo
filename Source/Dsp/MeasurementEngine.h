/*
  ------------------------------------------------------------------------------
    MeasurementEngine.h

    Part 2 of SuperMoTo: loudspeaker / system measurement. Sends a stimulus
    (band limited white noise or logarithmic sweep, 10 Hz .. 20 kHz) to each
    selected channel in turn while recording the measurement microphone on a
    selected input. Each measurement is a stereo file with channel 1 = sent
    signal and channel 2 = recorded signal.

    Three modes (MeasureMode):
      dryOutput  – stimulus straight to an output (raw speaker, to design FIRs),
                   saved as "<base>_<output>.wav".
      outputFir  – stimulus to an output through its trim + FIR (verify the FIR),
                   same naming.
      fullSystem – stimulus into a plugin INPUT, run through the whole engine
                   (matrix, crossover, FIRs, latency compensation); the selected
                   channels are inputs, saved as "<base>_in<input>.wav".

    Audio-thread part: process() renders the stimulus and captures the mic.
    When one capture completes, the file write and the advance to the next
    output happen on the message thread (AsyncUpdater).

    Author: Olivier Doaré, github.com/odoare
    Licenced under the GNU LGPL Version 3.0
    SPDX-License-Identifier: LGPL-3.0-or-later
  ------------------------------------------------------------------------------
*/

#pragma once

#include <JuceHeader.h>
#include "Biquad.h"
#include "MatrixEngine.h"

namespace smt
{

class MeasurementEngine : public juce::ChangeBroadcaster,
                          private juce::AsyncUpdater
{
public:
    enum class SignalType : int { whiteNoise = 0, logSweep = 1 };

    struct Settings
    {
        int micInput = 0;                                   // 0-based input index
        std::array<bool, numChannels> channelsToMeasure {}; // outputs, or inputs
                                                            // in fullSystem mode
        SignalType signalType = SignalType::logSweep;
        MeasureMode mode = MeasureMode::dryOutput;
        float durationS = 10.0f;                            // 5 .. 30
        float levelDb = -12.0f;
        juce::String basePath;                              // folder + base name
    };

    MeasurementEngine() = default;
    ~MeasurementEngine() override { cancelPendingUpdate(); }

    void prepare (double sampleRate, int maxBlockSize);

    /** Message thread. Returns false if no output selected / invalid path. */
    bool start (const Settings& s);
    void stop();

    bool isRunning() const noexcept     { return state.load() != State::idle; }
    float getProgress() const noexcept  { return progress.load(); }
    juce::String getStatusText() const  { juce::ScopedLock sl (statusLock); return statusText; }

    /** Audio thread. Returns true if the measurement owns the audio (the
        matrix must then stay silent). micInput points to the selected
        input channel's samples for this block. In fullSystem mode the stimulus
        is injected into the measured input and run through the engine with the
        given engaged configurations. */
    bool process (const float* micInput, juce::AudioBuffer<float>& output, int n,
                  MatrixEngine& engine, const std::array<bool, numConfigs>& configActive);

private:
    enum class State : int { idle = 0, playing, finishing };

    void handleAsyncUpdate() override;      // write file + advance
    void startCurrentOutput();              // reset generators & capture
    void setStatus (const juce::String& s);
    juce::String channelStatus (int idx) const;
    juce::File captureFile (int ch) const;

    float nextStimulusSample();

    double sr = 44100.0;

    Settings settings;
    std::vector<int> channelList;           // outputs (or inputs) still to do
    int currentChannel = -1;

    juce::AudioBuffer<float> capture;       // ch0 = sent, ch1 = recorded
    juce::AudioBuffer<float> inScratch;     // synthetic inputs (fullSystem mode)
    int capturePos = 0;
    int stimulusSamples = 0;                // stimulus length
    int totalSamples = 0;                   // stimulus + tail

    // Generators
    juce::Random random;
    Biquad noiseHp, noiseLp;
    double sweepK = 0.0, sweepL = 0.0;
    int genPos = 0;
    float levelGain = 1.0f;

    std::atomic<State> state { State::idle };
    std::atomic<float> progress { 0.0f };

    mutable juce::CriticalSection statusLock;
    juce::String statusText;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MeasurementEngine)
};

} // namespace smt
