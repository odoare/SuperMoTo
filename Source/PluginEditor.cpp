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
                                                           SuperMoToTheme::configEngage));
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

    levelSlider = std::make_unique<fxme::FxmeSlider> (audioProcessor.apvts, "Level", "Level",
                                                      SuperMoToTheme::master);
    levelSlider->setSliderStyle (juce::Slider::LinearHorizontal);
    levelSlider->setTooltip ("Master level (dB)");
    SuperMoToTheme::accentSlider (*levelSlider, SuperMoToTheme::master);
    levelSlider->setLookAndFeel (&fxmeLookAndFeel);
    addAndMakeVisible (*levelSlider);

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

    // The edit button of the currently displayed configuration lights up.
    for (int c = 0; c < smt::numConfigs; ++c)
    {
        auto* b = editConfigButtons.add (new juce::TextButton (smt::configName (c)));
        b->setClickingTogglesState (false);
        b->setColour (juce::TextButton::buttonColourId, SuperMoToTheme::panel);
        b->setColour (juce::TextButton::buttonOnColourId, SuperMoToTheme::configEngage.darker (0.25f));
        b->setColour (juce::TextButton::textColourOnId, SuperMoToTheme::configEngage);
        b->onClick = [this, c] { setEditConfig (c); };
        addAndMakeVisible (b);
    }

    addAndMakeVisible (spectrum);
    spectrum.sampleRateProvider = [this] { return audioProcessor.getSampleRate(); };

    // Microphone trace: shown whenever the calibration SPL meter is on (its tap
    // is enabled with the meter), so the EQ can be tuned against the measured
    // response right here in the matrix view.
    {
        SpectrumDisplay::TraceConfig mic;
        mic.tap       = &audioProcessor.splMeter.getMicSpectrumTap();
        mic.colour    = SuperMoToTheme::spectrum;
        mic.thickness = 1.6f;
        mic.label     = [] { return juce::String ("Microphone"); };
        spectrum.addTrace (std::move (mic));
    }

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

    // Per-page help, repositioned per view (kept on top of everything).
    addAndMakeVisible (infoButton);

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

    juce::String t, body;
    infoTextFor (v, t, body);
    infoButton.setInfo (t, body);
    infoButton.setVisible (! collapsed);
    layoutInfoButton();
}

// Matrix page: the info button sits in the empty top-left corner of the matrix
// (the cell where the input-label column meets the output strip), so it is
// clear of every crosspoint. Other pages: top-left, just before the title (the
// panels indent their title by titleIndent to leave room).
void SuperMoToAudioProcessorEditor::layoutInfoButton()
{
    constexpr int sz = 22;
    if (currentView == View::matrix)
        infoButton.setBounds (matrixArea.getX() + 4, matrixArea.getY() + 4, sz, sz);
    else
        infoButton.setBounds (panelArea.getX() + 8, panelArea.getY() + 14, sz, sz);
}

void SuperMoToAudioProcessorEditor::infoTextFor (View v, juce::String& title, juce::String& body)
{
    switch (v)
    {
        case View::matrix:
            title = "Matrix \xe2\x80\x94 monitoring & routing";
            body  =
                "The 16x16 matrix routes inputs (rows) to outputs (columns). Up to six "
                "configurations A-F can be stored; engage them from the top bar. With "
                "Exclusive only one is active at a time, otherwise the engaged matrices sum.\n\n"
                "Frame (crosspoint):\n"
                " - Click: select (opens the editor, bottom right)\n"
                " - Double-click: activate / deactivate\n"
                " - Vertical drag: gain\n"
                " - Alt+click: show / hide its analyzer trace\n"
                " - Right-click: context menu\n\n"
                "Output strip (top row):\n"
                " - Double-click: toggle the output FIR\n"
                " - Vertical drag: output trim\n"
                " - Alt+click: analyzer trace\n"
                " - Right-click: load / clear the FIR impulse\n"
                " - Gold dot: latency compensation was added to align this output with the "
                "longest output FIR (hover for the amount)\n\n"
                "Top bar: A-F engage, Exclusive, Level, Mute, Dim, Mono, and the up-arrow "
                "collapses to the output strip only.\n"
                "Bottom bar: matrix size, Edit A-F (view/edit a config without engaging it), "
                "and the page switch.\n\n"
                "Analyzer: click the avg/peak badge (bottom-right) to switch aggregation.";
            break;

        case View::configTool:
            title = "Speaker configuration tool";
            body  =
                "Fills one of the A-F configurations for a standard layout (2.0, 2.1, 4.0, "
                "5.1, 7.1) in a few clicks.\n\n"
                "1. Pick the Layout and the target (Write to config A-F).\n"
                "2. For each speaker choose its input(s) and output, and a gain.\n"
                "3. Bass management (optional): highpass the mains and send their lowpassed "
                "sum to the subwoofer output at the chosen crossover.\n"
                "4. Apply writes the frames into the configuration (overwriting it).\n\n"
                "Refine the result per-frame in the Matrix view, and add per-output FIR "
                "correction there.";
            break;

        case View::calibration:
            title = "Measurement & calibration";
            body  =
                "Measure loudspeakers or the whole system, and calibrate a dB SPL reference.\n\n"
                "Measurement\n"
                "1. Microphone input: the channel your measurement mic is on.\n"
                "2. Measure (type):\n"
                "    - Dry: stimulus straight to an output (raw speaker, to design FIRs)\n"
                "    - FIR: output through its trim + FIR (verify a correction)\n"
                "    - System: into a plugin input, through the whole engine (matrix, "
                "crossover, FIRs, latency compensation)\n"
                "3. Channels: outputs to measure (Dry/FIR), or inputs (System).\n"
                "4. Signal (sweep or noise), Duration, Level, base pathname.\n"
                "5. Run: writes one stereo wav per channel (ch1 = sent, ch2 = recorded). "
                "Inputs are tagged \"_in\".\n\n"
                "SPL meter\n"
                "Meter on shows the mic RMS (dBFS, and dB SPL once calibrated). Sine / White "
                "noise emit a test signal on the selected channels following the same mode. "
                "To calibrate: play a tone, read a real SPL meter, type the value in "
                "\"Measured dB SPL\".\n\n"
                "Spectrum: click the avg/peak badge (bottom-right) to switch aggregation.";
            break;

        case View::analysis:
            title = "Analysis & correction design";
            body  =
                "Load a speaker's measurement set (mic moved around the reference). The "
                "transfer functions are estimated (Welch), delay-aligned and averaged; a "
                "correction is derived from the inverse of the smoothed average (magnitude "
                "and phase).\n\n"
                " - Welch window: frequency resolution of the estimate.\n"
                " - Smooth LF/HF: nth-octave smoothing of the curves and the correction, "
                "set separately for low and high frequencies (interpolated across the band; "
                "finer in the bass, broader in the treble). Equal values = uniform.\n"
                " - Correction level: 0 (none) to 1 (flat).\n"
                " - Max boost: soft-knee ceiling on the correction gain.\n"
                " - FIR length: taps of the exported filter; shows the lowest corrected "
                "frequency and the latency it adds.\n"
                " - Phase: Linear corrects magnitude and phase (incl. subwoofer alignment) "
                "but adds firLength/2 latency; Min phase corrects magnitude only with "
                "near-zero latency (no phase correction, no subwoofer alignment) \xe2\x80\x94 "
                "use it for low-latency monitoring while tracking.\n"
                " - Analysis range: band the correction acts on; outside it the correction "
                "is unity and the exported IR rolls off.\n\n"
                "Subwoofer integration (optional): Load sub measurements (same positions), "
                "then set the Crossover (and Invert if needed) to phase-align the main with "
                "the sub through the crossover. The relative main/sub timing is taken from "
                "the measurements; align the drivers physically with the output delays.\n\n"
                "Export IR saves the measured response; Export correction IR saves the "
                "correction and can assign it directly to an output.";
            break;
    }
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

    // Scale the control widths down proportionally when space is tight. The
    // master toggles are 1.5x the A..F button width; the master level is a
    // horizontal slider (the freed knob space + a little more).
    constexpr int btnW = 44;                    // A..F config buttons
    constexpr int toggleW = 66;                 // 1.5 x btnW
    constexpr int levelW = 132;                 // horizontal level slider
    const int needed = smt::numConfigs * btnW + toggleW + levelW + 8 + toggleW * 3;
    const float scale = juce::jmin (1.0f, (float) top.getWidth() / (float) needed);
    auto sw = [scale] (int px) { return juce::roundToInt (scale * (float) px); };

    for (auto* b : configButtons)
        b->setBounds (top.removeFromLeft (sw (btnW)));
    exclusiveButton->setBounds (top.removeFromLeft (sw (toggleW)));
    levelSlider->setBounds (top.removeFromLeft (sw (levelW)).reduced (2, 9));
    top.removeFromLeft (sw (8));
    muteButton->setBounds (top.removeFromLeft (sw (toggleW)));
    dimButton->setBounds (top.removeFromLeft (sw (toggleW)));
    monoButton->setBounds (top.removeFromLeft (sw (toggleW)));

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
    panelArea = main;                   // full-window panels (info button: top-right)

    // Full-window panels
    configTool.setBounds (main);
    calibration.setBounds (main);
    analysis.setBounds (main);

    // Matrix view: matrix left, analyzer + frame editor right
    auto right = main.removeFromRight (juce::jmax (340, main.getWidth() / 4 + 60));
    main.removeFromRight (8);
    matrix.setBounds (main);
    matrixArea = main;                  // matrix corner (info button: top-left)

    frameEditor.setBounds (right.removeFromBottom (280));
    right.removeFromBottom (8);
    spectrum.setBounds (right);

    layoutInfoButton();
}
