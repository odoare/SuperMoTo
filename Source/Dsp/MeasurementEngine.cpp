/*
  ------------------------------------------------------------------------------
    MeasurementEngine.cpp

    Author: Olivier Doaré, github.com/odoare
    Licenced under the GNU LGPL Version 3.0
    SPDX-License-Identifier: LGPL-3.0-or-later
  ------------------------------------------------------------------------------
*/

#include "MeasurementEngine.h"

namespace smt
{

static constexpr float tailSeconds = 1.0f;      // capture the decay
static constexpr float fadeSeconds = 0.02f;     // stimulus fade in/out

void MeasurementEngine::prepare (double sampleRate, int maxBlockSize)
{
    sr = sampleRate;
    inScratch.setSize (numChannels, juce::jmax (1, maxBlockSize));
    stop();
}

bool MeasurementEngine::start (const Settings& s)
{
    if (isRunning())
        return false;

    channelList.clear();
    for (int o = 0; o < numChannels; ++o)
        if (s.channelsToMeasure[(size_t) o])
            channelList.push_back (o);

    if (channelList.empty() || s.basePath.isEmpty())
        return false;

    const auto baseFile = juce::File::createFileWithoutCheckingPath (s.basePath);
    if (! baseFile.getParentDirectory().exists())
        return false;

    settings = s;
    settings.durationS = juce::jlimit (5.0f, 30.0f, s.durationS);
    levelGain = juce::Decibels::decibelsToGain (settings.levelDb);

    stimulusSamples = (int) (settings.durationS * sr);
    totalSamples    = stimulusSamples + (int) (tailSeconds * sr);
    capture.setSize (2, totalSamples);

    // Log sweep parameters (f1 = 10 Hz, f2 = 20 kHz):
    //   phase(t) = K * (exp(t/L) - 1),  L = T/ln(f2/f1),  K = 2*pi*f1*L
    const double f1 = 10.0, f2 = 20000.0, T = (double) settings.durationS;
    sweepL = T / std::log (f2 / f1);
    sweepK = 2.0 * juce::MathConstants<double>::pi * f1 * sweepL;

    // Band-limit the white noise to 10 Hz .. 20 kHz.
    noiseHp.c = fxme::BiquadCoeffs::highpass (sr, 10.0f, 0.707f);
    noiseLp.c = fxme::BiquadCoeffs::lowpass (sr, juce::jmin (20000.0f, (float) (0.45 * sr)), 0.707f);

    currentChannel = 0;
    startCurrentOutput();
    setStatus (channelStatus (0));
    state.store (State::playing);
    sendChangeMessage();
    return true;
}

juce::String MeasurementEngine::channelStatus (int idx) const
{
    const bool full = settings.mode == MeasureMode::fullSystem;
    return "Measuring " + juce::String (full ? "input " : "output ")
         + juce::String (channelList[(size_t) idx] + 1) + "...";
}

juce::File MeasurementEngine::captureFile (int ch) const
{
    // Inputs (fullSystem) get an "in" tag so an input set and an output set can
    // share a base path without clobbering each other.
    const juce::String tag = (settings.mode == MeasureMode::fullSystem ? "_in" : "_")
                           + juce::String (ch + 1);
    return juce::File::createFileWithoutCheckingPath (settings.basePath + tag + ".wav");
}

void MeasurementEngine::stop()
{
    state.store (State::idle);
    progress.store (0.0f);
    setStatus ("Stopped");
    sendChangeMessage();
}

void MeasurementEngine::startCurrentOutput()
{
    capture.clear();
    capturePos = 0;
    genPos = 0;
    noiseHp.reset();
    noiseLp.reset();
}

void MeasurementEngine::setStatus (const juce::String& s)
{
    juce::ScopedLock sl (statusLock);
    statusText = s;
}

float MeasurementEngine::nextStimulusSample()
{
    if (genPos >= stimulusSamples)
        return 0.0f;

    const double t = (double) genPos / sr;
    float v;

    if (settings.signalType == SignalType::logSweep)
    {
        v = (float) std::sin (sweepK * (std::exp (t / sweepL) - 1.0));
    }
    else
    {
        v = random.nextFloat() * 2.0f - 1.0f;
        v = noiseLp.processSample (noiseHp.processSample (v));
    }

    // Fade in/out to avoid clicks.
    const int fadeSamples = juce::jmax (1, (int) (fadeSeconds * sr));
    if (genPos < fadeSamples)
        v *= (float) genPos / (float) fadeSamples;
    else if (genPos > stimulusSamples - fadeSamples)
        v *= (float) (stimulusSamples - genPos) / (float) fadeSamples;

    ++genPos;
    return v * levelGain;
}

bool MeasurementEngine::process (const float* micInput, juce::AudioBuffer<float>& output,
                                 int n, MatrixEngine& engine,
                                 const std::array<bool, numConfigs>& configActive)
{
    const auto st = state.load();
    if (st == State::idle)
        return false;

    for (int c = 0; c < output.getNumChannels(); ++c)
        output.clear (c, 0, n);

    // Hold silence while a finished capture is being written to disk, so
    // the monitoring matrix does not blast between consecutive measurements.
    if (st == State::finishing)
        return true;

    const int ch = channelList[(size_t) currentChannel];
    const bool full = settings.mode == MeasureMode::fullSystem;

    // Output modes write the stimulus straight onto the measured output;
    // fullSystem injects it into the measured input and runs the engine.
    auto* dest = (! full && ch < output.getNumChannels()) ? output.getWritePointer (ch) : nullptr;
    float* sIn = nullptr;
    if (full)
    {
        inScratch.clear();
        if (ch < inScratch.getNumChannels() && n <= inScratch.getNumSamples())
            sIn = inScratch.getWritePointer (ch);
    }

    auto* sent = capture.getWritePointer (0);
    auto* rec  = capture.getWritePointer (1);

    const int todo = juce::jmin (n, totalSamples - capturePos);
    for (int i = 0; i < todo; ++i)
    {
        const float v = nextStimulusSample();
        sent[capturePos] = v;                   // raw stimulus, pre-everything
        rec[capturePos]  = micInput != nullptr ? micInput[i] : 0.0f;
        if (dest != nullptr) dest[i] = v;
        if (sIn  != nullptr) sIn[i]  = v;
        ++capturePos;
    }

    if (full)
    {
        // Complete system: matrix routing, crossover filters, output FIRs and
        // inter-output latency compensation, exactly as monitored. Master gain
        // is bypassed (unity) so the level is set by the stimulus alone.
        engine.process (inScratch.getArrayOfReadPointers(), output, n, configActive, 1.0f);
    }
    else if (settings.mode == MeasureMode::outputFir && dest != nullptr)
    {
        // Verify the correction: emitted signal through the output trim + FIR
        // (the captured "sent" stays the raw stimulus). dryOutput does neither.
        engine.processOutputChainOnly (output, ch, n, true);
    }

    progress.store (((float) currentChannel + (float) capturePos / (float) totalSamples)
                    / (float) channelList.size());

    if (capturePos >= totalSamples)
    {
        state.store (State::finishing);
        triggerAsyncUpdate();       // write the file on the message thread
    }

    return true;
}

void MeasurementEngine::handleAsyncUpdate()
{
    if (state.load() != State::finishing)
        return;

    const int ch = channelList[(size_t) currentChannel];
    const auto file = captureFile (ch);

    file.deleteFile();
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::FileOutputStream> stream (file.createOutputStream());
    bool ok = false;

    if (stream != nullptr)
    {
        std::unique_ptr<juce::AudioFormatWriter> writer (
            wav.createWriterFor (stream.get(), sr, 2, 32, {}, 0));
        if (writer != nullptr)
        {
            stream.release();       // writer owns the stream now
            ok = writer->writeFromAudioSampleBuffer (capture, 0, totalSamples);
        }
    }

    if (! ok)
    {
        setStatus ("ERROR writing " + file.getFullPathName());
        state.store (State::idle);
        sendChangeMessage();
        return;
    }

    if (++currentChannel < (int) channelList.size())
    {
        setStatus (channelStatus (currentChannel));
        startCurrentOutput();
        state.store (State::playing);
    }
    else
    {
        setStatus ("Done (" + juce::String (channelList.size()) + " files written)");
        progress.store (1.0f);
        state.store (State::idle);
    }

    sendChangeMessage();
}

} // namespace smt
