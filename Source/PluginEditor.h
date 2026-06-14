/*
  ------------------------------------------------------------------------------

    PluginEditor.h
    Author:  Olivier Doaré
    github.com/odoare

    (c) 2023-2026 Olivier Doaré

    Licenced under the GNU Lesser General Public License (LGPL) Version 3.0
    SPDX-License-Identifier: LGPL-3.0-or-later

  ------------------------------------------------------------------------------
    This file is part of the SuperMoTo plugin.

    SuperMoTo is free software: you can redistribute it and/or modify
    it under the terms of the GNU Lesser General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    SuperMoTo is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU Lesser General Public License for more details.

    You should have received a copy of the GNU Lesser General Public License
    along with SuperMoTo. If not, see <https://www.gnu.org/licenses/>.
  ------------------------------------------------------------------------------
*/

#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "Theme.h"
#include "Components/MatrixComponent.h"
#include "Components/FrameEditorComponent.h"
#include "Components/SpectrumAnalyzerComponent.h"
#include "Components/ConfigToolComponent.h"
#include "Components/CalibrationComponent.h"
#include "Components/AnalysisComponent.h"

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
    enum class View { matrix, configTool, calibration, analysis };
    void setView (View v);
    void setEditConfig (int c);

    // Compact mode: only the top bar and the output strip stay visible and
    // the window shrinks to match.
    void setCollapsed (bool shouldCollapse);

    // The matrix view follows the engaged configuration (A..F buttons); the
    // Edit buttons still allow browsing a config without engaging it.
    void parameterChanged (const juce::String& parameterID, float newValue) override;

    // Keeps the matrix-size combos in sync with the model (state restore).
    void modelChanged() override;

    SuperMoToAudioProcessor& audioProcessor;

    fxme::FxmeLookAndFeel fxmeLookAndFeel;

    // ── Top bar ──────────────────────────────────────────────────────────────
    juce::Image logo;
    juce::OwnedArray<fxme::FxmeButton> configButtons;       // A..F (engage)
    std::unique_ptr<fxme::FxmeButton> exclusiveButton, muteButton, dimButton, monoButton;
    std::unique_ptr<fxme::FxmeKnob> levelKnob;
    juce::TextButton collapseButton;

    // ── Bottom control bar ───────────────────────────────────────────────────
    juce::TextButton matrixViewButton, configToolButton, calibrationButton, analysisButton;

    // ── Matrix view ──────────────────────────────────────────────────────────
    MatrixComponent matrix;
    juce::Label insLabel, outsLabel;
    juce::ComboBox insBox, outsBox;                         // matrix size
    juce::OwnedArray<juce::TextButton> editConfigButtons;   // which config is edited
    juce::Label editLabel;
    SpectrumAnalyzerComponent spectrum;
    FrameEditorComponent frameEditor;

    // ── Other views ──────────────────────────────────────────────────────────
    ConfigToolComponent configTool;
    CalibrationComponent calibration;
    AnalysisComponent analysis;

    View currentView = View::matrix;
    int editConfig = 0;

    bool collapsed = false;
    int expandedWidth = 1280, expandedHeight = 820;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SuperMoToAudioProcessorEditor)
};
