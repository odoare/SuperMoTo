/*
  ------------------------------------------------------------------------------
    AnalysisComponent.h

    Part 3 GUI: loads a set of measurements of one speaker (microphone
    moved around the reference position), draws the Welch transfer
    functions (thin lines), their average (thick line) and the proposed
    correction, with the correction level adjustable from 0 to 1. The
    correction is exported as an impulse response wav which the monitoring
    part loads as an output FIR (optionally assigned directly here).

    The component is a view. The engine, the loaded files and the settings
    live in smt::AnalysisSession, owned by the processor's workspace, so
    closing the editor loses none of it. Controls write the session's
    settings; reopening restores them without analyzing anything again.

    Author: Olivier Doaré, github.com/odoare
    Licenced under the GNU AGPL Version 3.0, or commercial terms (LICENSE.md)
    SPDX-License-Identifier: AGPL-3.0-or-later OR LicenseRef-FXME-Commercial
  ------------------------------------------------------------------------------
*/

#pragma once

#include <JuceHeader.h>
#include "../PluginProcessor.h"
#include "../Dsp/AnalysisEngine.h"
#include "../AppSettings.h"
#include "../Theme.h"
#include "../Tooltips.h"
#include "TransferFunctionPlot.h"

class AnalysisComponent : public juce::Component
{
public:
    explicit AnalysisComponent (SuperMoToAudioProcessor& p)
        : processor (p), session (p.workspace.analysis), analysis (session.engine)
    {
        title.setText ("Analysis & correction design", juce::dontSendNotification);
        title.setFont (juce::Font (juce::FontOptions (17.0f, juce::Font::bold)));
        title.setColour (juce::Label::textColourId, SuperMoToTheme::text);
        addAndMakeVisible (title);

        // Read-only reminder of the (global) mic calibration applied to the data;
        // it is loaded/cleared in the Measurement & Calibration pane.
        micCalInfo.setFont (juce::Font (juce::FontOptions (11.0f)));
        micCalInfo.setJustificationType (juce::Justification::centredRight);
        micCalInfo.setColour (juce::Label::textColourId, SuperMoToTheme::dimText);
        micCalInfo.setTooltip (smt::tips::shared::micCalInfo);
        addAndMakeVisible (micCalInfo);

        // Which mic calibration is divided out: the global one (loaded in the
        // Measurement pane), the curve embedded in the measurements' folder
        // manifest (measurement.xml, found next to the loaded files), or none.
        micCalSourceBox.addItem ("Global mic cal", 1);
        micCalSourceBox.addItem ("Folder mic cal", 2);
        micCalSourceBox.addItem ("No mic cal", 3);
        micCalSourceBox.setSelectedId (1, juce::dontSendNotification);
        micCalSourceBox.setItemEnabled (2, false);   // until loaded files provide one
        micCalSourceBox.setTooltip (smt::tips::ana::micCalSource);
        SuperMoToTheme::accentComboBox (micCalSourceBox, SuperMoToTheme::spectrum);
        micCalSourceBox.onChange = [this]
        {
            session.settings.micCalSource = micCalSourceBox.getSelectedId();
            pushMicCalibration();
            updatePlotData();
        };
        addAndMakeVisible (micCalSourceBox);

        loadButton.setButtonText ("Load measurements...");
        loadButton.setTooltip (smt::tips::ana::load);
        loadButton.onClick = [this] { loadFiles(); };
        addAndMakeVisible (loadButton);

        addLabel (windowLabel, "Welch window");
        updateWindowLabel();
        for (int size = 1 << 14; size <= 1 << 18; size <<= 1)
            windowBox.addItem (juce::String (size), size);
        windowBox.setSelectedId (65536, juce::dontSendNotification);
        SuperMoToTheme::accentComboBox (windowBox, SuperMoToTheme::spectrum);
        windowBox.onChange = [this]
        {
            session.setWindowSize (windowBox.getSelectedId());
            if (session.hasFiles())
                showAnalysis();
        };
        addAndMakeVisible (windowBox);

        // Frequency-dependent smoothing: separate octave fraction for the low
        // and the high end, log-interpolated across frequency by the engine.
        // Finer in the bass (resolve modes), broader in the treble (trends only).
        addLabel (smoothLabel, "Smooth LF/HF");
        auto setupSmoothBox = [this] (juce::ComboBox& box, int& setting, const juce::String& tip)
        {
            box.addItem ("Off", 1);
            box.addItem ("1/24 oct", 2);
            box.addItem ("1/12 oct", 3);
            box.addItem ("1/6 oct", 4);
            box.addItem ("1/3 oct", 5);
            box.addItem ("1/2 oct", 6);
            box.addItem ("1 oct", 7);
            box.setSelectedId (4, juce::dontSendNotification);
            box.setTooltip (tip);
            SuperMoToTheme::accentComboBox (box, SuperMoToTheme::spectrum);
            box.onChange = [this, &box, &setting] { setting = box.getSelectedId(); settingChanged(); };
            addAndMakeVisible (box);
        };
        setupSmoothBox (smoothLowBox,  session.settings.smoothLowId,  smt::tips::shared::smoothLow);
        setupSmoothBox (smoothHighBox, session.settings.smoothHighId, smt::tips::shared::smoothHigh);

        // Transfer-function estimation method; "Sweep" auto-selects when the
        // loaded files' manifest carries the sweep identity.
        addLabel (tfLabel, "TF");
        tfBox.addItem ("Welch", 1);
        tfBox.addItem ("Sweep (Farina)", 2);
        tfBox.setSelectedId (1, juce::dontSendNotification);
        tfBox.setItemEnabled (2, false);
        tfBox.setTooltip (smt::tips::shared::tfMethod);
        SuperMoToTheme::accentComboBox (tfBox, SuperMoToTheme::spectrum);
        tfBox.onChange = [this]
        {
            updateWindowLabel();
            session.setSweepMethod (tfBox.getSelectedId() == 2);
            if (session.hasFiles())
                showAnalysis();
        };
        addAndMakeVisible (tfBox);

        addLabel (levelLabel, "Correction level");
        levelSlider.setSliderStyle (juce::Slider::LinearHorizontal);
        levelSlider.setRange (0.0, 1.0, 0.01);
        levelSlider.setValue (1.0, juce::dontSendNotification);
        levelSlider.setDoubleClickReturnValue (true, 1.0);
        SuperMoToTheme::accentSlider (levelSlider, SuperMoToTheme::master);
        levelSlider.setTooltip (smt::tips::shared::correctionLevel);
        levelSlider.onValueChange = [this]
        {
            session.settings.correctionLevel = (float) levelSlider.getValue();
            settingChanged();
        };
        addAndMakeVisible (levelSlider);

        addLabel (boostLabel, "Max boost");
        boostSlider.setSliderStyle (juce::Slider::LinearHorizontal);
        boostSlider.setRange (0.0, 24.0, 0.5);
        boostSlider.setValue (12.0, juce::dontSendNotification);
        boostSlider.setDoubleClickReturnValue (true, 12.0);
        boostSlider.setTextValueSuffix (" dB");
        SuperMoToTheme::accentSlider (boostSlider, SuperMoToTheme::master);
        boostSlider.setTooltip (smt::tips::shared::maxBoost);
        boostSlider.onValueChange = [this]
        {
            session.settings.maxBoostDb = (float) boostSlider.getValue();
            settingChanged();
        };
        addAndMakeVisible (boostSlider);

        // Frequency band the analysis acts on: outside it the correction is
        // unity and the exported measured IR is rolled off. Editable, with a
        // few standard values preset.
        addLabel (rangeLabel, "Range");
        auto setupFreqBox = [this] (juce::ComboBox& box, juce::String& setting,
                                    std::initializer_list<int> presets, int def)
        {
            for (int f : presets)
                box.addItem (juce::String (f) + " Hz", f);
            box.setEditableText (true);
            box.setSelectedId (def, juce::dontSendNotification);
            SuperMoToTheme::accentComboBox (box, SuperMoToTheme::spectrum);
            box.onChange = [this, &box, &setting] { setting = box.getText(); settingChanged(); };
            addAndMakeVisible (box);
        };
        setupFreqBox (lowFreqBox,  session.settings.lowFreqText,
                      { 20, 30, 40, 50, 60, 80, 100, 150, 200, 300 }, 20);
        setupFreqBox (highFreqBox, session.settings.highFreqText,
                      { 5000, 8000, 10000, 12000, 15000, 16000, 18000, 20000 }, 20000);
        lowFreqBox.setTooltip (smt::tips::shared::rangeLow);
        highFreqBox.setTooltip (smt::tips::shared::rangeHigh);
        addLabel (rangeToLabel, juce::String::fromUTF8 ("\xe2\x80\x93"));    // en dash
        rangeToLabel.setJustificationType (juce::Justification::centred);

        // Vertical reference of the measured curves (display only — the
        // engine's normalized math is untouched).
        addLabel (levelRefLabel, "Level");
        levelRefBox.addItem ("Normalized", 1);
        levelRefBox.addItem ("Absolute dB", 2);
        levelRefBox.addItem ("dB SPL", 3);
        levelRefBox.setSelectedId (1, juce::dontSendNotification);
        levelRefBox.setItemEnabled (3, false);   // needs SPL cal + run info
        levelRefBox.setTooltip (smt::tips::shared::levelRef);
        SuperMoToTheme::accentComboBox (levelRefBox, SuperMoToTheme::spectrum);
        levelRefBox.onChange = [this]
        {
            session.settings.levelRefId = levelRefBox.getSelectedId();
            updatePlotData();
            plot.fitVerticalToData();   // the sensible window jumps between modes
        };
        addAndMakeVisible (levelRefBox);

        addLabel (firLabel, "FIR length");
        // Short FIRs (few taps) are cheaper and lower-latency but can only
        // correct higher frequencies; long FIRs reach the low end. See firInfo.
        for (int size = 1 << 8; size <= 1 << 16; size <<= 1)
            firBox.addItem (juce::String (size), size);
        firBox.setSelectedId (4096, juce::dontSendNotification);
        SuperMoToTheme::accentComboBox (firBox, SuperMoToTheme::fir);
        firBox.onChange = [this]
        {
            session.settings.firLength = firBox.getSelectedId();
            updateFirInfo();
            refreshIrIfVisible();
        };
        addAndMakeVisible (firBox);

        // Linear/mixed-phase corrects magnitude AND phase but adds firLength/2
        // latency; minimum-phase corrects magnitude only with ~no latency (and
        // drops the subwoofer phase alignment). See firInfo / the manual.
        addLabel (phaseLabel, "Phase");
        phaseBox.addItem ("Linear phase", 1);
        phaseBox.addItem ("Min phase", 2);
        phaseBox.setSelectedId (1, juce::dontSendNotification);
        phaseBox.setTooltip (smt::tips::shared::phaseType);
        SuperMoToTheme::accentComboBox (phaseBox, SuperMoToTheme::fir);
        phaseBox.onChange = [this]
        {
            session.settings.minimumPhase = phaseBox.getSelectedId() == 2;
            session.applySettings();
            updateFirInfo();
        };
        addAndMakeVisible (phaseBox);

        firInfo.setFont (juce::Font (juce::FontOptions (12.0f)));
        firInfo.setColour (juce::Label::textColourId, SuperMoToTheme::fir);
        addAndMakeVisible (firInfo);

        addLabel (assignLabel, "Assign to");
        assignBox.addItem ("(none)", 1);
        for (int o = 0; o < smt::numChannels; ++o)
            assignBox.addItem ("Output " + juce::String (o + 1), o + 2);
        assignBox.setSelectedId (1, juce::dontSendNotification);
        SuperMoToTheme::accentComboBox (assignBox, SuperMoToTheme::fir);
        assignBox.onChange = [this] { session.settings.assignOutputId = assignBox.getSelectedId(); };
        addAndMakeVisible (assignBox);

        exportMeasuredButton.setButtonText ("Export IR...");
        exportMeasuredButton.setColour (juce::TextButton::buttonColourId,
                                        SuperMoToTheme::spectrum.darker (1.0f));
        exportMeasuredButton.onClick = [this] { exportMeasuredIr(); };
        addAndMakeVisible (exportMeasuredButton);

        exportButton.setButtonText ("Export correction IR...");
        exportButton.setColour (juce::TextButton::buttonColourId, SuperMoToTheme::fir.darker (1.0f));
        exportButton.onClick = [this] { exportIr(); };
        addAndMakeVisible (exportButton);

        // ── Optional subwoofer phase integration ─────────────────────────────
        loadSubButton.setButtonText ("Load sub measurements...");
        loadSubButton.setColour (juce::TextButton::buttonColourId, SuperMoToTheme::mono.darker (1.4f));
        loadSubButton.setTooltip (smt::tips::ana::loadSub);
        loadSubButton.onClick = [this] { loadSubFiles(); };
        addAndMakeVisible (loadSubButton);

        addLabel (crossoverLabel, "Crossover");
        for (int f : { 40, 50, 60, 70, 80, 100, 120, 150 })
            crossoverBox.addItem (juce::String (f) + " Hz", f);
        crossoverBox.setEditableText (true);
        crossoverBox.setSelectedId (80, juce::dontSendNotification);
        SuperMoToTheme::accentComboBox (crossoverBox, SuperMoToTheme::mono);
        crossoverBox.onChange = [this]
        {
            session.settings.crossoverText = crossoverBox.getText();
            settingChanged();
        };
        addAndMakeVisible (crossoverBox);

        subInvertToggle.setButtonText ("Invert sub");
        SuperMoToTheme::accentToggleButton (subInvertToggle, SuperMoToTheme::mono);
        subInvertToggle.setTooltip (smt::tips::shared::subInvert);
        subInvertToggle.onClick = [this]
        {
            session.settings.subInverted = subInvertToggle.getToggleState();
            settingChanged();
        };
        addAndMakeVisible (subInvertToggle);

        // Time-alignment: the slider is the bulk delay you apply physically to
        // the mains; the correction is then designed for the residual only. The
        // right-justified info text (ending in a left arrow) replaces a separate
        // label on this row.
        addLabel (alignLabel, "Mains delay");
        alignLabel.setVisible (false);
        alignSlider.setSliderStyle (juce::Slider::LinearHorizontal);
        alignSlider.setRange (-40.0, 40.0, 0.1);
        alignSlider.setValue (0.0, juce::dontSendNotification);
        alignSlider.setDoubleClickReturnValue (true, 0.0);
        alignSlider.setTextValueSuffix (" ms");
        // Bipolar: the fill grows from 0 ms. setCentralValue rather than the
        // "drawFromCentre" property, which hardcodes the track's geometric
        // midpoint and so is only right while this range stays symmetric.
        alignSlider.setCentralValue (0.0);
        SuperMoToTheme::accentSlider (alignSlider, SuperMoToTheme::delay);
        alignSlider.setTooltip (smt::tips::ana::mainsDelay);
        alignSlider.onValueChange = [this]
        {
            session.settings.mainsDelayMs = (float) alignSlider.getValue();
            settingChanged();
            updateAlignInfo();
        };
        addAndMakeVisible (alignSlider);

        alignInfo.setFont (juce::Font (juce::FontOptions (12.0f)));
        alignInfo.setJustificationType (juce::Justification::centredRight);
        alignInfo.setColour (juce::Label::textColourId, SuperMoToTheme::mono.brighter (0.3f));
        addAndMakeVisible (alignInfo);

        // When set, assigning the exported correction to an output also writes
        // the Mains-delay value above onto that output's bulk delay — the
        // physical time-alignment the correction was designed around.
        applyDelayToggle.setButtonText ("Apply bulk delay");
        SuperMoToTheme::accentToggleButton (applyDelayToggle, SuperMoToTheme::mono);
        applyDelayToggle.setTooltip (smt::tips::ana::applyDelay);
        applyDelayToggle.onClick = [this]
        {
            session.settings.applyBulkDelay = applyDelayToggle.getToggleState();
        };
        addAndMakeVisible (applyDelayToggle);

        status.setColour (juce::Label::textColourId, SuperMoToTheme::spectrum);
        addAndMakeVisible (status);

        addAndMakeVisible (plot);
        // The frequency window the plot was left at, restored before the
        // callback below is wired so the restore itself triggers nothing.
        if (session.settings.viewHighHz > session.settings.viewLowHz
            && session.settings.viewLowHz > 0.0f)
            plot.setFreqWindow (session.settings.viewLowHz, session.settings.viewHighHz);

        plot.onViewChanged = [this]
        {
            session.settings.viewLowHz = plot.getViewLowHz();
            session.settings.viewHighHz = plot.getViewHighHz();
            buildFreqGrid();
            updatePlotData();
        };

        // Impulse-response view (fxme::WaveformDisplay), swapped in for the
        // frequency plot by the View selector. Shows the measured average IR
        // and the corrected prediction at the selected FIR length, t = 0 on
        // the (linear-phase) centre.
        addLabel (displayLabel, "View");
        displayBox.addItem ("Frequency response", 1);
        displayBox.addItem ("Impulse response", 2);
        displayBox.setSelectedId (1, juce::dontSendNotification);
        displayBox.setTooltip (smt::tips::ana::display);
        SuperMoToTheme::accentComboBox (displayBox, SuperMoToTheme::spectrum);
        displayBox.onChange = [this]
        {
            session.settings.showIr = displayBox.getSelectedId() == 2;
            updateDisplayMode();
        };
        addAndMakeVisible (displayBox);

        irPlot.setColours (SuperMoToTheme::waveformColours());
        irPlot.setChannelColours ({ SuperMoToTheme::curveAverage, SuperMoToTheme::fir });
        irPlot.setChannelNames ({ "measured", "corrected" });
        addChildComponent (irPlot);     // hidden until the View selector says so

        // Everything the controls show comes from the session, which may hold
        // a whole analysis from before the editor was last closed. Nothing is
        // analyzed again here.
        syncControlsFromSettings();
        buildFreqGrid();
        status.setText (session.status, juce::dontSendNotification);
        updateMicCalInfo();
        updateFirInfo();
        updateLevelRefChoices();
        updateAlignInfo();
        updateDisplayMode();
        updatePlotData();
    }


    /** The window size means two different things depending on the estimator:
        the Welch segment length, or the length of IR kept after the sweep
        deconvolution. Same control, same effect on resolution, but calling it a
        Welch window in sweep mode is simply wrong, so the label follows. */
    void updateWindowLabel()
    {
        const bool sweep = tfBox.getSelectedId() == 2;
        windowLabel.setText (sweep ? "IR gate" : "Welch window", juce::dontSendNotification);
        windowBox.setTooltip (sweep ? smt::tips::shared::windowGate
                                    : smt::tips::shared::windowWelch);
    }

    // Recommendation + matrix instruction shown while a sub set is loaded.
    void updateAlignInfo()
    {
        if (! analysis.hasSub())
        {
            alignInfo.setText ({}, juce::dontSendNotification);
            return;
        }

        const float v = (float) alignSlider.getValue();
        juce::String msg = "~" + juce::String (session.recommendedAlignMs, 0)
            + " ms main/sub offset detected.  ";

        if (std::abs (v) < 0.05f)
            msg += "Set the Mains delay to time-align (correction designed for the residual).";
        else
            msg += "Add " + juce::String (std::abs (v), 1) + " ms delay to the "
                 + juce::String (v >= 0.0f ? "main output(s)" : "subwoofer output")
                 + " (or tick Apply bulk delay).";

        // Ends with a left arrow, just left of the Mains-delay slider.
        msg += juce::String::fromUTF8 ("  \xe2\x86\x90");

        alignInfo.setText (msg, juce::dontSendNotification);
    }

    //==========================================================================
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
        auto titleRow = area.removeFromTop (26);
        micCalSourceBox.setBounds (titleRow.removeFromRight (130).reduced (0, 1));
        titleRow.removeFromRight (8);
        micCalInfo.setBounds (titleRow.removeFromRight (240));
        title.setBounds (titleRow.withTrimmedLeft (30));   // room for the info button
        area.removeFromTop (6);

        // The two export buttons share a right-aligned column: "Export IR" on
        // row 1 and "Export correction IR" directly below it on row 2, both
        // flush to the right edge (same width) so they always line up.
        constexpr int exportW = 165;

        // Row 1 — load the measurements, then how they become transfer
        // functions (window, method, smoothing, range); Export IR right.
        auto r1 = area.removeFromTop (24);
        exportMeasuredButton.setBounds (r1.removeFromRight (exportW));
        loadButton.setBounds (r1.removeFromLeft (170));
        r1.removeFromLeft (16);
        windowLabel.setBounds (r1.removeFromLeft (90));
        windowBox.setBounds (r1.removeFromLeft (90));
        r1.removeFromLeft (12);
        tfLabel.setBounds (r1.removeFromLeft (22));
        tfBox.setBounds (r1.removeFromLeft (130));
        r1.removeFromLeft (16);
        smoothLabel.setBounds (r1.removeFromLeft (96));
        smoothLowBox.setBounds (r1.removeFromLeft (78));
        r1.removeFromLeft (4);
        smoothHighBox.setBounds (r1.removeFromLeft (78));
        r1.removeFromLeft (16);
        rangeLabel.setBounds (r1.removeFromLeft (50));
        lowFreqBox.setBounds (r1.removeFromLeft (86));
        rangeToLabel.setBounds (r1.removeFromLeft (12));
        highFreqBox.setBounds (r1.removeFromLeft (86));

        // Row 2: correction level, max boost, Phase, FIR length, Assign to,
        // Export correction IR (aligned under "Export IR"). The two sliders
        // share whatever width is left after the fixed labels/boxes.
        area.removeFromTop (8);
        auto r2 = area.removeFromTop (24);
        exportButton.setBounds (r2.removeFromRight (exportW));
        r2.removeFromRight (16);

        const int fixedW  = 100 + 64 + 40 + 110 + 64 + 90 + 58 + 110;
        const int sliderW = juce::jmax (96, (r2.getWidth() - fixedW - 12 * 4) / 2);

        levelLabel.setBounds (r2.removeFromLeft (100));
        levelSlider.setBounds (r2.removeFromLeft (sliderW));
        r2.removeFromLeft (12);
        boostLabel.setBounds (r2.removeFromLeft (64));
        boostSlider.setBounds (r2.removeFromLeft (sliderW));
        r2.removeFromLeft (12);
        phaseLabel.setBounds (r2.removeFromLeft (40));
        phaseBox.setBounds (r2.removeFromLeft (110));
        r2.removeFromLeft (12);
        firLabel.setBounds (r2.removeFromLeft (64));
        firBox.setBounds (r2.removeFromLeft (90));
        r2.removeFromLeft (12);
        assignLabel.setBounds (r2.removeFromLeft (58));
        assignBox.setBounds (r2.removeFromLeft (110));

        // Row 3: FIR info line on the left, subwoofer controls on the right.
        area.removeFromTop (8);
        auto r3 = area.removeFromTop (24);
        subInvertToggle.setBounds (r3.removeFromRight (96));
        r3.removeFromRight (16);
        crossoverBox.setBounds (r3.removeFromRight (90));
        crossoverLabel.setBounds (r3.removeFromRight (66));
        r3.removeFromRight (16);
        loadSubButton.setBounds (r3.removeFromRight (190));
        r3.removeFromRight (16);
        firInfo.setBounds (r3);

        // Row 4, right-aligned: the delay-correction info text (right-justified,
        // ending in a left arrow), then the Mains-delay slider, then the
        // Apply-bulk-delay toggle.
        area.removeFromTop (6);
        auto r4 = area.removeFromTop (22);
        applyDelayToggle.setBounds (r4.removeFromRight (150));
        r4.removeFromRight (16);
        alignSlider.setBounds (r4.removeFromRight (220));
        r4.removeFromRight (16);
        alignInfo.setBounds (r4);

        area.removeFromTop (4);
        status.setBounds (area.removeFromBottom (20));
        area.removeFromBottom (4);

        // What the plot below shows — kept directly above it.
        auto rD = area.removeFromTop (24);
        displayLabel.setBounds (rD.removeFromLeft (36));
        displayBox.setBounds (rD.removeFromLeft (150));
        rD.removeFromLeft (16);
        levelRefLabel.setBounds (rD.removeFromLeft (40));
        levelRefBox.setBounds (rD.removeFromLeft (110));

        area.removeFromTop (6);
        plot.setBounds (area);
        irPlot.setBounds (area);    // same slot; View selector swaps them
    }

private:
    void addLabel (juce::Label& l, const juce::String& text)
    {
        l.setText (text, juce::dontSendNotification);
        l.setFont (juce::Font (juce::FontOptions (12.0f)));
        l.setColour (juce::Label::textColourId, SuperMoToTheme::dimText);
        addAndMakeVisible (l);
    }

    // The curves are sampled over the plot's current frequency window, so
    // zooming in keeps full resolution instead of stretching the full-range
    // grid (the plot calls back through onViewChanged when it moves).
    void buildFreqGrid()
    {
        freqs = TransferFunctionPlot::freqGridFor (plot.getViewLowHz(), plot.getViewHighHz());
    }

    /** A control wrote a setting that does not invalidate the analysis:
        push it and redraw. */
    void settingChanged()
    {
        session.applySettings();
        updatePlotData();
    }

    void setStatus (const juce::String& text)
    {
        session.status = text;
        status.setText (text, juce::dontSendNotification);
    }

    /** Sets every control from the session's settings, without notifications. */
    void syncControlsFromSettings()
    {
        const auto& st = session.settings;

        micCalSourceBox.setItemEnabled (2, session.hasFolderMicCal());
        micCalSourceBox.setSelectedId (st.micCalSource, juce::dontSendNotification);
        windowBox.setSelectedId (st.windowSize, juce::dontSendNotification);
        tfBox.setItemEnabled (2, session.isSweepAvailable());
        tfBox.setSelectedId (st.sweepMethod ? 2 : 1, juce::dontSendNotification);
        updateWindowLabel();
        smoothLowBox.setSelectedId (st.smoothLowId, juce::dontSendNotification);
        smoothHighBox.setSelectedId (st.smoothHighId, juce::dontSendNotification);
        lowFreqBox.setText (st.lowFreqText, juce::dontSendNotification);
        highFreqBox.setText (st.highFreqText, juce::dontSendNotification);

        levelSlider.setValue (st.correctionLevel, juce::dontSendNotification);
        boostSlider.setValue (st.maxBoostDb, juce::dontSendNotification);
        phaseBox.setSelectedId (st.minimumPhase ? 2 : 1, juce::dontSendNotification);
        firBox.setSelectedId (st.firLength, juce::dontSendNotification);
        assignBox.setSelectedId (st.assignOutputId, juce::dontSendNotification);

        crossoverBox.setText (st.crossoverText, juce::dontSendNotification);
        subInvertToggle.setToggleState (st.subInverted, juce::dontSendNotification);
        alignSlider.setValue (st.mainsDelayMs, juce::dontSendNotification);
        applyDelayToggle.setToggleState (st.applyBulkDelay, juce::dontSendNotification);

        levelRefBox.setSelectedId (st.levelRefId, juce::dontSendNotification);
        displayBox.setSelectedId (st.showIr ? 2 : 1, juce::dontSendNotification);
    }

    void loadFiles()
    {
        fileChooser = std::make_unique<juce::FileChooser> (
            "Select the measurement files of one speaker",
            smt::getLastBrowseDir(), "*.wav");

        fileChooser->launchAsync (juce::FileBrowserComponent::openMode
                                  | juce::FileBrowserComponent::canSelectFiles
                                  | juce::FileBrowserComponent::canSelectMultipleItems,
            [this] (const juce::FileChooser& fc)
            {
                if (fc.getResults().isEmpty())
                    return;
                smt::setLastBrowseDir (fc.getResults()[0]);

                // The session reads the manifest next to the files and adopts
                // its mic cal and sweep identity, which the controls then show.
                session.loadFiles (fc.getResults());
                syncControlsFromSettings();
                showAnalysis();
            });
    }

    void loadSubFiles()
    {
        if (! analysis.hasData())
        {
            setStatus ("Load the main measurements first.");
            return;
        }

        fileChooser = std::make_unique<juce::FileChooser> (
            "Select the subwoofer measurements (same positions as the main set)",
            smt::getLastBrowseDir(), "*.wav");

        fileChooser->launchAsync (juce::FileBrowserComponent::openMode
                                  | juce::FileBrowserComponent::canSelectFiles
                                  | juce::FileBrowserComponent::canSelectMultipleItems,
            [this] (const juce::FileChooser& fc)
            {
                if (fc.getResults().isEmpty())
                    return;
                smt::setLastBrowseDir (fc.getResults()[0]);
                session.loadSubFiles (fc.getResults());
                status.setText (session.status, juce::dontSendNotification);
                updateAlignInfo();
                updatePlotData();
            });
    }

    /** Redraws everything that follows an analysis the session just ran. */
    void showAnalysis()
    {
        status.setText (session.status, juce::dontSendNotification);
        updateMicCalInfo();
        updateFirInfo();
        updateLevelRefChoices();
        updateAlignInfo();
        updatePlotData();
    }

    // Push the selected mic calibration into the engine and refresh the
    // read-only reminder. The engine divides it out of the measured data.
    void pushMicCalibration()
    {
        session.applySettings();
        updateMicCalInfo();
    }

    void updateMicCalInfo()
    {
        const auto& cal = session.activeMicCal();
        const bool fromFolder = session.settings.micCalSource == 2;
        micCalInfo.setText (cal.isValid()
                                ? "Mic cal: " + cal.getName()
                                    + (fromFolder ? " (folder)" : juce::String())
                                : juce::String ("Mic cal: none"),
                            juce::dontSendNotification);
    }

    // The calibration can change in the Measurement pane while this view is
    // hidden; re-apply it (and redraw) whenever the view becomes visible.
    void visibilityChanged() override
    {
        if (isVisible())
        {
            pushMicCalibration();
            updateLevelRefChoices();    // the SPL cal may have changed too
            updatePlotData();
        }
    }

    // Spell out what the selected FIR length can actually correct: a FIR of N
    // taps resolves down to a couple of bins (~2*fs/N), spans N/fs seconds and
    // adds N/2 samples of latency (the correction is centred in the FIR).
    void updateFirInfo()
    {
        const int N = firBox.getSelectedId();
        double fs = analysis.getSampleRate();
        const bool known = fs > 0.0;
        if (! known)
            fs = 48000.0;

        const bool minPhase = analysis.getPhaseType() == smt::AnalysisEngine::PhaseType::minimum;
        const double durMs  = 1000.0 * (double) N / fs;
        const double latMs  = 1000.0 * (double) (N / 2) / fs;
        const double fMinHz = 2.0 * fs / (double) N;

        firInfo.setText (
            juce::String::fromUTF8 ("\xe2\x86\x92 corrects down to ~")
                + juce::String (fMinHz, fMinHz < 100.0 ? 1 : 0) + " Hz   "
                + juce::String::fromUTF8 ("\xc2\xb7  ") + juce::String (durMs, 0) + " ms long  "
                + juce::String::fromUTF8 ("\xc2\xb7  ")
                + (minPhase ? juce::String ("~0 ms latency (min phase)")
                            : juce::String (latMs, 0) + " ms latency")
                + (known ? juce::String() : juce::String ("   (at 48 kHz)")),
            juce::dontSendNotification);
    }

    // Display offset for the measured curves per the Level selector. dB SPL:
    // absolute |H| + stimulus level (sine RMS, hence -3 dB) + dBFS->SPL offset
    // = the SPL each frequency actually played at during the sweep.
    float measuredOffsetDb() const
    {
        const int mode = levelRefBox.getSelectedId();
        if (mode == 2)
            return analysis.getReferenceDb();
        if (mode == 3)
        {
            float off = 0.0f, lvl = 0.0f;
            if (session.getSplContext (off, lvl))
                return analysis.getReferenceDb() + lvl - 3.0f + off;
        }
        return 0.0f;
    }

    juce::String levelAxisText() const
    {
        switch (levelRefBox.getSelectedId())
        {
            case 2:  return "|H| (dB, absolute); correction in dB";
            case 3:  return "est. dB SPL during the measurement; correction in dB";
            default: return {};     // the plot's normalized wording
        }
    }

    // dB SPL is only offered when it can actually be computed; falls back to
    // Normalized when the selected mode just became unavailable.
    void updateLevelRefChoices()
    {
        float off = 0.0f, lvl = 0.0f;
        const bool splOk = analysis.hasData() && session.getSplContext (off, lvl);
        levelRefBox.setItemEnabled (3, splOk);
        if (! splOk && levelRefBox.getSelectedId() == 3)
        {
            levelRefBox.setSelectedId (1, juce::dontSendNotification);
            session.settings.levelRefId = 1;
        }
    }

    void updatePlotData()
    {
        TransferFunctionPlot::Data d;
        for (int i = 0; i < analysis.getNumCurves(); ++i)
        {
            d.curveDbs.push_back (analysis.getCurveDb (i, freqs));
            d.curvePhases.push_back (analysis.getCurvePhaseDeg (i, freqs));
        }
        d.averageDb       = analysis.getAverageDb (freqs);
        d.correctionDb    = analysis.getCorrectionDb (freqs);
        d.correctedDb     = analysis.getCorrectedDb (freqs);
        d.averagePhase    = analysis.getAveragePhaseDeg (freqs);
        d.correctionPhase = analysis.getCorrectionPhaseDeg (freqs);
        d.correctedPhase  = analysis.getCorrectedPhaseDeg (freqs);
        d.subDb           = analysis.getSubDb (freqs);
        d.subPhase        = analysis.getSubPhaseDeg (freqs);
        d.hasSub          = analysis.hasSub();
        d.crossoverHz     = analysis.getCrossoverHz();
        for (int i = 0; i < analysis.getNumHarmonics(); ++i)
            d.harmonicDbs.push_back (analysis.getHarmonicDb (i, freqs));
        d.measuredOffsetDb = measuredOffsetDb();
        d.levelAxisText    = levelAxisText();
        plot.setData (std::move (d));

        refreshIrIfVisible();   // every data/design change funnels through here
    }

    //==========================================================================
    // Impulse-response view

    bool showingIr() const          { return displayBox.getSelectedId() == 2; }
    void refreshIrIfVisible()       { if (showingIr()) updateIrPlot(); }

    void updateDisplayMode()
    {
        plot.setVisible (! showingIr());
        irPlot.setVisible (showingIr());
        if (showingIr())
            updateIrPlot();
    }

    // Renders the measured average and the predictions (corrected, and the
    // main + sub sum) at the current FIR length into the waveform view.
    //
    // The correction filter's own impulse is deliberately NOT among them. It is
    // a filter gain, peak around 1, while the other traces are
    // microphone/stimulus ratios still carrying the measured mid-band level, so
    // on one linear axis it dwarfs them by 1/referenceGain — around 40 dB for a
    // normal measurement, which flattens everything else to a line. Nor is
    // anything lost: what makes a correction impulse worth reading (pre-ringing,
    // decay, whether it fits the FIR length) sits 40-60 dB below its peak and
    // needs a dB axis, so it was never visible here anyway.
    //
    // The user's zoom survives design tweaks; the view resets only when the
    // time axis itself changes (FIR length or sample rate).
    void updateIrPlot()
    {
        if (! analysis.hasData())
        {
            irPlot.clear();
            return;
        }

        const int N = firBox.getSelectedId();
        const double sr = analysis.getSampleRate();

        std::vector<juce::AudioBuffer<float>> traces;
        juce::StringArray names;
        std::vector<juce::Colour> cols;
        auto add = [&] (juce::AudioBuffer<float> b, const juce::String& n, juce::Colour c)
        {
            if (b.getNumSamples() == 0)
                return;
            traces.push_back (std::move (b));
            names.add (n);
            cols.push_back (c);
        };

        add (analysis.renderMeasuredIR (N),   "measured",   SuperMoToTheme::curveAverage);
        add (analysis.renderCorrectedIR (N),  "corrected",  SuperMoToTheme::fir);
        // This pane has no level matching, so the sub is summed at the level it
        // was measured at: the prediction if no trim is applied to it.
        if (analysis.hasSub())
            add (analysis.renderSystemIR (N, 0.0f), "+ sub (as measured)", SuperMoToTheme::mono);

        if (traces.empty())
        {
            irPlot.clear();
            return;
        }

        juce::AudioBuffer<float> both ((int) traces.size(), N);
        both.clear();
        for (int i = 0; i < (int) traces.size(); ++i)
            both.copyFrom (i, 0, traces[(size_t) i], 0, 0,
                           juce::jmin (N, traces[(size_t) i].getNumSamples()));

        irPlot.setChannelNames (names);
        irPlot.setChannelColours (cols);

        const bool resetView = N != lastIrLength || sr != lastIrRate;
        lastIrLength = N;
        lastIrRate = sr;
        if (resetView)
            irPlot.setTimeOffset ((double) (N / 2) / sr);   // t = 0 at the IR centre
        irPlot.setBuffer (both, sr, resetView);
    }

    void exportMeasuredIr()
    {
        if (! analysis.hasData())
        {
            setStatus ("Load measurements first.");
            return;
        }

        fileChooser = std::make_unique<juce::FileChooser> (
            "Export measured impulse response",
            smt::getLastBrowseDir().getChildFile ("measurement.wav"), "*.wav");

        fileChooser->launchAsync (juce::FileBrowserComponent::saveMode
                                  | juce::FileBrowserComponent::canSelectFiles
                                  | juce::FileBrowserComponent::warnAboutOverwriting,
            [this] (const juce::FileChooser& fc)
            {
                auto file = fc.getResult();
                if (file == juce::File())
                    return;
                if (! file.hasFileExtension ("wav"))
                    file = file.withFileExtension ("wav");
                smt::setLastBrowseDir (file);

                setStatus (analysis.exportMeasuredIR (file, firBox.getSelectedId())
                               ? "Exported " + file.getFileName()
                               : "Export failed.");
            });
    }

    void exportIr()
    {
        if (! analysis.hasData())
        {
            setStatus ("Load measurements first.");
            return;
        }

        fileChooser = std::make_unique<juce::FileChooser> (
            "Export correction impulse response",
            smt::getLastBrowseDir().getChildFile ("correction.wav"), "*.wav");

        fileChooser->launchAsync (juce::FileBrowserComponent::saveMode
                                  | juce::FileBrowserComponent::canSelectFiles
                                  | juce::FileBrowserComponent::warnAboutOverwriting,
            [this] (const juce::FileChooser& fc)
            {
                auto file = fc.getResult();
                if (file == juce::File())
                    return;
                if (! file.hasFileExtension ("wav"))
                    file = file.withFileExtension ("wav");
                smt::setLastBrowseDir (file);

                if (! analysis.exportCorrectionIR (file, firBox.getSelectedId()))
                {
                    setStatus ("Export failed.");
                    return;
                }

                juce::String msg = "Exported " + file.getFileName();

                const int assignOut = assignBox.getSelectedId() - 2;
                if (assignOut >= 0)
                {
                    auto s = processor.configModel.getOutput (assignOut);
                    s.firPath = file.getFullPathName();
                    s.firOn = true;
                    msg << juce::String::fromUTF8 (" \xe2\x80\x94 assigned to output ")
                        << juce::String (assignOut + 1);

                    // Optionally also write the bulk (mains) delay onto the output,
                    // clamped to the output's non-negative delay range.
                    if (applyDelayToggle.getToggleState())
                    {
                        s.delayMs = juce::jlimit (0.0f, (float) smt::maxDelayMs,
                                                  (float) alignSlider.getValue());
                        msg << " (delay " << juce::String (s.delayMs, 1) << " ms)";
                    }

                    processor.configModel.setOutput (assignOut, s);
                    processor.engine.updateFirFiles();

                    // Unlike Group analysis, this pane does not know which
                    // output feeds the sub, so it cannot write the polarity
                    // the correction was designed around. Say so instead.
                    if (analysis.hasSub() && subInvertToggle.getToggleState())
                        msg << ". Invert sub is on: set Phase inv. on the subwoofer output.";
                }

                setStatus (msg);
            });
    }

    SuperMoToAudioProcessor& processor;
    smt::AnalysisSession& session;
    smt::AnalysisEngine& analysis;          // session.engine

    juce::Label title, windowLabel, smoothLabel, levelLabel, firLabel, assignLabel, boostLabel, status;
    juce::Label micCalInfo;
    juce::Label firInfo, rangeLabel, rangeToLabel, crossoverLabel, phaseLabel, levelRefLabel;
    juce::Label alignLabel, alignInfo;
    fxme::FxmeSlider boostSlider, alignSlider;
    juce::TextButton loadButton, exportButton, exportMeasuredButton, loadSubButton;

    juce::ComboBox windowBox, smoothLowBox, smoothHighBox, firBox, phaseBox, assignBox, lowFreqBox, highFreqBox, crossoverBox;
    juce::ComboBox micCalSourceBox, levelRefBox, displayBox, tfBox;
    juce::Label displayLabel, tfLabel;
    juce::ToggleButton subInvertToggle, applyDelayToggle;
    fxme::FxmeSlider levelSlider;

    std::vector<float> freqs;

    TransferFunctionPlot plot;
    fxme::WaveformDisplay irPlot;
    int lastIrLength = 0;
    double lastIrRate = 0.0;
    std::unique_ptr<juce::FileChooser> fileChooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AnalysisComponent)
};
