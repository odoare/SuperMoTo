/*
  ------------------------------------------------------------------------------
    SplMeterComponent.h

    Vertical SPL meter display: a single bar fed a dBFS value, with two
    graduations either side. The left graduation is the dBFS scale (full
    scale, linear 1.0, sits at 0 dB at the top); the right graduation is the
    dB SPL scale, offset from dBFS by the reference the user calibrates in the
    calibration window (dB SPL = dBFS + offset).

    Pure display: it is fed setLevelDb()/setSplOffset() by the owner and runs
    its own gentle release ballistics so the bar does not flicker.

    Author: Olivier Doaré, github.com/odoare
    Licenced under the GNU LGPL Version 3.0
    SPDX-License-Identifier: LGPL-3.0-or-later
  ------------------------------------------------------------------------------
*/

#pragma once

#include <JuceHeader.h>
#include "../Theme.h"

class SplMeterComponent : public juce::Component,
                          private juce::Timer
{
public:
    SplMeterComponent()
    {
        startTimerHz (30);
    }

    /** Current measured level in dBFS (the bar target). */
    void setLevelDb (float dbFs)
    {
        targetDb = juce::jlimit (bottomDb, topDb, dbFs);
        rawDb = dbFs;
    }

    /** dB SPL = dBFS + offset. Pass hasOffset=false before calibration. */
    void setSplOffset (float offsetDb, bool hasOffset)
    {
        splOffset = offsetDb;
        calibrated = hasOffset;
    }

    void setRange (float bottom, float top)
    {
        bottomDb = bottom;
        topDb = top;
    }

    void paint (juce::Graphics& g) override
    {
        auto b = getLocalBounds().toFloat().reduced (2.0f);

        // Reserve the two label columns; the bar sits between them.
        const float leftW  = 34.0f;
        const float rightW = 52.0f;
        auto bar = b.withTrimmedLeft (leftW).withTrimmedRight (rightW).reduced (4.0f, 14.0f);

        const float barTop = bar.getY();
        const float barBot = bar.getBottom();

        // Track background.
        g.setColour (juce::Colours::black);
        g.fillRoundedRectangle (bar, 3.0f);
        g.setColour (SuperMoToTheme::panelLine);
        g.drawRoundedRectangle (bar, 3.0f, 1.0f);

        auto dbToY = [&] (float db)
        {
            return juce::jmap (juce::jlimit (bottomDb, topDb, db), bottomDb, topDb, barBot, barTop);
        };

        // Filled level with a green -> amber -> red gradient.
        const float y = dbToY (displayDb);
        juce::ColourGradient grad (SuperMoToTheme::spectrum, 0.0f, barBot,
                                   SuperMoToTheme::mute,     0.0f, barTop, false);
        grad.addColour (0.72, SuperMoToTheme::spectrum);
        grad.addColour (0.88, SuperMoToTheme::dim);
        g.setGradientFill (grad);
        g.fillRect (juce::Rectangle<float> (bar.getX(), y, bar.getWidth(), barBot - y));

        // Graduations: dBFS on the left, dB SPL on the right, shared ticks.
        g.setFont (11.0f);
        for (float db = topDb; db >= bottomDb - 0.1f; db -= 10.0f)
        {
            const float ty = dbToY (db);
            g.setColour (SuperMoToTheme::panelLine);
            g.drawHorizontalLine ((int) ty, bar.getX(), bar.getRight());

            g.setColour (SuperMoToTheme::dimText);
            g.drawText (juce::String ((int) db),
                        (int) b.getX(), (int) ty - 7, (int) leftW - 4, 14,
                        juce::Justification::centredRight);

            if (calibrated)
                g.drawText (juce::String (juce::roundToInt (db + splOffset)),
                            (int) bar.getRight() + 4, (int) ty - 7, (int) rightW - 6, 14,
                            juce::Justification::centredLeft);
        }

        // Column captions.
        g.setColour (SuperMoToTheme::dimText);
        g.setFont (juce::Font (10.0f, juce::Font::bold));
        g.drawText ("dBFS", (int) b.getX(), (int) barTop - 14, (int) leftW + 2, 12,
                    juce::Justification::centredLeft);
        g.drawText (calibrated ? "dB SPL" : "dB SPL (-)",
                    (int) bar.getRight() - 2, (int) barTop - 14, (int) rightW + 4, 12,
                    juce::Justification::centredRight);

        // Numeric readout under the bar.
        g.setColour (SuperMoToTheme::text);
        g.setFont (juce::Font (12.0f, juce::Font::bold));
        juce::String read = juce::String (rawDb <= -119.0f ? -120.0f : rawDb, 1) + " dBFS";
        if (calibrated && rawDb > -119.0f)
            read += "   " + juce::String (juce::roundToInt (rawDb + splOffset)) + " dB SPL";
        g.drawText (read, (int) b.getX(), (int) barBot + 2, (int) b.getWidth(), 14,
                    juce::Justification::centred);
    }

private:
    void timerCallback() override
    {
        // Fast attack, slow release for a readable bar.
        if (targetDb >= displayDb)
            displayDb = targetDb;
        else
            displayDb += (targetDb - displayDb) * 0.3f;

        repaint();
    }

    float bottomDb = -60.0f, topDb = 0.0f;
    float targetDb = -120.0f, displayDb = -120.0f, rawDb = -120.0f;
    float splOffset = 0.0f;
    bool  calibrated = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SplMeterComponent)
};
