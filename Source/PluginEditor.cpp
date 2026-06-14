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
// Width of the matrix component for a given window width (expanded layout):
// window minus margins, right column and gap. Used to align the top bar and
// to size the collapsed window.
static int matrixWidthFor (int totalWidth)
{
    const int mainW = totalWidth - 16;
    const int rightW = juce::jmax (340, mainW / 4 + 60);
    return mainW - rightW - 8;
}

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
    // All config buttons share the same colour: on/off states read at a glance.
    for (int c = 0; c < smt::numConfigs; ++c)
    {
        auto* b = configButtons.add (new fxme::FxmeButton (audioProcessor.apvts,
                                                           smt::configName (c),
                                                           juce::Colours::cyan));
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

    collapseButton.setButtonText (juce::String::fromUTF8 ("\xe2\x96\xb2"));   // up triangle
    collapseButton.setColour (juce::TextButton::buttonColourId, SuperMoToTheme::panel);
    collapseButton.setColour (juce::TextButton::buttonOnColourId, SuperMoToTheme::master.darker (0.6f));
    collapseButton.setTooltip ("Compact view: only the output strip");
    collapseButton.onClick = [this] { setCollapsed (! collapsed); };
    addAndMakeVisible (collapseButton);

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
    // Matrix size selectors (rows = inputs, columns = outputs).
    auto initSizeBox = [this] (juce::Label& l, juce::ComboBox& b, const juce::String& text)
    {
        l.setText (text, juce::dontSendNotification);
        l.setFont (juce::Font (12.0f));
        l.setColour (juce::Label::textColourId, SuperMoToTheme::dimText);
        addAndMakeVisible (l);

        for (int n = 1; n <= smt::numChannels; ++n)
            b.addItem (juce::String (n), n);
        SuperMoToTheme::accentComboBox (b, SuperMoToTheme::master);
        b.onChange = [this]
        {
            audioProcessor.configModel.setMatrixSize (insBox.getSelectedId(),
                                                      outsBox.getSelectedId());
        };
        addAndMakeVisible (b);
    };
    initSizeBox (insLabel, insBox, "Inputs:");
    initSizeBox (outsLabel, outsBox, "Outputs:");
    insBox.setSelectedId (audioProcessor.configModel.getNumIns(), juce::dontSendNotification);
    outsBox.setSelectedId (audioProcessor.configModel.getNumOuts(), juce::dontSendNotification);
    audioProcessor.configModel.addListener (this);

    addAndMakeVisible (matrix);
    matrix.onFrameSelected = [this] (int in, int out)
    {
        frameEditor.setFrame (in, out, editConfig);
    };

    editLabel.setText ("Edit:", juce::dontSendNotification);
    editLabel.setColour (juce::Label::textColourId, SuperMoToTheme::dimText);
    addAndMakeVisible (editLabel);

    // The edit button of the currently displayed configuration lights cyan.
    for (int c = 0; c < smt::numConfigs; ++c)
    {
        auto* b = editConfigButtons.add (new juce::TextButton (smt::configName (c)));
        b->setClickingTogglesState (false);
        b->setColour (juce::TextButton::buttonColourId, SuperMoToTheme::panel);
        b->setColour (juce::TextButton::buttonOnColourId, juce::Colours::cyan.darker (0.25f));
        b->setColour (juce::TextButton::textColourOnId, juce::Colours::cyan);
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

    // Follow the engaged configuration in the matrix view.
    for (int c = 0; c < smt::numConfigs; ++c)
        audioProcessor.apvts.addParameterListener (smt::configName (c), this);

    // Start on the engaged config (the last one when several are active).
    int initial = 0;
    for (int c = 0; c < smt::numConfigs; ++c)
        if (audioProcessor.apvts.getRawParameterValue (smt::configName (c))->load() > 0.5f)
            initial = c;

    setEditConfig (initial);
    setView (View::matrix);

    setResizable (true, true);
    setResizeLimits (1100, 720, 2400, 1600);
    setSize (1280, 820);
}

SuperMoToAudioProcessorEditor::~SuperMoToAudioProcessorEditor()
{
    audioProcessor.configModel.removeListener (this);
    for (int c = 0; c < smt::numConfigs; ++c)
        audioProcessor.apvts.removeParameterListener (smt::configName (c), this);
    setLookAndFeel (nullptr);
}

void SuperMoToAudioProcessorEditor::modelChanged()
{
    insBox.setSelectedId (audioProcessor.configModel.getNumIns(), juce::dontSendNotification);
    outsBox.setSelectedId (audioProcessor.configModel.getNumOuts(), juce::dontSendNotification);
}

void SuperMoToAudioProcessorEditor::parameterChanged (const juce::String& parameterID, float newValue)
{
    if (newValue < 0.5f)
        return;     // a config being released does not change the displayed one

    for (int c = 0; c < smt::numConfigs; ++c)
    {
        if (smt::configName (c) == parameterID)
        {
            // This callback can come from the audio thread (host automation).
            juce::MessageManager::callAsync (
                [safeThis = juce::Component::SafePointer<SuperMoToAudioProcessorEditor> (this), c]
                {
                    if (safeThis != nullptr)
                        safeThis->setEditConfig (c);
                });
            break;
        }
    }
}

//==============================================================================
void SuperMoToAudioProcessorEditor::setView (View v)
{
    currentView = v;
    const bool m = v == View::matrix && ! collapsed;

    // In collapsed mode the matrix stays visible but shrinks to its output
    // strip; everything else goes away.
    matrix.setVisible (m || collapsed);
    spectrum.setVisible (m);
    frameEditor.setVisible (m);
    editLabel.setVisible (m);
    insLabel.setVisible (m);
    insBox.setVisible (m);
    outsLabel.setVisible (m);
    outsBox.setVisible (m);
    for (auto* b : editConfigButtons)
        b->setVisible (m);

    for (auto* b : { &matrixViewButton, &configToolButton, &calibrationButton, &analysisButton })
        b->setVisible (! collapsed);

    configTool.setVisible (v == View::configTool && ! collapsed);
    calibration.setVisible (v == View::calibration && ! collapsed);
    analysis.setVisible (v == View::analysis && ! collapsed);

    matrixViewButton.setToggleState (v == View::matrix, juce::dontSendNotification);
    configToolButton.setToggleState (v == View::configTool, juce::dontSendNotification);
    calibrationButton.setToggleState (v == View::calibration, juce::dontSendNotification);
    analysisButton.setToggleState (v == View::analysis, juce::dontSendNotification);
}

void SuperMoToAudioProcessorEditor::setCollapsed (bool shouldCollapse)
{
    if (collapsed == shouldCollapse)
        return;
    collapsed = shouldCollapse;

    collapseButton.setButtonText (juce::String::fromUTF8 (collapsed ? "\xe2\x96\xbc"      // down
                                                                    : "\xe2\x96\xb2"));   // up
    collapseButton.setToggleState (collapsed, juce::dontSendNotification);

    if (collapsed)
    {
        expandedWidth = getWidth();
        expandedHeight = getHeight();
        setView (currentView);

        // Shrink to the matrix width: the strip then spans the whole window.
        const int collapsedWidth = matrixWidthFor (expandedWidth) + 16;
        const int collapsedHeight = 60 + MatrixComponent::outputStripH + 16;
        setResizeLimits (collapsedWidth, collapsedHeight, collapsedWidth, collapsedHeight);
        setSize (collapsedWidth, collapsedHeight);
    }
    else
    {
        setResizeLimits (1100, 720, 2400, 1600);
        setSize (expandedWidth, expandedHeight);
        setView (currentView);
    }
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
    // Left-packed and kept within the matrix width, so the collapsed window
    // (sized to the matrix) shows every control.
    auto top = area.removeFromTop (60).reduced (6);
    top.removeFromLeft (232);                       // logo + title
    collapseButton.setBounds (top.removeFromLeft (28).reduced (0, 14));
    top.removeFromLeft (12);

    const int barRight = collapsed ? getWidth() - 8
                                   : 8 + matrixWidthFor (getWidth());
    top.setRight (juce::jmin (top.getRight(), barRight));

    // Scale the control widths down proportionally when space is tight.
    const int needed = smt::numConfigs * 44 + 86 + 66 + 8 + 62 + 56 + 62;
    const float scale = juce::jmin (1.0f, (float) top.getWidth() / (float) needed);
    auto sw = [scale] (int px) { return juce::roundToInt (scale * (float) px); };

    for (auto* b : configButtons)
        b->setBounds (top.removeFromLeft (sw (44)));
    exclusiveButton->setBounds (top.removeFromLeft (sw (86)));
    levelKnob->setBounds (top.removeFromLeft (sw (66)));
    top.removeFromLeft (sw (8));
    muteButton->setBounds (top.removeFromLeft (sw (62)));
    dimButton->setBounds (top.removeFromLeft (sw (56)));
    monoButton->setBounds (top.removeFromLeft (sw (62)));

    // ── Collapsed: only the output strip below the top bar ───────────────────
    if (collapsed)
    {
        matrix.setBounds (area.reduced (8, 4).removeFromTop (MatrixComponent::outputStripH));
        return;
    }

    // ── Bottom control bar: matrix size, edit config, view switcher ──────────
    auto bottom = area.removeFromBottom (34).reduced (8, 4);

    insLabel.setBounds (bottom.removeFromLeft (48));
    insBox.setBounds (bottom.removeFromLeft (58).reduced (0, 1));
    bottom.removeFromLeft (14);
    outsLabel.setBounds (bottom.removeFromLeft (56));
    outsBox.setBounds (bottom.removeFromLeft (58).reduced (0, 1));

    bottom.removeFromLeft (24);
    editLabel.setBounds (bottom.removeFromLeft (36));
    for (auto* b : editConfigButtons)
        b->setBounds (bottom.removeFromLeft (40).reduced (2, 1));

    const int vw = juce::jmin (110, bottom.getWidth() / 4);
    analysisButton.setBounds (bottom.removeFromRight (vw).reduced (2, 1));
    calibrationButton.setBounds (bottom.removeFromRight (vw).reduced (2, 1));
    configToolButton.setBounds (bottom.removeFromRight (vw).reduced (2, 1));
    matrixViewButton.setBounds (bottom.removeFromRight (vw).reduced (2, 1));

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

    frameEditor.setBounds (right.removeFromBottom (210));
    right.removeFromBottom (8);
    spectrum.setBounds (right);
}
