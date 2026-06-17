/*
  ------------------------------------------------------------------------------
    SpectrumDisplay.h

    Reusable spectrum view: draws a log-frequency / dB grid and one stroked
    trace per registered tap, running the shared SpectrumAnalyzer on a GUI
    timer. Used both by the monitoring matrix analyzer (many taps) and the
    calibration mic analyzer (one tap). When an SPL calibration is supplied the
    vertical axis is relabelled in dB SPL (= dBFS + offset).

    Author: Olivier Doaré, github.com/odoare
    Licenced under the GNU LGPL Version 3.0
    SPDX-License-Identifier: LGPL-3.0-or-later
  ------------------------------------------------------------------------------
*/

#pragma once

#include <JuceHeader.h>
#include "../Dsp/SpectrumAnalyzer.h"
#include "../Dsp/SpectrumTap.h"
#include "../Theme.h"

class SpectrumDisplay : public juce::Component,
                        private juce::Timer
{
public:
    struct TraceConfig
    {
        smt::SpectrumTap* tap = nullptr;
        juce::Colour colour { juce::Colours::white };
        std::function<juce::String()> label;       // evaluated at paint time
        float thickness = 1.4f;
    };

    SpectrumDisplay()
    {
        analyzer.setFftSize (1 << fftOrder);
        startTimerHz (25);
    }

    /** Registers a tap to display. Traces draw only while their tap is enabled. */
    void addTrace (TraceConfig cfg)
    {
        traces.push_back ({ std::move (cfg), false, {} });
        traces.back().smoothedDb.fill (-120.0f);
    }

    void clearTraces() { traces.clear(); }

    void setDbRange (float lo, float hi)
    {
        minDb = defaultMinDb = lo;      // also the double-click "reset" view
        maxDb = defaultMaxDb = hi;
    }

    using Mode = smt::SpectrumAnalyzer::Mode;
    void setSpectrumMode (Mode m) { mode = m; repaint(); }
    Mode getSpectrumMode() const  { return mode; }

    /** When calibrated, the vertical axis is labelled in dB SPL = dBFS + offset
        (the plotted curves do not move, only the numbers). */
    void setSplCalibration (bool calibrated, float offsetDb)
    {
        splCalibrated = calibrated;
        splOffset = offsetDb;
        repaint();
    }

    std::function<double()> sampleRateProvider;

    void paint (juce::Graphics& g) override;
    void mouseDown (const juce::MouseEvent& e) override;
    void mouseDrag (const juce::MouseEvent& e) override;
    void mouseDoubleClick (const juce::MouseEvent& e) override;
    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override;

private:
    struct Trace
    {
        TraceConfig cfg;
        bool enabled;
        std::array<float, (size_t) smt::SpectrumAnalyzer::numPoints> smoothedDb;
    };

    void timerCallback() override;

    juce::Rectangle<float> getPlotArea() const
    {
        return getLocalBounds().toFloat().reduced (8.0f)
                   .withTrimmedLeft (18.0f).withTrimmedBottom (14.0f);
    }

    static float freqToX (float f, juce::Rectangle<float> r)
    {
        const float lo = smt::SpectrumAnalyzer::fMin, hi = smt::SpectrumAnalyzer::fMax;
        return r.getX() + r.getWidth() * std::log (f / lo) / std::log (hi / lo);
    }

    float dbToY (float db, juce::Rectangle<float> r) const
    {
        return juce::jmap (db, minDb, maxDb, r.getBottom(), r.getY());
    }

    // Clickable badges. The detector (avg/peak) stays bottom-right; the window
    // size and temporal-averaging controls sit bottom-left.
    juce::Rectangle<int> detectorBadgeBounds() const
    {
        auto plot = getPlotArea();
        return { (int) plot.getRight() - 50, (int) plot.getBottom() - 18, 46, 14 };
    }
    juce::Rectangle<int> fftBadgeBounds() const
    {
        auto plot = getPlotArea();
        return { (int) plot.getX() + 4, (int) plot.getBottom() - 18, 58, 14 };
    }
    juce::Rectangle<int> avgBadgeBounds() const
    {
        return fftBadgeBounds().translated (62, 0).withWidth (38);
    }
    juce::Rectangle<int> nBadgeBounds() const
    {
        return avgBadgeBounds().translated (42, 0).withWidth (40);
    }

    void drawBadge (juce::Graphics& g, juce::Rectangle<int> r,
                    const juce::String& text, bool active) const;

    bool overBadge (juce::Point<int> p) const
    {
        return detectorBadgeBounds().contains (p) || fftBadgeBounds().contains (p)
            || avgBadgeBounds().contains (p) || nBadgeBounds().contains (p);
    }

    void restartAveraging()
    {
        for (auto& tr : traces)
            tr.smoothedDb.fill (-120.0f);
    }

    // Apply a [min, min+span] dB window, clamped to fit within [floor, ceil].
    void setDbWindow (float newMin, float span)
    {
        span   = juce::jlimit (dbMinSpan, dbCeil - dbFloor, span);
        newMin = juce::jlimit (dbFloor, dbCeil - span, newMin);
        minDb = newMin;
        maxDb = newMin + span;
        repaint();
    }

    std::vector<Trace> traces;
    smt::SpectrumAnalyzer analyzer;

    float minDb = -100.0f, maxDb = 10.0f;
    float defaultMinDb = -100.0f, defaultMaxDb = 10.0f;
    bool  splCalibrated = false;
    float splOffset = 0.0f;
    Mode  mode = Mode::average;

    int  fftOrder = smt::spectrumFftOrder;      // window size = 1 << fftOrder
    bool avgOn = true;                          // temporal averaging
    int  nAvg  = 4;                             // averaged over ~nAvg frames

    // dB-axis zoom/pan (drag) state.
    static constexpr float dbFloor = -140.0f, dbCeil = 40.0f, dbMinSpan = 10.0f;
    bool dragging = false;
    float dragStartY = 0.0f, dragStartMin = -100.0f, dragStartMax = 10.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SpectrumDisplay)
};
