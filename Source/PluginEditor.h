/*
  ------------------------------------------------------------------------------

    PluginEditor.h
    Author:  Olivier Doaré
    github.com/odoare

    (c) 2023-2026 Olivier Doaré

    Licenced under the GNU AGPL Version 3.0, or commercial terms (LICENSE.md)
    SPDX-License-Identifier: AGPL-3.0-or-later OR LicenseRef-FXME-Commercial

  ------------------------------------------------------------------------------
    This file is part of the SuperMoTo plugin.

    SuperMoTo is free software: you can redistribute it and/or modify
    it under the terms of the GNU Affero General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    SuperMoTo is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU Affero General Public License for more details.

    You should have received a copy of the GNU Affero General Public License
    along with SuperMoTo. If not, see <https://www.gnu.org/licenses/>.

    Alternatively, commercial terms are available from the author for
    holders of a commercial JUCE licence: see LICENSE.md.
  ------------------------------------------------------------------------------
*/

#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "Theme.h"
#include "Components/MatrixComponent.h"
#include "Components/OutputMetersStrip.h"
// fxme::InfoButton comes via the FxmeTools module umbrella (JuceHeader.h)
#include "Components/FrameEditorComponent.h"
#include "Components/OutputEditorComponent.h"
#include "Components/SpectrumAnalyzerComponent.h"
#include "Components/TargetCurveComponent.h"
#include "Components/ConfigToolComponent.h"
#include "Components/CalibrationComponent.h"
#include "Components/AnalysisComponent.h"
#include "Components/GroupAnalysisComponent.h"

//==============================================================================
class SuperMoToAudioProcessorEditor  : public juce::AudioProcessorEditor,
                                       private juce::AudioProcessorValueTreeState::Listener,
                                       private smt::ConfigModel::Listener
{
public:
    explicit SuperMoToAudioProcessorEditor (SuperMoToAudioProcessor&);
    ~SuperMoToAudioProcessorEditor() override;

    //==============================================================================
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    // Appended rather than inserted in switcher order: the value is what
    // EditorSettings stores, and renumbering would reopen the wrong view.
    enum class View { matrix, configTool, calibration, analysis, presets, groupAnalysis,
                      targetCurve };
    void setView (View v);
    void setEditConfig (int c);
    void layoutInfoButton();
    static void infoTextFor (View v, juce::String& title, juce::String& body);

    // Compact modes, cycled by the collapse button in the top bar.
    //   off   — the full editor.
    //   strip — top bar plus the matrix output strip, window shrunk to the
    //           matrix width. Everything else is hidden.
    //   mini  — the smallest useful footprint: the config buttons up to
    //           Exclusive on one row, then the master level, the mute/dim/mono
    //           toggles and a row of output meters on a second.
    enum class Compact { off, strip, mini };

    void setTooltipsEnabled (bool on);
    void setCompactMode (Compact m);
    bool isCompact() const noexcept     { return compactMode != Compact::off; }

    /** Fixed window size of the mini layout; depends on the output count,
        which sets how wide the meter strip has to be. */
    juce::Point<int> miniWindowSize() const;

    // The matrix view follows the engaged configuration (A..F buttons); the
    // Edit buttons still allow browsing a config without engaging it.
    void parameterChanged (const juce::String& parameterID, float newValue) override;

    // Keeps the matrix-size combos in sync with the model (state restore).
    void modelChanged() override;

    /** The engaged configurations, bit c for configuration c. */
    int engagedConfigMask() const;

    SuperMoToAudioProcessor& audioProcessor;

    // This instance's editor and matrix-view state, kept in the processor's
    // workspace so the next editor reopens as this one was left.
    smt::EditorSettings& state;

    fxme::FxmeLookAndFeel fxmeLookAndFeel;

    // Hover help, with an off switch (the "?" button in the bottom bar).
    // Overriding getTipFor rather than destroying the window: one object for the
    // editor's lifetime, and the suppression covers every component under it,
    // including the ones that build their tip dynamically (the matrix cells).
    struct ToggleableTooltipWindow : public juce::TooltipWindow
    {
        using juce::TooltipWindow::TooltipWindow;
        juce::String getTipFor (juce::Component& c) override
        {
            return enabled ? juce::TooltipWindow::getTipFor (c) : juce::String();
        }
        bool enabled = true;
    };

    ToggleableTooltipWindow tooltipWindow { this, 600 };

    // ── Top bar ──────────────────────────────────────────────────────────────
    juce::Image logo;
    juce::OwnedArray<fxme::FxmeButton> configButtons;       // A..F (engage)
    std::unique_ptr<fxme::FxmeButton> exclusiveButton, muteButton, dimButton, monoButton;
    std::unique_ptr<fxme::FxmeSlider> levelSlider;
    fxme::AccentToggle collapseButton;

    // ── Bottom control bar ───────────────────────────────────────────────────
    // fxme::AccentToggle: the house latching button. Their toggle state is driven
    // from the application state (setView / setEditConfig / setCompactMode), not by
    // the click, so each one has setClickingTogglesState(false) — see the ctor.
    fxme::AccentToggle matrixViewButton, targetCurveButton, configToolButton, calibrationButton,
                       analysisButton, presetsViewButton, groupAnalysisButton;
    fxme::AccentToggle tooltipsButton;      // "?" — turns the hover help on and off

    // ── Matrix view ──────────────────────────────────────────────────────────
    MatrixComponent matrix;
    OutputMetersStrip outputMeters;     // mini layout only, in place of the strip
    juce::Label insLabel, outsLabel;
    juce::ComboBox insBox, outsBox;                         // matrix size
    juce::OwnedArray<fxme::AccentToggle> editConfigButtons; // which config is edited
    juce::Label editLabel;
    SpectrumAnalyzerComponent spectrum;
    FrameEditorComponent frameEditor;
    OutputEditorComponent outputEditor;
    bool editingOutput = false;     // which editor occupies the detail panel
    fxme::InfoButton infoButton;

    // Bounds captured in resized() so the info button can be repositioned when
    // the view changes (matrix corner vs panel top-right).
    juce::Rectangle<int> matrixArea, panelArea;

    // ── Other views ──────────────────────────────────────────────────────────
    TargetCurveComponent targetCurve;
    ConfigToolComponent configTool;
    CalibrationComponent calibration;
    AnalysisComponent analysis;
    GroupAnalysisComponent groupAnalysis;

    // ── Presets ──────────────────────────────────────────────────────────────
    fxme::PresetComponent presetPane;    // full browser (Presets page)
    fxme::PresetBarComponent presetBar;  // compact selector, top-right (expanded only)

    // Reliable typing in every TextEditor under the editor (measurement folder
    // & comments, editable combos, right-click value entry) in hosted windows.
    fxme::TextEntryFocusFixer textEntryFixer { *this };

    View currentView = View::matrix;
    int editConfig = 0;

    Compact compactMode = Compact::off;
    int expandedWidth = 1280, expandedHeight = 820;     // restored from `state` by the ctor

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SuperMoToAudioProcessorEditor)
};
