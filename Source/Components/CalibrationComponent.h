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

    A right-hand SPL meter section (SplMeterEngine + SplMeterComponent) reuses
    the microphone, output and "through correction" selections: it shows the
    mic RMS on a dual dBFS / dB SPL bar and can emit a sine and/or white-noise
    test signal. The dB SPL scale is calibrated by playing a tone, reading a
    real SPL meter and entering its value.

    Author: Olivier Doaré, github.com/odoare
    Licenced under the GNU LGPL Version 3.0
    SPDX-License-Identifier: LGPL-3.0-or-later
  ------------------------------------------------------------------------------
*/

#pragma once

#include <JuceHeader.h>
#include "../PluginProcessor.h"
#include "../Theme.h"
#include "SplMeterComponent.h"
#include "SpectrumDisplay.h"

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

        // Mic / outputs / through-correction are shared with the SPL meter:
        // push them whenever they change.
        micBox.onChange = [this] { pushSplSettings(); };
        throughCorrection.onClick = [this] { pushSplSettings(); };
        for (auto* b : outputToggles)
            b->onClick = [this] { pushSplSettings(); };

        buildSplMeterControls();

        startTimerHz (20);
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

        // ── SPL meter section fills the space below the controls ──
        area.removeFromTop (14);
        layoutSplMeter (area);
    }

    void layoutSplMeter (juce::Rectangle<int> area)
    {
        // Controls on the left, then a narrow level meter, then the spectrum
        // analyzer filling the remaining width; all take the full height.
        auto col = area.removeFromLeft (380);
        area.removeFromLeft (16);
        meter.setBounds (area.removeFromLeft (170));
        area.removeFromLeft (16);
        spectrum.setBounds (area);

        splTitle.setBounds (col.removeFromTop (24));
        col.removeFromTop (8);

        auto rowA = col.removeFromTop (24);
        meterOnButton.setBounds (rowA.removeFromLeft (100));
        rowA.removeFromLeft (8);
        windowLabel.setBounds (rowA.removeFromLeft (78));
        windowBox.setBounds (rowA.removeFromLeft (90));

        col.removeFromTop (14);
        sineButton.setBounds (col.removeFromTop (24).removeFromLeft (120));
        col.removeFromTop (4);
        auto rowS = col.removeFromTop (24);
        sineAmpLabel.setBounds (rowS.removeFromLeft (60));
        sineAmp.setBounds (rowS.removeFromLeft (100));
        col.removeFromTop (2);
        auto rowSf = col.removeFromTop (24);
        sineFreqLabel.setBounds (rowSf.removeFromLeft (60));
        sineFreq.setBounds (rowSf.removeFromLeft (110));

        col.removeFromTop (14);
        noiseButton.setBounds (col.removeFromTop (24).removeFromLeft (120));
        col.removeFromTop (4);
        auto rowN = col.removeFromTop (24);
        noiseAmpLabel.setBounds (rowN.removeFromLeft (60));
        noiseAmp.setBounds (rowN.removeFromLeft (100));

        col.removeFromTop (16);
        auto rowR = col.removeFromTop (24);
        splRefLabel.setBounds (rowR.removeFromLeft (120));
        splRef.setBounds (rowR.removeFromLeft (130));
        col.removeFromTop (4);
        splInfo.setBounds (col.removeFromTop (16));
    }

private:
    void addLabel (juce::Label& l, const juce::String& text)
    {
        l.setText (text, juce::dontSendNotification);
        l.setFont (juce::Font (12.0f));
        l.setColour (juce::Label::textColourId, SuperMoToTheme::dimText);
        addAndMakeVisible (l);
    }

    // Small numeric entry (drag or type) in the existing slider style.
    void addNumberEntry (juce::Slider& s, double lo, double hi, double step,
                         double value, const juce::String& suffix)
    {
        s.setSliderStyle (juce::Slider::IncDecButtons);
        s.setTextBoxStyle (juce::Slider::TextBoxLeft, false, 70, 20);
        s.setRange (lo, hi, step);
        s.setValue (value, juce::dontSendNotification);
        s.setTextValueSuffix (suffix);
        SuperMoToTheme::accentSlider (s, SuperMoToTheme::measure);
        addAndMakeVisible (s);
    }

    void buildSplMeterControls()
    {
        splTitle.setText ("SPL meter", juce::dontSendNotification);
        splTitle.setFont (juce::Font (14.0f, juce::Font::bold));
        splTitle.setColour (juce::Label::textColourId, SuperMoToTheme::text);
        addAndMakeVisible (splTitle);

        meterOnButton.setButtonText ("Meter on");
        SuperMoToTheme::accentToggleButton (meterOnButton, SuperMoToTheme::measure);
        meterOnButton.onClick = [this] { pushSplSettings(); };
        addAndMakeVisible (meterOnButton);

        addLabel (windowLabel, "RMS window");
        windowBox.addItem ("50 ms", 1);
        windowBox.addItem ("100 ms", 2);
        windowBox.addItem ("300 ms", 3);
        windowBox.addItem ("1 s", 4);
        windowBox.setSelectedId (3, juce::dontSendNotification);
        SuperMoToTheme::accentComboBox (windowBox, SuperMoToTheme::measure);
        windowBox.onChange = [this] { pushSplSettings(); };
        addAndMakeVisible (windowBox);

        sineButton.setButtonText ("Sine");
        SuperMoToTheme::accentToggleButton (sineButton, SuperMoToTheme::measure);
        sineButton.onClick = [this] { pushSplSettings(); };
        addAndMakeVisible (sineButton);

        addLabel (sineAmpLabel, "Amplitude");
        addNumberEntry (sineAmp, -60.0, 0.0, 0.5, -12.0, " dB");
        sineAmp.onValueChange = [this] { pushSplSettings(); };

        addLabel (sineFreqLabel, "Frequency");
        addNumberEntry (sineFreq, 20.0, 20000.0, 1.0, 1000.0, " Hz");
        sineFreq.onValueChange = [this] { pushSplSettings(); };

        noiseButton.setButtonText ("White noise");
        SuperMoToTheme::accentToggleButton (noiseButton, SuperMoToTheme::measure);
        noiseButton.onClick = [this] { pushSplSettings(); };
        addAndMakeVisible (noiseButton);

        addLabel (noiseAmpLabel, "Amplitude");
        addNumberEntry (noiseAmp, -60.0, 0.0, 0.5, -12.0, " dB");
        noiseAmp.onValueChange = [this] { pushSplSettings(); };

        addLabel (splRefLabel, "Measured dB SPL");
        addNumberEntry (splRef, 0.0, 140.0, 0.1, 85.0, " dB");
        // On entry, snapshot the current dBFS so dB SPL = dBFS + offset.
        splRef.onValueChange = [this]
        {
            const float dbFs = processor.splMeter.getRmsDbFs();
            splOffset = (float) splRef.getValue() - dbFs;
            splCalibrated = dbFs > -119.0f;
            updateSplInfo();
        };

        splInfo.setFont (juce::Font (11.0f));
        splInfo.setColour (juce::Label::textColourId, SuperMoToTheme::dimText);
        addAndMakeVisible (splInfo);
        updateSplInfo();

        addAndMakeVisible (meter);

        // Mic spectrum analyzer (same kind as the matrix view), fed by the SPL
        // engine's mic tap and labelled in dB SPL once calibrated.
        SpectrumDisplay::TraceConfig micTrace;
        micTrace.tap = &processor.splMeter.getMicSpectrumTap();
        micTrace.colour = SuperMoToTheme::spectrum;
        micTrace.thickness = 1.6f;
        micTrace.label = [] { return juce::String ("Microphone"); };
        spectrum.addTrace (std::move (micTrace));
        spectrum.sampleRateProvider = [this] { return processor.getSampleRate(); };
        addAndMakeVisible (spectrum);

        pushSplSettings();
    }

    void updateSplInfo()
    {
        splInfo.setText (splCalibrated
            ? "0 dBFS = " + juce::String (juce::roundToInt (splOffset)) + " dB SPL"
            : juce::String ("Play a tone, read your SPL meter, type the value to calibrate."),
            juce::dontSendNotification);
        meter.setSplOffset (splOffset, splCalibrated);
        spectrum.setSplCalibration (splCalibrated, splOffset);
    }

    // Push the GUI state to the audio-thread SPL engine.
    void pushSplSettings()
    {
        auto& spl = processor.splMeter;
        spl.setMeterOn (meterOnButton.getToggleState());
        spl.setMicChannel (micBox.getSelectedId() - 1);
        spl.setThroughCorrection (throughCorrection.getToggleState());

        static const float windowSecs[] = { 0.05f, 0.1f, 0.3f, 1.0f };
        spl.setRmsWindowSeconds (windowSecs[juce::jlimit (0, 3, windowBox.getSelectedId() - 1)]);

        juce::uint32 mask = 0;
        const int numOuts = processor.configModel.getNumOuts();
        for (int o = 0; o < outputToggles.size() && o < numOuts; ++o)
            if (outputToggles[o]->getToggleState())
                mask |= (1u << (juce::uint32) o);
        spl.setOutputsMask (mask);

        spl.setSineOn (sineButton.getToggleState());
        spl.setSineAmpDb ((float) sineAmp.getValue());
        spl.setSineFreq ((float) sineFreq.getValue());
        spl.setNoiseOn (noiseButton.getToggleState());
        spl.setNoiseAmpDb ((float) noiseAmp.getValue());
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
        pushSplSettings();
        resized();
        repaint();
    }

    void timerCallback() override
    {
        if (processor.measurement.isRunning())
            refresh();

        meter.setLevelDb (processor.splMeter.getRmsDbFs());
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

    // ── SPL meter section ─────────────────────────────────────────────────────
    juce::Label splTitle, windowLabel, sineAmpLabel, sineFreqLabel, noiseAmpLabel,
                splRefLabel, splInfo;
    juce::ToggleButton meterOnButton, sineButton, noiseButton;
    juce::ComboBox windowBox;
    juce::Slider sineAmp, sineFreq, noiseAmp, splRef;
    SplMeterComponent meter;
    SpectrumDisplay spectrum;
    float splOffset = 0.0f;
    bool  splCalibrated = false;

    double progressValue = 0.0;
    juce::ProgressBar progress { progressValue };

    std::unique_ptr<juce::FileChooser> fileChooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CalibrationComponent)
};
