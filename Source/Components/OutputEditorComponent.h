/*
  ------------------------------------------------------------------------------
    OutputEditorComponent.h

    Detail editor for the selected output (loudspeaker): its description, trim,
    polarity, a time-alignment delay, the FIR correction (load / clear /
    enable), the analyzer checkbox and a 2-band EQ (BandEqEditor, shared with
    FrameEditorComponent's per-crosspoint EQ). Writes directly into the
    ConfigModel (these are not host parameters).

    The description is the one place an output is named ("Genelec 8030 Left"),
    and the name travels: the matrix and the Calibration pane show it on hover,
    and a measurement run writes it into the folder's manifest.

    Author: Olivier Doaré, github.com/odoare
    Licenced under the GNU AGPL Version 3.0, or commercial terms (LICENSE.md)
    SPDX-License-Identifier: AGPL-3.0-or-later OR LicenseRef-FXME-Commercial
  ------------------------------------------------------------------------------
*/

#pragma once

#include <JuceHeader.h>
#include <utility>
#include "../Model/ConfigModel.h"
#include "../Dsp/MatrixEngine.h"
#include "../AppSettings.h"
#include "../Theme.h"
#include "../Tooltips.h"
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

        // Same label and colour as the frame editor's polarity toggle.
        phaseButton.setButtonText ("Phase inv.");
        SuperMoToTheme::accentToggleButton (phaseButton, SuperMoToTheme::exclusive);
        phaseButton.setTooltip (smt::tips::mtx::outPhase);
        phaseButton.onClick = [this] { pushToModel(); };
        addAndMakeVisible (phaseButton);

        firButton.setButtonText ("FIR");
        SuperMoToTheme::accentToggleButton (firButton, SuperMoToTheme::fir);
        firButton.setTooltip (smt::tips::mtx::outFir);
        firButton.onClick = [this] { pushToModel(); };
        addAndMakeVisible (firButton);

        spectrumButton.setButtonText ("Analyzer");
        SuperMoToTheme::accentToggleButton (spectrumButton, SuperMoToTheme::spectrum);
        spectrumButton.setTooltip (smt::tips::mtx::outSpectrum);
        spectrumButton.onClick = [this] { pushToModel(); };
        addAndMakeVisible (spectrumButton);

        loadButton.setButtonText ("Load IR...");
        loadButton.setColour (juce::TextButton::buttonColourId, SuperMoToTheme::fir.darker (0.8f));
        loadButton.setTooltip (smt::tips::mtx::outLoadIr);
        loadButton.onClick = [this] { loadIr(); };
        addAndMakeVisible (loadButton);

        clearButton.setButtonText ("Clear");
        clearButton.setColour (juce::TextButton::buttonColourId, SuperMoToTheme::panel);
        clearButton.setTooltip (smt::tips::mtx::outClearIr);
        clearButton.onClick = [this] { clearIr(); };
        addAndMakeVisible (clearButton);

        descriptionLabel.setText ("Description", juce::dontSendNotification);
        descriptionLabel.setFont (juce::Font (juce::FontOptions (11.0f)));
        descriptionLabel.setColour (juce::Label::textColourId, SuperMoToTheme::dimText);
        addAndMakeVisible (descriptionLabel);

        descriptionEditor.setColour (juce::TextEditor::backgroundColourId,
                                     SuperMoToTheme::plotBackground.withAlpha (0.4f));
        descriptionEditor.setTooltip (smt::tips::mtx::outDescription);
        // Written on every keystroke rather than on Return, so a name typed
        // and left unfinished is still the one a preset save or a measurement
        // run picks up. The refresh below never types over the caret.
        descriptionEditor.onTextChange = [this] { pushToModel(); };
        addAndMakeVisible (descriptionEditor);

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
        levelSlider.setTooltip (smt::tips::mtx::outTrim);
        initBar (delaySlider, delayLabel, "Delay", 0.0, (double) smt::maxDelayMs, 0.01, SuperMoToTheme::delay);
        delaySlider.setTooltip (smt::tips::mtx::outDelay);
        // Two decimals whatever the step below turns out to be: the step is a
        // sample period, which is not a round number of milliseconds.
        delaySlider.setNumDecimalPlacesToDisplay (2);
        updateDelayStep();

        setOutput (-1);
    }

    ~OutputEditorComponent() override      { model.removeListener (this); }

    //==========================================================================
    void setOutput (int out)
    {
        curOut = out;
        // A different output's name replaces whatever is in the field, even
        // mid-edit: what is typed next belongs to the output now selected.
        showDescription = true;
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

        // Row 0: what this output drives, as the preset names it.
        auto row0 = area.removeFromTop (22);
        descriptionLabel.setBounds (row0.removeFromLeft (72));
        descriptionEditor.setBounds (row0.reduced (2, 1));

        area.removeFromTop (6);

        // Row 1: Phase inv. / FIR / Analyzer + Load / Clear. 320 px, which is
        // exactly the narrowest this editor gets (a 340 px column, less the
        // 10 px margins).
        auto row1 = area.removeFromTop (24);
        phaseButton.setBounds (row1.removeFromLeft (76));
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
        const bool forced = std::exchange (showDescription, false);

        if (curOut < 0)
        {
            title.setText (juce::String::fromUTF8 ("No output selected \xe2\x80\x94 click a top-strip cell"),
                           juce::dontSendNotification);
            descriptionEditor.setText ({}, false);
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

        phaseButton.setToggleState (s.phaseInvert, juce::dontSendNotification);
        firButton.setToggleState (s.firOn, juce::dontSendNotification);
        spectrumButton.setToggleState (s.spectrum, juce::dontSendNotification);
        levelSlider.setValue (s.gainDb, juce::dontSendNotification);

        // Never while it is being typed in: pushToModel() brings us straight
        // back here on every keystroke, and setText() would move the caret.
        if (forced || ! descriptionEditor.hasKeyboardFocus (true))
            descriptionEditor.setText (s.description, false);

        updateDelayStep();
        delaySlider.setValue (snapDelayMs (s.delayMs), juce::dontSendNotification);
        clearButton.setEnabled (s.firPath.isNotEmpty());
        bandEditor.setBands (s.bands.data(), (int) s.bands.size());

        setControlsEnabled (true);
        resized();
        updating = false;
    }

    /** The output stage rounds its delay to whole samples (OutputProcessor),
        so the control steps in whole samples too: every position it can take
        is one the engine will actually apply. Re-read on each refresh because
        the host can change the rate under us. */
    void updateDelayStep()
    {
        const double sr = engine.getSampleRate();
        if (sr > 0.0)
            delaySlider.setRange (0.0, (double) smt::maxDelayMs, 1000.0 / sr);
    }

    /** The delay the output stage will really apply, in milliseconds. */
    double snapDelayMs (double ms) const
    {
        const double sr = engine.getSampleRate();
        if (sr <= 0.0)
            return ms;
        return juce::jlimit (0.0, (double) smt::maxDelayMs,
                             juce::roundToInt (ms * 0.001 * sr) * 1000.0 / sr);
    }

    smt::OutputSettings collect() const
    {
        smt::OutputSettings s;
        s.phaseInvert = phaseButton.getToggleState();
        s.firOn       = firButton.getToggleState();
        s.spectrum    = spectrumButton.getToggleState();
        s.gainDb      = (float) levelSlider.getValue();
        s.delayMs     = (float) snapDelayMs (delaySlider.getValue());
        s.firPath     = curFirPath;
        s.description = descriptionEditor.getText();
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
        for (auto* c : { (juce::Component*) &phaseButton, (juce::Component*) &firButton,
                         (juce::Component*) &spectrumButton,
                         (juce::Component*) &loadButton, (juce::Component*) &levelSlider,
                         (juce::Component*) &delaySlider,
                         (juce::Component*) &descriptionEditor })
            c->setEnabled (e);
        clearButton.setEnabled (e && curFirPath.isNotEmpty());
        bandEditor.setBandsEnabled (e);
    }

    smt::ConfigModel& model;
    smt::MatrixEngine& engine;
    int curOut = -1;
    bool updating = false;
    bool showDescription = true;    // the next refresh replaces the field's text
    juce::String curFirPath;

    juce::Label title, descriptionLabel;
    juce::TextEditor descriptionEditor;
    juce::ToggleButton phaseButton, firButton, spectrumButton;
    juce::TextButton loadButton, clearButton;
    fxme::FxmeSlider levelSlider, delaySlider;
    juce::Label levelLabel, delayLabel;
    BandEqEditor bandEditor;

    std::unique_ptr<juce::FileChooser> fileChooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OutputEditorComponent)
};
