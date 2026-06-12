/*
  ------------------------------------------------------------------------------
    CalibrationComponent.h

    Part 2 GUI: measurement of the individual loudspeakers. Select the
    microphone input, the outputs to measure, the stimulus (band limited
    white noise or log sweep, 10 Hz .. 20 kHz), the duration (5..30 s) and
    the base pathname; Run measures each selected output in turn and writes
    "<base>_<output>.wav" stereo files (ch 1 = sent, ch 2 = recorded).
    The "through correction" toggle re-measures with the output FIR engaged
    to verify a correction curve.

    Author: Olivier Doaré, github.com/odoare
    Licenced under the GNU LGPL Version 3.0
    SPDX-License-Identifier: LGPL-3.0-or-later
  ------------------------------------------------------------------------------
*/

#pragma once

#include <JuceHeader.h>
#include "../PluginProcessor.h"
#include "../Theme.h"

class CalibrationComponent : public juce::Component,
                             private juce::ChangeListener,
                             private juce::Timer,
                             private smt::ConfigModel::Listener
{
public:
    explicit CalibrationComponent (SuperMoToAudioProcessor& p) : processor (p)
    {
        processor.measurement.addChangeListener (this);
        processor.configModel.addListener (this);

        title.setText ("Measurement & calibration", juce::dontSendNotification);
        title.setFont (juce::Font (17.0f, juce::Font::bold));
        title.setColour (juce::Label::textColourId, SuperMoToTheme::text);
        addAndMakeVisible (title);

        addLabel (micLabel, "Microphone input");
        for (int c = 1; c <= smt::numChannels; ++c)
            micBox.addItem ("Input " + juce::String (c), c);
        micBox.setSelectedId (1, juce::dontSendNotification);
        SuperMoToTheme::accentComboBox (micBox, SuperMoToTheme::measure);
        addAndMakeVisible (micBox);

        addLabel (outputsLabel, "Outputs to measure");
        for (int o = 0; o < smt::numChannels; ++o)
        {
            auto* b = outputToggles.add (new juce::ToggleButton (juce::String (o + 1)));
            SuperMoToTheme::accentToggleButton (*b, SuperMoToTheme::measure);
            addAndMakeVisible (b);
        }

        addLabel (signalLabel, "Signal");
        signalBox.addItem ("White noise (10 Hz - 20 kHz)", 1);
        signalBox.addItem ("Log sweep (10 Hz - 20 kHz)", 2);
        signalBox.setSelectedId (2, juce::dontSendNotification);
        SuperMoToTheme::accentComboBox (signalBox, SuperMoToTheme::measure);
        addAndMakeVisible (signalBox);

        addLabel (durationLabel, "Duration");
        duration.setSliderStyle (juce::Slider::LinearHorizontal);
        duration.setTextBoxStyle (juce::Slider::TextBoxRight, false, 64, 18);
        duration.setRange (5.0, 30.0, 1.0);
        duration.setValue (10.0, juce::dontSendNotification);
        duration.setTextValueSuffix (" s");
        SuperMoToTheme::accentSlider (duration, SuperMoToTheme::measure);
        addAndMakeVisible (duration);

        addLabel (levelLabel, "Level");
        level.setSliderStyle (juce::Slider::LinearHorizontal);
        level.setTextBoxStyle (juce::Slider::TextBoxRight, false, 64, 18);
        level.setRange (-60.0, 0.0, 0.5);
        level.setValue (-12.0, juce::dontSendNotification);
        level.setTextValueSuffix (" dB");
        SuperMoToTheme::accentSlider (level, SuperMoToTheme::measure);
        addAndMakeVisible (level);

        addLabel (pathLabel, "Base pathname (folder + base name)");
        pathEditor.setColour (juce::TextEditor::backgroundColourId, juce::Colours::black.withAlpha (0.4f));
        pathEditor.setText (juce::File::getSpecialLocation (juce::File::userHomeDirectory)
                                .getChildFile ("supermoto_measure").getFullPathName());
        addAndMakeVisible (pathEditor);

        browseButton.setButtonText ("...");
        browseButton.onClick = [this] { browse(); };
        addAndMakeVisible (browseButton);

        throughCorrection.setButtonText ("Measure through correction (verify FIR)");
        SuperMoToTheme::accentToggleButton (throughCorrection, SuperMoToTheme::fir);
        addAndMakeVisible (throughCorrection);

        runButton.setButtonText ("Run");
        runButton.setColour (juce::TextButton::buttonColourId, SuperMoToTheme::measure.darker (0.8f));
        runButton.onClick = [this] { runOrStop(); };
        addAndMakeVisible (runButton);

        progress.setColour (juce::ProgressBar::foregroundColourId, SuperMoToTheme::measure);
        addAndMakeVisible (progress);

        status.setColour (juce::Label::textColourId, SuperMoToTheme::spectrum);
        addAndMakeVisible (status);

        startTimerHz (8);
    }

    ~CalibrationComponent() override
    {
        processor.configModel.removeListener (this);
        processor.measurement.removeChangeListener (this);
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
        auto area = getLocalBounds().reduced (14);
        title.setBounds (area.removeFromTop (26));
        area.removeFromTop (8);

        auto r1 = area.removeFromTop (24);
        micLabel.setBounds (r1.removeFromLeft (130));
        micBox.setBounds (r1.removeFromLeft (120));
        r1.removeFromLeft (24);
        signalLabel.setBounds (r1.removeFromLeft (50));
        signalBox.setBounds (r1.removeFromLeft (210));

        area.removeFromTop (10);
        outputsLabel.setBounds (area.removeFromTop (18));
        auto toggleArea = area.removeFromTop (26);
        const int numOuts = processor.configModel.getNumOuts();
        const int tw = toggleArea.getWidth() / numOuts;
        for (int o = 0; o < outputToggles.size(); ++o)
        {
            outputToggles[o]->setVisible (o < numOuts);
            if (o < numOuts)
                outputToggles[o]->setBounds (toggleArea.removeFromLeft (tw));
        }

        area.removeFromTop (10);
        auto r2 = area.removeFromTop (24);
        durationLabel.setBounds (r2.removeFromLeft (70));
        duration.setBounds (r2.removeFromLeft (250));
        r2.removeFromLeft (24);
        levelLabel.setBounds (r2.removeFromLeft (50));
        level.setBounds (r2.removeFromLeft (250));

        area.removeFromTop (10);
        pathLabel.setBounds (area.removeFromTop (18));
        auto r3 = area.removeFromTop (24);
        browseButton.setBounds (r3.removeFromRight (36));
        r3.removeFromRight (6);
        pathEditor.setBounds (r3);

        area.removeFromTop (10);
        throughCorrection.setBounds (area.removeFromTop (24));

        area.removeFromTop (14);
        auto r4 = area.removeFromTop (28);
        runButton.setBounds (r4.removeFromLeft (120));
        r4.removeFromLeft (12);
        progress.setBounds (r4);

        area.removeFromTop (8);
        status.setBounds (area.removeFromTop (22));
    }

private:
    void addLabel (juce::Label& l, const juce::String& text)
    {
        l.setText (text, juce::dontSendNotification);
        l.setFont (juce::Font (12.0f));
        l.setColour (juce::Label::textColourId, SuperMoToTheme::dimText);
        addAndMakeVisible (l);
    }

    void browse()
    {
        fileChooser = std::make_unique<juce::FileChooser> (
            "Base file name for the measurements (without extension)",
            juce::File::createFileWithoutCheckingPath (pathEditor.getText()));

        fileChooser->launchAsync (juce::FileBrowserComponent::saveMode
                                  | juce::FileBrowserComponent::canSelectFiles
                                  | juce::FileBrowserComponent::warnAboutOverwriting,
            [this] (const juce::FileChooser& fc)
            {
                auto f = fc.getResult();
                if (f == juce::File())
                    return;
                pathEditor.setText (f.getFullPathName().upToLastOccurrenceOf (".wav", false, true));
            });
    }

    void runOrStop()
    {
        auto& m = processor.measurement;

        if (m.isRunning())
        {
            m.stop();
            return;
        }

        smt::MeasurementEngine::Settings s;
        s.micInput = micBox.getSelectedId() - 1;
        for (int o = 0; o < smt::numChannels; ++o)
            s.outputsToMeasure[(size_t) o] = outputToggles[o]->getToggleState();
        s.signalType = signalBox.getSelectedId() == 1
                           ? smt::MeasurementEngine::SignalType::whiteNoise
                           : smt::MeasurementEngine::SignalType::logSweep;
        s.durationS = (float) duration.getValue();
        s.levelDb = (float) level.getValue();
        s.basePath = pathEditor.getText().trim();
        s.throughCorrection = throughCorrection.getToggleState();

        processor.measurementMicChannel.store (s.micInput);

        if (! m.start (s))
            status.setText ("Cannot start: select at least one output and a valid folder.",
                            juce::dontSendNotification);
    }

    void changeListenerCallback (juce::ChangeBroadcaster*) override
    {
        refresh();
    }

    void modelChanged() override
    {
        // Matrix size changed: re-layout the output toggles and untick the
        // hidden ones so they cannot be measured.
        for (int o = processor.configModel.getNumOuts(); o < outputToggles.size(); ++o)
            outputToggles[o]->setToggleState (false, juce::dontSendNotification);
        resized();
        repaint();
    }

    void timerCallback() override
    {
        if (processor.measurement.isRunning())
            refresh();
    }

    void refresh()
    {
        const bool running = processor.measurement.isRunning();
        runButton.setButtonText (running ? "Stop" : "Run");
        progressValue = (double) processor.measurement.getProgress();
        status.setText (processor.measurement.getStatusText(), juce::dontSendNotification);
    }

    SuperMoToAudioProcessor& processor;

    juce::Label title, micLabel, outputsLabel, signalLabel, durationLabel,
                levelLabel, pathLabel, status;
    juce::ComboBox micBox, signalBox;
    juce::OwnedArray<juce::ToggleButton> outputToggles;
    juce::Slider duration, level;
    juce::TextEditor pathEditor;
    juce::TextButton browseButton, runButton;
    juce::ToggleButton throughCorrection;

    double progressValue = 0.0;
    juce::ProgressBar progress { progressValue };

    std::unique_ptr<juce::FileChooser> fileChooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CalibrationComponent)
};
