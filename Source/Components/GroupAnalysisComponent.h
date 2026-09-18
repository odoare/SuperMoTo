/*
  ------------------------------------------------------------------------------
    GroupAnalysisComponent.h

    Multi-speaker time-alignment & correction. Load a multi-position
    measurement set for each speaker in a group (plus one shared subwoofer
    set), share one set of correction-design settings across every speaker
    (window size, smoothing, correction level, max boost, phase type, analysis
    range, crossover, sub polarity — mirroring the single-speaker Analysis
    pane), compute the delay that time-aligns the whole group on the
    most-distant driver, then export each speaker's correction IR and apply
    delay + FIR to its assigned output in one step, together with a markdown
    report (smt::SpeakerGroupAnalysis).

    The component is a view. The group, its engines, the settings, the loaded
    folder and any background batch live in smt::GroupAnalysisSession, owned
    by the processor's workspace, so closing the editor loses none of it and a
    batch runs to the end without it. Controls write the session's settings;
    the session tells the view when it changes on its own (Listener).

    Author: Olivier Doaré, github.com/odoare
    Licenced under the GNU AGPL Version 3.0, or commercial terms (LICENSE.md)
    SPDX-License-Identifier: AGPL-3.0-or-later OR LicenseRef-FXME-Commercial
  ------------------------------------------------------------------------------
*/

#pragma once

#include <JuceHeader.h>
#include <set>
#include "../PluginProcessor.h"
#include "../Dsp/SpeakerGroupAnalysis.h"
#include "../AppSettings.h"
#include "../Theme.h"
#include "../Tooltips.h"
#include "TransferFunctionPlot.h"

class GroupAnalysisComponent : public juce::Component,
                               private juce::Timer,
                               private smt::GroupAnalysisSession::Listener
{
public:
    explicit GroupAnalysisComponent (SuperMoToAudioProcessor& p)
        : session (p.workspace.groupAnalysis), group (session.group)
    {
        title.setText ("Group analysis & multi-speaker alignment", juce::dontSendNotification);
        title.setFont (juce::Font (juce::FontOptions (17.0f, juce::Font::bold)));
        title.setColour (juce::Label::textColourId, SuperMoToTheme::text);
        addAndMakeVisible (title);

        micCalInfo.setFont (juce::Font (juce::FontOptions (11.0f)));
        micCalInfo.setJustificationType (juce::Justification::centredRight);
        micCalInfo.setColour (juce::Label::textColourId, SuperMoToTheme::dimText);
        micCalInfo.setTooltip (smt::tips::shared::micCalInfo);
        addAndMakeVisible (micCalInfo);

        // Which mic calibration is divided out: the global one (loaded in the
        // Measurement pane), the curve embedded in a loaded measurement
        // folder's manifest, or none. Loading a folder that carries one
        // auto-selects "Folder" (in the session).
        micCalSourceBox.addItem ("Global mic cal", 1);
        micCalSourceBox.addItem ("Folder mic cal", 2);
        micCalSourceBox.addItem ("No mic cal", 3);
        micCalSourceBox.setSelectedId (1, juce::dontSendNotification);
        micCalSourceBox.setItemEnabled (2, false);   // until a folder provides one
        micCalSourceBox.setTooltip (smt::tips::grp::micCalSource);
        SuperMoToTheme::accentComboBox (micCalSourceBox, SuperMoToTheme::spectrum);
        micCalSourceBox.onChange = [this]
        {
            session.settings.micCalSource = micCalSourceBox.getSelectedId();
            updateMicCalInfo();
            settingChanged();
        };
        addAndMakeVisible (micCalSourceBox);

        addLabel (countLabel, "Speakers");
        for (int n = 1; n <= smt::SpeakerGroupAnalysis::maxSpeakers; ++n)
            countBox.addItem (juce::String (n), n);
        countBox.setSelectedId (group.getNumSpeakers(), juce::dontSendNotification);
        SuperMoToTheme::accentComboBox (countBox, SuperMoToTheme::master);
        countBox.setTooltip (smt::tips::grp::count);
        countBox.onChange = [this] { setNumSpeakers (countBox.getSelectedId()); };
        addAndMakeVisible (countBox);

        subEnabledToggle.setButtonText ("Sub");
        subEnabledToggle.setToggleState (true, juce::dontSendNotification);
        SuperMoToTheme::accentToggleButton (subEnabledToggle, SuperMoToTheme::mono);
        subEnabledToggle.setTooltip (smt::tips::grp::subEnabled);
        subEnabledToggle.onClick = [this]
        {
            session.setSubEnabled (subEnabledToggle.getToggleState());
            setBusy (session.isBusy());     // refresh subRow's enablement to match
        };
        addAndMakeVisible (subEnabledToggle);

        computeButton.setButtonText ("Compute alignment");
        computeButton.setColour (juce::TextButton::buttonColourId, SuperMoToTheme::mono.darker (1.2f));
        computeButton.setTooltip (smt::tips::grp::compute);
        // The session applies a setting moved moments ago first, then notifies.
        computeButton.onClick = [this] { stopTimer(); session.computeAlignment(); };
        addAndMakeVisible (computeButton);

        applyButton.setButtonText ("Apply & export...");
        applyButton.setColour (juce::TextButton::buttonColourId, SuperMoToTheme::fir.darker (1.0f));
        applyButton.setTooltip (smt::tips::grp::apply);
        applyButton.onClick = [this] { applyAndExport(); };
        addAndMakeVisible (applyButton);

        // Everything a run exports is named "<prefix>_speakerN_correction.wav" /
        // "<prefix>_report.md", so several alignments can share one folder.
        addLabel (prefixLabel, "Prefix");
        prefixEditor.setColour (juce::TextEditor::backgroundColourId,
                                SuperMoToTheme::plotBackground.withAlpha (0.4f));
        prefixEditor.setTextToShowWhenEmpty ("(none)", SuperMoToTheme::dimText);
        prefixEditor.setTooltip (smt::tips::grp::prefix);
        prefixEditor.onTextChange = [this] { session.settings.prefix = prefixEditor.getText(); };
        addAndMakeVisible (prefixEditor);

        figuresToggle.setButtonText ("Figures");
        figuresToggle.setToggleState (true, juce::dontSendNotification);
        SuperMoToTheme::accentToggleButton (figuresToggle, SuperMoToTheme::spectrum);
        figuresToggle.setTooltip (smt::tips::grp::figures);
        figuresToggle.onClick = [this] { session.settings.figures = figuresToggle.getToggleState(); };
        addAndMakeVisible (figuresToggle);

        loadFolderButton.setButtonText ("Load measurement folder...");
        loadFolderButton.setColour (juce::TextButton::buttonColourId, SuperMoToTheme::spectrum.darker (1.0f));
        loadFolderButton.setTooltip (smt::tips::grp::loadFolder);
        loadFolderButton.onClick = [this] { loadMeasurementFolder(); };
        addAndMakeVisible (loadFolderButton);

        // Which of the loaded folder's measurement runs feed the analysis.
        runsButton.setButtonText ("Runs...");
        runsButton.setColour (juce::TextButton::buttonColourId, SuperMoToTheme::spectrum.darker (1.4f));
        runsButton.setTooltip (smt::tips::grp::runs);
        runsButton.onClick = [this] { showRunsDialog(); };
        runsButton.setEnabled (false);      // until a folder with a run log is loaded
        addAndMakeVisible (runsButton);

        status.setColour (juce::Label::textColourId, SuperMoToTheme::spectrum);
        addAndMakeVisible (status);

        // ── Shared correction-design controls (fanned out to every engine in
        // the group — one "tone" for the whole speaker set). ─────────────────
        addLabel (windowLabel, "Welch window");
        updateWindowLabel();
        for (int size = 1 << 14; size <= 1 << 18; size <<= 1)
            windowBox.addItem (juce::String (size), size);
        windowBox.setSelectedId (65536, juce::dontSendNotification);
        SuperMoToTheme::accentComboBox (windowBox, SuperMoToTheme::spectrum);
        windowBox.onChange = [this] { session.setWindowSize (windowBox.getSelectedId()); };
        addAndMakeVisible (windowBox);

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
        // loaded folder's manifest carries the sweep identity. Changing it
        // re-analyzes every loaded set (like the Welch window size).
        addLabel (tfLabel, "TF");
        tfBox.addItem ("Welch", 1);
        tfBox.addItem ("Sweep (Farina)", 2);
        tfBox.setSelectedId (1, juce::dontSendNotification);
        tfBox.setItemEnabled (2, false);
        tfBox.setTooltip (smt::tips::shared::tfMethod);
        SuperMoToTheme::accentComboBox (tfBox, SuperMoToTheme::spectrum);
        tfBox.onChange = [this] { updateWindowLabel(); session.setSweepMethod (tfBox.getSelectedId() == 2); };
        addAndMakeVisible (tfBox);

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
        addLabel (rangeToLabel, juce::String::fromUTF8 ("\xe2\x80\x93"));
        rangeToLabel.setJustificationType (juce::Justification::centred);

        addLabel (previewLabel, "Preview");
        SuperMoToTheme::accentComboBox (previewBox, SuperMoToTheme::spectrum);
        previewBox.setTooltip (smt::tips::grp::preview);
        previewBox.onChange = [this]
        {
            session.settings.previewId = previewBox.getSelectedId();
            updatePlotPreview();
        };
        addAndMakeVisible (previewBox);

        // Vertical reference of the measured curves (display only — the
        // engines' normalized math is untouched).
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
            updatePlotPreview();
            plot.fitVerticalToData();   // the sensible window jumps between modes
        };
        addAndMakeVisible (levelRefBox);

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

        addLabel (firLabel, "FIR length");
        for (int size = 1 << 8; size <= 1 << 16; size <<= 1)
            firBox.addItem (juce::String (size), size);
        firBox.setSelectedId (4096, juce::dontSendNotification);
        SuperMoToTheme::accentComboBox (firBox, SuperMoToTheme::fir);
        addAndMakeVisible (firBox);

        addLabel (phaseLabel, "Phase");
        phaseBox.addItem ("Linear phase", 1);
        phaseBox.addItem ("Min phase", 2);
        phaseBox.setSelectedId (1, juce::dontSendNotification);
        phaseBox.setTooltip (smt::tips::shared::phaseType);
        SuperMoToTheme::accentComboBox (phaseBox, SuperMoToTheme::fir);
        phaseBox.onChange = [this]
        {
            session.settings.minimumPhase = phaseBox.getSelectedId() == 2;
            updateFirInfo();
            settingChanged();
        };
        addAndMakeVisible (phaseBox);

        addLabel (crossoverLabel, "Crossover");
        for (int f : { 40, 50, 60, 70, 80, 100, 120, 150 })
            crossoverBox.addItem (juce::String (f) + " Hz", f);
        crossoverBox.setEditableText (true);
        crossoverBox.setSelectedId (80, juce::dontSendNotification);
        SuperMoToTheme::accentComboBox (crossoverBox, SuperMoToTheme::mono);
        crossoverBox.setTooltip (smt::tips::shared::crossover);
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

        // ── Per-speaker rows (pre-created up to maxSpeakers, shown/hidden as
        // the count changes) plus one dedicated sub row, in a scrollable list.
        for (int i = 0; i < smt::SpeakerGroupAnalysis::maxSpeakers; ++i)
        {
            auto* row = rows.add (new SpeakerRow());
            row->nameLabel.setText (group.speaker (i).label, juce::dontSendNotification);
            row->onLoad = [this, i] { loadSpeakerFiles (i); };
            row->outputBox.onChange = [this, i, row]
            {
                group.speaker (i).assignedOutput = row->outputBox.getSelectedId() - 2;
            };
            rowsHolder.addAndMakeVisible (row);
        }
        subRow.nameLabel.setText ("Sub", juce::dontSendNotification);
        subRow.nameLabel.setTooltip (smt::tips::grp::subName);
        subRow.onLoad = [this] { loadSubFiles(); };
        subRow.setSubMode (true);
        subRow.trimReadout.setTooltip (smt::tips::grp::subTrimLevel);
        // Coalesced like the other shared controls: applying the trim re-derives
        // the alignment, which re-designs every speaker's correction — far too
        // much to do once per drag tick. The slider writes the setting; the
        // timer has the session push it into the group.
        subRow.onTrimChanged = [this] (float ms)
        {
            session.settings.subTrimMs = ms;
            settingChanged();
        };
        subRow.onUseSuggestion = [this]
        {
            subRow.trimSlider.setValue ((double) group.getSuggestedSubTrimMs(),
                                        juce::sendNotificationSync);
        };
        subRow.outputBox.onChange = [this]
        {
            group.subEntry().assignedOutput = subRow.outputBox.getSelectedId() - 2;
        };
        rowsHolder.addAndMakeVisible (subRow);

        rowsViewport.setViewedComponent (&rowsHolder, false);
        rowsViewport.setScrollBarsShown (true, false);
        addAndMakeVisible (rowsViewport);

        addAndMakeVisible (plot);
        // The frequency window the plot was left at, restored before the
        // callback below is wired so the restore itself triggers nothing.
        if (session.settings.viewHighHz > session.settings.viewLowHz
            && session.settings.viewLowHz > 0.0f)
            plot.setFreqWindow (session.settings.viewLowHz, session.settings.viewHighHz);

        // Re-sample the curves over the new window on zoom/pan. setBusy()
        // freezes the plot's interaction while a background batch owns the
        // engines, so the axis can never move without the data following.
        plot.onViewChanged = [this]
        {
            session.settings.viewLowHz = plot.getViewLowHz();
            session.settings.viewHighHz = plot.getViewHighHz();
            buildFreqGrid();
            updatePlotPreview();
        };

        // Impulse-response view (fxme::WaveformDisplay), swapped in for the
        // frequency plot by the View selector; shows the previewed speaker's
        // measured average IR and its corrected prediction at the export FIR
        // length.
        addLabel (displayLabel, "View");
        displayBox.addItem ("Frequency response", 1);
        displayBox.addItem ("Impulse response", 2);
        displayBox.setSelectedId (1, juce::dontSendNotification);
        displayBox.setTooltip (smt::tips::grp::display);
        SuperMoToTheme::accentComboBox (displayBox, SuperMoToTheme::spectrum);
        displayBox.onChange = [this]
        {
            session.settings.showIr = displayBox.getSelectedId() == 2;
            updateDisplayMode();
        };
        addAndMakeVisible (displayBox);

        firBox.setTooltip (smt::tips::shared::firLength);
        firBox.onChange = [this]
        {
            session.settings.firLength = firBox.getSelectedId();
            updateFirInfo();
            refreshIrIfVisible();
        };

        firInfo.setFont (juce::Font (juce::FontOptions (12.0f)));
        firInfo.setColour (juce::Label::textColourId, SuperMoToTheme::fir);
        firInfo.setTooltip (smt::tips::shared::firInfo);
        addAndMakeVisible (firInfo);

        irPlot.setColours (SuperMoToTheme::waveformColours());
        irPlot.setChannelColours ({ SuperMoToTheme::curveAverage, SuperMoToTheme::fir });
        irPlot.setChannelNames ({ "measured", "corrected" });
        addChildComponent (irPlot);     // hidden until the View selector says so

        progressBar.setPercentageDisplay (true);
        addChildComponent (progressBar);   // shown only while a background batch runs

        // Everything the controls show comes from the session, which may hold a
        // whole analysis, or a batch still running, from before the editor was
        // last closed. Nothing is re-analyzed here.
        buildFreqGrid();
        session.addListener (this);
        refreshFromSession();
    }

    ~GroupAnalysisComponent() override
    {
        session.removeListener (this);

        // A setting moved in the last debounce interval is already in the
        // session; have it reach the engines now rather than at the next use.
        if (isTimerRunning())
        {
            stopTimer();
            session.applySettings();
        }
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
        auto titleRow = area.removeFromTop (26);
        micCalSourceBox.setBounds (titleRow.removeFromRight (130).reduced (0, 1));
        titleRow.removeFromRight (8);
        micCalInfo.setBounds (titleRow.removeFromRight (240));
        title.setBounds (titleRow.withTrimmedLeft (30));   // room for the info button
        area.removeFromTop (6);

        // Row 0 — the workflow, left to right: load the measurements, describe
        // the set, compute the alignment, apply & export.
        auto r0 = area.removeFromTop (24);
        loadFolderButton.setBounds (r0.removeFromLeft (190));
        r0.removeFromLeft (6);
        runsButton.setBounds (r0.removeFromLeft (96));
        r0.removeFromLeft (12);
        countLabel.setBounds (r0.removeFromLeft (62));
        countBox.setBounds (r0.removeFromLeft (56));
        r0.removeFromLeft (12);
        subEnabledToggle.setBounds (r0.removeFromLeft (56));
        r0.removeFromLeft (12);
        computeButton.setBounds (r0.removeFromLeft (140));
        r0.removeFromLeft (8);
        applyButton.setBounds (r0.removeFromLeft (140));
        r0.removeFromLeft (12);
        prefixLabel.setBounds (r0.removeFromLeft (42));
        prefixEditor.setBounds (r0.removeFromLeft (130).reduced (0, 1));
        r0.removeFromLeft (12);
        figuresToggle.setBounds (r0.removeFromLeft (86));
        r0.removeFromLeft (16);
        progressBar.setBounds (r0.removeFromRight (160));

        // Row 1 — how the measurements are turned into transfer functions.
        area.removeFromTop (8);
        auto r1 = area.removeFromTop (24);
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

        // Row 2 — how the correction is designed from them.
        area.removeFromTop (8);
        auto r2 = area.removeFromTop (24);
        levelLabel.setBounds (r2.removeFromLeft (100));
        levelSlider.setBounds (r2.removeFromLeft (110));
        r2.removeFromLeft (12);
        boostLabel.setBounds (r2.removeFromLeft (64));
        boostSlider.setBounds (r2.removeFromLeft (110));
        r2.removeFromLeft (12);
        phaseLabel.setBounds (r2.removeFromLeft (40));
        phaseBox.setBounds (r2.removeFromLeft (110));
        r2.removeFromLeft (12);
        firLabel.setBounds (r2.removeFromLeft (64));
        firBox.setBounds (r2.removeFromLeft (90));
        r2.removeFromLeft (10);
        // Crossover and Invert sub come off the RIGHT, so the read-out beside
        // the FIR length absorbs the slack in between and shrinks away on a
        // narrow window instead of pushing controls off the end.
        subInvertToggle.setBounds (r2.removeFromRight (96));
        r2.removeFromRight (12);
        crossoverBox.setBounds (r2.removeFromRight (90));
        crossoverLabel.setBounds (r2.removeFromRight (66));
        r2.removeFromRight (16);
        firInfo.setBounds (r2);

        area.removeFromTop (8);
        constexpr int rowH = 26;
        // Past a handful of speakers a single scrolling column wastes the
        // pane's width; split into two side-by-side columns instead (the sub
        // row stays full-width, below both columns — there's only ever one).
        constexpr int columnThreshold = 6;
        const int numColumns = group.getNumSpeakers() > columnThreshold ? 2 : 1;
        const int rowsPerColumn = (group.getNumSpeakers() + numColumns - 1) / numColumns;

        const int visibleRows = juce::jmin (5, rowsPerColumn) + 1;   // +1 for the sub row
        rowsViewport.setBounds (area.removeFromTop (visibleRows * rowH + 4));

        const int holderW = rowsViewport.getWidth() - rowsViewport.getScrollBarThickness() - 4;
        rowsHolder.setSize (holderW, (rowsPerColumn + 1) * rowH);

        auto full = rowsHolder.getLocalBounds();
        subRow.setBounds (full.removeFromBottom (rowH).reduced (0, 2));

        constexpr int columnGap = 8;
        const int colW = numColumns == 2 ? (full.getWidth() - columnGap) / 2 : full.getWidth();
        auto column1 = full.removeFromLeft (colW);
        full.removeFromLeft (numColumns == 2 ? columnGap : 0);
        auto& column2 = full;

        for (int i = 0; i < group.getNumSpeakers(); ++i)
        {
            auto& col = i < rowsPerColumn ? column1 : column2;
            rows[i]->setBounds (col.removeFromTop (rowH).reduced (0, 2));
        }

        // What the plot below shows — kept directly above it.
        area.removeFromTop (8);
        auto rD = area.removeFromTop (24);
        previewLabel.setBounds (rD.removeFromLeft (56));
        previewBox.setBounds (rD.removeFromLeft (110));
        rD.removeFromLeft (16);
        displayLabel.setBounds (rD.removeFromLeft (36));
        displayBox.setBounds (rD.removeFromLeft (150));
        rD.removeFromLeft (16);
        levelRefLabel.setBounds (rD.removeFromLeft (40));
        levelRefBox.setBounds (rD.removeFromLeft (110));
        // The status line lives here, right above the plot the user is
        // watching, where it has room for a full sentence.
        rD.removeFromLeft (20);
        status.setBounds (rD);

        area.removeFromTop (6);
        plot.setBounds (area);
        irPlot.setBounds (area);    // same slot; View selector swaps them
    }

    void visibilityChanged() override
    {
        // The global mic cal may have changed in the Calibration view meanwhile.
        if (isVisible())
        {
            updateMicCalInfo();
            if (session.applySettings())
                updatePlotPreview();
        }
    }

private:
    // Coalesces a burst of shared-control changes (e.g. a slider drag) into
    // one settings push + recompute per pause, instead of one per tick.
    static constexpr int debounceMs = 150;

    /** A control wrote a field of the session's settings. */
    void settingChanged()
    {
        session.settingsChanged();
        startTimer (debounceMs);
    }

    void timerCallback() override
    {
        stopTimer();
        // False while a batch owns the engines: the session keeps the change
        // and applies it when the batch ends, then notifies.
        if (session.applySettings())
        {
            updateSubTrimInfo();
            refreshRows (true);
            updatePlotPreview();
        }
    }

    //==========================================================================
    // Session -> view

    void groupSessionChanged() override     { refreshFromSession(); }

    /** Shows the session as it stands. While a batch runs, only what does not
        read an engine: the controls, the rows' files and outputs, the status
        and the progress bar. The rest follows when the batch ends and
        notifies again. */
    void refreshFromSession()
    {
        syncControlsFromSettings();
        syncRowsVisibility();
        if (! getLocalBounds().isEmpty())
            resized();      // the row count, hence the columns, may have changed

        const bool busy = session.isBusy();
        setBusy (busy);
        status.setText (session.getStatus(), juce::dontSendNotification);

        refreshRows (! busy);
        if (busy)
        {
            // Which plot is up, without filling it: that reads the engines.
            plot.setVisible (! showingIr());
            irPlot.setVisible (showingIr());
            return;
        }

        updateSubTrimInfo();
        updateFirInfo();
        updateDisplayMode();
        updatePlotPreview();
    }

    void syncControlsFromSettings()
    {
        const auto& s = session.settings;

        windowBox.setSelectedId (s.windowSize, juce::dontSendNotification);
        tfBox.setItemEnabled (2, session.isSweepAvailable());
        tfBox.setSelectedId (s.sweepMethod ? 2 : 1, juce::dontSendNotification);
        updateWindowLabel();
        smoothLowBox.setSelectedId (s.smoothLowId, juce::dontSendNotification);
        smoothHighBox.setSelectedId (s.smoothHighId, juce::dontSendNotification);
        lowFreqBox.setText (s.lowFreqText, juce::dontSendNotification);
        highFreqBox.setText (s.highFreqText, juce::dontSendNotification);

        micCalSourceBox.setItemEnabled (2, session.hasFolderMicCal());
        micCalSourceBox.setSelectedId (s.micCalSource, juce::dontSendNotification);
        updateMicCalInfo();

        levelSlider.setValue (s.correctionLevel, juce::dontSendNotification);
        boostSlider.setValue (s.maxBoostDb, juce::dontSendNotification);
        phaseBox.setSelectedId (s.minimumPhase ? 2 : 1, juce::dontSendNotification);
        firBox.setSelectedId (s.firLength, juce::dontSendNotification);
        crossoverBox.setText (s.crossoverText, juce::dontSendNotification);
        subInvertToggle.setToggleState (s.subInverted, juce::dontSendNotification);
        subRow.trimSlider.setValue (s.subTrimMs, juce::dontSendNotification);

        countBox.setSelectedId (group.getNumSpeakers(), juce::dontSendNotification);
        subEnabledToggle.setToggleState (group.isSubEnabled(), juce::dontSendNotification);

        // Never under the user's caret: the session may have suggested a
        // prefix from a folder name while they were typing one.
        if (! prefixEditor.hasKeyboardFocus (true))
            prefixEditor.setText (s.prefix, false);
        figuresToggle.setToggleState (s.figures, juce::dontSendNotification);

        levelRefBox.setSelectedId (s.levelRefId, juce::dontSendNotification);
        displayBox.setSelectedId (s.showIr ? 2 : 1, juce::dontSendNotification);

        updateRunsButton();
    }

    // One row: speaker name, "Load..." + file-count status, computed aligned
    // delay and suggested level-matching trim read-outs, and the output
    // channel it's assigned to.
    struct SpeakerRow : public juce::Component
    {
        SpeakerRow()
        {
            nameLabel.setFont (juce::Font (juce::FontOptions (13.0f)));
            nameLabel.setColour (juce::Label::textColourId, SuperMoToTheme::text);
            addAndMakeVisible (nameLabel);

            loadButton.setButtonText ("Load...");
            loadButton.setColour (juce::TextButton::buttonColourId, SuperMoToTheme::mono.darker (1.4f));
            loadButton.setTooltip (smt::tips::grp::rowLoad);
            loadButton.onClick = [this] { if (onLoad) onLoad(); };
            addAndMakeVisible (loadButton);

            fileStatus.setFont (juce::Font (juce::FontOptions (11.0f)));
            fileStatus.setColour (juce::Label::textColourId, SuperMoToTheme::dimText);
            addAndMakeVisible (fileStatus);

            delayReadout.setFont (juce::Font (juce::FontOptions (12.0f)));
            delayReadout.setColour (juce::Label::textColourId, SuperMoToTheme::fir);
            delayReadout.setJustificationType (juce::Justification::centredRight);
            delayReadout.setTooltip (smt::tips::grp::rowDelay);
            addAndMakeVisible (delayReadout);

            trimReadout.setFont (juce::Font (juce::FontOptions (12.0f)));
            trimReadout.setColour (juce::Label::textColourId, SuperMoToTheme::master);
            trimReadout.setJustificationType (juce::Justification::centredRight);
            trimReadout.setTooltip (smt::tips::grp::rowTrim);
            addAndMakeVisible (trimReadout);

            outputBox.addItem ("(none)", 1);
            for (int o = 0; o < smt::numChannels; ++o)
                outputBox.addItem ("Output " + juce::String (o + 1), o + 2);
            outputBox.setSelectedId (1, juce::dontSendNotification);
            outputBox.setTooltip (smt::tips::grp::rowOutput);
            SuperMoToTheme::accentComboBox (outputBox, SuperMoToTheme::fir);
            addAndMakeVisible (outputBox);

            // ── Sub row only (setSubMode) ───────────────────────────────────
            // The group's delays come from arrival times; what governs the
            // crossover is a group delay, and the two differ by the sub's own
            // filtering. This shifts the subwoofer against the whole group, so
            // every speaker's assumed offset moves with it and the correction
            // never assumes a delay other than the one applied.
            trimLabel.setFont (juce::Font (juce::FontOptions (12.0f)));
            trimLabel.setColour (juce::Label::textColourId, SuperMoToTheme::dimText);
            trimLabel.setText ("Sub trim", juce::dontSendNotification);
            trimLabel.setJustificationType (juce::Justification::centredRight);
            addChildComponent (trimLabel);

            trimSlider.setSliderStyle (juce::Slider::LinearHorizontal);
            trimSlider.setRange (-40.0, 40.0, 0.1);
            trimSlider.setValue (0.0, juce::dontSendNotification);
            trimSlider.setDoubleClickReturnValue (true, 0.0);
            trimSlider.setTextValueSuffix (" ms");
            // Bipolar: the fill grows from 0 ms, as on the Analysis pane's
            // Mains-delay slider (setCentralValue rather than drawFromCentre,
            // which would hardcode the track's geometric midpoint).
            trimSlider.setCentralValue (0.0);
            SuperMoToTheme::accentSlider (trimSlider, SuperMoToTheme::delay);
            trimSlider.setTooltip (smt::tips::grp::subTrim);
            trimSlider.onValueChange = [this]
            {
                if (onTrimChanged != nullptr)
                    onTrimChanged ((float) trimSlider.getValue());
            };
            addChildComponent (trimSlider);

            suggestButton.setButtonText ("Use x-over");
            suggestButton.setColour (juce::TextButton::buttonColourId,
                                     SuperMoToTheme::mono.darker (1.4f));
            suggestButton.setTooltip (smt::tips::grp::subSuggest);
            suggestButton.onClick = [this] { if (onUseSuggestion != nullptr) onUseSuggestion(); };
            addChildComponent (suggestButton);

            suggestReadout.setFont (juce::Font (juce::FontOptions (11.0f)));
            suggestReadout.setColour (juce::Label::textColourId, SuperMoToTheme::dimText);
            suggestReadout.setJustificationType (juce::Justification::centredLeft);
            addChildComponent (suggestReadout);
        }

        /** Turns this row into the subwoofer row: it gains the trim control,
            which is the group's one main-vs-sub degree of freedom. */
        void setSubMode (bool isSub)
        {
            subMode = isSub;
            outputBox.setTooltip (isSub ? smt::tips::grp::subOutput : smt::tips::grp::rowOutput);
            for (auto* c : std::initializer_list<juce::Component*> {
                     &trimLabel, &trimSlider, &suggestButton, &suggestReadout })
                c->setVisible (isSub);
            resized();
        }

        void resized() override
        {
            auto area = getLocalBounds();
            nameLabel.setBounds (area.removeFromLeft (84));
            outputBox.setBounds (area.removeFromRight (130));
            area.removeFromRight (8);
            trimReadout.setBounds (area.removeFromRight (64));
            area.removeFromRight (4);
            delayReadout.setBounds (area.removeFromRight (74));
            area.removeFromRight (8);
            loadButton.setBounds (area.removeFromLeft (80));
            area.removeFromLeft (8);

            // The sub row is full-width below both speaker columns, so the trim
            // takes the middle of it and the file status keeps what is left.
            if (subMode)
            {
                trimLabel.setBounds (area.removeFromLeft (56));
                area.removeFromLeft (4);
                trimSlider.setBounds (area.removeFromLeft (180).reduced (0, 2));
                area.removeFromLeft (6);
                suggestButton.setBounds (area.removeFromLeft (86).reduced (0, 1));
                area.removeFromLeft (6);
                suggestReadout.setBounds (area.removeFromLeft (150));
                area.removeFromLeft (8);
            }

            fileStatus.setBounds (area);
        }

        juce::Label nameLabel, fileStatus, delayReadout, trimReadout;
        juce::TextButton loadButton;
        juce::ComboBox outputBox;
        std::function<void()> onLoad;

        // Sub row only.
        bool subMode = false;
        juce::Label trimLabel, suggestReadout;
        fxme::FxmeSlider trimSlider;
        juce::TextButton suggestButton;
        std::function<void (float)> onTrimChanged;
        std::function<void()> onUseSuggestion;
    };

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

    void setNumSpeakers (int n)
    {
        session.setNumSpeakers (n);
        syncRowsVisibility();
        refreshRows (true);
        resized();
        updatePlotPreview();
    }

    void syncRowsVisibility()
    {
        for (int i = 0; i < rows.size(); ++i)
        {
            const bool visible = i < group.getNumSpeakers();
            rows[i]->setVisible (visible);
            if (visible)
                rows[i]->nameLabel.setText (group.speaker (i).label, juce::dontSendNotification);
        }
        rebuildPreviewBox();
    }

    void rebuildPreviewBox()
    {
        previewBox.clear (juce::dontSendNotification);
        for (int i = 0; i < group.getNumSpeakers(); ++i)
            previewBox.addItem (group.speaker (i).label, i + 1);
        previewBox.addItem ("Sub", group.getNumSpeakers() + 1);

        const int id = juce::jlimit (1, group.getNumSpeakers() + 1, session.settings.previewId);
        previewBox.setSelectedId (id, juce::dontSendNotification);
        session.settings.previewId = id;
    }

    /** The rows' files and output assignments, and with `readouts` also the
        delay and trim read-outs, which read the engines and so must wait
        while a batch owns them. */
    void refreshRows (bool readouts)
    {
        auto describe = [] (const smt::SpeakerGroupAnalysis::Entry& e)
        {
            return e.files.isEmpty() ? juce::String ("no files")
                                     : juce::String (e.files.size()) + " file(s)";
        };
        auto delayText = [] (const smt::SpeakerGroupAnalysis::Entry& e)
        {
            return e.hasData() ? juce::String (e.alignedDelayMs, 1) + " ms" : juce::String();
        };
        auto trimText = [] (const smt::SpeakerGroupAnalysis::Entry& e)
        {
            return e.hasData() ? juce::String (e.suggestedTrimDb, 1) + " dB" : juce::String();
        };

        for (int i = 0; i < group.getNumSpeakers(); ++i)
        {
            auto& e = group.speaker (i);
            rows[i]->fileStatus.setText (describe (e), juce::dontSendNotification);
            rows[i]->outputBox.setSelectedId (e.assignedOutput + 2, juce::dontSendNotification);
            if (readouts)
            {
                rows[i]->delayReadout.setText (delayText (e), juce::dontSendNotification);
                rows[i]->trimReadout.setText (trimText (e), juce::dontSendNotification);
            }
        }
        auto& s = group.subEntry();
        subRow.fileStatus.setText (describe (s), juce::dontSendNotification);
        subRow.outputBox.setSelectedId (s.assignedOutput + 2, juce::dontSendNotification);
        if (readouts)
        {
            subRow.delayReadout.setText (delayText (s), juce::dontSendNotification);
            // The sub gets its own level suggestion, read in the crossover band
            // rather than the mid-band the speakers are matched over. Blank when
            // the sub is excluded: computeSubLevelMatch() leaves it at zero then,
            // and a bare "0.0 dB" would read as a measurement rather than as
            // "not computed".
            subRow.trimReadout.setText (group.isSubEnabled() ? trimText (s) : juce::String(),
                                        juce::dontSendNotification);
        }
    }

    /** The crossover-band suggestion beside the trim, and whether it can be
        trusted at the current smoothing. Deliberately does NOT write the
        slider: the slider is what the user set, and the group mirrors it. */
    void updateSubTrimInfo()
    {
        const bool have = group.isSubEnabled() && group.subEntry().hasData();
        subRow.trimSlider.setEnabled (have);
        if (! have)
        {
            subRow.suggestButton.setEnabled (false);
            subRow.suggestReadout.setText ({}, juce::dontSendNotification);
            return;
        }

        const bool ok = group.isCrossoverEstimateReliable();
        subRow.suggestButton.setEnabled (ok);
        subRow.suggestReadout.setColour (juce::Label::textColourId,
                                         ok ? SuperMoToTheme::dimText : SuperMoToTheme::mute);
        subRow.suggestReadout.setText (
            ok ? "x-over: " + juce::String (group.getSuggestedSubTrimMs(), 1) + " ms"
               : "x-over estimate unreliable at this smoothing",
            juce::dontSendNotification);
    }

    void loadSpeakerFiles (int i)
    {
        fileChooser = std::make_unique<juce::FileChooser> (
            "Select speaker " + juce::String (i + 1)
                + "'s measurement files (same positions for every speaker)",
            smt::getLastBrowseDir(), "*.wav");

        fileChooser->launchAsync (juce::FileBrowserComponent::openMode
                                  | juce::FileBrowserComponent::canSelectFiles
                                  | juce::FileBrowserComponent::canSelectMultipleItems,
            [this, i] (const juce::FileChooser& fc)
            {
                if (fc.getResults().isEmpty())
                    return;
                smt::setLastBrowseDir (fc.getResults()[0]);
                session.loadSpeakerFiles (i, fc.getResults());
            });
    }

    void loadSubFiles()
    {
        fileChooser = std::make_unique<juce::FileChooser> (
            "Select the shared subwoofer measurement files (same positions as the speakers)",
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
            });
    }

    // Loads an entire measurement set written by CalibrationComponent's
    // folder-based capture in one step (smt::GroupAnalysisSession does the
    // work), so no manual multi-select, and no risk of mismatched position
    // order between speakers and sub, is needed.
    void loadMeasurementFolder()
    {
        fileChooser = std::make_unique<juce::FileChooser> ("Select a measurement folder",
                                                            smt::getLastBrowseDir());
        fileChooser->launchAsync (juce::FileBrowserComponent::openMode
                                  | juce::FileBrowserComponent::canSelectDirectories,
            [this] (const juce::FileChooser& fc)
            {
                auto dir = fc.getResult();
                if (dir == juce::File())
                    return;
                smt::setLastBrowseDir (dir);
                session.loadMeasurementFolder (dir, prefixEditor.hasKeyboardFocus (true));
            });
    }

    //==========================================================================
    // Measurement runs of the loaded folder

    void updateRunsButton()
    {
        const int total = (int) session.selectableRuns().size();
        const int kept = session.keptRunCount();

        runsButton.setButtonText (session.canSelectRuns() && kept < total
                                      ? "Runs " + juce::String (kept) + "/"
                                          + juce::String (total) + "..."
                                      : juce::String ("Runs..."));
        runsButton.setEnabled (session.canSelectRuns() && ! session.isBusy());
    }

    void showRunsDialog()
    {
        const auto runs = session.selectableRuns();
        if (! session.canSelectRuns() || runs.empty() || session.isBusy())
            return;

        std::vector<fxme::ChecklistPopup::Row> popupRows;
        for (const auto& run : runs)
        {
            fxme::ChecklistPopup::Row row;
            row.checked = session.getExcludedRuns().count (run.number) == 0;
            const bool listed = run.info != nullptr;
            row.cells.add (listed ? juce::String (run.number) : juce::String ("-"));
            row.cells.add (listed ? smt::GroupAnalysisSession::timeLabel (run.info->time) : juce::String());
            row.cells.add (listed ? smt::GroupAnalysisSession::modeLabel (run.info->mode) : juce::String());
            row.cells.add (listed ? run.info->signal : juce::String());
            row.cells.add (run.channels.joinIntoString (", "));
            row.cells.add (listed ? run.info->comment
                                  : juce::String ("(files not listed in measurement.xml)"));
            popupRows.push_back (std::move (row));
        }

        auto popup = std::make_unique<fxme::ChecklistPopup> (
            "Measurement runs",
            std::vector<fxme::ChecklistPopup::Column> {
                { "Run", 40 }, { "Time", 130 }, { "Mode", 60 }, { "Signal", 60 },
                { "Channels", 110 }, { "Comment", 0 } },
            std::move (popupRows));
        popup->setDescription ("Each run is one microphone position. Unchecked runs are left "
                               "out of every speaker and the sub, and OK re-analyzes the group. "
                               "FIR runs start unchecked, since they measure the speaker through "
                               "its correction.");
        popup->setColours (SuperMoToTheme::checklistColours());

        // The run numbers, row by row, to turn a checked state back into runs.
        std::vector<int> numbers;
        for (const auto& run : runs)
            numbers.push_back (run.number);

        auto excludedFrom = [numbers] (const std::vector<bool>& checked)
        {
            std::set<int> excluded;
            for (size_t k = 0; k < numbers.size() && k < checked.size(); ++k)
                if (! checked[k])
                    excluded.insert (numbers[k]);
            return excluded;
        };

        const juce::Component::SafePointer<GroupAnalysisComponent> safe (this);
        popup->validate = [safe, excludedFrom] (const std::vector<bool>& checked)
        {
            return safe != nullptr ? safe->session.refusalFor (excludedFrom (checked))
                                   : juce::String();
        };
        popup->onOk = [safe, excludedFrom] (const std::vector<bool>& checked)
        {
            if (safe != nullptr)
                safe->session.applyRunSelection (excludedFrom (checked));
        };

        fxme::ChecklistPopup::showAsCallOut (std::move (popup), runsButton);
    }

    void applyAndExport()
    {
        fileChooser = std::make_unique<juce::FileChooser> ("Choose export folder", smt::getLastBrowseDir());
        fileChooser->launchAsync (juce::FileBrowserComponent::openMode
                                  | juce::FileBrowserComponent::canSelectDirectories,
            [this] (const juce::FileChooser& fc)
            {
                auto dir = fc.getResult();
                if (dir == juce::File())
                    return;
                smt::setLastBrowseDir (dir);

                // The session applies a setting moved moments ago first, and
                // finishes the export even if the editor is closed meanwhile.
                stopTimer();
                session.applyAndExport (dir);
            });
    }

    //==========================================================================
    // Impulse-response view

    /** How low the chosen FIR length can still correct. Two bins of the
        filter's own resolution (fs/N): below that it cannot place a correction
        at all, whatever the analysis range allows. Same convention and wording
        as the single-speaker pane. */
    void updateFirInfo()
    {
        const int N = firBox.getSelectedId();
        auto* eng = previewedEngine();
        double fs = eng != nullptr ? eng->getSampleRate() : 0.0;
        const bool known = fs > 0.0;
        if (! known)
            fs = 48000.0;

        const double fMinHz = 2.0 * fs / (double) juce::jmax (1, N);
        firInfo.setText (juce::String::fromUTF8 ("\xe2\x86\x92 corrects down to ~")
                             + juce::String (fMinHz, fMinHz < 100.0 ? 1 : 0) + " Hz"
                             + (known ? juce::String() : juce::String (" (at 48 kHz)")),
                         juce::dontSendNotification);
    }

    /** Level of the subwoofer relative to speaker `i`, once both suggested
        trims are applied. That is what decides the shape of their sum, and it
        is what renderSystemIR needs. Zero when either figure is not available
        (Compute alignment not yet pressed), which then previews the two at
        their measured levels. */
    float relativeSubGainDb (int i) const
    {
        if (! group.isSubEnabled() || ! group.subEntry().hasData()
            || i < 0 || i >= group.getNumSpeakers())
            return 0.0f;
        return group.subEntry().suggestedTrimDb - group.speaker (i).suggestedTrimDb;
    }

    smt::AnalysisEngine* previewedEngine() const
    {
        const int id = previewBox.getSelectedId();
        return (id >= 1 && id <= group.getNumSpeakers())
                   ? group.speaker (id - 1).engine.get()
                   : group.subEntry().engine.get();
    }

    bool showingIr() const          { return displayBox.getSelectedId() == 2; }
    void refreshIrIfVisible()       { if (showingIr()) updateIrPlot(); }

    void updateDisplayMode()
    {
        plot.setVisible (! showingIr());
        irPlot.setVisible (showingIr());
        if (showingIr())
            updateIrPlot();
    }

    // Renders the previewed engine's measured average and correction at the
    // export FIR length into the waveform view. The user's zoom survives
    // design tweaks; the view resets only when the time axis itself changes.
    void updateIrPlot()
    {
        auto* eng = previewedEngine();
        if (eng == nullptr || ! eng->hasData())
        {
            irPlot.clear();
            return;
        }

        const int N = firBox.getSelectedId();
        const double sr = eng->getSampleRate();
        const int id = previewBox.getSelectedId();
        const bool isSpeaker = id >= 1 && id <= group.getNumSpeakers();

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

        // The correction filter's own impulse is deliberately not plotted: it is a
        // filter gain (peak ~1) while these traces still carry the measured
        // mid-band level, so sharing one linear axis flattens them (see the
        // single-speaker view's updateIrPlot for the full reasoning).
        add (eng->renderMeasuredIR (N), "measured", SuperMoToTheme::curveAverage);

        // Predictions only make sense for a speaker: the sub is never given a
        // correction FIR, and "main + sub" has no meaning on the sub's own row.
        if (isSpeaker)
        {
            add (eng->renderCorrectedIR (N), "corrected", SuperMoToTheme::fir);
            if (eng->hasSub())
                add (eng->renderSystemIR (N, relativeSubGainDb (id - 1)),
                     "+ sub (1 main)", SuperMoToTheme::mono);
        }

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

    // Refreshes the shared plot with the currently previewed engine's curves.
    void updatePlotPreview()
    {
        const int id = previewBox.getSelectedId();
        const bool isSpeaker = id >= 1 && id <= group.getNumSpeakers();
        smt::AnalysisEngine* eng = isSpeaker ? group.speaker (id - 1).engine.get()
                                             : group.subEntry().engine.get();
        const auto& entryFiles = isSpeaker ? group.speaker (id - 1).files
                                           : group.subEntry().files;

        // dB SPL is only offered when it can be computed for this entry.
        float splOff = 0.0f, stimLvl = 0.0f;
        const bool splOk = eng != nullptr && eng->hasData()
                        && session.getSplContext (entryFiles, splOff, stimLvl);
        levelRefBox.setItemEnabled (3, splOk);
        if (! splOk && levelRefBox.getSelectedId() == 3)
        {
            levelRefBox.setSelectedId (1, juce::dontSendNotification);
            session.settings.levelRefId = 1;
        }

        TransferFunctionPlot::Data d;
        if (eng != nullptr)
        {
            for (int i = 0; i < eng->getNumCurves(); ++i)
            {
                d.curveDbs.push_back (eng->getCurveDb (i, freqs));
                d.curvePhases.push_back (eng->getCurvePhaseDeg (i, freqs));
            }
            d.averageDb       = eng->getAverageDb (freqs);
            d.correctionDb    = eng->getCorrectionDb (freqs);
            d.correctedDb     = eng->getCorrectedDb (freqs);
            d.averagePhase    = eng->getAveragePhaseDeg (freqs);
            d.correctionPhase = eng->getCorrectionPhaseDeg (freqs);
            d.correctedPhase  = eng->getCorrectedPhaseDeg (freqs);
            d.subDb           = eng->getSubDb (freqs);
            d.subPhase        = eng->getSubPhaseDeg (freqs);
            d.hasSub          = eng->hasSub();
            d.crossoverHz     = eng->getCrossoverHz();
            for (int i = 0; i < eng->getNumHarmonics(); ++i)
                d.harmonicDbs.push_back (eng->getHarmonicDb (i, freqs));

            // Level reference: display offset on the measured curves. dB SPL =
            // absolute |H| + stimulus level (sine RMS, hence -3 dB) + offset.
            switch (levelRefBox.getSelectedId())
            {
                case 2:
                    d.measuredOffsetDb = eng->getReferenceDb();
                    d.levelAxisText = "|H| (dB, absolute); correction in dB";
                    break;
                case 3:
                    d.measuredOffsetDb = eng->getReferenceDb() + stimLvl - 3.0f + splOff;
                    d.levelAxisText = "est. dB SPL during the measurement; correction in dB";
                    break;
                default:
                    break;      // normalized: the plot's default wording
            }
        }
        plot.setData (std::move (d));

        refreshIrIfVisible();   // every data/design change funnels through here
    }

    // Disables every control that would otherwise touch an engine (directly,
    // or via the shared forEachEngine fan-out) while a background batch is
    // running, so no engine is ever read/written from two threads at once;
    // shows/hides the progress bar accordingly.

    // Disables every control that would otherwise touch an engine (directly,
    // or via the shared settings push) while a background batch owns them, so
    // no engine is ever read/written from two threads at once; shows/hides the
    // progress bar accordingly.
    void setBusy (bool busy)
    {
        updateRunsButton();
        progressBar.setVisible (busy);
        // The plot is a bare view, not in the enable/disable list below: freeze
        // its zoom/pan so onViewChanged cannot ask for a re-sample mid-batch.
        plot.setInteractionEnabled (! busy);
        for (auto* c : { &countBox, &windowBox, &smoothLowBox, &smoothHighBox,
                        &lowFreqBox, &highFreqBox, &previewBox, &firBox, &phaseBox, &crossoverBox,
                        &micCalSourceBox, &levelRefBox, &displayBox, &tfBox })
            c->setEnabled (! busy);
        for (auto* b : { &computeButton, &applyButton, &loadFolderButton })
            b->setEnabled (! busy);
        subEnabledToggle.setEnabled (! busy);
        prefixEditor.setEnabled (! busy);   // must not change mid-export
        figuresToggle.setEnabled (! busy);
        levelSlider.setEnabled (! busy);
        boostSlider.setEnabled (! busy);
        subInvertToggle.setEnabled (! busy);
        for (auto* row : rows)
        {
            row->loadButton.setEnabled (! busy);
            row->outputBox.setEnabled (! busy);
        }
        const bool subControlsEnabled = ! busy && subEnabledToggle.getToggleState();
        subRow.loadButton.setEnabled (subControlsEnabled);
        subRow.outputBox.setEnabled (subControlsEnabled);
        subRow.trimSlider.setEnabled (subControlsEnabled);
        // Short-circuits before the reliability test, which reads the engines.
        subRow.suggestButton.setEnabled (subControlsEnabled
                                         && group.isCrossoverEstimateReliable());
    }

    smt::GroupAnalysisSession& session;
    smt::SpeakerGroupAnalysis& group;       // session.group
    juce::ProgressBar progressBar { session.progress };

    juce::Label title, micCalInfo, countLabel, status;
    juce::ComboBox countBox, micCalSourceBox;
    juce::TextButton computeButton, applyButton, loadFolderButton, runsButton;
    juce::ToggleButton subEnabledToggle;
    juce::Label prefixLabel;
    juce::TextEditor prefixEditor;      // export file-name prefix
    juce::ToggleButton figuresToggle;   // render report figures on export

    juce::Label windowLabel, smoothLabel, rangeLabel, rangeToLabel, previewLabel, levelRefLabel;
    juce::Label levelLabel, boostLabel, firLabel, phaseLabel, crossoverLabel;
    juce::ComboBox windowBox, smoothLowBox, smoothHighBox, lowFreqBox, highFreqBox, previewBox;
    juce::ComboBox firBox, phaseBox, crossoverBox, levelRefBox, displayBox, tfBox;
    juce::Label displayLabel, tfLabel, firInfo;
    juce::ToggleButton subInvertToggle;
    fxme::FxmeSlider levelSlider, boostSlider;

    juce::OwnedArray<SpeakerRow> rows;
    SpeakerRow subRow;
    juce::Viewport rowsViewport;
    juce::Component rowsHolder;

    TransferFunctionPlot plot;
    fxme::WaveformDisplay irPlot;
    int lastIrLength = 0;
    double lastIrRate = 0.0;
    std::vector<float> freqs;
    std::unique_ptr<juce::FileChooser> fileChooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GroupAnalysisComponent)
};
