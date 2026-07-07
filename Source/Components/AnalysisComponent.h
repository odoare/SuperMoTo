/*
  ------------------------------------------------------------------------------
    AnalysisComponent.h

    Part 3 GUI: loads a set of measurements of one speaker (microphone
    moved around the reference position), draws the Welch transfer
    functions (thin lines), their average (thick line) and the proposed
    correction, with the correction level adjustable from 0 to 1. The
    correction is exported as an impulse response wav which the monitoring
    part loads as an output FIR (optionally assigned directly here).

    Author: Olivier Doaré, github.com/odoare
    Licenced under the GNU LGPL Version 3.0
    SPDX-License-Identifier: LGPL-3.0-or-later
  ------------------------------------------------------------------------------
*/

#pragma once

#include <JuceHeader.h>
#include "../PluginProcessor.h"
#include "../Dsp/AnalysisEngine.h"
#include "../AppSettings.h"
#include "../Theme.h"
#include "TransferFunctionPlot.h"

class AnalysisComponent : public juce::Component
{
public:
    explicit AnalysisComponent (SuperMoToAudioProcessor& p) : processor (p)
    {
        title.setText ("Analysis & correction design", juce::dontSendNotification);
        title.setFont (juce::Font (17.0f, juce::Font::bold));
        title.setColour (juce::Label::textColourId, SuperMoToTheme::text);
        addAndMakeVisible (title);

        // Read-only reminder of the (global) mic calibration applied to the data;
        // it is loaded/cleared in the Measurement & Calibration pane.
        micCalInfo.setFont (juce::Font (11.0f));
        micCalInfo.setJustificationType (juce::Justification::centredRight);
        micCalInfo.setColour (juce::Label::textColourId, SuperMoToTheme::dimText);
        micCalInfo.setTooltip ("Microphone calibration is divided out of the measurements. "
                               "Load it in the Measurement & Calibration pane.");
        addAndMakeVisible (micCalInfo);
        updateMicCalInfo();

        loadButton.setButtonText ("Load measurements...");
        loadButton.onClick = [this] { loadFiles(); };
        addAndMakeVisible (loadButton);

        addLabel (windowLabel, "Welch window");
        for (int size = 1 << 14; size <= 1 << 18; size <<= 1)
            windowBox.addItem (juce::String (size), size);
        windowBox.setSelectedId (65536, juce::dontSendNotification);
        SuperMoToTheme::accentComboBox (windowBox, SuperMoToTheme::spectrum);
        windowBox.onChange = [this]
        {
            analysis.setWindowSize (windowBox.getSelectedId());
            if (! loadedFiles.isEmpty())
                analyze();
        };
        addAndMakeVisible (windowBox);

        // Frequency-dependent smoothing: separate octave fraction for the low
        // and the high end, log-interpolated across frequency by the engine.
        // Finer in the bass (resolve modes), broader in the treble (trends only).
        addLabel (smoothLabel, "Smooth LF/HF");
        auto setupSmoothBox = [this] (juce::ComboBox& box, const juce::String& tip)
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
            box.onChange = [this] { pushSmoothing(); };
            addAndMakeVisible (box);
        };
        setupSmoothBox (smoothLowBox,  "Smoothing of the low frequencies (<= 100 Hz)");
        setupSmoothBox (smoothHighBox, "Smoothing of the high frequencies (>= 10 kHz)");

        addLabel (levelLabel, "Correction level");
        levelSlider.setSliderStyle (juce::Slider::LinearHorizontal);
        levelSlider.setRange (0.0, 1.0, 0.01);
        levelSlider.setValue (1.0, juce::dontSendNotification);
        levelSlider.setDoubleClickReturnValue (true, 1.0);
        SuperMoToTheme::accentSlider (levelSlider, SuperMoToTheme::master);
        levelSlider.onValueChange = [this]
        {
            analysis.setCorrectionLevel ((float) levelSlider.getValue());
            updatePlotData();
        };
        addAndMakeVisible (levelSlider);

        addLabel (boostLabel, "Max boost");
        boostSlider.setSliderStyle (juce::Slider::LinearHorizontal);
        boostSlider.setRange (0.0, 24.0, 0.5);
        boostSlider.setValue (12.0, juce::dontSendNotification);
        boostSlider.setDoubleClickReturnValue (true, 12.0);
        boostSlider.setTextValueSuffix (" dB");
        SuperMoToTheme::accentSlider (boostSlider, SuperMoToTheme::master);
        boostSlider.onValueChange = [this]
        {
            analysis.setMaxBoostDb ((float) boostSlider.getValue());
            updatePlotData();
        };
        addAndMakeVisible (boostSlider);

        // Frequency band the analysis acts on: outside it the correction is
        // unity and the exported measured IR is rolled off. Editable, with a
        // few standard values preset.
        addLabel (rangeLabel, "Range");
        auto setupFreqBox = [this] (juce::ComboBox& box,
                                    std::initializer_list<int> presets, int def)
        {
            for (int f : presets)
                box.addItem (juce::String (f) + " Hz", f);
            box.setEditableText (true);
            box.setSelectedId (def, juce::dontSendNotification);
            SuperMoToTheme::accentComboBox (box, SuperMoToTheme::spectrum);
            box.onChange = [this] { pushRange(); };
            addAndMakeVisible (box);
        };
        setupFreqBox (lowFreqBox,  { 20, 30, 40, 50, 60, 80, 100, 150, 200, 300 }, 20);
        setupFreqBox (highFreqBox, { 5000, 8000, 10000, 12000, 15000, 16000, 18000, 20000 }, 20000);
        addLabel (rangeToLabel, juce::String::fromUTF8 ("\xe2\x80\x93"));    // en dash
        rangeToLabel.setJustificationType (juce::Justification::centred);

        addLabel (firLabel, "FIR length");
        // Short FIRs (few taps) are cheaper and lower-latency but can only
        // correct higher frequencies; long FIRs reach the low end. See firInfo.
        for (int size = 1 << 8; size <= 1 << 16; size <<= 1)
            firBox.addItem (juce::String (size), size);
        firBox.setSelectedId (4096, juce::dontSendNotification);
        SuperMoToTheme::accentComboBox (firBox, SuperMoToTheme::fir);
        firBox.onChange = [this] { updateFirInfo(); };
        addAndMakeVisible (firBox);

        // Linear/mixed-phase corrects magnitude AND phase but adds firLength/2
        // latency; minimum-phase corrects magnitude only with ~no latency (and
        // drops the subwoofer phase alignment). See firInfo / the manual.
        addLabel (phaseLabel, "Phase");
        phaseBox.addItem ("Linear phase", 1);
        phaseBox.addItem ("Min phase", 2);
        phaseBox.setSelectedId (1, juce::dontSendNotification);
        phaseBox.setTooltip ("Linear: corrects magnitude and phase (incl. subwoofer "
                             "alignment), adds firLength/2 latency.\n"
                             "Min phase: magnitude only, near-zero latency, no phase "
                             "correction or subwoofer alignment \xe2\x80\x94 for tracking.");
        SuperMoToTheme::accentComboBox (phaseBox, SuperMoToTheme::fir);
        phaseBox.onChange = [this]
        {
            analysis.setPhaseType (phaseBox.getSelectedId() == 2
                                       ? smt::AnalysisEngine::PhaseType::minimum
                                       : smt::AnalysisEngine::PhaseType::linear);
            updateFirInfo();
        };
        addAndMakeVisible (phaseBox);

        firInfo.setFont (juce::Font (12.0f));
        firInfo.setColour (juce::Label::textColourId, SuperMoToTheme::fir);
        addAndMakeVisible (firInfo);

        addLabel (assignLabel, "Assign to");
        assignBox.addItem ("(none)", 1);
        for (int o = 0; o < smt::numChannels; ++o)
            assignBox.addItem ("Output " + juce::String (o + 1), o + 2);
        assignBox.setSelectedId (1, juce::dontSendNotification);
        SuperMoToTheme::accentComboBox (assignBox, SuperMoToTheme::fir);
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
            analysis.setCrossoverHz (crossoverBox.getText().getFloatValue());
            updatePlotData();
        };
        addAndMakeVisible (crossoverBox);

        subInvertToggle.setButtonText ("Invert sub");
        SuperMoToTheme::accentToggleButton (subInvertToggle, SuperMoToTheme::mono);
        subInvertToggle.onClick = [this]
        {
            analysis.setSubPolarityInverted (subInvertToggle.getToggleState());
            updatePlotData();
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
        alignSlider.getProperties().set ("drawFromCentre", true);   // bipolar: fill from 0
        SuperMoToTheme::accentSlider (alignSlider, SuperMoToTheme::mono);
        alignSlider.onValueChange = [this]
        {
            analysis.setTimeAlignMs ((float) alignSlider.getValue());
            updatePlotData();
            updateAlignInfo();
        };
        addAndMakeVisible (alignSlider);

        alignInfo.setFont (juce::Font (12.0f));
        alignInfo.setJustificationType (juce::Justification::centredRight);
        alignInfo.setColour (juce::Label::textColourId, SuperMoToTheme::mono.brighter (0.3f));
        addAndMakeVisible (alignInfo);

        // When set, assigning the exported correction to an output also writes
        // the Mains-delay value above onto that output's bulk delay — the
        // physical time-alignment the correction was designed around.
        applyDelayToggle.setButtonText ("Apply bulk delay");
        SuperMoToTheme::accentToggleButton (applyDelayToggle, SuperMoToTheme::mono);
        applyDelayToggle.setTooltip ("On Export correction IR + assign, also set that output's "
                                     "delay to the Mains-delay value (the bulk time-alignment "
                                     "the correction was designed for). Negative values clamp to 0.");
        addAndMakeVisible (applyDelayToggle);

        status.setColour (juce::Label::textColourId, SuperMoToTheme::spectrum);
        addAndMakeVisible (status);

        addAndMakeVisible (plot);

        buildFreqGrid();
        updateFirInfo();
        updateAlignInfo();
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
        juce::String msg = "~" + juce::String (recommendedAlignMs, 0)
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
        micCalInfo.setBounds (titleRow.removeFromRight (260));
        title.setBounds (titleRow.withTrimmedLeft (30));   // room for the info button
        area.removeFromTop (6);

        // The two export buttons share a right-aligned column: "Export IR" on
        // row 1 and "Export correction IR" directly below it on row 2, both
        // flush to the right edge (same width) so they always line up.
        constexpr int exportW = 165;

        // Row 1: load, Welch window, smoothing, analysis range, Export IR.
        auto r1 = area.removeFromTop (24);
        exportMeasuredButton.setBounds (r1.removeFromRight (exportW));
        loadButton.setBounds (r1.removeFromLeft (170));
        r1.removeFromLeft (16);
        windowLabel.setBounds (r1.removeFromLeft (90));
        windowBox.setBounds (r1.removeFromLeft (90));
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
        plot.setBounds (area);
    }

private:
    static constexpr int numPoints = 400;
    static constexpr float fMin = 20.0f, fMax = 20000.0f;

    void addLabel (juce::Label& l, const juce::String& text)
    {
        l.setText (text, juce::dontSendNotification);
        l.setFont (juce::Font (12.0f));
        l.setColour (juce::Label::textColourId, SuperMoToTheme::dimText);
        addAndMakeVisible (l);
    }

    void buildFreqGrid()
    {
        freqs.resize (numPoints);
        for (int p = 0; p < numPoints; ++p)
            freqs[(size_t) p] = fMin * std::pow (fMax / fMin, (float) p / (float) (numPoints - 1));
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
                loadedFiles = fc.getResults();
                smt::setLastBrowseDir (loadedFiles[0]);
                analyze();
            });
    }

    void loadSubFiles()
    {
        if (! analysis.hasData())
        {
            status.setText ("Load the main measurements first.", juce::dontSendNotification);
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
                const int ok = analysis.loadSubFiles (fc.getResults());
                status.setText (ok > 0
                    ? juce::String (ok) + " sub measurement(s) aligned for phase integration."
                    : "No sub file could be analyzed (need stereo wavs at the main's rate/length).",
                    juce::dontSendNotification);

                // Auto-detect the main/sub time offset (recommendation only;
                // the user drives the slider to apply it).
                if (ok > 0)
                    recommendedAlignMs = analysis.estimateMainSubOffsetMs();
                updateAlignInfo();
                updatePlotData();
            });
    }

    void analyze()
    {
        status.setText ("Analyzing " + juce::String (loadedFiles.size()) + " file(s)...",
                        juce::dontSendNotification);

        analysis.setCorrectionLevel ((float) levelSlider.getValue());
        pushMicCalibration();               // apply the current mic cal to the data
        const int ok = analysis.loadFiles (loadedFiles);

        status.setText (ok > 0
            ? juce::String (ok) + " measurement(s) analyzed at "
                + juce::String (analysis.getSampleRate() / 1000.0, 1) + " kHz."
            : "No file could be analyzed (need stereo wavs longer than the window).",
            juce::dontSendNotification);

        updateFirInfo();
        updatePlotData();
    }

    // Push the shared (global) mic calibration into the engine and refresh the
    // read-only reminder. The engine divides it out of the measured data.
    void pushMicCalibration()
    {
        analysis.setMicCalibration (smt::sharedMicCalibration());
        updateMicCalInfo();
    }

    void updateMicCalInfo()
    {
        auto& cal = smt::sharedMicCalibration();
        micCalInfo.setText (cal.isValid() ? "Mic cal: " + cal.getName()
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

    void pushRange()
    {
        analysis.setAnalysisRange (lowFreqBox.getText().getFloatValue(),
                                   highFreqBox.getText().getFloatValue());
        updatePlotData();
    }

    void pushSmoothing()
    {
        // Index 0..6 -> Off, 1/24, 1/12, 1/6, 1/3, 1/2, 1 oct.
        static const float fractions[] = { 0.0f, 1.0f / 24.0f, 1.0f / 12.0f,
                                           1.0f / 6.0f, 1.0f / 3.0f, 1.0f / 2.0f, 1.0f };
        analysis.setSmoothing (fractions[juce::jlimit (0, 6, smoothLowBox.getSelectedId()  - 1)],
                               fractions[juce::jlimit (0, 6, smoothHighBox.getSelectedId() - 1)]);
        updatePlotData();
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
        plot.setData (std::move (d));
    }

    void exportMeasuredIr()
    {
        if (! analysis.hasData())
        {
            status.setText ("Load measurements first.", juce::dontSendNotification);
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

                status.setText (analysis.exportMeasuredIR (file, firBox.getSelectedId())
                                    ? "Exported " + file.getFileName()
                                    : "Export failed.",
                                juce::dontSendNotification);
            });
    }

    void exportIr()
    {
        if (! analysis.hasData())
        {
            status.setText ("Load measurements first.", juce::dontSendNotification);
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
                    status.setText ("Export failed.", juce::dontSendNotification);
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
                }

                status.setText (msg, juce::dontSendNotification);
            });
    }

    SuperMoToAudioProcessor& processor;
    smt::AnalysisEngine analysis;

    juce::Label title, windowLabel, smoothLabel, levelLabel, firLabel, assignLabel, boostLabel, status;
    juce::Label micCalInfo;
    juce::Label firInfo, rangeLabel, rangeToLabel, crossoverLabel, phaseLabel;
    juce::Label alignLabel, alignInfo;
    fxme::FxmeSlider boostSlider, alignSlider;
    float recommendedAlignMs = 0.0f;
    juce::TextButton loadButton, exportButton, exportMeasuredButton, loadSubButton;

    juce::ComboBox windowBox, smoothLowBox, smoothHighBox, firBox, phaseBox, assignBox, lowFreqBox, highFreqBox, crossoverBox;
    juce::ToggleButton subInvertToggle, applyDelayToggle;
    fxme::FxmeSlider levelSlider;

    juce::Array<juce::File> loadedFiles;
    std::vector<float> freqs;

    TransferFunctionPlot plot;
    std::unique_ptr<juce::FileChooser> fileChooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AnalysisComponent)
};
