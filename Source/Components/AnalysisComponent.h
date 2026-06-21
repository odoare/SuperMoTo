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
        levelSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 50, 18);
        levelSlider.setRange (0.0, 1.0, 0.01);
        levelSlider.setValue (1.0, juce::dontSendNotification);
        SuperMoToTheme::accentSlider (levelSlider, SuperMoToTheme::master);
        levelSlider.onValueChange = [this]
        {
            analysis.setCorrectionLevel ((float) levelSlider.getValue());
            updatePlotData();
        };
        addAndMakeVisible (levelSlider);

        addLabel (boostLabel, "Max boost");
        boostSlider.setSliderStyle (juce::Slider::LinearHorizontal);
        boostSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 50, 18);
        boostSlider.setRange (0.0, 24.0, 0.5);
        boostSlider.setValue (12.0, juce::dontSendNotification);
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
        // the mains; the correction is then designed for the residual only.
        addLabel (alignLabel, "Mains delay");
        alignSlider.setSliderStyle (juce::Slider::LinearHorizontal);
        alignSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 60, 18);
        alignSlider.setRange (-40.0, 40.0, 0.1);
        alignSlider.setValue (0.0, juce::dontSendNotification);
        alignSlider.setTextValueSuffix (" ms");
        SuperMoToTheme::accentSlider (alignSlider, SuperMoToTheme::mono);
        alignSlider.onValueChange = [this]
        {
            analysis.setTimeAlignMs ((float) alignSlider.getValue());
            updatePlotData();
            updateAlignInfo();
        };
        addAndMakeVisible (alignSlider);

        alignInfo.setFont (juce::Font (12.0f));
        alignInfo.setColour (juce::Label::textColourId, SuperMoToTheme::mono.brighter (0.3f));
        addAndMakeVisible (alignInfo);

        status.setColour (juce::Label::textColourId, SuperMoToTheme::spectrum);
        addAndMakeVisible (status);

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
        juce::String msg = juce::String::fromUTF8 ("\xe2\x86\x92 ~")
            + juce::String (recommendedAlignMs, 0) + " ms main/sub offset detected.  ";

        if (std::abs (v) < 0.05f)
            msg += "Set 'Mains delay' to time-align (correction designed for the residual).";
        else
            msg += "Add " + juce::String (std::abs (v), 1) + " ms delay to the "
                 + juce::String (v >= 0.0f ? "main output(s)" : "subwoofer output")
                 + " (Matrix view) to match.";

        alignInfo.setText (msg, juce::dontSendNotification);
    }

    //==========================================================================
    // Mouse interaction over the magnitude panel zooms/pans the dB axis:
    // wheel = zoom around the cursor, drag = pan, double-click = reset.
    static constexpr float dbFloor = -90.0f, dbCeil = 60.0f, dbMinSpan = 5.0f;

    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override
    {
        auto m = magPanelArea();
        if (! m.contains (e.position))
            return;

        const float span  = plotMaxDb - plotMinDb;
        const float dbAtY  = juce::jmap (e.position.y, m.getBottom(), m.getY(), plotMinDb, plotMaxDb);
        const float factor = w.deltaY > 0.0f ? 0.85f : 1.0f / 0.85f;
        const float newSpan = juce::jlimit (dbMinSpan, dbCeil - dbFloor, span * factor);
        const float frac   = span > 0.0f ? (dbAtY - plotMinDb) / span : 0.5f;
        setDbWindow (dbAtY - frac * newSpan, newSpan);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        dragging = magPanelArea().contains (e.position);
        dragStartY = e.position.y;
        dragStartMin = plotMinDb;
        dragStartMax = plotMaxDb;
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (! dragging)
            return;
        const float span = dragStartMax - dragStartMin;
        const float dbPerPx = span / juce::jmax (1.0f, magPanelArea().getHeight());
        setDbWindow (dragStartMin + (e.position.y - dragStartY) * dbPerPx, span);
    }

    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        if (magPanelArea().contains (e.position))
        {
            plotMinDb = -30.0f;     // default view
            plotMaxDb = 30.0f;
            repaint();
        }
    }

    void mouseMove (const juce::MouseEvent& e) override
    {
        cursorPos = e.position;
        const bool in = magPanelArea().contains (e.position)
                     || phasePanelArea().contains (e.position);
        if (in != cursorInPlot || in)
        {
            cursorInPlot = in;
            repaint();
        }
    }

    void mouseExit (const juce::MouseEvent&) override
    {
        if (cursorInPlot) { cursorInPlot = false; repaint(); }
    }

    // Apply a [min, min+span] dB window, clamped to fit within [floor, ceil].
    void setDbWindow (float newMin, float span)
    {
        span   = juce::jlimit (dbMinSpan, dbCeil - dbFloor, span);
        newMin = juce::jlimit (dbFloor, dbCeil - span, newMin);
        plotMinDb = newMin;
        plotMaxDb = newMin + span;
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        g.setColour (SuperMoToTheme::panel.withAlpha (0.7f));
        g.fillRoundedRectangle (getLocalBounds().toFloat(), 6.0f);
        g.setColour (SuperMoToTheme::panelLine);
        g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (0.5f), 6.0f, 1.0f);

        paintPlot (g, plotArea.toFloat());
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

        // Row 4: time-alignment (mains delay) + the recommendation/instruction.
        area.removeFromTop (6);
        auto r4 = area.removeFromTop (22);
        alignLabel.setBounds (r4.removeFromLeft (78));
        alignSlider.setBounds (r4.removeFromLeft (220));
        r4.removeFromLeft (16);
        alignInfo.setBounds (r4);

        area.removeFromTop (4);
        status.setBounds (area.removeFromBottom (20));
        area.removeFromBottom (4);
        plotArea = area;
    }

private:
    static constexpr int numPoints = 400;
    static constexpr float fMin = 20.0f, fMax = 20000.0f;
    float plotMinDb = -30.0f, plotMaxDb = 30.0f;     // adjustable vertical limits

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
        curveDbs.clear();
        curvePhases.clear();
        for (int i = 0; i < analysis.getNumCurves(); ++i)
        {
            curveDbs.push_back (analysis.getCurveDb (i, freqs));
            curvePhases.push_back (analysis.getCurvePhaseDeg (i, freqs));
        }
        averageDb       = analysis.getAverageDb (freqs);
        correctionDb    = analysis.getCorrectionDb (freqs);
        correctedDb     = analysis.getCorrectedDb (freqs);
        averagePhase    = analysis.getAveragePhaseDeg (freqs);
        correctionPhase = analysis.getCorrectionPhaseDeg (freqs);
        correctedPhase  = analysis.getCorrectedPhaseDeg (freqs);
        subDb           = analysis.getSubDb (freqs);
        subPhase        = analysis.getSubPhaseDeg (freqs);
        repaint();
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
                    processor.configModel.setOutput (assignOut, s);
                    processor.engine.updateFirFiles();
                    msg << juce::String::fromUTF8 (" \xe2\x80\x94 assigned to output ")
                        << juce::String (assignOut + 1);
                }

                status.setText (msg, juce::dontSendNotification);
            });
    }

    //==========================================================================
    float freqToX (float f, juce::Rectangle<float> r) const
    {
        return r.getX() + r.getWidth() * std::log (f / fMin) / std::log (fMax / fMin);
    }

    float dbToY (float db, juce::Rectangle<float> r) const
    {
        return juce::jmap (juce::jlimit (plotMinDb, plotMaxDb, db),
                           plotMinDb, plotMaxDb, r.getBottom(), r.getY());
    }

    void drawCurve (juce::Graphics& g, const std::vector<float>& dbs,
                    juce::Rectangle<float> r, juce::Colour colour, float thickness) const
    {
        if (dbs.size() != freqs.size())
            return;
        juce::Path path;
        for (size_t p = 0; p < freqs.size(); ++p)
        {
            const float x = freqToX (freqs[p], r);
            const float y = dbToY (dbs[p], r);
            if (p == 0) path.startNewSubPath (x, y);
            else        path.lineTo (x, y);
        }
        g.setColour (colour);
        g.strokePath (path, juce::PathStrokeType (thickness));
    }

    static float phaseToY (float deg, juce::Rectangle<float> r)
    {
        return juce::jmap (juce::jlimit (-180.0f, 180.0f, deg),
                           -180.0f, 180.0f, r.getBottom(), r.getY());
    }

    void drawPhaseCurve (juce::Graphics& g, const std::vector<float>& degs,
                         juce::Rectangle<float> r, juce::Colour colour, float thickness) const
    {
        if (degs.size() != freqs.size())
            return;
        juce::Path path;
        bool started = false;
        for (size_t p = 0; p < freqs.size(); ++p)
        {
            const float x = freqToX (freqs[p], r);
            const float y = phaseToY (degs[p], r);
            // Break the line on +/-180 degree wraps instead of drawing a
            // vertical jump across the panel.
            if (! started || (p > 0 && std::abs (degs[p] - degs[p - 1]) > 180.0f))
            {
                path.startNewSubPath (x, y);
                started = true;
            }
            else
                path.lineTo (x, y);
        }
        g.setColour (colour);
        g.strokePath (path, juce::PathStrokeType (thickness));
    }

    // Magnitude panel rectangle (mirrors the split done in paintPlot).
    juce::Rectangle<float> magPanelArea() const
    {
        auto inner = plotArea.toFloat().reduced (24.0f, 12.0f);
        return inner.removeFromTop (inner.getHeight() * 0.62f);
    }

    // Phase panel rectangle (the lower part, mirroring paintPlot).
    juce::Rectangle<float> phasePanelArea() const
    {
        auto inner = plotArea.toFloat().reduced (24.0f, 12.0f);
        inner.removeFromTop (inner.getHeight() * 0.62f);
        inner.removeFromTop (14.0f);
        return inner;
    }

    void paintPlot (juce::Graphics& g, juce::Rectangle<float> bounds) const
    {
        g.setColour (SuperMoToTheme::plotBackground);
        g.fillRoundedRectangle (bounds, 4.0f);

        auto inner = bounds.reduced (24.0f, 12.0f);
        auto magR = inner.removeFromTop (inner.getHeight() * 0.62f);
        inner.removeFromTop (14.0f);
        auto phR = inner;

        // Frequency grid spans both panels, labels under the phase panel.
        g.setFont (13.0f);
        for (float f : { 20.f, 50.f, 100.f, 200.f, 500.f, 1000.f, 2000.f, 5000.f, 10000.f, 20000.f })
        {
            const float x = freqToX (f, magR);
            g.setColour (SuperMoToTheme::grid);
            g.drawVerticalLine ((int) x, magR.getY(), magR.getBottom());
            g.drawVerticalLine ((int) x, phR.getY(), phR.getBottom());
            g.setColour (SuperMoToTheme::dimText);
            g.drawText (f >= 1000.0f ? juce::String (f / 1000.0f) + "k" : juce::String ((int) f),
                        (int) x - 18, (int) phR.getBottom() + 2, 36, 14, juce::Justification::centred);
        }

        // ── Magnitude panel ──────────────────────────────────────────────────
        for (float db = plotMinDb; db <= plotMaxDb; db += 10.0f)
        {
            const float y = dbToY (db, magR);
            g.setColour (db == 0.0f ? SuperMoToTheme::gridZero : SuperMoToTheme::grid);
            g.drawHorizontalLine ((int) y, magR.getX(), magR.getRight());
            g.setColour (SuperMoToTheme::dimText);
            g.drawText (juce::String ((int) db), (int) bounds.getX() + 1, (int) y - 7, 23, 14,
                        juce::Justification::centredRight);
        }

        // Crossover marker (where the main phase is steered onto the sub).
        const bool hasSub = analysis.hasSub();
        if (hasSub)
        {
            const float xc = freqToX (juce::jlimit (fMin, fMax, analysis.getCrossoverHz()), magR);
            g.setColour (SuperMoToTheme::mono.withAlpha (0.5f));
            g.drawVerticalLine ((int) xc, magR.getY(), magR.getBottom());
            g.drawVerticalLine ((int) xc, phR.getY(), phR.getBottom());
        }

        for (const auto& c : curveDbs)
            drawCurve (g, c, magR, SuperMoToTheme::curveMeasurement.withAlpha (0.55f), 1.0f);

        if (hasSub)
            drawCurve (g, subDb, magR, SuperMoToTheme::mono, 1.8f);
        drawCurve (g, averageDb, magR, SuperMoToTheme::curveAverage, 2.4f);
        drawCurve (g, correctionDb, magR, SuperMoToTheme::master, 1.6f);
        drawCurve (g, correctedDb, magR, SuperMoToTheme::spectrum, 1.6f);

        // ── Phase panel ──────────────────────────────────────────────────────
        for (float deg = -180.0f; deg <= 180.0f; deg += 90.0f)
        {
            const float y = phaseToY (deg, phR);
            g.setColour (deg == 0.0f ? SuperMoToTheme::gridZero : SuperMoToTheme::grid);
            g.drawHorizontalLine ((int) y, phR.getX(), phR.getRight());
            g.setColour (SuperMoToTheme::dimText);
            g.drawText (juce::String ((int) deg), (int) bounds.getX() + 1, (int) y - 7, 23, 14,
                        juce::Justification::centredRight);
        }

        for (const auto& c : curvePhases)
            drawPhaseCurve (g, c, phR, SuperMoToTheme::curveMeasurement.withAlpha (0.45f), 1.0f);

        if (hasSub)
            drawPhaseCurve (g, subPhase, phR, SuperMoToTheme::mono, 1.8f);
        drawPhaseCurve (g, averagePhase, phR, SuperMoToTheme::curveAverage, 2.0f);
        drawPhaseCurve (g, correctionPhase, phR, SuperMoToTheme::master, 1.4f);
        drawPhaseCurve (g, correctedPhase, phR, SuperMoToTheme::spectrum, 1.4f);

        // ── Legend & axis descriptions ───────────────────────────────────────
        struct Item { const char* name; juce::Colour col; };
        std::vector<Item> items { { "measurements", SuperMoToTheme::curveMeasurement },
                                  { "average", SuperMoToTheme::curveAverage },
                                  { "correction", SuperMoToTheme::master },
                                  { "corrected", SuperMoToTheme::spectrum } };
        if (hasSub)
            items.push_back ({ "sub", SuperMoToTheme::mono });
        int x = (int) magR.getX() + 6;
        g.setFont (11.0f);
        for (const auto& item : items)
        {
            g.setColour (item.col);
            g.fillRect (x, (int) magR.getY() + 4, 10, 3);
            g.setColour (SuperMoToTheme::text);
            const auto label = juce::String (item.name);
            const int w = (int) juce::GlyphArrangement::getStringWidth (juce::Font (11.0f), label) + 6;
            g.drawText (label, x + 13, (int) magR.getY() - 2, w, 14, juce::Justification::centredLeft);
            x += w + 26;
        }

        g.setColour (SuperMoToTheme::dimText);
        g.drawText (juce::String::fromUTF8 ("|H| (dB) \xe2\x80\x94 0 dB = 200 Hz\xe2\x80\x93"
                                            "2 kHz mean of the average; correction in absolute dB"),
                    (int) magR.getX(), (int) magR.getBottom() - 14,
                    (int) magR.getWidth() - 6, 12, juce::Justification::centredRight);
        g.drawText (juce::String::fromUTF8 ("phase (\xc2\xb0, propagation delay removed)"),
                    (int) phR.getX(), (int) phR.getY() + 2,
                    (int) phR.getWidth() - 6, 12, juce::Justification::centredRight);

        // Cursor read-out (top-right): frequency + level over the magnitude
        // panel, frequency + phase over the phase panel.
        if (cursorInPlot)
        {
            const bool inPhase = phR.contains (cursorPos);
            const auto& r = inPhase ? phR : magR;
            const float relX = juce::jlimit (0.0f, 1.0f, (cursorPos.x - magR.getX()) / magR.getWidth());
            const float f = fMin * std::exp (relX * std::log (fMax / fMin));
            const float cy = juce::jlimit (r.getY(), r.getBottom(), cursorPos.y);

            juce::String txt = (f >= 1000.0f ? juce::String (f / 1000.0f, 2) + " kHz"
                                             : juce::String (juce::roundToInt (f)) + " Hz") + "   ";
            if (inPhase)
                txt += juce::String (juce::roundToInt (
                           juce::jmap (cy, r.getBottom(), r.getY(), -180.0f, 180.0f)))
                       + juce::String::fromUTF8 ("\xc2\xb0");
            else
                txt += juce::String (juce::jmap (cy, r.getBottom(), r.getY(), plotMinDb, plotMaxDb), 1)
                       + " dB";

            g.setFont (11.0f);
            const int tw = (int) juce::GlyphArrangement::getStringWidth (juce::Font (11.0f), txt) + 12;
            juce::Rectangle<int> box ((int) magR.getRight() - tw, (int) magR.getY() - 2, tw, 15);
            g.setColour (SuperMoToTheme::plotBackground.withAlpha (0.8f));
            g.fillRoundedRectangle (box.toFloat(), 3.0f);
            g.setColour (SuperMoToTheme::panelLine);
            g.drawRoundedRectangle (box.toFloat(), 3.0f, 1.0f);
            g.setColour (SuperMoToTheme::text);
            g.drawText (txt, box, juce::Justification::centred);
        }
    }

    SuperMoToAudioProcessor& processor;
    smt::AnalysisEngine analysis;

    juce::Label title, windowLabel, smoothLabel, levelLabel, firLabel, assignLabel, boostLabel, status;
    juce::Label micCalInfo;
    juce::Label firInfo, rangeLabel, rangeToLabel, crossoverLabel, phaseLabel;
    juce::Label alignLabel, alignInfo;
    juce::Slider boostSlider, alignSlider;
    float recommendedAlignMs = 0.0f;
    juce::TextButton loadButton, exportButton, exportMeasuredButton, loadSubButton;

    // dB-axis zoom/pan state.
    bool dragging = false;
    float dragStartY = 0.0f, dragStartMin = -30.0f, dragStartMax = 30.0f;

    // Cursor read-out (frequency / level or phase at the pointer), top-right.
    juce::Point<float> cursorPos;
    bool cursorInPlot = false;
    juce::ComboBox windowBox, smoothLowBox, smoothHighBox, firBox, phaseBox, assignBox, lowFreqBox, highFreqBox, crossoverBox;
    juce::ToggleButton subInvertToggle;
    juce::Slider levelSlider;

    juce::Array<juce::File> loadedFiles;
    std::vector<float> freqs;
    std::vector<std::vector<float>> curveDbs, curvePhases;
    std::vector<float> averageDb, correctionDb, correctedDb;
    std::vector<float> averagePhase, correctionPhase, correctedPhase;
    std::vector<float> subDb, subPhase;

    juce::Rectangle<int> plotArea;
    std::unique_ptr<juce::FileChooser> fileChooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AnalysisComponent)
};
