/*
  ------------------------------------------------------------------------------
    SpectrumAnalyzerComponent.h

    Global spectrum analyzer of the monitoring part. Draws one trace per
    enabled tap of the engine's SpectrumBus: matrix frame signals (checkbox
    in each frame) and output sums (checkbox in each output strip). The FFT,
    windowing and exponential averaging run on the GUI timer; the audio
    thread only fills the tap ring buffers.

    Author: Olivier Doaré, github.com/odoare
    Licenced under the GNU LGPL Version 3.0
    SPDX-License-Identifier: LGPL-3.0-or-later
  ------------------------------------------------------------------------------
*/

#pragma once

#include <JuceHeader.h>
#include "../Dsp/MatrixEngine.h"
#include "../Theme.h"

class SpectrumAnalyzerComponent : public juce::Component,
                                  private juce::Timer
{
public:
    explicit SpectrumAnalyzerComponent (smt::MatrixEngine& e)
        : engine (e), fft (smt::spectrumFftOrder)
    {
        for (int i = 0; i < smt::spectrumFftSize; ++i)
            window[(size_t) i] = 0.5f * (1.0f - std::cos (
                2.0f * juce::MathConstants<float>::pi * (float) i
                / (float) (smt::spectrumFftSize - 1)));

        for (auto& t : traces)
            t.smoothedDb.fill (-120.0f);

        startTimerHz (25);
    }

    void paint (juce::Graphics& g) override
    {
        auto bounds = getLocalBounds().toFloat();
        g.setColour (juce::Colours::black);
        g.fillRoundedRectangle (bounds, 6.0f);

        const auto plot = getPlotArea();

        // Grid: decades and dB lines
        g.setFont (10.0f);
        for (float f : { 20.f, 50.f, 100.f, 200.f, 500.f, 1000.f, 2000.f, 5000.f, 10000.f, 20000.f })
        {
            const float x = freqToX (f, plot);
            g.setColour (juce::Colours::darkgrey.withAlpha (0.4f));
            g.drawVerticalLine ((int) x, plot.getY(), plot.getBottom());
            g.setColour (SuperMoToTheme::dimText);
            g.drawText (f >= 1000.0f ? juce::String (f / 1000.0f) + "k" : juce::String ((int) f),
                        (int) x - 14, (int) plot.getBottom() + 2, 28, 12,
                        juce::Justification::centred);
        }
        for (float db = 0.0f; db >= minDb; db -= 20.0f)
        {
            const float y = dbToY (db, plot);
            g.setColour (juce::Colours::darkgrey.withAlpha (db == 0.0f ? 0.8f : 0.4f));
            g.drawHorizontalLine ((int) y, plot.getX(), plot.getRight());
            g.setColour (SuperMoToTheme::dimText);
            g.drawText (juce::String ((int) db), 2, (int) y - 6, 22, 12,
                        juce::Justification::centredRight);
        }

        // Traces
        int legendX = (int) plot.getX() + 4;
        for (int t = 0; t < smt::numSpectrumTaps; ++t)
        {
            auto& trace = traces[(size_t) t];
            if (! trace.enabled)
                continue;

            const auto colour = traceColour (t);
            juce::Path path;
            bool started = false;
            for (int p = 0; p < numPoints; ++p)
            {
                const float f = pointFreq (p);
                const float x = freqToX (f, plot);
                const float y = dbToY (juce::jlimit (minDb, maxDb, trace.smoothedDb[(size_t) p]), plot);
                if (! started) { path.startNewSubPath (x, y); started = true; }
                else           path.lineTo (x, y);
            }
            g.setColour (colour);
            g.strokePath (path, juce::PathStrokeType (t < smt::numChannels ? 1.6f : 1.1f));

            // Legend chip
            g.setFont (11.0f);
            const auto label = traceLabel (t);
            const int w = 14 + (int) juce::GlyphArrangement::getStringWidth (juce::Font (11.0f), label);
            g.setColour (colour);
            g.fillRect (legendX, (int) plot.getY() + 3, 8, 8);
            g.setColour (SuperMoToTheme::text);
            g.drawText (label, legendX + 11, (int) plot.getY(), w, 14, juce::Justification::centredLeft);
            legendX += w + 10;
        }

        g.setColour (SuperMoToTheme::panelLine);
        g.drawRoundedRectangle (bounds.reduced (0.5f), 6.0f, 1.0f);
    }

    /** Resolves a trace's display label from the model routing. */
    std::function<juce::String (int frameSlot)> frameLabelProvider;

private:
    struct Trace
    {
        bool enabled = false;
        std::array<float, 512> smoothedDb;
    };

    static constexpr int numPoints = 512;
    static constexpr float minDb = -100.0f, maxDb = 10.0f;
    static constexpr float fMin = 20.0f, fMax = 20000.0f;

    juce::Rectangle<float> getPlotArea() const
    {
        return getLocalBounds().toFloat().reduced (8.0f).withTrimmedLeft (18.0f)
                               .withTrimmedBottom (14.0f);
    }

    static float pointFreq (int p)
    {
        return fMin * std::pow (fMax / fMin, (float) p / (float) (numPoints - 1));
    }

    static float freqToX (float f, juce::Rectangle<float> r)
    {
        return r.getX() + r.getWidth() * std::log (f / fMin) / std::log (fMax / fMin);
    }

    static float dbToY (float db, juce::Rectangle<float> r)
    {
        return juce::jmap (db, minDb, maxDb, r.getBottom(), r.getY());
    }

    juce::Colour traceColour (int tapIndex) const
    {
        if (tapIndex < smt::numChannels)
            return SuperMoToTheme::outputTraceColour (tapIndex);
        return SuperMoToTheme::frameTraceColour (tapIndex - smt::numChannels);
    }

    juce::String traceLabel (int tapIndex) const
    {
        if (tapIndex < smt::numChannels)
            return "Out " + juce::String (tapIndex + 1);
        const int slot = tapIndex - smt::numChannels;
        if (frameLabelProvider != nullptr)
            return frameLabelProvider (slot);
        return "Frame " + juce::String (slot + 1);
    }

    void timerCallback() override
    {
        auto& bus = engine.getSpectrumBus();
        bool any = false;

        for (int t = 0; t < smt::numSpectrumTaps; ++t)
        {
            auto& tap = bus.tap (t);
            auto& trace = traces[(size_t) t];
            const bool en = tap.isEnabled();

            if (en != trace.enabled)
            {
                trace.enabled = en;
                trace.smoothedDb.fill (-120.0f);
                any = true;
            }
            if (! en)
                continue;
            any = true;

            tap.snapshot (fftData.data());
            for (int i = 0; i < smt::spectrumFftSize; ++i)
                fftData[(size_t) i] *= window[(size_t) i];
            std::fill (fftData.begin() + smt::spectrumFftSize, fftData.end(), 0.0f);

            fft.performFrequencyOnlyForwardTransform (fftData.data());

            for (int p = 0; p < numPoints; ++p)
            {
                // Map display frequency to FFT bin assuming the session rate;
                // we read it from the processor via the engine-prepared rate.
                const float f = pointFreq (p);
                const float bin = f / binHz();
                const int b0 = juce::jlimit (1, smt::spectrumFftSize / 2 - 1, (int) bin);

                // Average the bins covered by this display point (at HF many
                // bins map to one pixel).
                const float bin1 = pointFreq (juce::jmin (p + 1, numPoints - 1)) / binHz();
                const int b1 = juce::jlimit (b0, smt::spectrumFftSize / 2 - 1, (int) bin1);
                float mag = 0.0f;
                for (int b = b0; b <= b1; ++b)
                    mag = juce::jmax (mag, fftData[(size_t) b]);

                const float db = juce::Decibels::gainToDecibels (
                    mag * 2.0f / (float) smt::spectrumFftSize, -120.0f);

                auto& s = trace.smoothedDb[(size_t) p];
                s = s * 0.7f + db * 0.3f;
            }
        }

        if (any)
            repaint();
    }

    float binHz() const
    {
        const double sr = sampleRateProvider != nullptr ? sampleRateProvider() : 0.0;
        return (float) ((sr > 0.0 ? sr : 48000.0) / (double) smt::spectrumFftSize);
    }

public:
    std::function<double()> sampleRateProvider;

private:
    smt::MatrixEngine& engine;
    juce::dsp::FFT fft;
    std::array<float, (size_t) smt::spectrumFftSize> window;
    std::array<float, (size_t) (2 * smt::spectrumFftSize)> fftData {};
    std::array<Trace, (size_t) smt::numSpectrumTaps> traces;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SpectrumAnalyzerComponent)
};
