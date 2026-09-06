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
#include "AppSettings.h"

//==============================================================================
// Width of the matrix component for a given window width (expanded layout):
// window minus margins, right column and gap. Used to align the top bar and
// to size the compact (strip) window.
static int matrixWidthFor (int totalWidth)
{
    const int mainW = totalWidth - 16;
    const int rightW = juce::jmax (340, mainW / 4 + 60);
    return mainW - rightW - 8;
}

//==============================================================================
// Mini layout: the logo, then two short control rows. Row 1 is the collapse
// button, the A..F config buttons and Exclusive; row 2 is the master level, the
// mute/dim/mono toggles and the output meters. Every size is fixed — the mini
// window is not resizable and its width follows only the output count.
namespace mini
{
    constexpr int margin    = 8;
    constexpr int rowH      = 30;
    constexpr int rowGap    = 4;
    constexpr int rowsH     = 2 * rowH + rowGap;    // both control rows
    constexpr int gap       = 6;
    // Wide enough that the logo fills rowsH exactly: the asset is 269x227, so
    // the width is that aspect times the height, rounded up so the fit stays
    // limited by the height rather than the width.
    constexpr int logoW     = 76;
    constexpr int collapseW = 26;
    constexpr int btnW      = 34;       // A..F
    constexpr int toggleW   = 34;       // X / M / D / null set — one glyph each
    constexpr int levelW    = 104;
    constexpr int metersMinW = 56;      // keep the meter row readable at 4 outs
}

//==============================================================================
SuperMoToAudioProcessorEditor::SuperMoToAudioProcessorEditor (SuperMoToAudioProcessor& p)
    : AudioProcessorEditor (&p), audioProcessor (p),
      matrix (p.configModel, p.engine),
      outputMeters (p.configModel, p.engine),
      spectrum (p.engine),
      frameEditor (p.configModel),
      outputEditor (p.configModel, p.engine),
      configTool (p.configModel),
      calibration (p),
      analysis (p),
      groupAnalysis (p),
      presetPane (p.getPresetManager()),
      presetBar (p.getPresetManager())
{
    setLookAndFeel (&fxmeLookAndFeel);

    // Drop-down menus and tooltips are their own windows: they never see the
    // combo box or the control that opened them, so they cannot pick up its
    // colour and default to a neutral grey. This tints them (menu panel
    // hairline, highlighted row, the tick marking the current selection, and the
    // tooltip hairline) for every combo and every tooltip under this editor,
    // since they all inherit this one look-and-feel. Master is the plugin's
    // identity colour and is already what the preset components use, below.
    fxmeLookAndFeel.setAccentColour (SuperMoToTheme::master);

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

    // The mini layout shortens these to single glyphs, so the tooltip is the
    // only thing left saying what they do. On the inner ToggleButton: it fills
    // the FxmeButton wrapper, so it is what the mouse actually lands on.
    exclusiveButton->button.setTooltip ("Exclusive: engaging a configuration releases the others");
    muteButton->button.setTooltip ("Mute the master output");
    dimButton->button.setTooltip ("Dim the master output");
    monoButton->button.setTooltip ("Sum to mono (checks phase cancellation)");

    levelSlider = std::make_unique<fxme::FxmeSlider> (audioProcessor.apvts, "Level", "Level",
                                                      SuperMoToTheme::master);
    levelSlider->setSliderStyle (juce::Slider::LinearHorizontal);
    levelSlider->setTooltip ("Master level (dB)");
    SuperMoToTheme::accentSlider (*levelSlider, SuperMoToTheme::master);
    levelSlider->setLookAndFeel (&fxmeLookAndFeel);
    addAndMakeVisible (*levelSlider);

    // The latching buttons below are all fxme::AccentToggle, but their state
    // follows the application (which view is up, which config is edited, whether
    // which compact layout is up) rather than the click, so each one turns off
    // the click-latching AccentToggle enables by default and is driven by
    // setToggleState from setView / setEditConfig / setCompactMode.
    collapseButton.setButtonText (juce::String::fromUTF8 ("\xe2\x96\xb2"));   // up triangle
    collapseButton.setClickingTogglesState (false);
    collapseButton.setAccent (SuperMoToTheme::viewSelected, SuperMoToTheme::text,
                              SuperMoToTheme::panel);
    collapseButton.setTooltip ("Compact view: the output strip only");
    // One button, three layouts: full -> output strip -> mini -> full.
    collapseButton.onClick = [this]
    {
        setCompactMode (compactMode == Compact::off   ? Compact::strip
                      : compactMode == Compact::strip ? Compact::mini
                                                      : Compact::off);
    };
    addAndMakeVisible (collapseButton);

    // Hidden until the mini layout asks for it (setView).
    addChildComponent (outputMeters);

    auto initViewButton = [this] (fxme::AccentToggle& b, const juce::String& text, View v)
    {
        b.setButtonText (text);
        b.setClickingTogglesState (false);
        b.setAccent (SuperMoToTheme::viewSelected, SuperMoToTheme::text,
                     SuperMoToTheme::panel);
        b.onClick = [this, v] { setView (v); };
        addAndMakeVisible (b);
    };
    initViewButton (matrixViewButton, "Matrix", View::matrix);
    initViewButton (configToolButton, "Config tool", View::configTool);
    initViewButton (calibrationButton, "Calibration", View::calibration);
    initViewButton (analysisButton, "Analysis", View::analysis);
    initViewButton (groupAnalysisButton, "Group", View::groupAnalysis);
    initViewButton (presetsViewButton, "Presets", View::presets);

    // Compact preset selector: top-right corner, expanded mode only.
    presetBar.setAccentColour (SuperMoToTheme::master);
    addAndMakeVisible (presetBar);

    // ── Matrix view ──────────────────────────────────────────────────────────
    // Matrix size selectors (rows = inputs, columns = outputs).
    auto initSizeBox = [this] (juce::Label& l, juce::ComboBox& b, const juce::String& text)
    {
        l.setText (text, juce::dontSendNotification);
        l.setFont (juce::Font (juce::FontOptions (12.0f)));
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
        editingOutput = false;
        frameEditor.setFrame (in, out, editConfig);
        const bool show = currentView == View::matrix && ! isCompact();
        frameEditor.setVisible (show);
        outputEditor.setVisible (false);
    };
    matrix.onOutputSelected = [this] (int out)
    {
        editingOutput = true;
        outputEditor.setOutput (out);
        const bool show = currentView == View::matrix && ! isCompact();
        outputEditor.setVisible (show);
        frameEditor.setVisible (false);
    };

    editLabel.setText ("Edit:", juce::dontSendNotification);
    editLabel.setColour (juce::Label::textColourId, SuperMoToTheme::dimText);
    addAndMakeVisible (editLabel);

    // The edit button of the currently displayed configuration lights up.
    for (int c = 0; c < smt::numConfigs; ++c)
    {
        auto* b = editConfigButtons.add (new fxme::AccentToggle());
        b->setButtonText (smt::configName (c));
        b->setClickingTogglesState (false);
        // Same cyan as the A..F engage buttons in the top bar, so "edited" and
        // "engaged" read as the same family of control.
        b->setAccent (SuperMoToTheme::configEngage, SuperMoToTheme::text,
                      SuperMoToTheme::panel);
        b->onClick = [this, c] { setEditConfig (c); };
        addAndMakeVisible (b);
    }

    addAndMakeVisible (spectrum);
    spectrum.sampleRateProvider = [this] { return audioProcessor.getSampleRate(); };

    // Microphone trace: shown whenever the calibration SPL meter is on (its tap
    // is enabled with the meter), so the EQ can be tuned against the measured
    // response right here in the matrix view.
    {
        fxme::SpectrumDisplay::TraceConfig mic;
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
    addChildComponent (outputEditor);   // shown when an output is selected

    // ── Other views (hidden until selected) ─────────────────────────────────
    addChildComponent (configTool);
    addChildComponent (calibration);
    addChildComponent (analysis);
    addChildComponent (groupAnalysis);
    presetPane.setAccentColour (SuperMoToTheme::master);
    addChildComponent (presetPane);

    // Per-page help, repositioned per view (kept on top of everything).
    infoButton.setColours (SuperMoToTheme::infoButtonColours());
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

    // Restore the interface mode left last time (which panel, compact or not).
    setView (static_cast<View> (juce::jlimit (0, 5, smt::getUiView())));

    setResizable (true, true);
    setResizeLimits (1100, 720, 2400, 1600);
    setSize (1280, 820);

    setCompactMode (static_cast<Compact> (juce::jlimit (0, 2, smt::getUiCompactMode())));
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

    // The mini window is exactly as wide as its meter row needs, so a change of
    // output count (state restore, preset, matrix-size combo) has to resize it.
    if (compactMode == Compact::mini)
    {
        const auto sz = miniWindowSize();
        if (sz.x != getWidth() || sz.y != getHeight())
        {
            setResizeLimits (sz.x, sz.y, sz.x, sz.y);
            setSize (sz.x, sz.y);
        }
    }
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
    smt::setUiView (static_cast<int> (v));
    const bool m = v == View::matrix && ! isCompact();

    // The strip layout keeps the matrix visible but shrunk to its output strip;
    // the mini layout drops it entirely and shows the meter row instead.
    // Everything else goes away in both.
    matrix.setVisible (m || compactMode == Compact::strip);
    outputMeters.setVisible (compactMode == Compact::mini);
    spectrum.setVisible (m);
    frameEditor.setVisible (m && ! editingOutput);
    outputEditor.setVisible (m && editingOutput);
    editLabel.setVisible (m);
    insLabel.setVisible (m);
    insBox.setVisible (m);
    outsLabel.setVisible (m);
    outsBox.setVisible (m);
    for (auto* b : editConfigButtons)
        b->setVisible (m);

    for (auto* b : { &matrixViewButton, &configToolButton, &calibrationButton, &analysisButton,
                     &groupAnalysisButton, &presetsViewButton })
        b->setVisible (! isCompact());

    configTool.setVisible (v == View::configTool && ! isCompact());
    calibration.setVisible (v == View::calibration && ! isCompact());
    analysis.setVisible (v == View::analysis && ! isCompact());
    groupAnalysis.setVisible (v == View::groupAnalysis && ! isCompact());
    presetPane.setVisible (v == View::presets && ! isCompact());
    presetBar.setVisible (! isCompact());

    matrixViewButton.setToggleState (v == View::matrix, juce::dontSendNotification);
    configToolButton.setToggleState (v == View::configTool, juce::dontSendNotification);
    calibrationButton.setToggleState (v == View::calibration, juce::dontSendNotification);
    analysisButton.setToggleState (v == View::analysis, juce::dontSendNotification);
    groupAnalysisButton.setToggleState (v == View::groupAnalysis, juce::dontSendNotification);
    presetsViewButton.setToggleState (v == View::presets, juce::dontSendNotification);

    juce::String t, body;
    infoTextFor (v, t, body);
    infoButton.setInfo (t, body);
    infoButton.setVisible (! isCompact());
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
                "The matrix routes inputs (rows) to outputs (columns). Up to six "
                "configurations A-F can be stored; engage them from the top bar. With "
                "Exclusive only one is active at a time, otherwise the engaged matrices sum.\n\n"
                "A frame (crosspoint) is a routing cell (gain + phase). The speaker "
                "processing — trim, EQ, delay and FIR — lives on the output.\n\n"
                "Frame (crosspoint):\n"
                " - Click: select (opens the frame editor: level, phase, EQ)\n"
                " - Double-click: activate / deactivate\n"
                " - Vertical drag: gain\n"
                " - Alt+click: show / hide its analyzer trace\n"
                " - Right-click: context menu\n\n"
                "Output strip (top row):\n"
                " - Click: select (opens the output editor: trim, EQ, delay, FIR)\n"
                " - Double-click: toggle the output FIR\n"
                " - Vertical drag: output trim\n"
                " - Alt+click: analyzer trace\n"
                " - Right-click: load / clear the FIR impulse\n"
                " - Gold dot: this output's alignment is automatic — either extra delay was "
                "added to match the longest output FIR, or its own Delay control is self-"
                "absorbing its own FIR's latency (hover for the amount)\n\n"
                "Top bar: A-F engage, Exclusive, Level, Mute, Dim, Mono, and the up-arrow "
                "collapses to the output strip only.\n"
                "Bottom bar: matrix size, Edit A-F (view/edit a config without engaging it), "
                "and the page switch.\n\n"
                "Keyboard (click a cell first to give the matrix focus):\n"
                " - Arrow keys: move the selection across the grid and the output strip "
                "(top row); left/right and up/down both wrap around\n"
                " - On a frame: A active, P phase, N analyzer, 1-2 toggle EQ band, "
                "+/- gain \xc2\xb1 0.1 dB\n"
                " - On an output: F FIR, 1-2 toggle EQ band, N analyzer, +/- trim \xc2\xb1 0.1 dB\n\n"
                "Analyzer: click the avg/peak badge (bottom-right) to switch aggregation.";
            break;

        case View::configTool:
            title = "Speaker configuration tool";
            body  =
                "Fills one of the A-F configurations for a standard layout (2.0, 2.1, 4.0, "
                "4.1, 5.1, 7.1) or periphonic Ambisonics (orders 1-3) in a few clicks.\n\n"
                "Channel layouts\n"
                "1. Pick the Layout and the target (Write to config A-F).\n"
                "2. For each speaker choose its input(s) and output, and a gain.\n"
                "3. Bass management (optional): highpass the mains and send their lowpassed "
                "sum to the subwoofer output at the chosen crossover.\n\n"
                "Ambisonics (AmbiX: ACN order, SN3D)\n"
                "- B-format is taken as inputs 1..(order+1)^2 (4 / 9 / 16 channels).\n"
                "- Speakers: choose the count (default 8 / 12 / 16; other counts get a "
                "near-uniform layout you then edit). Set each loudspeaker's "
                "azimuth/elevation/radius and output; Apply builds the max-rE sampling "
                "decoder. Bass management lowpasses W to the sub and highpasses the speakers.\n"
                "- Radius compensation delays + attenuates closer speakers (referenced to "
                "the farthest) so an irregular rig still sums at the centre.\n"
                "- 'Write radius gain' / 'Write radius delay' each optional: off keeps the "
                "outputs' existing level/delay, so you can apply the decode AFTER aligning or "
                "leveling the speakers by hand (or via the analysis pane). The matrix grows "
                "to fit the channels.\n"
                "- 'Load IEM decoder...' imports an IEM AllRADecoder .json as the decode "
                "matrix instead (better for irregular rigs); SN3D/maxRE conversion, routing "
                "and imaginary speakers are handled automatically. The same radius toggles "
                "apply.\n\n"
                "Apply writes the frames into the configuration (overwriting it). Refine "
                "per-frame in the Matrix view, and add per-output FIR correction there.";
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
                " - Mains delay: the bulk time-alignment you intend to apply to the output; "
                "the correction is then designed for the residual only.\n"
                " - Apply bulk delay: on Export correction IR + assign, also write that Mains "
                "delay onto the output (so the FIR and its delay land together).\n\n"
                "Export IR saves the measured response; Export correction IR saves the "
                "correction and can assign it directly to an output.";
            break;

        case View::groupAnalysis:
            title = "Group analysis \xe2\x80\x94 multi-speaker alignment";
            body  =
                "Time-align and correct several speakers (plus a shared subwoofer) at once, "
                "instead of one at a time in the Analysis page.\n\n"
                "1. Speakers: how many to align (each gets its own measurement set, same mic "
                "positions as the others).\n"
                "2. Load... each speaker's multi-position measurement set, and the shared sub "
                "set (same positions).\n"
                "3. The correction-design controls (Welch window, smoothing, correction level, "
                "max boost, FIR length, Phase, Range, Crossover, Invert sub) are shared: one "
                "tone for the whole group. Preview switches which speaker's curves the plot "
                "below shows.\n"
                "4. Compute alignment: measures each speaker's (and the sub's) propagation "
                "delay and proposes the delay that time-aligns everyone on the most-distant "
                "driver, plus a suggested level-matching trim per speaker (corrected mid-band "
                "level, 500 Hz - 2 kHz per SMPTE ST 2095-1, relative to the quietest speaker "
                "\xe2\x80\x94 informational, apply it via the output Trim). Both shown next to "
                "each row.\n"
                "5. Assign each row to the output channel that speaker is on.\n"
                "6. Apply & export...: designs and exports each assigned speaker's correction "
                "IR, writes its delay and FIR onto that output, and saves a markdown report "
                "alongside the impulse responses.\n\n"
                "The subwoofer only ever gets the time-alignment delay \xe2\x80\x94 no correction "
                "FIR is designed for it. Above a sub's real passband a measurement is just "
                "noise (no coherent sent/recorded content), so fitting an inverse filter to it "
                "would be fitting noise.";
            break;

        case View::presets:
            title = "Presets";
            body  =
                "Save and recall complete SuperMoTo setups: the six matrix "
                "configurations, the matrix size, the output processing (trim, EQ, "
                "delay, FIR correction) and the top-bar parameters.\n\n"
                "Factory presets ship with the plugin; user presets are XML files in "
                "the folder shown at the bottom, so they can be backed up or shared. "
                "Click a preset to load it. Save overwrites the current user preset "
                "(or asks for a name), Save As always creates a new one; Rename and "
                "Delete act on the selected user preset.\n\n"
                "A * after the name means the state changed since the preset was "
                "loaded or saved.\n\n"
                "FIR correction impulses are embedded in the preset itself "
                "(FLAC-compressed), so a preset keeps working even if the original "
                "wav files are moved or the preset travels to another machine.\n\n"
                "The compact selector at the top right steps through the same presets "
                "from any page.";
            break;
    }
}

juce::Point<int> SuperMoToAudioProcessorEditor::miniWindowSize() const
{
    using namespace mini;
    const int row1 = collapseW + gap + smt::numConfigs * btnW + gap + toggleW;
    const int row2 = levelW + gap + 3 * toggleW + gap
                   + juce::jmax (metersMinW,
                                 OutputMetersStrip::widthFor (audioProcessor.configModel.getNumOuts()));

    return { margin + logoW + gap + juce::jmax (row1, row2) + margin,
             margin + rowsH + margin };
}

void SuperMoToAudioProcessorEditor::setCompactMode (Compact m)
{
    if (compactMode == m)
        return;

    // Capture the size to restore only on the way out of the full layout, so
    // hopping between the two compact modes does not record a compact size.
    if (compactMode == Compact::off)
    {
        expandedWidth = getWidth();
        expandedHeight = getHeight();
    }

    compactMode = m;
    smt::setUiCompactMode (static_cast<int> (m));

    // The arrow points at what the next click does: shrink further, or — from
    // the smallest layout — go back to the full editor.
    collapseButton.setButtonText (juce::String::fromUTF8 (m == Compact::mini ? "\xe2\x96\xbc"      // down
                                                                             : "\xe2\x96\xb2"));   // up
    collapseButton.setToggleState (isCompact(), juce::dontSendNotification);
    collapseButton.setTooltip (m == Compact::off   ? "Compact view: the output strip only"
                             : m == Compact::strip ? "Mini view: master controls and output meters"
                                                   : "Back to the full editor");

    // The mini row has no room for words. Mono becomes the empty-set sign, the
    // usual shorthand for the cancellation the button is there to reveal; the
    // rest are initials. Their tooltips (set in the constructor) carry the
    // meaning either way.
    const bool tiny = m == Compact::mini;
    exclusiveButton->button.setButtonText (tiny ? "X" : "Exclusive");
    muteButton->button.setButtonText      (tiny ? "M" : "Mute");
    dimButton->button.setButtonText       (tiny ? "D" : "Dim");
    monoButton->button.setButtonText      (tiny ? juce::String::fromUTF8 ("\xe2\x88\x85")  // empty set
                                                : juce::String ("Mono"));

    setView (currentView);      // visibility follows the mode

    if (m == Compact::off)
    {
        setResizeLimits (1100, 720, 2400, 1600);
        setSize (expandedWidth, expandedHeight);
    }
    else
    {
        // Both compact layouts are fixed-size: there is nothing in them that
        // benefits from being stretched.
        const auto sz = m == Compact::strip
            ? juce::Point<int> (matrixWidthFor (expandedWidth) + 16,
                                60 + MatrixComponent::outputStripH + 16)
            : miniWindowSize();
        setResizeLimits (sz.x, sz.y, sz.x, sz.y);
        setSize (sz.x, sz.y);
    }

    // setSize is a no-op when the size happens to be unchanged (strip and mini
    // can coincide), but the layout still has to be rebuilt for the new mode.
    resized();
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

    // Mini layout: the logo alone, spanning both control rows. No room for the
    // title, and the logo alone still identifies the window.
    if (compactMode == Compact::mini)
    {
        if (logo.isValid())
            g.drawImage (logo,
                         juce::Rectangle<float> ((float) mini::margin, (float) mini::margin,
                                                 (float) mini::logoW, (float) mini::rowsH),
                         juce::RectanglePlacement::centred);
        return;
    }

    // Title + logo
    auto top = getLocalBounds().removeFromTop (60);
    if (logo.isValid())
        g.drawImage (logo, juce::Rectangle<float> (8.0f, 6.0f, 48.0f, 48.0f),
                     juce::RectanglePlacement::centred);
    g.setColour (SuperMoToTheme::text);
    g.setFont (juce::Font (juce::FontOptions (24.0f, juce::Font::bold)));
    g.drawText ("SuperMoTo", 62, 0, 170, top.getHeight(), juce::Justification::centredLeft);
}

void SuperMoToAudioProcessorEditor::resized()
{
    auto area = getLocalBounds();

    // ── Mini: two short rows beside the logo, nothing else ───────────────────
    if (compactMode == Compact::mini)
    {
        using namespace mini;
        auto r = area.reduced (margin);
        r.removeFromLeft (logoW + gap);         // the logo, painted in paint()

        auto row1 = r.removeFromTop (rowH);
        r.removeFromTop (rowGap);
        auto row2 = r.removeFromTop (rowH);

        collapseButton.setBounds (row1.removeFromLeft (collapseW).reduced (0, 3));
        row1.removeFromLeft (gap);
        for (auto* b : configButtons)
            b->setBounds (row1.removeFromLeft (btnW));
        row1.removeFromLeft (gap);
        exclusiveButton->setBounds (row1.removeFromLeft (toggleW));

        levelSlider->setBounds (row2.removeFromLeft (levelW).reduced (2, 4));
        row2.removeFromLeft (gap);
        muteButton->setBounds (row2.removeFromLeft (toggleW));
        dimButton->setBounds (row2.removeFromLeft (toggleW));
        monoButton->setBounds (row2.removeFromLeft (toggleW));
        row2.removeFromLeft (gap);
        outputMeters.setBounds (row2);
        return;
    }

    // ── Top bar ──────────────────────────────────────────────────────────────
    // Left-packed and kept within the matrix width, so the strip window
    // (sized to the matrix) shows every control.
    auto top = area.removeFromTop (60).reduced (6);
    top.removeFromLeft (232);                       // logo + title
    collapseButton.setBounds (top.removeFromLeft (28).reduced (0, 14));
    top.removeFromLeft (12);

    const int barRight = isCompact() ? getWidth() - 8
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

    // Compact preset selector: the top-right corner, right of the Mono button
    // in the space left free by the matrix-width-clipped control bar
    // (full layout only; setView hides it in both compact modes).
    if (! isCompact())
    {
        auto barArea = getLocalBounds().removeFromTop (60).reduced (6);
        barArea.removeFromLeft (barRight + 8);
        presetBar.setBounds (barArea.removeFromRight (juce::jmin (280, barArea.getWidth()))
                                    .reduced (0, 13));
    }

    // ── Strip: only the output strip below the top bar ───────────────────────
    if (compactMode == Compact::strip)
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

    const int vw = juce::jmin (110, bottom.getWidth() / 6);
    presetsViewButton.setBounds (bottom.removeFromRight (vw).reduced (2, 1));
    groupAnalysisButton.setBounds (bottom.removeFromRight (vw).reduced (2, 1));
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
    groupAnalysis.setBounds (main);
    presetPane.setBounds (main);

    // Matrix view: matrix left, analyzer + frame editor right
    auto right = main.removeFromRight (juce::jmax (340, main.getWidth() / 4 + 60));
    main.removeFromRight (8);
    matrix.setBounds (main);
    matrixArea = main;                  // matrix corner (info button: top-left)

    const auto editorArea = right.removeFromBottom (280);
    frameEditor.setBounds (editorArea);
    outputEditor.setBounds (editorArea);
    right.removeFromBottom (8);
    spectrum.setBounds (right);

    layoutInfoButton();
}
