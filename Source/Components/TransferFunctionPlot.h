/*
  ------------------------------------------------------------------------------
    TransferFunctionPlot.h

    Magnitude + phase plot shared by the single-speaker Analysis pane and the
    multi-speaker Group analysis pane: measurement curves, their average, the
    designed correction and the corrected result, plus an optional subwoofer
    curve and crossover marker. Purely a display: the owner pushes data via
    setData() whenever the underlying analysis changes.

    Mouse: wheel zooms the dB axis around the cursor, ctrl+wheel zooms the
    frequency axis around it, drag pans both, double-click resets both. The
    legend is clickable: a click on an entry hides/shows that curve family.
    A read-out in the top-right corner shows frequency + level/phase at the
    cursor.

    The owner may set onViewChanged to be told when the frequency window
    moves, so it can re-sample its curves over the new range (see
    getViewLowHz / getViewHighHz) and keep full resolution when zoomed in.

    Author: Olivier Doaré, github.com/odoare
    Licenced under the GNU AGPL Version 3.0, or commercial terms (LICENSE.md)
    SPDX-License-Identifier: AGPL-3.0-or-later OR LicenseRef-FXME-Commercial
  ------------------------------------------------------------------------------
*/

#pragma once

#include <JuceHeader.h>
#include "../Theme.h"
#include <array>
#include <cmath>
#include <limits>
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

        /** Harmonic-distortion magnitude curves (sweep analysis): index i is
            order i + 2, magnitude panel only, legend "H2", "H3", ... */
        std::vector<std::vector<float>> harmonicDbs;
        bool hasSub = false;
        float crossoverHz = 80.0f;

        /** Display offset (dB) added to the MEASURED-family curves (curves,
            average, corrected, sub) — the correction stays absolute dB. The
            owner uses it to switch between the engine's normalized view and
            absolute / dB SPL level references without touching the engine. */
        float measuredOffsetDb = 0.0f;

        /** Replaces the magnitude panel's default axis description ("0 dB =
            mid-band mean...") when non-empty — set it whenever
            measuredOffsetDb changes the meaning of the axis. */
        juce::String levelAxisText;
    };

    TransferFunctionPlot()
    {
        buildFreqGrid();
        setInterceptsMouseClicks (true, false);
    }

    void setData (Data d)
    {
        data = std::move (d);
        // Keep the per-harmonic legend toggles, growing/shrinking with the set.
        harmonicVisible.resize (data.harmonicDbs.size(), true);
        repaint();
    }

    //==========================================================================
    // Frequency window (log axis). The owner samples its curves over
    // [getViewLowHz(), getViewHighHz()] and is notified via onViewChanged
    // whenever the user zooms or pans, so resolution follows the zoom.

    static constexpr float fullFMin = 20.0f, fullFMax = 20000.0f;

    float getViewLowHz() const noexcept     { return viewFMin; }
    float getViewHighHz() const noexcept    { return viewFMax; }

    /** Called after the frequency window changed (not on construction). */
    std::function<void()> onViewChanged;

    /** Freezes zoom/pan/legend interaction. The owner disables it while it
        cannot honour onViewChanged (e.g. a background batch owns the data),
        so the axis can never move away from the data currently plotted. */
    void setInteractionEnabled (bool shouldBeEnabled) { interactive = shouldBeEnabled; }

    void setFreqWindow (float lo, float hi)
    {
        // Keep at least a tenth of a decade visible, inside the full range.
        hi = juce::jlimit (fullFMin * 1.26f, fullFMax, hi);
        lo = juce::jlimit (fullFMin, hi / 1.26f, lo);
        if (lo == viewFMin && hi == viewFMax)
            return;
        viewFMin = lo;
        viewFMax = hi;
        buildFreqGrid();
        repaint();
        if (onViewChanged != nullptr)
            onViewChanged();
    }

    /** Fits the vertical dB window to the current data (in display units,
        i.e. including Data::measuredOffsetDb), padded out to 10 dB steps,
        and makes the result the new double-click default view. Falls back
        to -30..+30 dB with no data. Call after switching level references,
        where the sensible window jumps (e.g. 0-centred -> ~85 dB SPL).
        Harmonic curves are deliberately excluded from the fit: they sit tens
        of dB below the fundamental and would stretch the window. */
    void fitVerticalToData()
    {
        float lo = std::numeric_limits<float>::max();
        float hi = std::numeric_limits<float>::lowest();
        auto scan = [&] (const std::vector<float>& dbs, float off)
        {
            for (float d : dbs)
                if (std::isfinite (d) && d > -119.0f)   // skip the engine's floor
                {
                    lo = std::min (lo, d + off);
                    hi = std::max (hi, d + off);
                }
        };
        const float off = data.measuredOffsetDb;
        for (const auto& c : data.curveDbs)
            scan (c, off);
        scan (data.averageDb,    off);
        scan (data.correctedDb,  off);
        scan (data.correctionDb, 0.0f);
        if (data.hasSub)
            scan (data.subDb, off);

        if (lo > hi)
        {
            lo = -30.0f;
            hi = 30.0f;
        }
        else
        {
            lo = 10.0f * std::floor ((lo - 3.0f) / 10.0f);
            hi = 10.0f * std::ceil  ((hi + 3.0f) / 10.0f);
        }
        lo = juce::jlimit (dbFloor, dbCeil - dbMinSpan, lo);
        hi = juce::jlimit (lo + dbMinSpan, dbCeil, hi);
        defaultMinDb = lo;
        defaultMaxDb = hi;
        setDbWindow (lo, hi - lo);
    }

    //==========================================================================
    static constexpr float dbFloor = -200.0f, dbCeil = 200.0f, dbMinSpan = 5.0f;

    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override
    {
        const auto m = magPanelArea();
        const bool inPlot = m.contains (e.position) || phasePanelArea().contains (e.position);
        if (! inPlot || ! interactive)
            return;

        const float factor = w.deltaY > 0.0f ? 0.85f : 1.0f / 0.85f;

        // Ctrl (or cmd): zoom the shared frequency axis around the cursor.
        if (e.mods.isCtrlDown() || e.mods.isCommandDown())
        {
            const float fAtX = xToFreq (e.position.x, m);
            const float ratio = viewFMax / viewFMin;
            const float newRatio = std::pow (ratio, factor);
            // Keep the frequency under the cursor pinned.
            const float rel = std::log (fAtX / viewFMin) / std::log (ratio);
            const float newLo = fAtX / std::pow (newRatio, rel);
            setFreqWindow (newLo, newLo * newRatio);
            return;
        }

        // Otherwise the dB axis (magnitude panel only — the phase axis is fixed).
        if (! m.contains (e.position))
            return;

        const float span  = plotMaxDb - plotMinDb;
        const float dbAtY  = juce::jmap (e.position.y, m.getBottom(), m.getY(), plotMinDb, plotMaxDb);
        const float newSpan = juce::jlimit (dbMinSpan, dbCeil - dbFloor, span * factor);
        const float frac   = span > 0.0f ? (dbAtY - plotMinDb) / span : 0.5f;
        setDbWindow (dbAtY - frac * newSpan, newSpan);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        dragging = false;
        if (! interactive)
            return;

        // A click on a legend entry toggles that curve family.
        for (const auto& hit : legendHits)
            if (hit.first.contains (e.getPosition()))
            {
                toggleLegendItem (hit.second);
                dragging = false;
                return;
            }

        dragging = magPanelArea().contains (e.position)
                || phasePanelArea().contains (e.position);
        dragVertical = magPanelArea().contains (e.position);
        dragStartX = e.position.x;
        dragStartY = e.position.y;
        dragStartMin = plotMinDb;
        dragStartMax = plotMaxDb;
        dragStartFMin = viewFMin;
        dragStartFMax = viewFMax;
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (! dragging)
            return;

        // Horizontal: pan the (log) frequency window by the dragged decades.
        const auto m = magPanelArea();
        const float ratio = dragStartFMax / dragStartFMin;
        const float decades = std::log (ratio)
                            * (dragStartX - e.position.x) / juce::jmax (1.0f, m.getWidth());
        const float shift = std::exp (decades);
        setFreqWindow (dragStartFMin * shift, dragStartFMax * shift);

        // Vertical: pan the dB window (only when the drag began over it).
        if (dragVertical)
        {
            const float span = dragStartMax - dragStartMin;
            const float dbPerPx = span / juce::jmax (1.0f, m.getHeight());
            setDbWindow (dragStartMin + (e.position.y - dragStartY) * dbPerPx, span);
        }
    }

    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        if (interactive
            && (magPanelArea().contains (e.position) || phasePanelArea().contains (e.position)))
        {
            plotMinDb = defaultMinDb;   // default view (fitVerticalToData
            plotMaxDb = defaultMaxDb;   // updates it per level reference)
            setFreqWindow (fullFMin, fullFMax);
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
    float viewFMin = fullFMin, viewFMax = fullFMax;  // adjustable frequency window
    float plotMinDb = -30.0f, plotMaxDb = 30.0f;     // adjustable vertical limits
    float defaultMinDb = -30.0f, defaultMaxDb = 30.0f;   // double-click view

    // The sampling grid the curves are expected to be on: numPoints points,
    // log-spaced across the current frequency window. Owners build the same
    // grid (freqGridFor) when onViewChanged asks them to re-sample.
    void buildFreqGrid()
    {
        freqs = freqGridFor (viewFMin, viewFMax);
    }

public:
    /** The frequency grid the plot expects setData()'s curves to be sampled
        on for a given window — use freqGridFor (getViewLowHz(), getViewHighHz()). */
    static std::vector<float> freqGridFor (float lo, float hi)
    {
        std::vector<float> f ((size_t) numPoints);
        for (int p = 0; p < numPoints; ++p)
            f[(size_t) p] = lo * std::pow (hi / lo, (float) p / (float) (numPoints - 1));
        return f;
    }

private:

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
        return r.getX() + r.getWidth() * std::log (f / viewFMin) / std::log (viewFMax / viewFMin);
    }

    float xToFreq (float x, juce::Rectangle<float> r) const
    {
        const float rel = juce::jlimit (0.0f, 1.0f, (x - r.getX()) / juce::jmax (1.0f, r.getWidth()));
        return viewFMin * std::exp (rel * std::log (viewFMax / viewFMin));
    }

    float dbToY (float db, juce::Rectangle<float> r) const
    {
        return juce::jmap (juce::jlimit (plotMinDb, plotMaxDb, db),
                           plotMinDb, plotMaxDb, r.getBottom(), r.getY());
    }

    void drawCurve (juce::Graphics& g, const std::vector<float>& dbs,
                    juce::Rectangle<float> r, juce::Colour colour, float thickness,
                    float offsetDb = 0.0f) const
    {
        if (dbs.size() != freqs.size())
            return;
        juce::Path path;
        for (size_t p = 0; p < freqs.size(); ++p)
        {
            const float x = freqToX (freqs[p], r);
            const float y = dbToY (dbs[p] + offsetDb, r);
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

    // Legend entries that can be toggled off by clicking them. Curve families
    // have fixed ids; harmonics follow, one id per order.
    enum Family { famMeasurements = 0, famAverage, famCorrection, famCorrected,
                  famSub, numFamilies };

    bool familyOn (int f) const     { return familyVisible[(size_t) f]; }

    bool harmonicOn (size_t i) const
    {
        return i >= harmonicVisible.size() || harmonicVisible[i];
    }

    void toggleLegendItem (int id)
    {
        if (id < numFamilies)
            familyVisible[(size_t) id] = ! familyVisible[(size_t) id];
        else
        {
            const size_t h = (size_t) (id - numFamilies);
            if (h < harmonicVisible.size())
                harmonicVisible[h] = ! harmonicVisible[h];
        }
        repaint();
    }

    // A round dB gridline step (1-2-5-10...) giving roughly 6 divisions.
    static float niceDbStep (float span)
    {
        const double raw = juce::jmax (1.0e-3, (double) span / 6.0);
        const double mag = std::pow (10.0, std::floor (std::log10 (raw)));
        const double n = raw / mag;
        return (float) ((n <= 1.0 ? 1.0 : n <= 2.0 ? 2.0 : n <= 5.0 ? 5.0 : 10.0) * mag);
    }

    // 1-2-5 frequency ticks across the current window (so zoomed-in views keep
    // labelled gridlines instead of the fixed full-range set).
    std::vector<float> freqTicks() const
    {
        std::vector<float> t;
        const double firstDec = std::floor (std::log10 ((double) viewFMin));
        const double lastDec  = std::log10 ((double) viewFMax);
        for (double dec = firstDec; dec <= lastDec + 1.0; dec += 1.0)
            for (double m : { 1.0, 2.0, 5.0 })
            {
                const double f = m * std::pow (10.0, dec);
                if (f >= (double) viewFMin * 0.999 && f <= (double) viewFMax * 1.001)
                    t.push_back ((float) f);
            }
        return t;
    }

    // Harmonic-distortion trace palette (H2, H3, ...).
    static juce::Colour harmonicColour (int idx)
    {
        static const juce::Colour cols[] = { SuperMoToTheme::measure,
                                             SuperMoToTheme::dim,
                                             juce::Colour (0xffb070e0),
                                             juce::Colour (0xff70b0e0) };
        return cols[idx % 4];
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
        for (float f : freqTicks())
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
        // Gridlines on round dB values, at a step that keeps ~6 divisions
        // whatever the zoom (so a zoomed-in axis is not labelled -27, -17, ...).
        const float dbStep = niceDbStep (plotMaxDb - plotMinDb);
        for (float db = std::ceil (plotMinDb / dbStep) * dbStep; db <= plotMaxDb; db += dbStep)
        {
            const float y = dbToY (db, magR);
            g.setColour (std::abs (db) < dbStep * 0.5f ? SuperMoToTheme::gridZero
                                                       : SuperMoToTheme::grid);
            g.drawHorizontalLine ((int) y, magR.getX(), magR.getRight());
            g.setColour (SuperMoToTheme::dimText);
            g.drawText (dbStep < 1.0f ? juce::String (db, 1) : juce::String ((int) db),
                        (int) bounds.getX() + 1, (int) y - 7, 23, 14,
                        juce::Justification::centredRight);
        }

        // Crossover marker (where the main phase is steered onto the sub).
        if (data.hasSub)
        {
            const float xc = freqToX (juce::jlimit (viewFMin, viewFMax, data.crossoverHz), magR);
            g.setColour (SuperMoToTheme::mono.withAlpha (0.5f));
            g.drawVerticalLine ((int) xc, magR.getY(), magR.getBottom());
            g.drawVerticalLine ((int) xc, phR.getY(), phR.getBottom());
        }

        // Measured-family curves carry the level-reference offset; the
        // correction is a filter gain and stays absolute dB. Clipped to the
        // panel: when zoomed in, out-of-window points must not paint over the
        // axis labels or the legend.
        const float off = data.measuredOffsetDb;
        {
            juce::Graphics::ScopedSaveState clipState (g);
            g.reduceClipRegion (magR.toNearestInt());

            if (familyOn (famMeasurements))
                for (const auto& c : data.curveDbs)
                    drawCurve (g, c, magR, SuperMoToTheme::curveMeasurement.withAlpha (0.55f), 1.0f, off);

            for (size_t hIdx = 0; hIdx < data.harmonicDbs.size(); ++hIdx)
                if (harmonicOn (hIdx))
                    drawCurve (g, data.harmonicDbs[hIdx], magR, harmonicColour ((int) hIdx), 1.2f, off);

            if (data.hasSub && familyOn (famSub))
                drawCurve (g, data.subDb, magR, SuperMoToTheme::mono, 1.8f, off);
            if (familyOn (famAverage))
                drawCurve (g, data.averageDb, magR, SuperMoToTheme::curveAverage, 2.4f, off);
            if (familyOn (famCorrection))
                drawCurve (g, data.correctionDb, magR, SuperMoToTheme::master, 1.6f);
            if (familyOn (famCorrected))
                drawCurve (g, data.correctedDb, magR, SuperMoToTheme::spectrum, 1.6f, off);
        }

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

        {
            juce::Graphics::ScopedSaveState clipState (g);
            g.reduceClipRegion (phR.toNearestInt());

            if (familyOn (famMeasurements))
                for (const auto& c : data.curvePhases)
                    drawPhaseCurve (g, c, phR, SuperMoToTheme::curveMeasurement.withAlpha (0.45f), 1.0f);

            if (data.hasSub && familyOn (famSub))
                drawPhaseCurve (g, data.subPhase, phR, SuperMoToTheme::mono, 1.8f);
            if (familyOn (famAverage))
                drawPhaseCurve (g, data.averagePhase, phR, SuperMoToTheme::curveAverage, 2.0f);
            if (familyOn (famCorrection))
                drawPhaseCurve (g, data.correctionPhase, phR, SuperMoToTheme::master, 1.4f);
            if (familyOn (famCorrected))
                drawPhaseCurve (g, data.correctedPhase, phR, SuperMoToTheme::spectrum, 1.4f);
        }

        // ── Legend & axis descriptions ───────────────────────────────────────
        // Each entry is clickable (mouseDown scans legendHits): its swatch and
        // label dim when the family is hidden.
        struct Item { juce::String name; juce::Colour col; int id; bool on; };
        std::vector<Item> items {
            { "measurements", SuperMoToTheme::curveMeasurement, famMeasurements, familyOn (famMeasurements) },
            { "average",      SuperMoToTheme::curveAverage,     famAverage,      familyOn (famAverage) },
            { "correction",   SuperMoToTheme::master,           famCorrection,   familyOn (famCorrection) },
            { "corrected",    SuperMoToTheme::spectrum,         famCorrected,    familyOn (famCorrected) } };
        if (data.hasSub)
            items.push_back ({ "sub", SuperMoToTheme::mono, famSub, familyOn (famSub) });
        for (size_t hIdx = 0; hIdx < data.harmonicDbs.size(); ++hIdx)
            items.push_back ({ "H" + juce::String ((int) hIdx + 2), harmonicColour ((int) hIdx),
                               numFamilies + (int) hIdx, harmonicOn (hIdx) });

        legendHits.clear();
        int x = (int) magR.getX() + 6;

        // Built once per repaint rather than once per legend entry. Kept separate
        // from g.setFont(11.0f) below, which sets only the height and so keeps
        // whatever style the Graphics already carries — measuring with a plain
        // 11 px font is what this code has always done.
        const juce::Font legendFont { juce::FontOptions (11.0f) };

        g.setFont (11.0f);
        for (const auto& item : items)
        {
            const int w = (int) juce::GlyphArrangement::getStringWidth (legendFont, item.name) + 6;
            g.setColour (item.on ? item.col : item.col.withAlpha (0.3f));
            g.fillRect (x, (int) magR.getY() + 4, 10, 3);
            g.setColour (item.on ? SuperMoToTheme::text : SuperMoToTheme::dimText.withAlpha (0.6f));
            g.drawText (item.name, x + 13, (int) magR.getY() - 2, w, 14, juce::Justification::centredLeft);
            legendHits.push_back ({ { x - 3, (int) magR.getY() - 2, w + 18, 14 }, item.id });
            x += w + 26;
        }

        g.setColour (SuperMoToTheme::dimText);
        g.drawText (data.levelAxisText.isNotEmpty()
                        ? data.levelAxisText
                        : juce::String::fromUTF8 ("|H| (dB) \xe2\x80\x94 0 dB = 200 Hz\xe2\x80\x93"
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
            const float f = xToFreq (cursorPos.x, magR);
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
            const int tw = (int) juce::GlyphArrangement::getStringWidth (
                               juce::Font (juce::FontOptions (11.0f)), txt) + 12;
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

    // Legend visibility (clickable entries) + the hit rects paint() lays out.
    std::array<bool, (size_t) numFamilies> familyVisible { { true, true, true, true, true } };
    std::vector<bool> harmonicVisible;
    mutable std::vector<std::pair<juce::Rectangle<int>, int>> legendHits;

    // Zoom/pan state (dB axis, and the shared frequency axis).
    bool interactive = true;
    bool dragging = false, dragVertical = false;
    float dragStartY = 0.0f, dragStartMin = -30.0f, dragStartMax = 30.0f;
    float dragStartX = 0.0f, dragStartFMin = fullFMin, dragStartFMax = fullFMax;

    // Cursor read-out (frequency / level or phase at the pointer), top-right.
    juce::Point<float> cursorPos;
    bool cursorInPlot = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TransferFunctionPlot)
};
