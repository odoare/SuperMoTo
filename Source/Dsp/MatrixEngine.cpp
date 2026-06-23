/*
  ------------------------------------------------------------------------------
    MatrixEngine.cpp

    Author: Olivier Doaré, github.com/odoare
    Licenced under the GNU LGPL Version 3.0
    SPDX-License-Identifier: LGPL-3.0-or-later
  ------------------------------------------------------------------------------
*/

#include "MatrixEngine.h"

namespace smt
{

void MatrixEngine::prepare (double sampleRate, int maxBlockSize)
{
    sr = sampleRate;

    for (auto& cfg : frames)
        for (auto& row : cfg)
            for (auto& f : row)
                f.prepare (sampleRate, maxBlockSize);

    for (int o = 0; o < numChannels; ++o)
    {
        if (firs[(size_t) o] == nullptr)
            firs[(size_t) o] = std::make_unique<fxme::FirFilter>();
        firs[(size_t) o]->prepare (sampleRate, maxBlockSize);

        outputProc[(size_t) o].prepare (sampleRate, maxBlockSize);

        outputGains[(size_t) o].reset (sampleRate, 0.05);
        outputGains[(size_t) o].setCurrentAndTargetValue (1.0f);
        outputLevels[(size_t) o].store (0.0f);

        compBuf[(size_t) o].assign ((size_t) compCap, 0.0f);
        compWrite[(size_t) o] = 0;
        compDelay[(size_t) o].store (0);
    }

    smoothedMaster.reset (sampleRate, 0.05);
    smoothedMaster.setCurrentAndTargetValue (0.0f);

    outScratch.setSize (numChannels, maxBlockSize);

    prepared = true;
    lastModelVersion = 0;       // force a settings pull on the first block
}

void MatrixEngine::pullModelIfChanged()
{
    const int v = model.getVersion();
    if (v == lastModelVersion)
        return;
    lastModelVersion = v;

    model.copyAll (frameSettings, outputSettings);

    // Visible/processed matrix size. Frames revealed by a grow are reset so
    // stale delay-line content does not play back.
    const int newIns = model.getNumIns();
    const int newOuts = model.getNumOuts();
    if (newIns > visIns || newOuts > visOuts)
        for (int c = 0; c < numConfigs; ++c)
            for (int i = 0; i < newIns; ++i)
                for (int o = 0; o < newOuts; ++o)
                    if (i >= visIns || o >= visOuts)
                        frames[(size_t) c][(size_t) i][(size_t) o].reset();
    visIns = newIns;
    visOuts = newOuts;

    for (int c = 0; c < numConfigs; ++c)
        for (int i = 0; i < numChannels; ++i)
            for (int o = 0; o < numChannels; ++o)
                frames[(size_t) c][(size_t) i][(size_t) o]
                    .applySettings (frameSettings[(size_t) c][(size_t) i][(size_t) o]);

    // Frame spectrum tap routing: checked visible frames in scan order, capped.
    int slot = 0;
    for (int c = 0; c < numConfigs && slot < maxFrameTaps; ++c)
        for (int i = 0; i < visIns && slot < maxFrameTaps; ++i)
            for (int o = 0; o < visOuts && slot < maxFrameTaps; ++o)
                if (frameSettings[(size_t) c][(size_t) i][(size_t) o].spectrum)
                    spectrumBus.setFrameRoute (slot++, c, i, o);
    for (; slot < maxFrameTaps; ++slot)
        spectrumBus.setFrameRoute (slot, -1, -1, -1);

    for (int o = 0; o < numChannels; ++o)
    {
        outputGains[(size_t) o].setTargetValue (
            juce::Decibels::decibelsToGain (outputSettings[(size_t) o].gainDb));
        outputProc[(size_t) o].applySettings (outputSettings[(size_t) o]);
        spectrumBus.outputTap (o).setEnabled (outputSettings[(size_t) o].spectrum
                                              && o < visOuts);
    }

    computeFedMask();           // frame active-states / matrix size may have changed
    recomputeLatencyComp();     // firOn toggles change the alignment
}

// An output is "fed" if any engaged configuration has an active frame routing
// into it. Audio-thread only (reads the live frames + activeConfigs).
void MatrixEngine::computeFedMask()
{
    juce::uint32 mask = 0;
    for (int o = 0; o < visOuts; ++o)
    {
        bool fed = false;
        for (int c = 0; c < numConfigs && ! fed; ++c)
        {
            if (! activeConfigs[(size_t) c])
                continue;
            for (int i = 0; i < visIns; ++i)
                if (frames[(size_t) c][(size_t) i][(size_t) o].isRouted())
                {
                    fed = true;
                    break;
                }
        }
        if (fed)
            mask |= (1u << o);
    }
    fedOutputsMask.store (mask);
}

// Delay every fed output so they share the longest fed output FIR's bulk
// latency. The output owning that FIR gets 0; the rest get the difference. An
// unfed output, or one whose FIR is not engaged by the current preset, is left
// alone — so a sub gets aligned to a main's linear-phase correction only when
// the engaged preset actually routes to both.
void MatrixEngine::recomputeLatencyComp()
{
    const juce::uint32 fed = fedOutputsMask.load();
    auto isFed = [fed] (int o) { return (fed & (1u << o)) != 0; };
    auto latOf = [this] (int o)
    {
        return outputSettings[(size_t) o].firOn ? firs[(size_t) o]->getLatencySamples() : 0;
    };

    int lmax = 0;
    for (int o = 0; o < numChannels; ++o)
        if (isFed (o))
            lmax = juce::jmax (lmax, latOf (o));

    for (int o = 0; o < numChannels; ++o)
        compDelay[(size_t) o].store (isFed (o)
            ? juce::jlimit (0, compCap - 1, lmax - latOf (o)) : 0);
}

void MatrixEngine::process (const float* const* inputs, juce::AudioBuffer<float>& output,
                            int n, const std::array<bool, numConfigs>& configActive,
                            float masterGain)
{
    if (! prepared)
        return;

    // Re-derive the latency compensation on preset changes too (which outputs
    // are fed depends on the engaged configs, not just the model version).
    const bool configsChanged = (configActive != activeConfigs);
    activeConfigs = configActive;

    pullModelIfChanged();
    if (configsChanged)
    {
        computeFedMask();
        recomputeLatencyComp();
    }

    smoothedMaster.setTargetValue (masterGain);

    for (int o = 0; o < numChannels; ++o)
        outScratch.clear (o, 0, n);

    // Matrix: every active frame of every engaged configuration adds its
    // filtered/delayed/gained input into the destination output.
    for (int c = 0; c < numConfigs; ++c)
    {
        if (! configActive[(size_t) c])
            continue;

        for (int i = 0; i < visIns; ++i)
        {
            const float* in = inputs[i];
            for (int o = 0; o < visOuts; ++o)
            {
                auto& frame = frames[(size_t) c][(size_t) i][(size_t) o];
                if (! frame.isActive())
                    continue;

                const float* post = frame.process (in, outScratch.getWritePointer (o), n);

                if (frame.wantsSpectrum())
                {
                    const int slot = spectrumBus.findFrameSlot (c, i, o);
                    if (slot >= 0)
                        spectrumBus.frameTap (slot).push (post, n);
                }
            }
        }
    }

    // Per-output chain: trim, FIR correction, master, metering, spectrum tap.
    for (int o = 0; o < numChannels; ++o)
    {
        if (o >= visOuts)
        {
            // Hidden output: already cleared, just keep its meter at silence.
            outputLevels[(size_t) o].store (0.0f);
            continue;
        }

        auto* data = outScratch.getWritePointer (o);

        for (int s = 0; s < n; ++s)
            data[s] *= outputGains[(size_t) o].getNextValue();

        // Per-output EQ (e.g. the bass-management crossover) + time-align delay.
        outputProc[(size_t) o].process (data, n);

        if (outputSettings[(size_t) o].firOn && firs[(size_t) o]->hasImpulse())
            firs[(size_t) o]->process (data, n);

        // Inter-output latency compensation (integer-sample ring delay).
        if (const int d = compDelay[(size_t) o].load(); d > 0)
        {
            auto& buf = compBuf[(size_t) o];
            constexpr int mask = compCap - 1;
            int wp = compWrite[(size_t) o];
            for (int s = 0; s < n; ++s)
            {
                buf[(size_t) wp] = data[s];
                data[s] = buf[(size_t) ((wp - d) & mask)];
                wp = (wp + 1) & mask;
            }
            compWrite[(size_t) o] = wp;
        }

        spectrumBus.outputTap (o).push (data, n);

        float peak = 0.0f;
        for (int s = 0; s < n; ++s)
            peak = juce::jmax (peak, std::abs (data[s]));
        const float prev = outputLevels[(size_t) o].load();
        outputLevels[(size_t) o].store (juce::jmax (peak, prev * 0.85f));
    }

    // Master gain (level/mute/dim), one smoother shared by all outputs.
    for (int s = 0; s < n; ++s)
    {
        const float g = smoothedMaster.getNextValue();
        for (int o = 0; o < numChannels; ++o)
            outScratch.getWritePointer (o)[s] *= g;
    }

    for (int o = 0; o < juce::jmin (numChannels, output.getNumChannels()); ++o)
        output.copyFrom (o, 0, outScratch, o, 0, n);
}

void MatrixEngine::processOutputChainOnly (juce::AudioBuffer<float>& buffer, int channel,
                                           int n, bool applyFir)
{
    if (! prepared || channel < 0 || channel >= buffer.getNumChannels())
        return;

    pullModelIfChanged();

    auto* data = buffer.getWritePointer (channel);
    const float g = juce::Decibels::decibelsToGain (outputSettings[(size_t) channel].gainDb);
    for (int s = 0; s < n; ++s)
        data[s] *= g;

    // Measuring "output + FIR" includes the per-output EQ (the speaker's own
    // processing); skip the alignment delay so the captured IR stays at t=0.
    if (applyFir)
        outputProc[(size_t) channel].process (data, n, false);

    if (applyFir && outputSettings[(size_t) channel].firOn
        && firs[(size_t) channel]->hasImpulse())
        firs[(size_t) channel]->process (data, n);
}

void MatrixEngine::updateFirFiles()
{
    for (int o = 0; o < numChannels; ++o)
    {
        const auto s = model.getOutput (o);
        if (s.firPath == loadedFirPaths[(size_t) o])
            continue;

        loadedFirPaths[(size_t) o] = s.firPath;

        if (s.firPath.isEmpty())
            firs[(size_t) o]->clearImpulse();
        else
            firs[(size_t) o]->loadFile (juce::File (s.firPath));
    }

    recomputeLatencyComp();     // loaded IRs changed the latencies
}

} // namespace smt
