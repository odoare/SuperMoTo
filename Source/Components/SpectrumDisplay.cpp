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
        analyzer.update (*tr.cfg.tap, tr.smoothedDb, sr, mode);
    }

    if (any)
        repaint();
}

void SpectrumDisplay::mouseDown (const juce::MouseEvent& e)
{
    if (modeBadgeBounds().contains (e.getPosition()))
    {
        mode = mode == Mode::peak ? Mode::average : Mode::peak;
        for (auto& tr : traces)
            tr.smoothedDb.fill (-120.0f);    // restart averaging after the switch
        repaint();
    }
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

    // Level grid: lines at 0 dBFS and below. Labels in dBFS, or in dB SPL when
    // a calibration has been entered (the curves keep their dBFS position).
    for (float db = 0.0f; db >= minDb; db -= 20.0f)
    {
        const float y = dbToY (db, plot);
        g.setColour (db == 0.0f ? SuperMoToTheme::gridZero : SuperMoToTheme::grid);
        g.drawHorizontalLine ((int) y, plot.getX(), plot.getRight());
        g.setColour (SuperMoToTheme::dimText);
        const int label = splCalibrated ? juce::roundToInt (db + splOffset) : (int) db;
        g.drawText (juce::String (label), 2, (int) y - 6, 22, 12, juce::Justification::centredRight);
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

    // Aggregation-mode badge (click to toggle avg / peak).
    {
        auto badge = modeBadgeBounds().toFloat();
        g.setColour (SuperMoToTheme::plotBackground.withAlpha (0.6f));
        g.fillRoundedRectangle (badge, 3.0f);
        g.setColour (SuperMoToTheme::panelLine);
        g.drawRoundedRectangle (badge, 3.0f, 1.0f);
        g.setColour (SuperMoToTheme::dimText);
        g.setFont (10.0f);
        g.drawText (mode == Mode::peak ? "peak" : "avg", badge, juce::Justification::centred);
    }

    g.setColour (SuperMoToTheme::panelLine);
    g.drawRoundedRectangle (bounds.reduced (0.5f), 6.0f, 1.0f);
}
