/*
  ------------------------------------------------------------------------------
    SpectrumDisplay.cpp

    Author: Olivier Doaré, github.com/odoare
    Licenced under the GNU LGPL Version 3.0
    SPDX-License-Identifier: LGPL-3.0-or-later
  ------------------------------------------------------------------------------
*/

#include "SpectrumDisplay.h"

void SpectrumDisplay::timerCallback()
{
    const double sr = sampleRateProvider != nullptr ? sampleRateProvider() : 0.0;
    bool any = false;

    for (auto& tr : traces)
    {
        const bool en = tr.cfg.tap != nullptr && tr.cfg.tap->isEnabled();
        if (en != tr.enabled)
        {
            tr.enabled = en;
            tr.smoothedDb.fill (-120.0f);
            any = true;
        }
        if (! en)
            continue;

        any = true;
        const float weight = avgOn ? 1.0f / (float) juce::jmax (1, nAvg) : 1.0f;
        analyzer.update (*tr.cfg.tap, tr.smoothedDb, sr, mode, weight);
    }

    if (any)
        repaint();
}

void SpectrumDisplay::mouseDown (const juce::MouseEvent& e)
{
    const auto p = e.getPosition();
    dragging = false;

    // Badges first; each click restarts the averaging.
    if (detectorBadgeBounds().contains (p))
    {
        mode = mode == Mode::peak ? Mode::average : Mode::peak;
        restartAveraging();
        repaint();
        return;
    }
    if (fftBadgeBounds().contains (p))
    {
        // Cycle the window size through the supported orders.
        fftOrder = fftOrder >= smt::spectrumMaxFftOrder ? smt::spectrumMinFftOrder : fftOrder + 1;
        analyzer.setFftSize (1 << fftOrder);
        restartAveraging();
        repaint();
        return;
    }
    if (avgBadgeBounds().contains (p))
    {
        avgOn = ! avgOn;
        restartAveraging();
        repaint();
        return;
    }
    if (nBadgeBounds().contains (p))
    {
        static const int opts[] = { 2, 4, 8, 16, 32 };
        int i = 0;
        while (i < 4 && opts[i] < nAvg) ++i;        // index of current (or next) value
        nAvg = opts[(i + 1) % 5];
        avgOn = true;                                // choosing N implies averaging on
        restartAveraging();
        repaint();
        return;
    }

    // Otherwise begin a dB-axis pan if the press is inside the plot.
    dragging = getPlotArea().contains (e.position);
    dragStartY = e.position.y;
    dragStartMin = minDb;
    dragStartMax = maxDb;
}

void SpectrumDisplay::mouseDrag (const juce::MouseEvent& e)
{
    if (! dragging)
        return;
    const float span = dragStartMax - dragStartMin;
    const float dbPerPx = span / juce::jmax (1.0f, getPlotArea().getHeight());
    setDbWindow (dragStartMin + (e.position.y - dragStartY) * dbPerPx, span);
}

void SpectrumDisplay::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (getPlotArea().contains (e.position) && ! overBadge (e.getPosition()))
    {
        minDb = defaultMinDb;
        maxDb = defaultMaxDb;
        repaint();
    }
}

void SpectrumDisplay::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    auto plot = getPlotArea();
    if (! plot.contains (e.position))
        return;

    const float span   = maxDb - minDb;
    const float dbAtY   = juce::jmap (e.position.y, plot.getBottom(), plot.getY(), minDb, maxDb);
    const float factor  = w.deltaY > 0.0f ? 0.85f : 1.0f / 0.85f;
    const float newSpan = juce::jlimit (dbMinSpan, dbCeil - dbFloor, span * factor);
    const float frac    = span > 0.0f ? (dbAtY - minDb) / span : 0.5f;
    setDbWindow (dbAtY - frac * newSpan, newSpan);
}

void SpectrumDisplay::drawBadge (juce::Graphics& g, juce::Rectangle<int> r,
                                 const juce::String& text, bool active) const
{
    auto b = r.toFloat();
    g.setColour (SuperMoToTheme::plotBackground.withAlpha (0.6f));
    g.fillRoundedRectangle (b, 3.0f);
    g.setColour (SuperMoToTheme::panelLine);
    g.drawRoundedRectangle (b, 3.0f, 1.0f);
    g.setColour (active ? SuperMoToTheme::text : SuperMoToTheme::dimText);
    g.setFont (10.0f);
    g.drawText (text, b, juce::Justification::centred);
}

void SpectrumDisplay::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();
    g.setColour (SuperMoToTheme::plotBackground);
    g.fillRoundedRectangle (bounds, 6.0f);

    const auto plot = getPlotArea();

    // Frequency grid (decades) with labels under the plot.
    g.setFont (10.0f);
    for (float f : { 20.f, 50.f, 100.f, 200.f, 500.f, 1000.f, 2000.f, 5000.f, 10000.f, 20000.f })
    {
        const float x = freqToX (f, plot);
        g.setColour (SuperMoToTheme::grid);
        g.drawVerticalLine ((int) x, plot.getY(), plot.getBottom());
        g.setColour (SuperMoToTheme::dimText);
        g.drawText (f >= 1000.0f ? juce::String (f / 1000.0f) + "k" : juce::String ((int) f),
                    (int) x - 14, (int) plot.getBottom() + 2, 28, 12, juce::Justification::centred);
    }

    // Level grid: a labelled line every 10 dB and a faint secondary line every
    // 5 dB. Labels in dBFS, or in dB SPL when a calibration has been entered
    // (the curves keep their dBFS position).
    const int firstDb = (int) std::ceil  (minDb / 5.0f) * 5;
    const int lastDb  = (int) std::floor (maxDb / 5.0f) * 5;
    for (int db = firstDb; db <= lastDb; db += 5)
    {
        const float y = dbToY ((float) db, plot);
        const bool labelled = (db % 10) == 0;

        g.setColour (labelled ? (db == 0 ? SuperMoToTheme::gridZero : SuperMoToTheme::grid)
                              : SuperMoToTheme::grid.withMultipliedAlpha (0.5f));
        g.drawHorizontalLine ((int) y, plot.getX(), plot.getRight());

        if (labelled)
        {
            g.setColour (SuperMoToTheme::dimText);
            const int label = splCalibrated ? juce::roundToInt ((float) db + splOffset) : db;
            g.drawText (juce::String (label), 2, (int) y - 6, 22, 12, juce::Justification::centredRight);
        }
    }

    if (splCalibrated)
    {
        g.setColour (SuperMoToTheme::dimText);
        g.setFont (juce::Font (10.0f, juce::Font::bold));
        g.drawText ("dB SPL", (int) plot.getRight() - 62, (int) plot.getY() + 2, 60, 12,
                    juce::Justification::centredRight);
    }

    // Traces + legend.
    int legendX = (int) plot.getX() + 4;
    for (auto& tr : traces)
    {
        if (! tr.enabled)
            continue;

        juce::Path path;
        bool started = false;
        for (int p = 0; p < smt::SpectrumAnalyzer::numPoints; ++p)
        {
            const float x = freqToX (smt::SpectrumAnalyzer::pointFreq (p), plot);
            const float y = dbToY (juce::jlimit (minDb, maxDb, tr.smoothedDb[(size_t) p]), plot);
            if (! started) { path.startNewSubPath (x, y); started = true; }
            else           path.lineTo (x, y);
        }
        g.setColour (tr.cfg.colour);
        g.strokePath (path, juce::PathStrokeType (tr.cfg.thickness));

        const auto label = tr.cfg.label != nullptr ? tr.cfg.label() : juce::String();
        if (label.isNotEmpty())
        {
            g.setFont (11.0f);
            const int w = 14 + (int) juce::GlyphArrangement::getStringWidth (juce::Font (11.0f), label);
            g.setColour (tr.cfg.colour);
            g.fillRect (legendX, (int) plot.getY() + 3, 8, 8);
            g.setColour (SuperMoToTheme::text);
            g.drawText (label, legendX + 11, (int) plot.getY(), w, 14, juce::Justification::centredLeft);
            legendX += w + 10;
        }
    }

    // Clickable badges: window size + temporal averaging (bottom-left),
    // per-point detector avg/peak (bottom-right).
    drawBadge (g, fftBadgeBounds(), "fft " + juce::String (analyzer.getFftSize()), true);
    drawBadge (g, avgBadgeBounds(), "avg", avgOn);
    drawBadge (g, nBadgeBounds(),   "N " + juce::String (nAvg), avgOn);
    drawBadge (g, detectorBadgeBounds(), mode == Mode::peak ? "peak" : "avg", true);

    g.setColour (SuperMoToTheme::panelLine);
    g.drawRoundedRectangle (bounds.reduced (0.5f), 6.0f, 1.0f);
}
