/*
  ------------------------------------------------------------------------------
    FirFilter.h

    Mono FIR convolver backed by the WDL convolution engine (zero latency),
    used for the per-output loudspeaker correction. The impulse response is
    loaded from a wav file on the message thread (with resampling to the
    session rate) and swapped under a short lock, following the FxmeFX Cab
    effect pattern.

    Author: Olivier Doaré, github.com/odoare
    Licenced under the GNU LGPL Version 3.0
    SPDX-License-Identifier: LGPL-3.0-or-later
  ------------------------------------------------------------------------------
*/

#pragma once

#include <JuceHeader.h>
#include "../../lib/FxmeFX/WDL/WDL/convoengine.h"

namespace smt
{

class FirFilter
{
public:
    FirFilter() = default;

    void prepare (double sampleRate, int maxBlockSize)
    {
        juce::ScopedLock sl (lock);
        currentSampleRate = sampleRate;
        wdlInput.resize ((size_t) maxBlockSize);
        engine.Reset();
        if (sourceIR.getNumSamples() > 0)
            rebuildEngineImpulse();
        else
            loadSilentImpulse();
    }

    /** Load an IR wav file. Returns true on success. Message thread. */
    bool loadFile (const juce::File& file)
    {
        juce::AudioFormatManager fm;
        fm.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> reader (fm.createReaderFor (file));
        if (reader == nullptr || reader->lengthInSamples <= 0)
            return false;

        const int n = (int) std::min (reader->lengthInSamples, (juce::int64) (1 << 20));
        juce::AudioBuffer<float> temp ((int) reader->numChannels, n);
        reader->read (&temp, 0, n, 0, true, true);

        // Collapse to mono
        juce::AudioBuffer<float> mono (1, n);
        mono.clear();
        for (int c = 0; c < temp.getNumChannels(); ++c)
            mono.addFrom (0, 0, temp, c, 0, n, 1.0f / (float) temp.getNumChannels());

        {
            juce::ScopedLock sl (lock);
            sourceIR = std::move (mono);
            sourceRate = reader->sampleRate;
            rebuildEngineImpulse();
        }
        return true;
    }

    /** Use an in-memory IR (e.g. just exported from the analysis part). */
    void setImpulse (const juce::AudioBuffer<float>& ir, double rate)
    {
        juce::ScopedLock sl (lock);
        sourceIR.makeCopyOf (ir);
        sourceRate = rate;
        rebuildEngineImpulse();
    }

    void clearImpulse()
    {
        juce::ScopedLock sl (lock);
        sourceIR.setSize (0, 0);
        loadSilentImpulse();
    }

    bool hasImpulse() const
    {
        juce::ScopedLock sl (lock);
        return sourceIR.getNumSamples() > 0;
    }

    int getImpulseLength() const
    {
        juce::ScopedLock sl (lock);
        return sourceIR.getNumSamples();
    }

    /** In-place convolution of one channel. Audio thread. */
    void process (float* data, int n)
    {
        juce::ScopedLock sl (lock);

        if (sourceIR.getNumSamples() == 0)
            return;     // bypass when no IR loaded

        if ((int) wdlInput.size() < n)
            return;     // should not happen (prepare sizes it)

        for (int i = 0; i < n; ++i)
            wdlInput[(size_t) i] = (WDL_FFT_REAL) data[i];

        WDL_FFT_REAL* in = wdlInput.data();
        engine.Add (&in, n, 1);

        const int avail = engine.Avail (n);
        WDL_FFT_REAL** out = engine.Get();
        const int toCopy = juce::jmin (avail, n);

        for (int i = 0; i < toCopy; ++i)
            data[i] = (float) out[0][i];
        for (int i = toCopy; i < n; ++i)
            data[i] = 0.0f;

        engine.Advance (toCopy);
    }

private:
    void rebuildEngineImpulse()
    {
        const int n = sourceIR.getNumSamples();
        if (n <= 0)
        {
            loadSilentImpulse();
            return;
        }

        // Resample to the session rate if needed.
        juce::AudioBuffer<float> resampled;
        const float* src = sourceIR.getReadPointer (0);
        int len = n;

        if (sourceRate > 0 && currentSampleRate > 0
            && std::abs (sourceRate - currentSampleRate) > 1.0)
        {
            const double ratio = sourceRate / currentSampleRate;
            const int outLen = (int) ((double) n / ratio) + 1;
            resampled.setSize (1, outLen);
            juce::LagrangeInterpolator interp;
            interp.process (ratio, src, resampled.getWritePointer (0), outLen);
            src = resampled.getReadPointer (0);
            len = outLen;
        }

        impulseBuffer.SetNumChannels (1);
        impulseBuffer.SetLength (len);
        impulseBuffer.samplerate = currentSampleRate;
        auto* dst = impulseBuffer.impulses[0].Get();
        for (int i = 0; i < len; ++i)
            dst[i] = (WDL_FFT_REAL) src[i];

        engine.SetImpulse (&impulseBuffer);
    }

    void loadSilentImpulse()
    {
        impulseBuffer.SetNumChannels (1);
        impulseBuffer.SetLength (1);
        impulseBuffer.samplerate = currentSampleRate;
        impulseBuffer.impulses[0].Get()[0] = 0.0;
        engine.SetImpulse (&impulseBuffer);
    }

    mutable juce::CriticalSection lock;
    WDL_ImpulseBuffer impulseBuffer;
    WDL_ConvolutionEngine_Div engine;
    std::vector<WDL_FFT_REAL> wdlInput;

    juce::AudioBuffer<float> sourceIR;   // as loaded (mono)
    double sourceRate = 0.0;
    double currentSampleRate = 44100.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FirFilter)
};

} // namespace smt
