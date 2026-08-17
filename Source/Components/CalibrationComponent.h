/*
  ------------------------------------------------------------------------------
    CalibrationComponent.h

    Part 2 GUI: measurement of the loudspeakers and the full system. Select the
    microphone input, the measurement type (Dry outputs / outputs+FIR / complete
    System), the channels to measure (outputs, or plugin inputs in System mode),
    the stimulus (band limited white noise or log sweep, 10 Hz .. 20 kHz), the
    duration (5..30 s) and the base pathname; Run measures each selected channel
    in turn and writes stereo files (ch 1 = sent, ch 2 = recorded). System mode
    sends the stimulus through the whole engine (matrix, crossover, FIRs, latency
    compensation) so a mains+sub crossover can be verified.

    A right-hand SPL meter section (SplMeterEngine + SplMeterComponent) reuses
    the microphone, channel and measurement-type selections: it shows the
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
#include "../AppSettings.h"
#include "../Theme.h"
// fxme::SplMeterComponent and fxme::SpectrumDisplay come via the FxmeTools
// module umbrella (JuceHeader.h)

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
        title.setFont (juce::Font (juce::FontOptions (17.0f, juce::Font::bold)));
        title.setColour (juce::Label::textColourId, SuperMoToTheme::text);
        addAndMakeVisible (title);

        addLabel (micLabel, "Microphone input");
        for (int c = 1; c <= smt::numChannels; ++c)
            micBox.addItem ("Input " + juce::String (c), c);
        micBox.setSelectedId (1, juce::dontSendNotification);
        SuperMoToTheme::accentComboBox (micBox, SuperMoToTheme::measure);
        addAndMakeVisible (micBox);

        // Microphone calibration (global, shared with the analysis pane). The
        // live spectrum reads it directly; here we just load / clear / show it.
        addLabel (micCalLabel, "Mic cal");
        micCalValue.setFont (juce::Font (juce::FontOptions (12.0f)));
        micCalValue.setColour (juce::Label::textColourId, SuperMoToTheme::text);
        micCalValue.setColour (juce::Label::backgroundColourId,
                               SuperMoToTheme::plotBackground.withAlpha (0.4f));
        micCalValue.setTooltip ("REW / miniDSP / FRD text calibration; divided out of the "
                                "live spectrum and the analysis measurements.");
        addAndMakeVisible (micCalValue);

        micCalLoadButton.setButtonText (juce::String::fromUTF8 ("\xe2\x80\xa6"));   // ellipsis
        micCalLoadButton.setColour (juce::TextButton::buttonColourId, SuperMoToTheme::measure.darker (0.8f));
        micCalLoadButton.onClick = [this] { loadMicCal(); };
        addAndMakeVisible (micCalLoadButton);

        micCalClearButton.setButtonText (juce::String::fromUTF8 ("\xc3\x97"));      // multiply sign
        micCalClearButton.setColour (juce::TextButton::buttonColourId, SuperMoToTheme::panel);
        micCalClearButton.onClick = [this] { clearMicCal(); };
        addAndMakeVisible (micCalClearButton);

        updateMicCalLabel();

        addLabel (outputsLabel, "Outputs to measure");
        for (int o = 0; o < smt::numChannels; ++o)
        {
            auto* b = outputToggles.add (new juce::ToggleButton (juce::String (o + 1)));
            SuperMoToTheme::accentToggleButton (*b, SuperMoToTheme::measure);
            addAndMakeVisible (b);
        }

        // Measurement type: dry outputs / outputs+FIR / complete system.
        addLabel (measureLabel, "Measure");
        auto setupModeButton = [this] (juce::TextButton& b, const juce::String& text,
                                       int edges)
        {
            b.setButtonText (text);
            b.setClickingTogglesState (true);
            b.setRadioGroupId (7);
            b.setConnectedEdges (edges);
            b.setColour (juce::TextButton::buttonColourId,   SuperMoToTheme::panel);
            b.setColour (juce::TextButton::buttonOnColourId, SuperMoToTheme::measure.darker (0.25f));
            b.setColour (juce::TextButton::textColourOffId,  SuperMoToTheme::dimText);
            b.setColour (juce::TextButton::textColourOnId,   SuperMoToTheme::measure.brighter (0.7f));
            b.onClick = [this] { updateModeUi(); };
            addAndMakeVisible (b);
        };
        setupModeButton (modeDryButton,    "Dry",    juce::Button::ConnectedOnRight);
        setupModeButton (modeFirButton,    "FIR",    juce::Button::ConnectedOnLeft | juce::Button::ConnectedOnRight);
        setupModeButton (modeSystemButton, "System", juce::Button::ConnectedOnLeft);
        modeDryButton.setToggleState (true, juce::dontSendNotification);

        addLabel (signalLabel, "Signal");
        signalBox.addItem ("White noise (10 Hz - 20 kHz)", 1);
        signalBox.addItem ("Log sweep (10 Hz - 20 kHz)", 2);
        signalBox.setSelectedId (2, juce::dontSendNotification);
        SuperMoToTheme::accentComboBox (signalBox, SuperMoToTheme::measure);
        addAndMakeVisible (signalBox);

        // Duration / level bars: FxmeSlider gives right-click value entry (and
        // double-click resets to the default), matching the matrix frame bars.
        addLabel (durationLabel, "Duration");
        duration.setSliderStyle (juce::Slider::LinearHorizontal);
        duration.setRange (5.0, 30.0, 1.0);
        duration.setValue (10.0, juce::dontSendNotification);
        duration.setDoubleClickReturnValue (true, 10.0);
        SuperMoToTheme::accentSlider (duration, SuperMoToTheme::measure);
        addAndMakeVisible (duration);

        addLabel (levelLabel, "Level");
        level.setSliderStyle (juce::Slider::LinearHorizontal);
        level.setRange (-60.0, 0.0, 0.5);
        level.setValue (-12.0, juce::dontSendNotification);
        level.setDoubleClickReturnValue (true, -12.0);
        SuperMoToTheme::accentSlider (level, SuperMoToTheme::measure);
        addAndMakeVisible (level);

        subEnabledToggle.setButtonText ("Sub");
        subEnabledToggle.setToggleState (false, juce::dontSendNotification);
        SuperMoToTheme::accentToggleButton (subEnabledToggle, SuperMoToTheme::measure);
        subEnabledToggle.setTooltip ("Whether this measurement run includes a subwoofer channel. When "
                                     "off, no channel is tagged as sub even if one is picked below.");
        subEnabledToggle.onClick = [this] { updateSubUi(); };
        addAndMakeVisible (subEnabledToggle);

        addLabel (subChannelLabel, "Sub channel");
        subChannelBox.addItem ("(none)", 1);
        for (int c = 0; c < smt::numChannels; ++c)
            subChannelBox.addItem (juce::String (c + 1), c + 2);
        subChannelBox.setSelectedId (1, juce::dontSendNotification);
        subChannelBox.setTooltip ("Which output channel is the subwoofer. Its files are named "
                                  "sub_pos<N>.wav instead of ch<channel>_pos<N>.wav, so Group analysis's "
                                  "\"Load measurement folder...\" recognizes it automatically. Not used "
                                  "in Full system mode (there the toggled channels are plugin inputs).");
        SuperMoToTheme::accentComboBox (subChannelBox, SuperMoToTheme::measure);
        addAndMakeVisible (subChannelBox);

        addLabel (pathLabel, "Measurement folder");
        pathEditor.setColour (juce::TextEditor::backgroundColourId, SuperMoToTheme::plotBackground.withAlpha (0.4f));
        pathEditor.setText (smt::getLastBrowseDir().getFullPathName());
        // A hand-typed folder may hold a manifest with a general comment:
        // pick it up once the path entry is left.
        pathEditor.onReturnKey = [this] { refreshGeneralComment(); };
        pathEditor.onFocusLost = [this] { refreshGeneralComment(); };
        addAndMakeVisible (pathEditor);

        browseButton.setButtonText ("...");
        browseButton.onClick = [this] { browse(); };
        addAndMakeVisible (browseButton);

        // Comments stored in the manifests: a general (whole-folder) comment
        // behind a button-opened popup, and a one-line comment for the next run.
        commentButton.setButtonText ("Folder comment...");
        commentButton.setColour (juce::TextButton::buttonColourId, SuperMoToTheme::panel);
        commentButton.setTooltip ("General comment for this measurement folder, written to "
                                  "measurement.xml and readme_measurement.md at the end of every "
                                  "run \xe2\x80\x94 edit it between runs and the next save rewrites it. "
                                  "Pre-filled from the folder's existing manifest.");
        commentButton.onClick = [this] { editGeneralComment(); };
        addAndMakeVisible (commentButton);

        addLabel (runCommentLabel, "Run comment");
        runCommentEditor.setColour (juce::TextEditor::backgroundColourId,
                                    SuperMoToTheme::plotBackground.withAlpha (0.4f));
        runCommentEditor.setTooltip ("One-line comment for the next run, logged with that run's "
                                     "entry in the manifests.");
        addAndMakeVisible (runCommentEditor);

        refreshGeneralComment();

        runButton.setButtonText ("Run");
        runButton.setColour (juce::TextButton::buttonColourId, SuperMoToTheme::measure.darker (0.8f));
        runButton.onClick = [this] { runOrStop(); };
        addAndMakeVisible (runButton);

        progress.setColour (juce::ProgressBar::foregroundColourId, SuperMoToTheme::measure);
        addAndMakeVisible (progress);

        status.setColour (juce::Label::textColourId, SuperMoToTheme::spectrum);
        addAndMakeVisible (status);

        // Mic / channels / mode are shared with the SPL meter: push on change.
        micBox.onChange = [this] { pushSplSettings(); };
        for (auto* b : outputToggles)
            b->onClick = [this] { pushSplSettings(); };

        buildSplMeterControls();
        updateModeUi();

        // Show mic-corrected level: subtract the mic's deviation from each point.
        spectrum.magnitudeOffsetDb = [] (float f)
        {
            return -smt::sharedMicCalibration().magnitudeDbAt (f);
        };

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
        title.setBounds (area.removeFromTop (26).withTrimmedLeft (30));   // room for the info button
        area.removeFromTop (8);

        // The rows follow the order of the actions: pick the microphone (and
        // its calibration), choose what to measure, set the stimulus, choose
        // where it is written, then Run.
        auto r1 = area.removeFromTop (26);
        micLabel.setBounds (r1.removeFromLeft (118));
        micBox.setBounds (r1.removeFromLeft (104));
        r1.removeFromLeft (16);
        micCalLabel.setBounds (r1.removeFromLeft (56));
        micCalValue.setBounds (r1.removeFromLeft (200));
        r1.removeFromLeft (4);
        micCalLoadButton.setBounds (r1.removeFromLeft (30));
        r1.removeFromLeft (3);
        micCalClearButton.setBounds (r1.removeFromLeft (24));

        // Measurement mode on the left, numbered input/output toggles on the
        // right; each keeps its label on the row above.
        area.removeFromTop (10);
        auto labelRow  = area.removeFromTop (18);
        auto toggleRow = area.removeFromTop (26);
        constexpr int modeW = 56 + 56 + 64;
        measureLabel.setBounds (labelRow.removeFromLeft (modeW));
        labelRow.removeFromLeft (16);
        outputsLabel.setBounds (labelRow);

        modeDryButton.setBounds (toggleRow.removeFromLeft (56));
        modeFirButton.setBounds (toggleRow.removeFromLeft (56));
        modeSystemButton.setBounds (toggleRow.removeFromLeft (64));
        toggleRow.removeFromLeft (16);

        const int numCh = measureChannelCount();
        const int tw = toggleRow.getWidth() / juce::jmax (1, numCh);
        for (int o = 0; o < outputToggles.size(); ++o)
        {
            outputToggles[o]->setVisible (o < numCh);
            if (o < numCh)
                outputToggles[o]->setBounds (toggleRow.removeFromLeft (tw));
        }

        area.removeFromTop (10);
        auto rSub = area.removeFromTop (24);
        subEnabledToggle.setBounds (rSub.removeFromLeft (56));
        rSub.removeFromLeft (8);
        subChannelLabel.setBounds (rSub.removeFromLeft (80));
        subChannelBox.setBounds (rSub.removeFromLeft (90));

        // Stimulus: what is played into the channels selected above.
        area.removeFromTop (10);
        auto rStim = area.removeFromTop (26);
        signalLabel.setBounds (rStim.removeFromLeft (46));
        signalBox.setBounds (rStim.removeFromLeft (200));
        rStim.removeFromLeft (16);
        durationLabel.setBounds (rStim.removeFromLeft (56));
        duration.setBounds (rStim.removeFromLeft (96));
        rStim.removeFromLeft (14);
        levelLabel.setBounds (rStim.removeFromLeft (38));
        level.setBounds (rStim.removeFromLeft (96));

        area.removeFromTop (10);
        pathLabel.setBounds (area.removeFromTop (18));
        auto r3 = area.removeFromTop (24);
        browseButton.setBounds (r3.removeFromRight (36));
        r3.removeFromRight (6);
        pathEditor.setBounds (r3);

        area.removeFromTop (8);
        auto rC = area.removeFromTop (24);
        commentButton.setBounds (rC.removeFromLeft (130));
        rC.removeFromLeft (16);
        runCommentLabel.setBounds (rC.removeFromLeft (86));
        runCommentEditor.setBounds (rC.removeFromLeft (420));

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
    smt::MeasureMode currentMode() const
    {
        if (modeFirButton.getToggleState())    return smt::MeasureMode::outputFir;
        if (modeSystemButton.getToggleState()) return smt::MeasureMode::fullSystem;
        return smt::MeasureMode::dryOutput;
    }

    // The channel toggles mean outputs in the output modes, inputs in fullSystem.
    int measureChannelCount() const
    {
        return currentMode() == smt::MeasureMode::fullSystem
                   ? processor.configModel.getNumIns()
                   : processor.configModel.getNumOuts();
    }

    // The sub-channel tag is an output-channel concept; fullSystem's toggled
    // channels are plugin inputs, so neither the switch nor the channel combo
    // apply there. Otherwise the combo only matters once the switch is on.
    void updateSubUi()
    {
        const bool full = currentMode() == smt::MeasureMode::fullSystem;
        subEnabledToggle.setEnabled (! full);
        subChannelLabel.setEnabled (! full && subEnabledToggle.getToggleState());
        subChannelBox.setEnabled (! full && subEnabledToggle.getToggleState());
    }

    void updateModeUi()
    {
        const bool full = currentMode() == smt::MeasureMode::fullSystem;
        outputsLabel.setText (full ? "Inputs to measure" : "Outputs to measure",
                              juce::dontSendNotification);

        updateSubUi();

        // Switching between output- and input-meaning clears the selection so
        // stale channels are not measured under the new meaning.
        if (full != lastModeWasFull)
        {
            for (auto* b : outputToggles)
                b->setToggleState (false, juce::dontSendNotification);
            lastModeWasFull = full;
        }

        pushSplSettings();
        resized();
        repaint();
    }

    void addLabel (juce::Label& l, const juce::String& text)
    {
        l.setText (text, juce::dontSendNotification);
        l.setFont (juce::Font (juce::FontOptions (12.0f)));
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
        splTitle.setFont (juce::Font (juce::FontOptions (14.0f, juce::Font::bold)));
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
        // On entry, snapshot the current dBFS so dB SPL = dBFS + offset. The
        // calibration is persisted (AppSettings) so it survives closing the
        // editor and is written into the measurement manifests.
        splRef.onValueChange = [this]
        {
            const float dbFs = processor.splMeter.getRmsDbFs();
            splOffset = (float) splRef.getValue() - dbFs;
            splCalibrated = dbFs > -119.0f;
            smt::setSplCalibration (splOffset, splCalibrated);
            updateSplInfo();
        };

        splInfo.setFont (juce::Font (juce::FontOptions (11.0f)));
        splInfo.setColour (juce::Label::textColourId, SuperMoToTheme::dimText);
        addAndMakeVisible (splInfo);
        updateSplInfo();

        meter.setColours (SuperMoToTheme::splMeterColours());
        addAndMakeVisible (meter);

        // Mic spectrum analyzer (same kind as the matrix view), fed by the SPL
        // engine's mic tap and labelled in dB SPL once calibrated.
        spectrum.setColours (SuperMoToTheme::spectrumColours());
        fxme::SpectrumDisplay::TraceConfig micTrace;
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
        spl.setMode (currentMode());

        static const float windowSecs[] = { 0.05f, 0.1f, 0.3f, 1.0f };
        spl.setRmsWindowSeconds (windowSecs[juce::jlimit (0, 3, windowBox.getSelectedId() - 1)]);

        juce::uint32 mask = 0;
        const int numCh = measureChannelCount();
        for (int o = 0; o < outputToggles.size() && o < numCh; ++o)
            if (outputToggles[o]->getToggleState())
                mask |= (1u << (juce::uint32) o);
        spl.setChannelsMask (mask);

        spl.setSineOn (sineButton.getToggleState());
        spl.setSineAmpDb ((float) sineAmp.getValue());
        spl.setSineFreq ((float) sineFreq.getValue());
        spl.setNoiseOn (noiseButton.getToggleState());
        spl.setNoiseAmpDb ((float) noiseAmp.getValue());
    }

    void updateMicCalLabel()
    {
        auto& cal = smt::sharedMicCalibration();
        micCalValue.setText (cal.isValid() ? cal.getName() : juce::String ("none"),
                             juce::dontSendNotification);
        micCalValue.setColour (juce::Label::textColourId,
                               cal.isValid() ? SuperMoToTheme::text : SuperMoToTheme::dimText);
    }

    void loadMicCal()
    {
        fileChooser = std::make_unique<juce::FileChooser> (
            "Load microphone calibration (REW / miniDSP / FRD text file)",
            smt::getLastBrowseDir(), "*.txt;*.cal;*.frd");

        fileChooser->launchAsync (juce::FileBrowserComponent::openMode
                                  | juce::FileBrowserComponent::canSelectFiles,
            [this] (const juce::FileChooser& fc)
            {
                auto f = fc.getResult();
                if (f == juce::File())
                    return;
                smt::setLastBrowseDir (f);
                const bool ok = smt::setMicCalibrationFile (f);
                updateMicCalLabel();
                status.setText (ok ? "Loaded mic calibration: " + f.getFileName()
                                   : "Could not read the calibration file.",
                                juce::dontSendNotification);
            });
    }

    void clearMicCal()
    {
        smt::setMicCalibrationFile ({});
        updateMicCalLabel();
        status.setText ("Mic calibration cleared.", juce::dontSendNotification);
    }

    // Reloads the general comment from the current folder's manifest — only
    // when the folder actually changed, so an unsaved comment typed for the
    // current folder survives focus churn on the path entry.
    void refreshGeneralComment()
    {
        const juce::File folder (pathEditor.getText().trim());
        if (folder == lastCommentFolder)
            return;
        lastCommentFolder = folder;
        generalComment = smt::readMeasurementGeneralComment (folder);
    }

    // Popup with a multiline entry for the folder comment; edits land in
    // generalComment as they are typed (dismiss by clicking outside), and the
    // manifests pick it up at the end of the next run.
    void editGeneralComment()
    {
        struct CommentPopup : public juce::Component
        {
            CommentPopup (const juce::String& initialText,
                          std::function<void (const juce::String&)> cb)
                : onChange (std::move (cb))
            {
                editor.setMultiLine (true, true);
                editor.setReturnKeyStartsNewLine (true);
                editor.setColour (juce::TextEditor::backgroundColourId,
                                  SuperMoToTheme::plotBackground);
                editor.setText (initialText, false);
                editor.onTextChange = [this] { onChange (editor.getText()); };
                addAndMakeVisible (editor);
                setSize (420, 150);
            }

            void resized() override { editor.setBounds (getLocalBounds().reduced (6)); }

            juce::TextEditor editor;
            std::function<void (const juce::String&)> onChange;
            fxme::TextEntryFocusFixer fixer { *this };  // the popup is its own window
        };

        auto popup = std::make_unique<CommentPopup> (generalComment,
            [safe = juce::Component::SafePointer<CalibrationComponent> (this)] (const juce::String& t)
            {
                if (safe != nullptr)
                    safe->generalComment = t;
            });

        auto* parent = getTopLevelComponent();
        juce::CallOutBox::launchAsynchronously (
            std::move (popup),
            parent->getLocalArea (&commentButton, commentButton.getLocalBounds()),
            parent);
    }

    void browse()
    {
        fileChooser = std::make_unique<juce::FileChooser> (
            "Select the measurement folder",
            juce::File::createFileWithoutCheckingPath (pathEditor.getText()));

        fileChooser->launchAsync (juce::FileBrowserComponent::openMode
                                  | juce::FileBrowserComponent::canSelectDirectories,
            [this] (const juce::FileChooser& fc)
            {
                auto f = fc.getResult();
                if (f == juce::File())
                    return;
                smt::setLastBrowseDir (f);
                pathEditor.setText (f.getFullPathName());
                refreshGeneralComment();
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
        const int numCh = measureChannelCount();
        for (int o = 0; o < smt::numChannels; ++o)
            s.channelsToMeasure[(size_t) o] = o < numCh && outputToggles[o]->getToggleState();
        s.signalType = signalBox.getSelectedId() == 1
                           ? smt::MeasurementEngine::SignalType::whiteNoise
                           : smt::MeasurementEngine::SignalType::logSweep;
        s.mode = currentMode();
        s.durationS = (float) duration.getValue();
        s.levelDb = (float) level.getValue();
        s.folder = pathEditor.getText().trim();
        refreshGeneralComment();    // a hand-typed path may not have lost focus yet
        s.generalComment = generalComment;
        s.runComment = runCommentEditor.getText().trim();

        // Calibration in effect, recorded in the manifests.
        s.splCalibrated = splCalibrated;
        s.splOffsetDb = splOffset;
        auto& cal = smt::sharedMicCalibration();
        if (cal.isValid())
        {
            s.micCalName = cal.getName();
            s.micCalText = cal.getRawText();
        }
        s.subChannel = (s.mode == smt::MeasureMode::fullSystem || ! subEnabledToggle.getToggleState())
                           ? -1 : (subChannelBox.getSelectedId() - 2);

        processor.measurementMicChannel.store (s.micInput);

        if (! m.start (s))
            status.setText (juce::String ("Cannot start: select at least one ")
                            + (s.mode == smt::MeasureMode::fullSystem ? "input" : "output")
                            + " and a valid folder.", juce::dontSendNotification);
        else if (juce::File::isAbsolutePath (s.folder))
            // Remember where the measurements are written so the Analysis view's
            // "Load measurements" starts in the same folder.
            smt::setLastBrowseDir (juce::File (s.folder));
    }

    void changeListenerCallback (juce::ChangeBroadcaster*) override
    {
        refresh();
    }

    void modelChanged() override
    {
        // Matrix size changed: re-layout the toggles and untick the hidden ones
        // (count depends on the mode: outputs, or inputs in fullSystem).
        for (int o = measureChannelCount(); o < outputToggles.size(); ++o)
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

    juce::Label title, micLabel, outputsLabel, signalLabel, measureLabel, durationLabel,
                levelLabel, pathLabel, status, micCalLabel, micCalValue, subChannelLabel;
    juce::ComboBox micBox, signalBox, subChannelBox;
    juce::ToggleButton subEnabledToggle;
    juce::OwnedArray<juce::ToggleButton> outputToggles;
    juce::TextButton modeDryButton, modeFirButton, modeSystemButton;
    juce::TextButton micCalLoadButton, micCalClearButton;
    fxme::FxmeSlider duration, level;
    juce::TextEditor pathEditor;
    juce::TextButton browseButton, runButton;
    bool lastModeWasFull = false;

    // Manifest comments: general (whole-folder, edited in a popup) and per-run.
    juce::TextButton commentButton;
    juce::Label runCommentLabel;
    juce::TextEditor runCommentEditor;
    juce::String generalComment;
    juce::File lastCommentFolder;       // folder generalComment was loaded for

    // ── SPL meter section ─────────────────────────────────────────────────────
    juce::Label splTitle, windowLabel, sineAmpLabel, sineFreqLabel, noiseAmpLabel,
                splRefLabel, splInfo;
    juce::ToggleButton meterOnButton, sineButton, noiseButton;
    juce::ComboBox windowBox;
    juce::Slider sineAmp, sineFreq, noiseAmp, splRef;
    fxme::SplMeterComponent meter;
    fxme::SpectrumDisplay spectrum;
    // SPL calibration, restored from the persisted machine-wide value.
    float splOffset = smt::getSplOffsetDb();
    bool  splCalibrated = smt::isSplCalibrated();

    double progressValue = 0.0;
    juce::ProgressBar progress { progressValue };

    std::unique_ptr<juce::FileChooser> fileChooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CalibrationComponent)
};
