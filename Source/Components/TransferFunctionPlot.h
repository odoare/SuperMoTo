/*
  ------------------------------------------------------------------------------
    TransferFunctionPlot.h

    Magnitude + phase plot shared by the single-speaker Analysis pane and the
    multi-speaker Group analysis pane: measurement curves, their average, the
    designed correction and the corrected result, plus an optional subwoofer
    curve and crossover marker. Purely a display: the owner pushes data via
    setData() whenever the underlying analysis changes.

    Mouse: wheel zooms the dB axis around the cursor, drag pans it, double-click
    resets it. A read-out in the top-right corner shows frequency + level/phase
    at the cursor.

    Author: Olivier Doaré, github.com/odoare
    Licenced under the GNU LGPL Version 3.0
    SPDX-License-Identifier: LGPL-3.0-or-later
  ------------------------------------------------------------------------------
*/

#pragma once

#include <JuceHeader.h>
#include "../Theme.h"
#include <cmath>
#include <vector>

class TransferFunctionPlot : public juce::Component
{
public:
    struct Data
    {
        std::vector<std::vector<float>> curveDbs, curvePhases;
        std::vector<float> averageDb, correctionDb, correctedDb;
        std::vector<float> averagePhase, correctionPhase, correctedPhase;
        std::vector<float> subDb, subPhase;
        bool hasSub = false;
        float crossoverHz = 80.0f;
    };

    TransferFunctionPlot()
    {
        buildFreqGrid();
        setInterceptsMouseClicks (true, false);
    }

    void setData (Data d)
    {
        data = std::move (d);
        repaint();
    }

    //==========================================================================
    static constexpr float dbFloor = -200.0f, dbCeil = 200.0f, dbMinSpan = 5.0f;

    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override
    {
        auto m = magPanelArea();
        if (! m.contains (e.position))
            return;

        const float span  = plotMaxDb - plotMinDb;
        const float dbAtY  = juce::jmap (e.position.y, m.getBottom(), m.getY(), plotMinDb, plotMaxDb);
        const float factor = w.deltaY > 0.0f ? 0.85f : 1.0f / 0.85f;
        const float newSpan = juce::jlimit (dbMinSpan, dbCeil - dbFloor, span * factor);
        const float frac   = span > 0.0f ? (dbAtY - plotMinDb) / span : 0.5f;
        setDbWindow (dbAtY - frac * newSpan, newSpan);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        dragging = magPanelArea().contains (e.position);
        dragStartY = e.position.y;
        dragStartMin = plotMinDb;
        dragStartMax = plotMaxDb;
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (! dragging)
            return;
        const float span = dragStartMax - dragStartMin;
        const float dbPerPx = span / juce::jmax (1.0f, magPanelArea().getHeight());
        setDbWindow (dragStartMin + (e.position.y - dragStartY) * dbPerPx, span);
    }

    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        if (magPanelArea().contains (e.position))
        {
            plotMinDb = -30.0f;     // default view
            plotMaxDb = 30.0f;
            repaint();
        }
    }

    void mouseMove (const juce::MouseEvent& e) override
    {
        cursorPos = e.position;
        const bool in = magPanelArea().contains (e.position)
                     || phasePanelArea().contains (e.position);
        if (in != cursorInPlot || in)
        {
            cursorInPlot = in;
            repaint();
        }
    }

    void mouseExit (const juce::MouseEvent&) override
    {
        if (cursorInPlot) { cursorInPlot = false; repaint(); }
    }

    void paint (juce::Graphics& g) override
    {
        paintPlot (g, getLocalBounds().toFloat());
    }

private:
    static constexpr int numPoints = 400;
    static constexpr float fMin = 20.0f, fMax = 20000.0f;
    float plotMinDb = -30.0f, plotMaxDb = 30.0f;     // adjustable vertical limits

    void buildFreqGrid()
    {
        freqs.resize (numPoints);
        for (int p = 0; p < numPoints; ++p)
            freqs[(size_t) p] = fMin * std::pow (fMax / fMin, (float) p / (float) (numPoints - 1));
    }

    // Apply a [min, min+span] dB window, clamped to fit within [floor, ceil].
    void setDbWindow (float newMin, float span)
    {
        span   = juce::jlimit (dbMinSpan, dbCeil - dbFloor, span);
        newMin = juce::jlimit (dbFloor, dbCeil - span, newMin);
        plotMinDb = newMin;
        plotMaxDb = newMin + span;
        repaint();
    }

    float freqToX (float f, juce::Rectangle<float> r) const
    {
        return r.getX() + r.getWidth() * std::log (f / fMin) / std::log (fMax / fMin);
    }

    float dbToY (float db, juce::Rectangle<float> r) const
    {
        return juce::jmap (juce::jlimit (plotMinDb, plotMaxDb, db),
                           plotMinDb, plotMaxDb, r.getBottom(), r.getY());
    }

    void drawCurve (juce::Graphics& g, const std::vector<float>& dbs,
                    juce::Rectangle<float> r, juce::Colour colour, float thickness) const
    {
        if (dbs.size() != freqs.size())
            return;
        juce::Path path;
        for (size_t p = 0; p < freqs.size(); ++p)
        {
            const float x = freqToX (freqs[p], r);
            const float y = dbToY (dbs[p], r);
            if (p == 0) path.startNewSubPath (x, y);
            else        path.lineTo (x, y);
        }
        g.setColour (colour);
        g.strokePath (path, juce::PathStrokeType (thickness));
    }

    static float phaseToY (float deg, juce::Rectangle<float> r)
    {
        return juce::jmap (juce::jlimit (-180.0f, 180.0f, deg),
                           -180.0f, 180.0f, r.getBottom(), r.getY());
    }

    void drawPhaseCurve (juce::Graphics& g, const std::vector<float>& degs,
                         juce::Rectangle<float> r, juce::Colour colour, float thickness) const
    {
        if (degs.size() != freqs.size())
            return;
        juce::Path path;
        bool started = false;
        for (size_t p = 0; p < freqs.size(); ++p)
        {
            const float x = freqToX (freqs[p], r);
            const float y = phaseToY (degs[p], r);
            // Break the line on +/-180 degree wraps instead of drawing a
            // vertical jump across the panel.
            if (! started || (p > 0 && std::abs (degs[p] - degs[p - 1]) > 180.0f))
            {
                path.startNewSubPath (x, y);
                started = true;
            }
            else
                path.lineTo (x, y);
        }
        g.setColour (colour);
        g.strokePath (path, juce::PathStrokeType (thickness));
    }

    // Magnitude panel rectangle (mirrors the split done in paintPlot).
    juce::Rectangle<float> magPanelArea() const
    {
        auto inner = getLocalBounds().toFloat().reduced (24.0f, 12.0f);
        return inner.removeFromTop (inner.getHeight() * 0.62f);
    }

    // Phase panel rectangle (the lower part, mirroring paintPlot).
    juce::Rectangle<float> phasePanelArea() const
    {
        auto inner = getLocalBounds().toFloat().reduced (24.0f, 12.0f);
        inner.removeFromTop (inner.getHeight() * 0.62f);
        inner.removeFromTop (14.0f);
        return inner;
    }

    void paintPlot (juce::Graphics& g, juce::Rectangle<float> bounds) const
    {
        g.setColour (SuperMoToTheme::plotBackground);
        g.fillRoundedRectangle (bounds, 4.0f);

        auto inner = bounds.reduced (24.0f, 12.0f);
        auto magR = inner.removeFromTop (inner.getHeight() * 0.62f);
        inner.removeFromTop (14.0f);
        auto phR = inner;

        // Frequency grid spans both panels, labels under the phase panel.
        g.setFont (13.0f);
        for (float f : { 20.f, 50.f, 100.f, 200.f, 500.f, 1000.f, 2000.f, 5000.f, 10000.f, 20000.f })
        {
            const float x = freqToX (f, magR);
            g.setColour (SuperMoToTheme::grid);
            g.drawVerticalLine ((int) x, magR.getY(), magR.getBottom());
            g.drawVerticalLine ((int) x, phR.getY(), phR.getBottom());
            g.setColour (SuperMoToTheme::dimText);
            g.drawText (f >= 1000.0f ? juce::String (f / 1000.0f) + "k" : juce::String ((int) f),
                        (int) x - 18, (int) phR.getBottom() + 2, 36, 14, juce::Justification::centred);
        }

        // ── Magnitude panel ──────────────────────────────────────────────────
        for (float db = plotMinDb; db <= plotMaxDb; db += 10.0f)
        {
            const float y = dbToY (db, magR);
            g.setColour (db == 0.0f ? SuperMoToTheme::gridZero : SuperMoToTheme::grid);
            g.drawHorizontalLine ((int) y, magR.getX(), magR.getRight());
            g.setColour (SuperMoToTheme::dimText);
            g.drawText (juce::String ((int) db), (int) bounds.getX() + 1, (int) y - 7, 23, 14,
                        juce::Justification::centredRight);
        }

        // Crossover marker (where the main phase is steered onto the sub).
        if (data.hasSub)
        {
            const float xc = freqToX (juce::jlimit (fMin, fMax, data.crossoverHz), magR);
            g.setColour (SuperMoToTheme::mono.withAlpha (0.5f));
            g.drawVerticalLine ((int) xc, magR.getY(), magR.getBottom());
            g.drawVerticalLine ((int) xc, phR.getY(), phR.getBottom());
        }

        for (const auto& c : data.curveDbs)
            drawCurve (g, c, magR, SuperMoToTheme::curveMeasurement.withAlpha (0.55f), 1.0f);

        if (data.hasSub)
            drawCurve (g, data.subDb, magR, SuperMoToTheme::mono, 1.8f);
        drawCurve (g, data.averageDb, magR, SuperMoToTheme::curveAverage, 2.4f);
        drawCurve (g, data.correctionDb, magR, SuperMoToTheme::master, 1.6f);
        drawCurve (g, data.correctedDb, magR, SuperMoToTheme::spectrum, 1.6f);

        // ── Phase panel ──────────────────────────────────────────────────────
        for (float deg = -180.0f; deg <= 180.0f; deg += 90.0f)
        {
            const float y = phaseToY (deg, phR);
            g.setColour (deg == 0.0f ? SuperMoToTheme::gridZero : SuperMoToTheme::grid);
            g.drawHorizontalLine ((int) y, phR.getX(), phR.getRight());
            g.setColour (SuperMoToTheme::dimText);
            g.drawText (juce::String ((int) deg), (int) bounds.getX() + 1, (int) y - 7, 23, 14,
                        juce::Justification::centredRight);
        }

        for (const auto& c : data.curvePhases)
            drawPhaseCurve (g, c, phR, SuperMoToTheme::curveMeasurement.withAlpha (0.45f), 1.0f);

        if (data.hasSub)
            drawPhaseCurve (g, data.subPhase, phR, SuperMoToTheme::mono, 1.8f);
        drawPhaseCurve (g, data.averagePhase, phR, SuperMoToTheme::curveAverage, 2.0f);
        drawPhaseCurve (g, data.correctionPhase, phR, SuperMoToTheme::master, 1.4f);
        drawPhaseCurve (g, data.correctedPhase, phR, SuperMoToTheme::spectrum, 1.4f);

        // ── Legend & axis descriptions ───────────────────────────────────────
        struct Item { const char* name; juce::Colour col; };
        std::vector<Item> items { { "measurements", SuperMoToTheme::curveMeasurement },
                                  { "average", SuperMoToTheme::curveAverage },
                                  { "correction", SuperMoToTheme::master },
                                  { "corrected", SuperMoToTheme::spectrum } };
        if (data.hasSub)
            items.push_back ({ "sub", SuperMoToTheme::mono });
        int x = (int) magR.getX() + 6;
        g.setFont (11.0f);
        for (const auto& item : items)
        {
            g.setColour (item.col);
            g.fillRect (x, (int) magR.getY() + 4, 10, 3);
            g.setColour (SuperMoToTheme::text);
            const auto label = juce::String (item.name);
            const int w = (int) juce::GlyphArrangement::getStringWidth (juce::Font (11.0f), label) + 6;
            g.drawText (label, x + 13, (int) magR.getY() - 2, w, 14, juce::Justification::centredLeft);
            x += w + 26;
        }

        g.setColour (SuperMoToTheme::dimText);
        g.drawText (juce::String::fromUTF8 ("|H| (dB) \xe2\x80\x94 0 dB = 200 Hz\xe2\x80\x93"
                                            "2 kHz mean of the average; correction in absolute dB"),
                    (int) magR.getX(), (int) magR.getBottom() - 14,
                    (int) magR.getWidth() - 6, 12, juce::Justification::centredRight);
        g.drawText (juce::String::fromUTF8 ("phase (\xc2\xb0, propagation delay removed)"),
                    (int) phR.getX(), (int) phR.getY() + 2,
                    (int) phR.getWidth() - 6, 12, juce::Justification::centredRight);

        // Cursor read-out (top-right): frequency + level over the magnitude
        // panel, frequency + phase over the phase panel.
        if (cursorInPlot)
        {
            const bool inPhase = phR.contains (cursorPos);
            const auto& r = inPhase ? phR : magR;
            const float relX = juce::jlimit (0.0f, 1.0f, (cursorPos.x - magR.getX()) / magR.getWidth());
            const float f = fMin * std::exp (relX * std::log (fMax / fMin));
            const float cy = juce::jlimit (r.getY(), r.getBottom(), cursorPos.y);

            juce::String txt = (f >= 1000.0f ? juce::String (f / 1000.0f, 2) + " kHz"
                                             : juce::String (juce::roundToInt (f)) + " Hz") + "   ";
            if (inPhase)
                txt += juce::String (juce::roundToInt (
                           juce::jmap (cy, r.getBottom(), r.getY(), -180.0f, 180.0f)))
                       + juce::String::fromUTF8 ("\xc2\xb0");
            else
                txt += juce::String (juce::jmap (cy, r.getBottom(), r.getY(), plotMinDb, plotMaxDb), 1)
                       + " dB";

            g.setFont (11.0f);
            const int tw = (int) juce::GlyphArrangement::getStringWidth (juce::Font (11.0f), txt) + 12;
            juce::Rectangle<int> box ((int) magR.getRight() - tw, (int) magR.getY() - 2, tw, 15);
            g.setColour (SuperMoToTheme::plotBackground.withAlpha (0.8f));
            g.fillRoundedRectangle (box.toFloat(), 3.0f);
            g.setColour (SuperMoToTheme::panelLine);
            g.drawRoundedRectangle (box.toFloat(), 3.0f, 1.0f);
            g.setColour (SuperMoToTheme::text);
            g.drawText (txt, box, juce::Justification::centred);
        }
    }

    Data data;
    std::vector<float> freqs;

    // dB-axis zoom/pan state.
    bool dragging = false;
    float dragStartY = 0.0f, dragStartMin = -30.0f, dragStartMax = 30.0f;

    // Cursor read-out (frequency / level or phase at the pointer), top-right.
    juce::Point<float> cursorPos;
    bool cursorInPlot = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TransferFunctionPlot)
};
