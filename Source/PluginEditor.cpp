/*
  ------------------------------------------------------------------------------

    PluginEditor.cpp
    Author:  Olivier Doaré
    github.com/odoare

    (c) 2023-2026 Olivier Doaré

    Licenced under the GNU Lesser General Public License (LGPL) Version 3.0
    SPDX-License-Identifier: LGPL-3.0-or-later

  ------------------------------------------------------------------------------
    This file is part of the SuperMoTo plugin.
  ------------------------------------------------------------------------------
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
SuperMoToAudioProcessorEditor::SuperMoToAudioProcessorEditor (SuperMoToAudioProcessor& p)
    : AudioProcessorEditor (&p), audioProcessor (p),
      matrix (p.configModel, p.engine),
      spectrum (p.engine),
      frameEditor (p.configModel),
      configTool (p.configModel),
      calibration (p),
      analysis (p)
{
    setLookAndFeel (&fxmeLookAndFeel);

    logo = juce::ImageCache::getFromMemory (BinaryData::logo686_png, BinaryData::logo686_pngSize);

    // ── Top bar ──────────────────────────────────────────────────────────────
    for (int c = 0; c < smt::numConfigs; ++c)
    {
        auto* b = configButtons.add (new fxme::FxmeButton (audioProcessor.apvts,
                                                           smt::configName (c),
                                                           SuperMoToTheme::configColour (c)));
        b->setLookAndFeel (&fxmeLookAndFeel);
        addAndMakeVisible (b);
    }

    exclusiveButton = std::make_unique<fxme::FxmeButton> (audioProcessor.apvts, "Exclusive",
                                                          SuperMoToTheme::exclusive);
    muteButton = std::make_unique<fxme::FxmeButton> (audioProcessor.apvts, "Mute",
                                                     SuperMoToTheme::mute);
    dimButton = std::make_unique<fxme::FxmeButton> (audioProcessor.apvts, "Dim",
                                                    SuperMoToTheme::dim);
    monoButton = std::make_unique<fxme::FxmeButton> (audioProcessor.apvts, "Mono",
                                                     SuperMoToTheme::mono);
    for (auto* b : { exclusiveButton.get(), muteButton.get(), dimButton.get(), monoButton.get() })
    {
        b->setLookAndFeel (&fxmeLookAndFeel);
        addAndMakeVisible (*b);
    }

    levelKnob = std::make_unique<fxme::FxmeKnob> (audioProcessor.apvts, "Level", "Level",
                                                  SuperMoToTheme::master);
    levelKnob->setLookAndFeel (&fxmeLookAndFeel);
    addAndMakeVisible (*levelKnob);

    auto initViewButton = [this] (juce::TextButton& b, const juce::String& text, View v)
    {
        b.setButtonText (text);
        b.setClickingTogglesState (false);
        b.setColour (juce::TextButton::buttonColourId, SuperMoToTheme::panel);
        b.setColour (juce::TextButton::buttonOnColourId, SuperMoToTheme::master.darker (0.6f));
        b.onClick = [this, v] { setView (v); };
        addAndMakeVisible (b);
    };
    initViewButton (matrixViewButton, "Matrix", View::matrix);
    initViewButton (configToolButton, "Config tool", View::configTool);
    initViewButton (calibrationButton, "Calibration", View::calibration);
    initViewButton (analysisButton, "Analysis", View::analysis);

    // ── Matrix view ──────────────────────────────────────────────────────────
    addAndMakeVisible (matrix);
    matrix.onFrameSelected = [this] (int in, int out)
    {
        frameEditor.setFrame (in, out, editConfig);
    };

    editLabel.setText ("Edit:", juce::dontSendNotification);
    editLabel.setColour (juce::Label::textColourId, SuperMoToTheme::dimText);
    addAndMakeVisible (editLabel);

    for (int c = 0; c < smt::numConfigs; ++c)
    {
        auto* b = editConfigButtons.add (new juce::TextButton (smt::configName (c)));
        b->setClickingTogglesState (false);
        b->setColour (juce::TextButton::buttonColourId, SuperMoToTheme::panel);
        b->setColour (juce::TextButton::buttonOnColourId, SuperMoToTheme::configColour (c).darker (0.4f));
        b->onClick = [this, c] { setEditConfig (c); };
        addAndMakeVisible (b);
    }

    addAndMakeVisible (spectrum);
    spectrum.sampleRateProvider = [this] { return audioProcessor.getSampleRate(); };
    spectrum.frameLabelProvider = [this] (int slot) -> juce::String
    {
        const auto refs = audioProcessor.configModel.getSpectrumFrames();
        if (slot >= 0 && slot < (int) refs.size())
            return smt::configName (refs[(size_t) slot].config) + " "
                 + juce::String (refs[(size_t) slot].in + 1)
                 + juce::String::fromUTF8 ("\xe2\x86\x92")
                 + juce::String (refs[(size_t) slot].out + 1);
        return "Frame " + juce::String (slot + 1);
    };

    addAndMakeVisible (frameEditor);

    // ── Other views (hidden until selected) ─────────────────────────────────
    addChildComponent (configTool);
    addChildComponent (calibration);
    addChildComponent (analysis);

    setEditConfig (0);
    setView (View::matrix);

    setResizable (true, true);
    setResizeLimits (1100, 720, 2400, 1600);
    setSize (1280, 820);
}

SuperMoToAudioProcessorEditor::~SuperMoToAudioProcessorEditor()
{
    setLookAndFeel (nullptr);
}

//==============================================================================
void SuperMoToAudioProcessorEditor::setView (View v)
{
    currentView = v;
    const bool m = v == View::matrix;
    matrix.setVisible (m);
    spectrum.setVisible (m);
    frameEditor.setVisible (m);
    editLabel.setVisible (m);
    for (auto* b : editConfigButtons)
        b->setVisible (m);

    configTool.setVisible (v == View::configTool);
    calibration.setVisible (v == View::calibration);
    analysis.setVisible (v == View::analysis);

    matrixViewButton.setToggleState (m, juce::dontSendNotification);
    configToolButton.setToggleState (v == View::configTool, juce::dontSendNotification);
    calibrationButton.setToggleState (v == View::calibration, juce::dontSendNotification);
    analysisButton.setToggleState (v == View::analysis, juce::dontSendNotification);
}

void SuperMoToAudioProcessorEditor::setEditConfig (int c)
{
    editConfig = juce::jlimit (0, smt::numConfigs - 1, c);
    matrix.setEditConfig (editConfig);
    frameEditor.setFrame (-1, -1, editConfig);
    matrix.setSelectedFrame (-1, -1);
    for (int i = 0; i < editConfigButtons.size(); ++i)
        editConfigButtons[i]->setToggleState (i == editConfig, juce::dontSendNotification);
}

//==============================================================================
void SuperMoToAudioProcessorEditor::paint (juce::Graphics& g)
{
    SuperMoToTheme::paintBackground (g, getLocalBounds().toFloat());

    // Title + logo
    auto top = getLocalBounds().removeFromTop (60);
    if (logo.isValid())
        g.drawImage (logo, juce::Rectangle<float> (8.0f, 6.0f, 48.0f, 48.0f),
                     juce::RectanglePlacement::centred);
    g.setColour (SuperMoToTheme::text);
    g.setFont (juce::Font (24.0f, juce::Font::bold));
    g.drawText ("SuperMoTo", 62, 0, 170, top.getHeight(), juce::Justification::centredLeft);
}

void SuperMoToAudioProcessorEditor::resized()
{
    auto area = getLocalBounds();

    // ── Top bar ──────────────────────────────────────────────────────────────
    auto top = area.removeFromTop (60).reduced (6);
    top.removeFromLeft (230);                       // logo + title

    auto cfgArea = top.removeFromLeft (smt::numConfigs * 44);
    for (auto* b : configButtons)
        b->setBounds (cfgArea.removeFromLeft (44));
    exclusiveButton->setBounds (top.removeFromLeft (86));

    levelKnob->setBounds (top.removeFromRight (66));
    monoButton->setBounds (top.removeFromRight (62));
    dimButton->setBounds (top.removeFromRight (56));
    muteButton->setBounds (top.removeFromRight (62));

    top.removeFromLeft (12);
    auto views = top.reduced (0, 8);
    const int vw = juce::jmin (110, views.getWidth() / 4);
    matrixViewButton.setBounds (views.removeFromLeft (vw).reduced (2, 0));
    configToolButton.setBounds (views.removeFromLeft (vw).reduced (2, 0));
    calibrationButton.setBounds (views.removeFromLeft (vw).reduced (2, 0));
    analysisButton.setBounds (views.removeFromLeft (vw).reduced (2, 0));

    // ── Main area ────────────────────────────────────────────────────────────
    auto main = area.reduced (8);

    // Full-window panels
    configTool.setBounds (main);
    calibration.setBounds (main);
    analysis.setBounds (main);

    // Matrix view: matrix left, analyzer + frame editor right
    auto right = main.removeFromRight (juce::jmax (340, main.getWidth() / 4 + 60));
    main.removeFromRight (8);
    matrix.setBounds (main);

    auto editRow = right.removeFromTop (26);
    editLabel.setBounds (editRow.removeFromLeft (36));
    for (auto* b : editConfigButtons)
        b->setBounds (editRow.removeFromLeft (40).reduced (2, 0));

    right.removeFromTop (6);
    frameEditor.setBounds (right.removeFromBottom (210));
    right.removeFromBottom (8);
    spectrum.setBounds (right);
}
