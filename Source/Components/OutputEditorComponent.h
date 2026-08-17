/*
  ------------------------------------------------------------------------------
    OutputEditorComponent.h

    Detail editor for the selected output (loudspeaker): trim, a time-alignment
    delay, the FIR correction (load / clear / enable), the analyzer checkbox and
    a 2-band EQ (BandEqEditor, shared with FrameEditorComponent's per-crosspoint
    EQ). Writes directly into the ConfigModel (these are not host parameters).

    Author: Olivier Doaré, github.com/odoare
    Licenced under the GNU LGPL Version 3.0
    SPDX-License-Identifier: LGPL-3.0-or-later
  ------------------------------------------------------------------------------
*/

#pragma once

#include <JuceHeader.h>
#include "../Model/ConfigModel.h"
#include "../Dsp/MatrixEngine.h"
#include "../AppSettings.h"
#include "../Theme.h"
#include "BandEqEditor.h"

class OutputEditorComponent : public juce::Component,
                              private smt::ConfigModel::Listener
{
public:
    OutputEditorComponent (smt::ConfigModel& m, smt::MatrixEngine& e)
        : model (m), engine (e), bandEditor (smt::numOutputBands, SuperMoToTheme::fir)
    {
        model.addListener (this);

        title.setJustificationType (juce::Justification::centredLeft);
        title.setColour (juce::Label::textColourId, SuperMoToTheme::text);
        title.setFont (juce::Font (juce::FontOptions (15.0f, juce::Font::bold)));
        addAndMakeVisible (title);

        firButton.setButtonText ("FIR");
        SuperMoToTheme::accentToggleButton (firButton, SuperMoToTheme::fir);
        firButton.onClick = [this] { pushToModel(); };
        addAndMakeVisible (firButton);

        spectrumButton.setButtonText ("Analyzer");
        SuperMoToTheme::accentToggleButton (spectrumButton, SuperMoToTheme::spectrum);
        spectrumButton.onClick = [this] { pushToModel(); };
        addAndMakeVisible (spectrumButton);

        loadButton.setButtonText ("Load IR...");
        loadButton.setColour (juce::TextButton::buttonColourId, SuperMoToTheme::fir.darker (0.8f));
        loadButton.onClick = [this] { loadIr(); };
        addAndMakeVisible (loadButton);

        clearButton.setButtonText ("Clear");
        clearButton.setColour (juce::TextButton::buttonColourId, SuperMoToTheme::panel);
        clearButton.onClick = [this] { clearIr(); };
        addAndMakeVisible (clearButton);

        addAndMakeVisible (bandEditor);
        bandEditor.onChange = [this] { pushToModel(); };

        auto initBar = [this] (juce::Slider& s, juce::Label& l, const juce::String& name,
                               double lo, double hi, double step, juce::Colour col)
        {
            s.setSliderStyle (juce::Slider::LinearHorizontal);
            s.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
            s.setRange (lo, hi, step);
            s.setDoubleClickReturnValue (true, 0.0);
            SuperMoToTheme::accentSlider (s, col);
            s.onValueChange = [this] { pushToModel(); };
            addAndMakeVisible (s);

            l.setText (name, juce::dontSendNotification);
            l.setFont (juce::Font (juce::FontOptions (11.0f)));
            l.setColour (juce::Label::textColourId, SuperMoToTheme::dimText);
            addAndMakeVisible (l);
        };
        initBar (levelSlider, levelLabel, "Trim", -60.0, 12.0, 0.1, SuperMoToTheme::master);
        initBar (delaySlider, delayLabel, "Delay", 0.0, (double) smt::maxDelayMs, 0.01, SuperMoToTheme::dim);

        setOutput (-1);
    }

    ~OutputEditorComponent() override      { model.removeListener (this); }

    //==========================================================================
    void setOutput (int out)
    {
        curOut = out;
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
        area.removeFromTop (2);

        // Row 1: FIR / Analyzer + Load / Clear.
        auto row1 = area.removeFromTop (24);
        firButton.setBounds (row1.removeFromLeft (52));
        spectrumButton.setBounds (row1.removeFromLeft (76));
        loadButton.setBounds (row1.removeFromLeft (66).reduced (2, 1));
        clearButton.setBounds (row1.removeFromLeft (50).reduced (2, 1));

        area.removeFromTop (6);

        // Row 2: trim + delay horizontal sliders.
        auto row2 = area.removeFromTop (22);
        auto lhalf = row2.removeFromLeft (row2.getWidth() / 2);
        levelLabel.setBounds (lhalf.removeFromLeft (38));
        levelSlider.setBounds (lhalf.reduced (2, 1));
        delayLabel.setBounds (row2.removeFromLeft (38));
        delaySlider.setBounds (row2.reduced (2, 1));

        area.removeFromTop (6);

        bandEditor.setBounds (area);
    }

private:
    void modelChanged() override            { pullFromModel(); }

    void pullFromModel()
    {
        if (curOut < 0)
        {
            title.setText (juce::String::fromUTF8 ("No output selected \xe2\x80\x94 click a top-strip cell"),
                           juce::dontSendNotification);
            setControlsEnabled (false);
            return;
        }

        const auto s = model.getOutput (curOut);
        curFirPath = s.firPath;

        updating = true;
        title.setText ("Output " + juce::String (curOut + 1)
                       + (s.firPath.isNotEmpty()
                            ? juce::String::fromUTF8 ("  \xe2\x80\x94  ")
                              + juce::File (s.firPath).getFileName()
                            : juce::String()),
                       juce::dontSendNotification);

        firButton.setToggleState (s.firOn, juce::dontSendNotification);
        spectrumButton.setToggleState (s.spectrum, juce::dontSendNotification);
        levelSlider.setValue (s.gainDb, juce::dontSendNotification);
        delaySlider.setValue (s.delayMs, juce::dontSendNotification);
        clearButton.setEnabled (s.firPath.isNotEmpty());
        bandEditor.setBands (s.bands.data(), (int) s.bands.size());

        setControlsEnabled (true);
        resized();
        updating = false;
    }

    smt::OutputSettings collect() const
    {
        smt::OutputSettings s;
        s.firOn    = firButton.getToggleState();
        s.spectrum = spectrumButton.getToggleState();
        s.gainDb   = (float) levelSlider.getValue();
        s.delayMs  = (float) delaySlider.getValue();
        s.firPath  = curFirPath;
        bandEditor.collectInto (s.bands.data(), (int) s.bands.size());
        return s;
    }

    void pushToModel()
    {
        if (updating || curOut < 0)
            return;
        model.setOutput (curOut, collect());
    }

    void loadIr()
    {
        if (curOut < 0)
            return;

        fileChooser = std::make_unique<juce::FileChooser> (
            "Load impulse response for output " + juce::String (curOut + 1),
            smt::getLastBrowseDir(), "*.wav");

        fileChooser->launchAsync (juce::FileBrowserComponent::openMode
                                  | juce::FileBrowserComponent::canSelectFiles,
            [this] (const juce::FileChooser& fc)
            {
                const auto file = fc.getResult();
                if (! file.existsAsFile() || curOut < 0)
                    return;
                smt::setLastBrowseDir (file);
                curFirPath = file.getFullPathName();
                auto s = collect();
                s.firOn = true;
                firButton.setToggleState (true, juce::dontSendNotification);
                model.setOutput (curOut, s);
                engine.updateFirFiles();
            });
    }

    void clearIr()
    {
        if (curOut < 0)
            return;
        curFirPath.clear();
        auto s = collect();
        s.firOn = false;
        firButton.setToggleState (false, juce::dontSendNotification);
        model.setOutput (curOut, s);
        engine.updateFirFiles();
    }

    void setControlsEnabled (bool e)
    {
        for (auto* c : { (juce::Component*) &firButton, (juce::Component*) &spectrumButton,
                         (juce::Component*) &loadButton, (juce::Component*) &levelSlider,
                         (juce::Component*) &delaySlider })
            c->setEnabled (e);
        clearButton.setEnabled (e && curFirPath.isNotEmpty());
        bandEditor.setBandsEnabled (e);
    }

    smt::ConfigModel& model;
    smt::MatrixEngine& engine;
    int curOut = -1;
    bool updating = false;
    juce::String curFirPath;

    juce::Label title;
    juce::ToggleButton firButton, spectrumButton;
    juce::TextButton loadButton, clearButton;
    fxme::FxmeSlider levelSlider, delaySlider;
    juce::Label levelLabel, delayLabel;
    BandEqEditor bandEditor;

    std::unique_ptr<juce::FileChooser> fileChooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OutputEditorComponent)
};
