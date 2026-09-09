/*
  ------------------------------------------------------------------------------
    BandEqEditor.h

    A small, self-contained cascaded-band EQ editor: for each band, an on/off
    toggle (numbered 1..N), a type (Lowpass / Highpass / Bandpass / Band) and
    order (2nd / 4th, hidden for Band/peaking) combo, and frequency / Q / gain
    knobs (gain only shown for the peaking type). Bands are laid out in pairs,
    top to bottom. The compact knobs use the FxmeLookAndFeel value-in-centre +
    label display (right-click a knob to type a value).

    Used identically by OutputEditorComponent (2-band output EQ) and
    FrameEditorComponent (2-band per-crosspoint EQ) so the band-editing UI
    exists in exactly one place; the band count is fixed at construction time.

    Author: Olivier Doaré, github.com/odoare
    Licenced under the GNU LGPL Version 3.0
    SPDX-License-Identifier: LGPL-3.0-or-later
  ------------------------------------------------------------------------------
*/

#pragma once

#include <JuceHeader.h>
#include "../Model/ConfigModel.h"
#include "../Theme.h"
#include "../Tooltips.h"

class BandEqEditor : public juce::Component
{
public:
    BandEqEditor (int numBandsIn, juce::Colour accentColour)
        : numBands (juce::jlimit (1, maxBandsSupported, numBandsIn)), accent (accentColour)
    {
        for (int bi = 0; bi < numBands; ++bi)
        {
            bandOn[bi].setButtonText (juce::String (bi + 1));
            SuperMoToTheme::accentToggleButton (bandOn[bi], accent);
            bandOn[bi].setTooltip (smt::tips::mtx::bandOn);
            bandOn[bi].onClick = [this] { fireOnChange(); };
            addAndMakeVisible (bandOn[bi]);

            bandType[bi].addItemList ({ "Lowpass", "Highpass", "Bandpass", "Band" }, 1);
            SuperMoToTheme::accentComboBox (bandType[bi], accent);
            bandType[bi].setTooltip (smt::tips::mtx::bandType);
            bandType[bi].onChange = [this] { fireOnChange(); updateBandEnablement(); resized(); };
            addAndMakeVisible (bandType[bi]);

            bandOrder[bi].addItemList ({ "2nd", "4th" }, 1);
            SuperMoToTheme::accentComboBox (bandOrder[bi], accent);
            bandOrder[bi].setTooltip (smt::tips::mtx::bandOrder);
            bandOrder[bi].onChange = [this] { fireOnChange(); };
            addAndMakeVisible (bandOrder[bi]);

            initKnob (bandFreq[bi], "Freq", 10.0, 20000.0, 1.0);
            bandFreq[bi].setSkewFactorFromMidPoint (630.0);
            bandFreq[bi].setTooltip (smt::tips::mtx::bandFreq);

            initKnob (bandQ[bi], "Q", 0.1, 10.0, 0.01);
            bandQ[bi].setSkewFactorFromMidPoint (0.707);
            bandQ[bi].setDoubleClickReturnValue (true, 0.707);
            bandQ[bi].setTooltip (smt::tips::mtx::bandQ);

            initKnob (bandGain[bi], "Gain", -24.0, 24.0, 0.1);
            bandGain[bi].setCentralValue (0.0);     // bipolar: arc grows from 0 dB
            bandGain[bi].setDoubleClickReturnValue (true, 0.0);
            bandGain[bi].setTooltip (smt::tips::mtx::bandGain);
        }

        updateBandEnablement();
    }

    /** Fired whenever any band control changes (mirrors a slider's onChange). */
    std::function<void()> onChange;

    /** Pulls `count` bands (dontSendNotification) into the UI. */
    void setBands (const smt::FrameBand* bands, int count)
    {
        updating = true;
        for (int bi = 0; bi < numBands && bi < count; ++bi)
        {
            const auto& b = bands[bi];
            bandOn[bi].setToggleState (b.on, juce::dontSendNotification);
            bandType[bi].setSelectedId (b.type + 1, juce::dontSendNotification);
            bandOrder[bi].setSelectedId (b.order >= 4 ? 2 : 1, juce::dontSendNotification);
            bandFreq[bi].setValue (b.freq, juce::dontSendNotification);
            bandQ[bi].setValue (b.q, juce::dontSendNotification);
            bandGain[bi].setValue (b.gainDb, juce::dontSendNotification);
        }
        updateBandEnablement();
        resized();
        updating = false;
    }

    /** Writes the current UI state into `count` caller-owned bands. */
    void collectInto (smt::FrameBand* bands, int count) const
    {
        for (int bi = 0; bi < numBands && bi < count; ++bi)
        {
            auto& b = bands[bi];
            b.on     = bandOn[bi].getToggleState();
            b.type   = bandType[bi].getSelectedId() - 1;
            b.order  = bandOrder[bi].getSelectedId() == 2 ? 4 : 2;
            b.freq   = (float) bandFreq[bi].getValue();
            b.q      = (float) bandQ[bi].getValue();
            b.gainDb = (float) bandGain[bi].getValue();
        }
    }

    void setBandsEnabled (bool e)
    {
        for (int bi = 0; bi < numBands; ++bi)
            for (auto* c : { (juce::Component*) &bandOn[bi], (juce::Component*) &bandType[bi],
                             (juce::Component*) &bandOrder[bi], (juce::Component*) &bandFreq[bi],
                             (juce::Component*) &bandQ[bi], (juce::Component*) &bandGain[bi] })
                c->setEnabled (e);
    }

    void resized() override
    {
        auto area = getLocalBounds();
        constexpr int combosH = 24, gap = 6;
        const int numPairs = (numBands + 1) / 2;
        const int knobsH = numPairs > 0
            ? juce::jmax (54, (area.getHeight() - numPairs * combosH - (numPairs - 1) * gap) / numPairs)
            : 0;

        for (int bi = 0; bi < numBands; bi += 2)
        {
            if (bi > 0)
                area.removeFromTop (gap);

            if (bi + 1 < numBands)
            {
                layoutBandPair (area, bi, bi + 1, combosH, knobsH);
            }
            else
            {
                auto combos = area.removeFromTop (combosH);
                auto knobs  = area.removeFromTop (knobsH);
                layoutBand (bi, combos, knobs);
            }
        }
    }

private:
    static constexpr int maxBandsSupported = 4;

    void initKnob (fxme::FxmeSlider& s, const juce::String& name, double lo, double hi, double step)
    {
        s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        s.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        s.setRange (lo, hi, step);
        s.setName (name);
        s.setShowLabel (true);
        SuperMoToTheme::accentSlider (s, accent);
        s.onValueChange = [this] { fireOnChange(); };
        addAndMakeVisible (s);
    }

    void layoutBandPair (juce::Rectangle<int>& area, int bL, int bR, int combosH, int knobsH)
    {
        auto combos = area.removeFromTop (combosH);
        auto knobs  = area.removeFromTop (knobsH);
        layoutBand (bL, combos.removeFromLeft (combos.getWidth() / 2),
                        knobs.removeFromLeft (knobs.getWidth() / 2));
        layoutBand (bR, combos, knobs);
    }

    void layoutBand (int bi, juce::Rectangle<int> combos, juce::Rectangle<int> knobs)
    {
        bandOn[bi].setBounds (combos.removeFromLeft (24));
        combos.removeFromLeft (2);
        bandType[bi].setBounds (combos.removeFromLeft ((int) (combos.getWidth() * 0.62f)).reduced (2, 2));
        bandOrder[bi].setBounds (combos.reduced (2, 2));

        const bool peaking = currentType (bi) == (int) smt::FilterType::peaking;
        bandGain[bi].setVisible (peaking);

        const int nk = peaking ? 3 : 2;
        const int kw = knobs.getWidth() / nk;
        bandFreq[bi].setBounds (knobs.removeFromLeft (kw));
        bandQ[bi].setBounds (knobs.removeFromLeft (kw));
        if (peaking)
            bandGain[bi].setBounds (knobs);
    }

    int currentType (int bi) const { return bandType[bi].getSelectedId() - 1; }

    void updateBandEnablement()
    {
        for (int bi = 0; bi < numBands; ++bi)
            bandOrder[bi].setEnabled (currentType (bi) != (int) smt::FilterType::peaking);
    }

    void fireOnChange()
    {
        if (! updating && onChange)
            onChange();
    }

    const int numBands;
    juce::Colour accent;
    bool updating = false;

    juce::ToggleButton bandOn[maxBandsSupported];
    juce::ComboBox bandType[maxBandsSupported], bandOrder[maxBandsSupported];
    fxme::FxmeSlider bandFreq[maxBandsSupported], bandQ[maxBandsSupported], bandGain[maxBandsSupported];

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BandEqEditor)
};
