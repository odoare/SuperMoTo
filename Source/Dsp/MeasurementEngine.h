/*
  ------------------------------------------------------------------------------
    MeasurementEngine.h

    Part 2 of SuperMoTo: loudspeaker / system measurement. Sends a stimulus
    (band limited white noise over 10 Hz .. 20 kHz, or a SYNCHRONIZED
    logarithmic sweep — Novak et al. JAES 2015, f1*L integer so deconvolution
    separates the harmonic IRs with true phase — running from ~10 Hz up to
    NYQUIST over a whole number of octaves, see sweepBandFor) to each selected
    channel in turn while recording the measurement microphone on a selected
    input. Each measurement
    is a stereo file with channel 1 = sent signal and channel 2 = recorded
    signal; measurement.xml records the sweep identity (f1, f2, L) per run.

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
    Licenced under the GNU AGPL Version 3.0, or commercial terms (LICENSE.md)
    SPDX-License-Identifier: AGPL-3.0-or-later OR LicenseRef-FXME-Commercial
  ------------------------------------------------------------------------------
*/

#pragma once

#include <JuceHeader.h>
#include "MatrixEngine.h"     // pulls fxme::FirFilter; fxme::Biquad via module umbrella
#include "MeasurementFolder.h" // the reading side: manifest, folder scan, run helpers

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
        juce::String folder;                                // destination folder
        juce::String generalComment;                        // whole folder; rewritten
                                                            // at each manifest save
        juce::String runComment;                            // this run only, one line
        juce::String micCalName;                            // mic correction curve in
        juce::String micCalText;                            //   effect: name + raw cal
                                                            //   text for the manifest
        bool  splCalibrated = false;                        // SPL-meter calibration:
        float splOffsetDb = 0.0f;                           //   dB SPL = dBFS + offset
        // What each output drives, as the preset names it, for every output
        // rather than only the measured ones: writeManifests() records a
        // folder's whole channel list, including channels an earlier run of
        // the same folder wrote. Empty in fullSystem mode, where the toggled
        // channels are inputs.
        std::array<juce::String, numChannels> outputDescriptions {};
        int subChannel = -1;                                // 0-based; -1 = none.
                                                            // Only meaningful for dry/FIR
                                                            // modes (fullSystem's toggled
                                                            // channels are inputs).
    };

    MeasurementEngine() = default;
    ~MeasurementEngine() override { cancelPendingUpdate(); }

    void prepare (double sampleRate, int maxBlockSize);

    /** Message thread. Returns false if no output selected / invalid path. */
    bool start (const Settings& s);

    /** Cancels the run and DELETES the captures it had already written: a run
        is all or nothing, and half of one left in the folder is a set of
        measurements with channels missing and no manifest entry to say which.
        Only this run's files can go — each run takes the next free position
        number per channel, so it never writes over an earlier one's. Harmless
        when nothing is running. */
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
    int discardRunFiles();                  // delete this run's captures; returns how many
    void startCurrentOutput();              // reset generators & capture
    void setStatus (const juce::String& s);
    juce::String channelStatus (int idx) const;
    juce::File captureFile (int ch) const;
    void writeManifests() const;    // measurement.xml + readme_measurement.md

    float nextStimulusSample();

    // Noise band, fixed. The sweep band is derived per run instead (see
    // sweepBandFor), so changing it cannot move the noise stimulus.
    static constexpr double noiseF1Hz = 10.0, noiseF2Hz = 20000.0;

    /** This run's sweep band: f2 = Nyquist, f1 = f2 / 2^P with P chosen to
        land f1 near 10 Hz (11 octaves at 44.1/48 kHz, 12 at 88.2/96 kHz).

        Running the sweep all the way to Nyquist is what keeps the deconvolved
        impulse response clean. Stopping short of it leaves a brick-wall band
        edge whose symmetric sinc rings around the peak of every measurement;
        measured on a loopback that ringing is about -18 dB at 48 kHz, and it
        gets WORSE as the sample rate rises because the gap to Nyquist grows.
        Sweeping to Nyquist is worth ~43 dB of it. An integer octave count
        additionally makes the sweep's end phase an exact multiple of 2*pi on
        top of Novak's f1*L condition, so the sweep ends at a zero crossing of
        its own accord. See Farina, AES 122 (2007), section 3.1, and Vetter &
        di Rosario, ExpoChirpToolbox (2011), sections 2.1 and 2.5.

        Written to the manifest per run, so folders measured with the older
        fixed 10 Hz .. 20 kHz band keep deconvolving with their own band. */
    static void sweepBandFor (double sampleRate, double& f1, double& f2) noexcept;

    double sr = 44100.0;

    Settings settings;
    std::vector<int> channelList;           // outputs (or inputs) still to do
    std::vector<int> positions;             // per-channel position number for this run,
                                            // parallel to channelList
    // The captures this run has written, and therefore the ones it would have
    // to take back if it never finishes. Emptied when the run completes, so
    // what it holds is always an unfinished run's (see stop()).
    juce::Array<juce::File> filesWrittenThisRun;
    int currentChannel = -1;

    juce::AudioBuffer<float> capture;       // ch0 = sent, ch1 = recorded
    juce::AudioBuffer<float> inScratch;     // synthetic inputs (fullSystem mode)
    int capturePos = 0;
    int stimulusSamples = 0;                // stimulus length
    int totalSamples = 0;                   // stimulus + tail

    // Generators
    juce::Random random;
    fxme::Biquad noiseHp, noiseLp;
    double sweepF1 = 0.0, sweepF2 = 0.0;    // this run's sweep band
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
