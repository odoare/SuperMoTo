/*
  ------------------------------------------------------------------------------
    FrameEditorComponent.h

    Detail editor for the selected matrix frame (crosspoint): the routing
    controls only — Active, overall level, phase inversion and the analyzer
    checkbox. The speaker processing (EQ, delay, FIR) is edited on the output
    (see OutputEditorComponent). Writes directly into the ConfigModel (these are
    not host parameters).

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

        levelSlider.setSliderStyle (juce::Slider::LinearHorizontal);
        levelSlider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        levelSlider.setRange (-60.0, 12.0, 0.1);
        levelSlider.setDoubleClickReturnValue (true, 0.0);
        SuperMoToTheme::accentSlider (levelSlider, SuperMoToTheme::master);
        levelSlider.onValueChange = [this] { pushToModel(); };
        addAndMakeVisible (levelSlider);

        levelLabel.setText ("Level", juce::dontSendNotification);
        levelLabel.setFont (juce::Font (11.0f));
        levelLabel.setColour (juce::Label::textColourId, SuperMoToTheme::dimText);
        addAndMakeVisible (levelLabel);

        hint.setJustificationType (juce::Justification::topLeft);
        hint.setColour (juce::Label::textColourId, SuperMoToTheme::dimText);
        hint.setFont (juce::Font (11.0f));
        hint.setText (juce::String::fromUTF8 (
            "EQ, delay and FIR correction are set on the output "
            "(click an output cell in the top strip)."), juce::dontSendNotification);
        addAndMakeVisible (hint);

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

        title.setBounds (area.removeFromTop (20));
        area.removeFromTop (4);

        auto row1 = area.removeFromTop (24);
        const int tw = row1.getWidth() / 3;
        activeButton.setBounds (row1.removeFromLeft (tw));
        phaseButton.setBounds (row1.removeFromLeft (tw));
        spectrumButton.setBounds (row1);

        area.removeFromTop (8);

        auto row2 = area.removeFromTop (22);
        levelLabel.setBounds (row2.removeFromLeft (40));
        levelSlider.setBounds (row2.reduced (2, 1));

        area.removeFromTop (10);
        hint.setBounds (area.removeFromTop (48));
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
        levelSlider.setValue (f.gainDb, juce::dontSendNotification);

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
        f.gainDb      = (float) levelSlider.getValue();
        model.setFrame (curConfig, curIn, curOut, f);
    }

    void setControlsEnabled (bool e)
    {
        for (auto* c : { (juce::Component*) &activeButton, (juce::Component*) &phaseButton,
                         (juce::Component*) &spectrumButton, (juce::Component*) &levelSlider })
            c->setEnabled (e);
    }

    smt::ConfigModel& model;
    int curIn = -1, curOut = -1, curConfig = 0;
    bool updating = false;

    juce::Label title, levelLabel, hint;
    juce::ToggleButton activeButton, phaseButton, spectrumButton;
    fxme::FxmeSlider levelSlider;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FrameEditorComponent)
};
