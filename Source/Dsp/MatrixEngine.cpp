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

    // Visible/processed matrix size, read lock-free (atomics) so the copy below
    // knows which sub-range it needs.
    const int newIns = model.getNumIns();
    const int newOuts = model.getNumOuts();

    // Try-lock. A failure means the message thread is mid-edit (or mid-restore,
    // which holds the lock across a whole ValueTree parse). Leave
    // lastModelVersion alone and keep processing with the settings we already
    // have; the next block retries. Blocking here would be an xrun, and the
    // edit is a GUI action whose effect can be one buffer late.
    if (! model.tryCopyForEngine (newIns, newOuts, frameSettings, outputSettings))
        return;

    lastModelVersion = v;

    // Frames revealed by a grow are reset so stale delay-line content does not
    // play back. They are inside the new visible range, so the apply loop below
    // gives them their current settings in this same pull.
    if (newIns > visIns || newOuts > visOuts)
        for (int c = 0; c < numConfigs; ++c)
            for (int i = 0; i < newIns; ++i)
                for (int o = 0; o < newOuts; ++o)
                    if (i >= visIns || o >= visOuts)
                        frames[(size_t) c][(size_t) i][(size_t) o].reset();
    visIns = newIns;
    visOuts = newOuts;

    // Visible range only: frames outside it are never processed, and their
    // settings are not copied either (tryCopyForEngine leaves them stale on
    // purpose). 6 x 8 x 8 in the default matrix rather than 6 x 32 x 32.
    for (int c = 0; c < numConfigs; ++c)
        for (int i = 0; i < visIns; ++i)
            for (int o = 0; o < visOuts; ++o)
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

// Keeps every fed output time-aligned despite differing FIR latencies, while
// minimizing how much delay actually needs to be added anywhere.
//
// Each fed output o has a user-set manual delay (userDelay[o], the Delay
// control) and a FIR latency (firLat[o] = firOn ? the loaded IR's peak
// position : 0). Simply adding (lmax - firLat[o]) after the FIR — the
// previous approach — preserves alignment but ignores that an output with
// its OWN manual delay already budgeted can "self-fund" its own FIR's
// latency: up to min(userDelay[o], firLat[o]) samples of that manual delay
// are spent paying for the FIR's own bulk latency instead of being applied
// as an actual delay line (OutputProcessor::setFirLatencySamples), so the
// FIR's latency arrives "for free" as part of what the user already dialled
// in. Only the *unabsorbed* remainder (firLat[o] - userDelay[o], if positive)
// still needs compensating by delaying every fed output uniformly by that
// worst-case remainder k — exactly today's mechanism, just usually smaller
// (often zero). This never changes the RELATIVE alignment between outputs
// (still exactly userDelay[o] - userDelay[o'] apart), only the absolute
// latency needed to achieve it. An unfed output, or one whose FIR is not
// engaged by the current preset, is left alone.
void MatrixEngine::recomputeLatencyComp()
{
    const juce::uint32 fed = fedOutputsMask.load();
    auto isFed = [fed] (int o) { return (fed & (1u << o)) != 0; };
    auto firLatOf = [this] (int o)
    {
        return outputSettings[(size_t) o].firOn ? firs[(size_t) o]->getLatencySamples() : 0;
    };
    auto userDelayOf = [this] (int o)
    {
        return juce::roundToInt (outputSettings[(size_t) o].delayMs * 0.001 * sr);
    };

    int k = 0;
    for (int o = 0; o < numChannels; ++o)
        if (isFed (o))
            k = juce::jmax (k, firLatOf (o) - userDelayOf (o));

    for (int o = 0; o < numChannels; ++o)
    {
        if (! isFed (o))
        {
            compDelay[(size_t) o].store (0);
            selfAbsorbDelay[(size_t) o].store (0);
            outputProc[(size_t) o].setFirLatencySamples (0);
            continue;
        }

        const int firLat = firLatOf (o);
        const int userDelay = userDelayOf (o);
        const int absorbed = juce::jmin (userDelay, firLat);
        const int baseline = juce::jmax (userDelay, firLat);

        outputProc[(size_t) o].setFirLatencySamples (firLat);
        selfAbsorbDelay[(size_t) o].store (absorbed);
        compDelay[(size_t) o].store (juce::jlimit (0, compCap - 1, userDelay + k - baseline));
    }
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

    // Always consumed, even when configsChanged already forces a recompute
    // below, so a dirty flag never lingers for an extra, redundant block.
    const bool firLatencyDirty = latencyCompDirty.exchange (false);
    if (configsChanged)
    {
        computeFedMask();
        recomputeLatencyComp();
    }
    else if (firLatencyDirty)
    {
        // A message-thread updateFirFiles() flagged a possible FIR-latency
        // change; recomputeLatencyComp() mutates OutputProcessor, so it must
        // run here on the audio thread, not from the message thread directly.
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

void MatrixEngine::updateFirFiles (bool force)
{
    for (int o = 0; o < numChannels; ++o)
    {
        const auto s = model.getOutput (o);
        if (! force && s.firPath == loadedFirPaths[(size_t) o])
            continue;

        loadedFirPaths[(size_t) o] = s.firPath;

        if (s.firPath.isEmpty())
        {
            firs[(size_t) o]->clearImpulse();
            continue;
        }

        // The state's embedded copy of the impulse wins over the file: it is
        // what was chosen even if the preset/session comes from a machine
        // where the path is stale. The file is the pre-embedding fallback.
        bool loaded = false;
        if (embeddedIrProvider != nullptr)
            if (auto reader = embeddedIrProvider (o))
                loaded = firs[(size_t) o]->loadFromReader (*reader);

        if (! loaded && ! firs[(size_t) o]->loadFile (juce::File (s.firPath)))
            firs[(size_t) o]->clearImpulse();
    }

    // Loaded IRs may have changed the latencies, but recomputeLatencyComp()
    // mutates OutputProcessor (audio-thread-owned state) and this method runs
    // on the message thread — flag it instead and let process() pick it up.
    latencyCompDirty.store (true);
}

} // namespace smt
