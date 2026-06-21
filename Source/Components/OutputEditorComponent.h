/*
  ------------------------------------------------------------------------------
    OutputEditorComponent.h

    Detail editor for the selected output (loudspeaker): trim, a time-alignment
    delay, the FIR correction (load / clear / enable), the analyzer checkbox and
    a 4-band EQ. Each band has an on/off button (1..4), a type (Lowpass /
    Highpass / Bandpass / Band) and order (2nd / 4th, except Band), a frequency
    and Q, and — for the Band (peaking) type — a gain. The compact knobs use the
    FxmeLookAndFeel value-in-centre + label display (right-click a knob to type a
    value). Writes directly into the ConfigModel (these are not host parameters).

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

class OutputEditorComponent : public juce::Component,
                              private smt::ConfigModel::Listener
{
public:
    OutputEditorComponent (smt::ConfigModel& m, smt::MatrixEngine& e) : model (m), engine (e)
    {
        model.addListener (this);

        title.setJustificationType (juce::Justification::centredLeft);
        title.setColour (juce::Label::textColourId, SuperMoToTheme::text);
        title.setFont (juce::Font (15.0f, juce::Font::bold));
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

        for (int bi = 0; bi < smt::numFrameBands; ++bi)
        {
            bandOn[bi].setButtonText (juce::String (bi + 1));
            SuperMoToTheme::accentToggleButton (bandOn[bi], SuperMoToTheme::fir);
            bandOn[bi].onClick = [this] { pushToModel(); };
            addAndMakeVisible (bandOn[bi]);
        }

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
            l.setFont (juce::Font (11.0f));
            l.setColour (juce::Label::textColourId, SuperMoToTheme::dimText);
            addAndMakeVisible (l);
        };
        initBar (levelSlider, levelLabel, "Trim", -60.0, 12.0, 0.1, SuperMoToTheme::master);
        initBar (delaySlider, delayLabel, "Delay", 0.0, (double) smt::maxDelayMs, 0.01, SuperMoToTheme::dim);

        auto initKnob = [this] (fxme::FxmeSlider& s, const juce::String& name,
                                double lo, double hi, double step, juce::Colour col)
        {
            s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
            s.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
            s.setRange (lo, hi, step);
            s.setName (name);
            s.getProperties().set ("showLabel", true);
            SuperMoToTheme::accentSlider (s, col);
            s.onValueChange = [this] { pushToModel(); };
            addAndMakeVisible (s);
        };

        for (int bi = 0; bi < smt::numFrameBands; ++bi)
        {
            bandType[bi].addItemList ({ "Lowpass", "Highpass", "Bandpass", "Band" }, 1);
            SuperMoToTheme::accentComboBox (bandType[bi], SuperMoToTheme::fir);
            bandType[bi].onChange = [this] { pushToModel(); updateBandEnablement(); resized(); };
            addAndMakeVisible (bandType[bi]);

            bandOrder[bi].addItemList ({ "2nd", "4th" }, 1);
            SuperMoToTheme::accentComboBox (bandOrder[bi], SuperMoToTheme::fir);
            bandOrder[bi].onChange = [this] { pushToModel(); };
            addAndMakeVisible (bandOrder[bi]);

            initKnob (bandFreq[bi], "Freq", 10.0, 20000.0, 1.0, SuperMoToTheme::fir);
            bandFreq[bi].setSkewFactorFromMidPoint (630.0);

            initKnob (bandQ[bi], "Q", 0.1, 10.0, 0.01, SuperMoToTheme::fir);
            bandQ[bi].setSkewFactorFromMidPoint (0.707);
            bandQ[bi].setDoubleClickReturnValue (true, 0.707);

            initKnob (bandGain[bi], "Gain", -24.0, 24.0, 0.1, SuperMoToTheme::master);
            bandGain[bi].getProperties().set ("centralValue", 0.0);
            bandGain[bi].setDoubleClickReturnValue (true, 0.0);
        }

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

        // Row 1: FIR / Analyzer + Load / Clear + the four band on/off buttons.
        auto row1 = area.removeFromTop (24);
        auto bandBtns = row1.removeFromRight (4 * 26);
        firButton.setBounds (row1.removeFromLeft (52));
        spectrumButton.setBounds (row1.removeFromLeft (76));
        loadButton.setBounds (row1.removeFromLeft (66).reduced (2, 1));
        clearButton.setBounds (row1.removeFromLeft (50).reduced (2, 1));
        for (int bi = 0; bi < smt::numFrameBands; ++bi)
            bandOn[bi].setBounds (bandBtns.removeFromLeft (bandBtns.getWidth() / (smt::numFrameBands - bi)));

        area.removeFromTop (6);

        // Row 2: trim + delay horizontal sliders.
        auto row2 = area.removeFromTop (22);
        auto lhalf = row2.removeFromLeft (row2.getWidth() / 2);
        levelLabel.setBounds (lhalf.removeFromLeft (38));
        levelSlider.setBounds (lhalf.reduced (2, 1));
        delayLabel.setBounds (row2.removeFromLeft (38));
        delaySlider.setBounds (row2.reduced (2, 1));

        area.removeFromTop (6);

        constexpr int combosH = 24, gap = 6;
        const int knobsH = juce::jmax (54, (area.getHeight() - 2 * combosH - gap) / 2);

        layoutBandPair (area, 0, 1, combosH, knobsH);
        area.removeFromTop (gap);
        layoutBandPair (area, 2, 3, combosH, knobsH);
    }

private:
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
        bandType[bi].setBounds (combos.removeFromLeft ((int) (combos.getWidth() * 0.58f)).reduced (2, 2));
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

        for (int bi = 0; bi < smt::numFrameBands; ++bi)
        {
            const auto& b = s.bands[(size_t) bi];
            bandOn[bi].setToggleState (b.on, juce::dontSendNotification);
            bandType[bi].setSelectedId (b.type + 1, juce::dontSendNotification);
            bandOrder[bi].setSelectedId (b.order >= 4 ? 2 : 1, juce::dontSendNotification);
            bandFreq[bi].setValue (b.freq, juce::dontSendNotification);
            bandQ[bi].setValue (b.q, juce::dontSendNotification);
            bandGain[bi].setValue (b.gainDb, juce::dontSendNotification);
        }

        setControlsEnabled (true);
        updateBandEnablement();
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

        for (int bi = 0; bi < smt::numFrameBands; ++bi)
        {
            auto& b = s.bands[(size_t) bi];
            b.on     = bandOn[bi].getToggleState();
            b.type   = currentType (bi);
            b.order  = bandOrder[bi].getSelectedId() == 2 ? 4 : 2;
            b.freq   = (float) bandFreq[bi].getValue();
            b.q      = (float) bandQ[bi].getValue();
            b.gainDb = (float) bandGain[bi].getValue();
        }
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

    void updateBandEnablement()
    {
        for (int bi = 0; bi < smt::numFrameBands; ++bi)
            bandOrder[bi].setEnabled (currentType (bi) != (int) smt::FilterType::peaking);
    }

    void setControlsEnabled (bool e)
    {
        for (auto* c : { (juce::Component*) &firButton, (juce::Component*) &spectrumButton,
                         (juce::Component*) &loadButton, (juce::Component*) &levelSlider,
                         (juce::Component*) &delaySlider })
            c->setEnabled (e);
        clearButton.setEnabled (e && curFirPath.isNotEmpty());

        for (int bi = 0; bi < smt::numFrameBands; ++bi)
            for (auto* c : { (juce::Component*) &bandOn[bi], (juce::Component*) &bandType[bi],
                             (juce::Component*) &bandOrder[bi], (juce::Component*) &bandFreq[bi],
                             (juce::Component*) &bandQ[bi], (juce::Component*) &bandGain[bi] })
                c->setEnabled (e);
    }

    smt::ConfigModel& model;
    smt::MatrixEngine& engine;
    int curOut = -1;
    bool updating = false;
    juce::String curFirPath;

    juce::Label title;
    juce::ToggleButton firButton, spectrumButton;
    juce::TextButton loadButton, clearButton;
    juce::ToggleButton bandOn[smt::numFrameBands];
    fxme::FxmeSlider levelSlider, delaySlider;
    juce::Label levelLabel, delayLabel;
    juce::ComboBox bandType[smt::numFrameBands], bandOrder[smt::numFrameBands];
    fxme::FxmeSlider bandFreq[smt::numFrameBands], bandQ[smt::numFrameBands], bandGain[smt::numFrameBands];

    std::unique_ptr<juce::FileChooser> fileChooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OutputEditorComponent)
};
