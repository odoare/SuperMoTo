/*
  ------------------------------------------------------------------------------
    OutputMetersStrip.h

    A row of narrow vertical output level meters, one per output, for the mini
    (two-row) editor layout where there is no room for the matrix output strip.
    Pure display: it hit-tests (so hovering names the output and reads back its
    level and trim) but handles no mouse event, so a stray click on it does
    nothing rather than acting on an output the way the matrix strip would.

    The bars run bottom to top over meterLowDb..meterHighDb with a hairline at
    0 dB; anything above 0 dB is drawn in the clip colour. Levels come straight
    from MatrixEngine::getOutputLevelDb(), the same source the matrix strip
    uses, and the meter count follows ConfigModel::getNumOuts() — the owner
    should re-lay-out when that changes (see the editor's modelChanged()).

    Author: Olivier Doaré, github.com/odoare
    Licenced under the GNU LGPL Version 3.0
    SPDX-License-Identifier: LGPL-3.0-or-later
  ------------------------------------------------------------------------------
*/

#pragma once

#include <JuceHeader.h>
#include "../Model/ConfigModel.h"
#include "../Dsp/MatrixEngine.h"
#include "../Theme.h"

class OutputMetersStrip : public juce::Component,
                          public juce::TooltipClient,
                          private juce::Timer
{
public:
    OutputMetersStrip (smt::ConfigModel& m, smt::MatrixEngine& e)
        : model (m), engine (e)
    {
        startTimerHz (20);          // same refresh as the matrix vu-meters
        setOpaque (false);
        setInterceptsMouseClicks (true, false);   // hover tooltips only; no handlers
    }

    /** Width one meter occupies, bar plus the gap to its neighbour. */
    static constexpr int meterW = 7;

    /** Width this strip wants for n outputs. */
    static int widthFor (int numOuts)   { return juce::jmax (1, numOuts) * meterW; }

    void paint (juce::Graphics& g) override
    {
        const int numOuts = model.getNumOuts();
        if (numOuts <= 0)
            return;

        auto area = getLocalBounds().toFloat();
        g.setColour (SuperMoToTheme::plotBackground.withAlpha (0.6f));
        g.fillRoundedRectangle (area, 2.0f);

        // The bars share the full height; the 0 dB hairline is drawn across the
        // whole strip rather than per meter, so it reads as one scale.
        const auto bars = area.reduced (1.0f);
        const float zeroY = juce::jmap (0.0f, meterLowDb, meterHighDb,
                                        bars.getBottom(), bars.getY());

        const float cw = bars.getWidth() / (float) numOuts;
        for (int o = 0; o < numOuts; ++o)
        {
            const float lvl = juce::jlimit (meterLowDb, meterHighDb,
                                            engine.getOutputLevelDb (o));
            const float top = juce::jmap (lvl, meterLowDb, meterHighDb,
                                          bars.getBottom(), bars.getY());
            const juce::Rectangle<float> bar (bars.getX() + (float) o * cw + 1.0f,
                                              top,
                                              juce::jmax (1.0f, cw - 2.0f),
                                              bars.getBottom() - top);

            g.setColour (lvl > 0.0f ? SuperMoToTheme::meterClip : SuperMoToTheme::meterOk);
            g.fillRect (bar);
        }

        g.setColour (SuperMoToTheme::panelLine);
        g.drawHorizontalLine (juce::roundToInt (zeroY), bars.getX(), bars.getRight());
    }

    juce::String getTooltip() override
    {
        const int numOuts = model.getNumOuts();
        const auto pos = getMouseXYRelative();
        if (numOuts <= 0 || ! getLocalBounds().contains (pos))
            return {};

        const int o = juce::jlimit (0, numOuts - 1,
                                    (int) ((float) pos.x / (float) getWidth() * (float) numOuts));
        return "Output " + juce::String (o + 1)
             + ": " + juce::String (engine.getOutputLevelDb (o), 1) + " dB"
             + " (trim " + juce::String (model.getOutput (o).gainDb, 1) + " dB)";
    }

private:
    void timerCallback() override       { repaint(); }

    static constexpr float meterLowDb  = -60.0f;
    static constexpr float meterHighDb = 6.0f;

    smt::ConfigModel& model;
    smt::MatrixEngine& engine;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OutputMetersStrip)
};
