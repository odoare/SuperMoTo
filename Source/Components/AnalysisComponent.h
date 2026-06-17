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

        addLabel (smoothLabel, "Smoothing");
        smoothBox.addItem ("Off", 1);
        smoothBox.addItem ("1/24 oct", 2);
        smoothBox.addItem ("1/12 oct", 3);
        smoothBox.addItem ("1/6 oct", 4);
        smoothBox.addItem ("1/3 oct", 5);
        smoothBox.addItem ("1 oct", 6);
        smoothBox.setSelectedId (4, juce::dontSendNotification);
        SuperMoToTheme::accentComboBox (smoothBox, SuperMoToTheme::spectrum);
        smoothBox.onChange = [this]
        {
            static const float fractions[] = { 0.0f, 1.0f / 24.0f, 1.0f / 12.0f,
                                               1.0f / 6.0f, 1.0f / 3.0f, 1.0f };
            analysis.setSmoothing (fractions[juce::jlimit (0, 5, smoothBox.getSelectedId() - 1)]);
            updatePlotData();
        };
        addAndMakeVisible (smoothBox);

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
        addLabel (rangeLabel, "Analysis range");
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

        status.setColour (juce::Label::textColourId, SuperMoToTheme::spectrum);
        addAndMakeVisible (status);

        buildFreqGrid();
        updateFirInfo();
    }

    void paint (juce::Graphics& g) override
    {
        g.setColour (SuperMoToTheme::panel.withAlpha (0.7f));
        g.fillRoundedRectangle (getLocalBounds().toFloat(), 6.0f);
        g.setColour (SuperMoToTheme::panelLine);
        g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (0.5f), 6.0f, 1.0f);

        paintPlot (g, plotArea.toFloat());
    }

    // The four +/- badges adjust the magnitude axis limits (10 dB steps).
    void mouseDown (const juce::MouseEvent& e) override
    {
        const auto p = e.getPosition();
        bool changed = true;
        if      (limitButton (0).contains (p)) plotMaxDb = juce::jmin (60.0f, plotMaxDb + 10.0f);
        else if (limitButton (1).contains (p)) plotMaxDb = juce::jmax (plotMinDb + 20.0f, plotMaxDb - 10.0f);
        else if (limitButton (2).contains (p)) plotMinDb = juce::jmin (plotMaxDb - 20.0f, plotMinDb + 10.0f);
        else if (limitButton (3).contains (p)) plotMinDb = juce::jmax (-90.0f, plotMinDb - 10.0f);
        else changed = false;

        if (changed)
            repaint();
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced (14);
        title.setBounds (area.removeFromTop (26).withTrimmedLeft (30));   // room for the info button
        area.removeFromTop (6);

        // The two export buttons share a column: "Export IR" sits to the right
        // of the smoothing control on row 1, the correction export directly
        // below it on row 2 (same x, same width). The level / boost sliders take
        // whatever is left to the right of that column.
        constexpr int exportW = 165;

        auto r1 = area.removeFromTop (24);
        loadButton.setBounds (r1.removeFromLeft (170));
        r1.removeFromLeft (16);
        windowLabel.setBounds (r1.removeFromLeft (90));
        windowBox.setBounds (r1.removeFromLeft (100));
        r1.removeFromLeft (16);
        smoothLabel.setBounds (r1.removeFromLeft (70));
        smoothBox.setBounds (r1.removeFromLeft (90));
        r1.removeFromLeft (16);
        exportMeasuredButton.setBounds (r1.removeFromLeft (exportW));
        const int exportX = exportMeasuredButton.getX();
        r1.removeFromLeft (16);
        levelLabel.setBounds (r1.removeFromLeft (100));
        levelSlider.setBounds (r1);

        area.removeFromTop (8);
        auto r2 = area.removeFromTop (24);
        firLabel.setBounds (r2.removeFromLeft (70));
        firBox.setBounds (r2.removeFromLeft (100));
        r2.removeFromLeft (16);
        assignLabel.setBounds (r2.removeFromLeft (62));
        assignBox.setBounds (r2.removeFromLeft (110));
        // Jump to the export column so the correction button lines up under
        // "Export IR" regardless of the controls on its left.
        r2.removeFromLeft (juce::jmax (16, exportX - r2.getX()));
        exportButton.setBounds (r2.removeFromLeft (exportW));
        r2.removeFromLeft (16);
        boostLabel.setBounds (r2.removeFromLeft (70));
        boostSlider.setBounds (r2);

        area.removeFromTop (6);
        auto r3 = area.removeFromTop (22);
        rangeLabel.setBounds (r3.removeFromLeft (96));
        lowFreqBox.setBounds (r3.removeFromLeft (88));
        rangeToLabel.setBounds (r3.removeFromLeft (14));
        highFreqBox.setBounds (r3.removeFromLeft (88));
        r3.removeFromLeft (20);
        firInfo.setBounds (r3);

        area.removeFromTop (8);
        auto r4 = area.removeFromTop (24);
        loadSubButton.setBounds (r4.removeFromLeft (190));
        r4.removeFromLeft (16);
        crossoverLabel.setBounds (r4.removeFromLeft (66));
        crossoverBox.setBounds (r4.removeFromLeft (90));
        r4.removeFromLeft (16);
        subInvertToggle.setBounds (r4.removeFromLeft (96));

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
            juce::File::getSpecialLocation (juce::File::userHomeDirectory), "*.wav");

        fileChooser->launchAsync (juce::FileBrowserComponent::openMode
                                  | juce::FileBrowserComponent::canSelectFiles
                                  | juce::FileBrowserComponent::canSelectMultipleItems,
            [this] (const juce::FileChooser& fc)
            {
                if (fc.getResults().isEmpty())
                    return;
                loadedFiles = fc.getResults();
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
            juce::File::getSpecialLocation (juce::File::userHomeDirectory), "*.wav");

        fileChooser->launchAsync (juce::FileBrowserComponent::openMode
                                  | juce::FileBrowserComponent::canSelectFiles
                                  | juce::FileBrowserComponent::canSelectMultipleItems,
            [this] (const juce::FileChooser& fc)
            {
                if (fc.getResults().isEmpty())
                    return;
                const int ok = analysis.loadSubFiles (fc.getResults());
                status.setText (ok > 0
                    ? juce::String (ok) + " sub measurement(s) aligned for phase integration."
                    : "No sub file could be analyzed (need stereo wavs at the main's rate/length).",
                    juce::dontSendNotification);
                updatePlotData();
            });
    }

    void analyze()
    {
        status.setText ("Analyzing " + juce::String (loadedFiles.size()) + " file(s)...",
                        juce::dontSendNotification);

        analysis.setCorrectionLevel ((float) levelSlider.getValue());
        const int ok = analysis.loadFiles (loadedFiles);

        status.setText (ok > 0
            ? juce::String (ok) + " measurement(s) analyzed at "
                + juce::String (analysis.getSampleRate() / 1000.0, 1) + " kHz."
            : "No file could be analyzed (need stereo wavs longer than the window).",
            juce::dontSendNotification);

        updateFirInfo();
        updatePlotData();
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

        const double durMs  = 1000.0 * (double) N / fs;
        const double latMs  = 1000.0 * (double) (N / 2) / fs;
        const double fMinHz = 2.0 * fs / (double) N;

        firInfo.setText (
            juce::String::fromUTF8 ("\xe2\x86\x92 corrects down to ~")
                + juce::String (fMinHz, fMinHz < 100.0 ? 1 : 0) + " Hz   "
                + juce::String::fromUTF8 ("\xc2\xb7  ") + juce::String (durMs, 0) + " ms long  "
                + juce::String::fromUTF8 ("\xc2\xb7  ") + juce::String (latMs, 0) + " ms latency"
                + (known ? juce::String() : juce::String ("   (at 48 kHz)")),
            juce::dontSendNotification);
    }

    void pushRange()
    {
        analysis.setAnalysisRange (lowFreqBox.getText().getFloatValue(),
                                   highFreqBox.getText().getFloatValue());
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
            juce::File::getSpecialLocation (juce::File::userHomeDirectory)
                .getChildFile ("measurement.wav"), "*.wav");

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
            juce::File::getSpecialLocation (juce::File::userHomeDirectory)
                .getChildFile ("correction.wav"), "*.wav");

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

    // Magnitude panel rectangle (mirrors the split done in paintPlot), used to
    // place the vertical-limit badges so paint and hit-testing agree.
    juce::Rectangle<float> magPanelArea() const
    {
        auto inner = plotArea.toFloat().reduced (24.0f, 12.0f);
        return inner.removeFromTop (inner.getHeight() * 0.62f);
    }

    // Vertical-limit badges, just right of the dB axis: a +/- pair at the top
    // (max) and at the bottom (min). idx 0=max+,1=max-,2=min+,3=min-.
    juce::Rectangle<int> limitButton (int idx) const
    {
        auto m = magPanelArea();
        constexpr int w = 16, h = 14, gap = 2;
        const int x = (int) m.getX() + 2 + (idx % 2 == 0 ? 0 : w + gap);
        const int y = idx < 2 ? (int) m.getY() + 1 : (int) m.getBottom() - h - 1;
        return { x, y, w, h };
    }

    void drawLimitButton (juce::Graphics& g, juce::Rectangle<int> r, const juce::String& t) const
    {
        auto b = r.toFloat();
        g.setColour (SuperMoToTheme::plotBackground.withAlpha (0.6f));
        g.fillRoundedRectangle (b, 3.0f);
        g.setColour (SuperMoToTheme::panelLine);
        g.drawRoundedRectangle (b, 3.0f, 1.0f);
        g.setColour (SuperMoToTheme::dimText);
        g.setFont (12.0f);
        g.drawText (t, b, juce::Justification::centred);
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
        int x = (int) magR.getX() + 42;     // clear the top vertical-limit badges
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

        // Vertical-limit badges: +/- for the max (top) and the min (bottom).
        drawLimitButton (g, limitButton (0), "+");
        drawLimitButton (g, limitButton (1), juce::String::fromUTF8 ("\xe2\x88\x92"));
        drawLimitButton (g, limitButton (2), "+");
        drawLimitButton (g, limitButton (3), juce::String::fromUTF8 ("\xe2\x88\x92"));
    }

    SuperMoToAudioProcessor& processor;
    smt::AnalysisEngine analysis;

    juce::Label title, windowLabel, smoothLabel, levelLabel, firLabel, assignLabel, boostLabel, status;
    juce::Label firInfo, rangeLabel, rangeToLabel, crossoverLabel;
    juce::Slider boostSlider;
    juce::TextButton loadButton, exportButton, exportMeasuredButton, loadSubButton;
    juce::ComboBox windowBox, smoothBox, firBox, assignBox, lowFreqBox, highFreqBox, crossoverBox;
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
