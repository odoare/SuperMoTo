/*
  ------------------------------------------------------------------------------
    FrameEditorComponent.h

    Detail editor for the selected matrix frame: gain, IIR filter (type,
    order, frequency, Q), phase, delay and analyzer checkbox. Writes
    directly into the ConfigModel (these are not host parameters).

    Author: Olivier Doaré, github.com/odoare
    Licenced under the GNU LGPL Version 3.0
    SPDX-License-Identifier: LGPL-3.0-or-later
  ------------------------------------------------------------------------------
*/

#pragma once

#include <JuceHeader.h>
#include "../Model/ConfigModel.h"
#include "../Theme.h"

class FrameEditorComponent : public juce::Component,
                             private smt::ConfigModel::Listener
{
public:
    explicit FrameEditorComponent (smt::ConfigModel& m) : model (m)
    {
        model.addListener (this);

        title.setJustificationType (juce::Justification::centredLeft);
        title.setColour (juce::Label::textColourId, SuperMoToTheme::text);
        title.setFont (juce::Font (15.0f, juce::Font::bold));
        addAndMakeVisible (title);

        auto initToggle = [this] (juce::ToggleButton& b, const juce::String& text, juce::Colour col)
        {
            b.setButtonText (text);
            SuperMoToTheme::accentToggleButton (b, col);
            b.onClick = [this] { pushToModel(); };
            addAndMakeVisible (b);
        };
        initToggle (activeButton, "Active", SuperMoToTheme::master);
        initToggle (phaseButton, "Phase inv.", SuperMoToTheme::exclusive);
        initToggle (spectrumButton, "Analyzer", SuperMoToTheme::spectrum);
        initToggle (filterOnButton, "Filter", SuperMoToTheme::fir);

        auto initRotary = [this] (juce::Slider& s, juce::Label& l, const juce::String& name,
                                  double min, double max, double step, juce::Colour col)
        {
            s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
            s.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 64, 16);
            s.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
            s.setRange (min, max, step);
            SuperMoToTheme::accentSlider (s, col);
            s.onValueChange = [this] { pushToModel(); };
            addAndMakeVisible (s);

            l.setText (name, juce::dontSendNotification);
            l.setJustificationType (juce::Justification::centred);
            l.setFont (juce::Font (11.0f));
            l.setColour (juce::Label::textColourId, SuperMoToTheme::dimText);
            addAndMakeVisible (l);
        };

        initRotary (gainSlider, gainLabel, "Gain (dB)", -60.0, 12.0, 0.1, SuperMoToTheme::master);
        gainSlider.setDoubleClickReturnValue (true, 0.0);

        initRotary (freqSlider, freqLabel, "Freq (Hz)", 10.0, 20000.0, 1.0, SuperMoToTheme::fir);
        freqSlider.setSkewFactorFromMidPoint (630.0);

        initRotary (qSlider, qLabel, "Q", 0.1, 10.0, 0.01, SuperMoToTheme::fir);
        qSlider.setSkewFactorFromMidPoint (0.707);
        qSlider.setDoubleClickReturnValue (true, 0.707);

        initRotary (delaySlider, delayLabel, "Delay (ms)", 0.0, (double) smt::maxDelayMs, 0.01,
                    SuperMoToTheme::dim);
        delaySlider.setDoubleClickReturnValue (true, 0.0);

        typeBox.addItemList ({ "Lowpass", "Highpass", "Bandpass" }, 1);
        SuperMoToTheme::accentComboBox (typeBox, SuperMoToTheme::fir);
        typeBox.onChange = [this] { pushToModel(); };
        addAndMakeVisible (typeBox);

        orderBox.addItemList ({ "2nd order", "4th order" }, 1);
        SuperMoToTheme::accentComboBox (orderBox, SuperMoToTheme::fir);
        orderBox.onChange = [this] { pushToModel(); };
        addAndMakeVisible (orderBox);

        setFrame (-1, -1, 0);
    }

    ~FrameEditorComponent() override       { model.removeListener (this); }

    //==========================================================================
    void setFrame (int in, int out, int config)
    {
        curIn = in; curOut = out; curConfig = config;
        pullFromModel();
    }

    void paint (juce::Graphics& g) override
    {
        g.setColour (SuperMoToTheme::panel.withAlpha (0.7f));
        g.fillRoundedRectangle (getLocalBounds().toFloat(), 6.0f);
        g.setColour (SuperMoToTheme::panelLine);
        g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (0.5f), 6.0f, 1.0f);
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced (10);

        title.setBounds (area.removeFromTop (22));

        auto toggles = area.removeFromTop (24);
        const int tw = toggles.getWidth() / 4;
        activeButton.setBounds (toggles.removeFromLeft (tw));
        phaseButton.setBounds (toggles.removeFromLeft (tw));
        spectrumButton.setBounds (toggles.removeFromLeft (tw));
        filterOnButton.setBounds (toggles);

        auto combos = area.removeFromTop (26).reduced (0, 2);
        typeBox.setBounds (combos.removeFromLeft (combos.getWidth() / 2).reduced (2, 0));
        orderBox.setBounds (combos.reduced (2, 0));

        auto knobs = area;
        const int kw = knobs.getWidth() / 4;
        auto place = [&] (juce::Slider& s, juce::Label& l)
        {
            auto cell = knobs.removeFromLeft (kw);
            l.setBounds (cell.removeFromTop (14));
            s.setBounds (cell);
        };
        place (gainSlider, gainLabel);
        place (freqSlider, freqLabel);
        place (qSlider, qLabel);
        place (delaySlider, delayLabel);
    }

private:
    void modelChanged() override            { pullFromModel(); }

    void pullFromModel()
    {
        if (curIn < 0 || curOut < 0)
        {
            title.setText (juce::String::fromUTF8 ("No frame selected \xe2\x80\x94 click a matrix cell"),
                           juce::dontSendNotification);
            setControlsEnabled (false);
            return;
        }

        const auto f = model.getFrame (curConfig, curIn, curOut);

        updating = true;
        title.setText ("Config " + smt::configName (curConfig)
                       + "   In " + juce::String (curIn + 1)
                       + juce::String::fromUTF8 ("  \xe2\x86\x92  Out ") + juce::String (curOut + 1),
                       juce::dontSendNotification);

        activeButton.setToggleState (f.active, juce::dontSendNotification);
        phaseButton.setToggleState (f.phaseInvert, juce::dontSendNotification);
        spectrumButton.setToggleState (f.spectrum, juce::dontSendNotification);
        filterOnButton.setToggleState (f.filterOn, juce::dontSendNotification);
        gainSlider.setValue (f.gainDb, juce::dontSendNotification);
        freqSlider.setValue (f.filterFreq, juce::dontSendNotification);
        qSlider.setValue (f.filterQ, juce::dontSendNotification);
        delaySlider.setValue (f.delayMs, juce::dontSendNotification);
        typeBox.setSelectedId (f.filterType + 1, juce::dontSendNotification);
        orderBox.setSelectedId (f.filterOrder >= 4 ? 2 : 1, juce::dontSendNotification);

        setControlsEnabled (true);
        updating = false;
    }

    void pushToModel()
    {
        if (updating || curIn < 0 || curOut < 0)
            return;

        smt::FrameSettings f;
        f.active      = activeButton.getToggleState();
        f.phaseInvert = phaseButton.getToggleState();
        f.spectrum    = spectrumButton.getToggleState();
        f.filterOn    = filterOnButton.getToggleState();
        f.gainDb      = (float) gainSlider.getValue();
        f.filterFreq  = (float) freqSlider.getValue();
        f.filterQ     = (float) qSlider.getValue();
        f.delayMs     = (float) delaySlider.getValue();
        f.filterType  = typeBox.getSelectedId() - 1;
        f.filterOrder = orderBox.getSelectedId() == 2 ? 4 : 2;
        model.setFrame (curConfig, curIn, curOut, f);
    }

    void setControlsEnabled (bool e)
    {
        for (auto* c : { (juce::Component*) &activeButton, (juce::Component*) &phaseButton,
                         (juce::Component*) &spectrumButton, (juce::Component*) &filterOnButton,
                         (juce::Component*) &gainSlider, (juce::Component*) &freqSlider,
                         (juce::Component*) &qSlider, (juce::Component*) &delaySlider,
                         (juce::Component*) &typeBox, (juce::Component*) &orderBox })
            c->setEnabled (e);
    }

    smt::ConfigModel& model;
    int curIn = -1, curOut = -1, curConfig = 0;
    bool updating = false;

    juce::Label title;
    juce::ToggleButton activeButton, phaseButton, spectrumButton, filterOnButton;
    juce::Slider gainSlider, freqSlider, qSlider, delaySlider;
    juce::Label gainLabel, freqLabel, qLabel, delayLabel;
    juce::ComboBox typeBox, orderBox;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FrameEditorComponent)
};
