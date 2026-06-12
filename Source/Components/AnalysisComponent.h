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

        addLabel (firLabel, "FIR length");
        for (int size = 1 << 10; size <= 1 << 16; size <<= 1)
            firBox.addItem (juce::String (size), size);
        firBox.setSelectedId (4096, juce::dontSendNotification);
        SuperMoToTheme::accentComboBox (firBox, SuperMoToTheme::fir);
        addAndMakeVisible (firBox);

        addLabel (assignLabel, "Assign to");
        assignBox.addItem ("(none)", 1);
        for (int o = 0; o < smt::numChannels; ++o)
            assignBox.addItem ("Output " + juce::String (o + 1), o + 2);
        assignBox.setSelectedId (1, juce::dontSendNotification);
        SuperMoToTheme::accentComboBox (assignBox, SuperMoToTheme::fir);
        addAndMakeVisible (assignBox);

        exportButton.setButtonText ("Export correction IR...");
        exportButton.setColour (juce::TextButton::buttonColourId, SuperMoToTheme::fir.darker (1.0f));
        exportButton.onClick = [this] { exportIr(); };
        addAndMakeVisible (exportButton);

        status.setColour (juce::Label::textColourId, SuperMoToTheme::spectrum);
        addAndMakeVisible (status);

        buildFreqGrid();
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
        title.setBounds (area.removeFromTop (26));
        area.removeFromTop (6);

        auto r1 = area.removeFromTop (24);
        loadButton.setBounds (r1.removeFromLeft (170));
        r1.removeFromLeft (16);
        windowLabel.setBounds (r1.removeFromLeft (90));
        windowBox.setBounds (r1.removeFromLeft (100));
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
        r2.removeFromLeft (16);
        exportButton.setBounds (r2.removeFromLeft (180));

        area.removeFromTop (6);
        status.setBounds (area.removeFromBottom (20));
        area.removeFromBottom (4);
        plotArea = area;
    }

private:
    static constexpr int numPoints = 400;
    static constexpr float fMin = 20.0f, fMax = 20000.0f;
    static constexpr float plotMinDb = -30.0f, plotMaxDb = 30.0f;

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

        updatePlotData();
    }

    void updatePlotData()
    {
        curveDbs.clear();
        for (int i = 0; i < analysis.getNumCurves(); ++i)
            curveDbs.push_back (analysis.getCurveDb (i, freqs));
        averageDb   = analysis.getAverageDb (freqs);
        correctionDb = analysis.getCorrectionDb (freqs);
        correctedDb = analysis.getCorrectedDb (freqs);
        repaint();
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

    void paintPlot (juce::Graphics& g, juce::Rectangle<float> bounds) const
    {
        g.setColour (juce::Colours::black);
        g.fillRoundedRectangle (bounds, 4.0f);

        auto r = bounds.reduced (24.0f, 12.0f);

        g.setFont (10.0f);
        for (float f : { 20.f, 50.f, 100.f, 200.f, 500.f, 1000.f, 2000.f, 5000.f, 10000.f, 20000.f })
        {
            const float x = freqToX (f, r);
            g.setColour (juce::Colours::darkgrey.withAlpha (0.4f));
            g.drawVerticalLine ((int) x, r.getY(), r.getBottom());
            g.setColour (SuperMoToTheme::dimText);
            g.drawText (f >= 1000.0f ? juce::String (f / 1000.0f) + "k" : juce::String ((int) f),
                        (int) x - 14, (int) r.getBottom() + 1, 28, 10, juce::Justification::centred);
        }
        for (float db = plotMinDb; db <= plotMaxDb; db += 10.0f)
        {
            const float y = dbToY (db, r);
            g.setColour (juce::Colours::darkgrey.withAlpha (db == 0.0f ? 0.8f : 0.4f));
            g.drawHorizontalLine ((int) y, r.getX(), r.getRight());
            g.setColour (SuperMoToTheme::dimText);
            g.drawText (juce::String ((int) db), (int) bounds.getX() + 1, (int) y - 5, 21, 10,
                        juce::Justification::centredRight);
        }

        // Individual measurements: thin dim lines.
        for (const auto& c : curveDbs)
            drawCurve (g, c, r, SuperMoToTheme::dimText.withAlpha (0.5f), 0.8f);

        // Average: thick line. Correction & corrected previews on top.
        drawCurve (g, averageDb, r, juce::Colours::white, 2.4f);
        drawCurve (g, correctionDb, r, SuperMoToTheme::master, 1.6f);
        drawCurve (g, correctedDb, r, SuperMoToTheme::spectrum, 1.6f);

        // Legend
        struct Item { const char* name; juce::Colour col; };
        const Item items[] = { { "measurements", SuperMoToTheme::dimText },
                               { "average", juce::Colours::white },
                               { "correction", SuperMoToTheme::master },
                               { "corrected", SuperMoToTheme::spectrum } };
        int x = (int) r.getX() + 6;
        g.setFont (11.0f);
        for (const auto& item : items)
        {
            g.setColour (item.col);
            g.fillRect (x, (int) r.getY() + 4, 10, 3);
            g.setColour (SuperMoToTheme::text);
            const auto label = juce::String (item.name);
            const int w = (int) juce::GlyphArrangement::getStringWidth (juce::Font (11.0f), label) + 6;
            g.drawText (label, x + 13, (int) r.getY() - 2, w, 14, juce::Justification::centredLeft);
            x += w + 26;
        }
    }

    SuperMoToAudioProcessor& processor;
    smt::AnalysisEngine analysis;

    juce::Label title, windowLabel, levelLabel, firLabel, assignLabel, status;
    juce::TextButton loadButton, exportButton;
    juce::ComboBox windowBox, firBox, assignBox;
    juce::Slider levelSlider;

    juce::Array<juce::File> loadedFiles;
    std::vector<float> freqs;
    std::vector<std::vector<float>> curveDbs;
    std::vector<float> averageDb, correctionDb, correctedDb;

    juce::Rectangle<int> plotArea;
    std::unique_ptr<juce::FileChooser> fileChooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AnalysisComponent)
};
