/*
  ------------------------------------------------------------------------------
    MeasurementEngine.h

    Part 2 of SuperMoTo: loudspeaker measurement. Sends a stimulus (band
    limited white noise or logarithmic sweep, 10 Hz .. 20 kHz) to each
    selected output in turn while recording the measurement microphone on a
    selected input. Each measurement is saved as "<base>_<output>.wav", a
    stereo file with channel 1 = sent signal and channel 2 = recorded signal.

    The stimulus can optionally pass through the output correction chain
    (trim + FIR) so a correction curve can be verified by re-measuring.

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
        std::array<bool, numChannels> outputsToMeasure {};
        SignalType signalType = SignalType::logSweep;
        float durationS = 10.0f;                            // 5 .. 30
        float levelDb = -12.0f;
        juce::String basePath;                              // folder + base name
        bool throughCorrection = false;                     // verify correction
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
        input channel's samples for this block. */
    bool process (const float* micInput, juce::AudioBuffer<float>& output, int n,
                  MatrixEngine& engine);

private:
    enum class State : int { idle = 0, playing, finishing };

    void handleAsyncUpdate() override;      // write file + advance
    void startCurrentOutput();              // reset generators & capture
    void setStatus (const juce::String& s);

    float nextStimulusSample();

    double sr = 44100.0;

    Settings settings;
    std::vector<int> outputList;            // outputs still to measure
    int currentOutput = -1;

    juce::AudioBuffer<float> capture;       // ch0 = sent, ch1 = recorded
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
