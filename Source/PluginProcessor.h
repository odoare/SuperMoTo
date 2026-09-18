/*
  ------------------------------------------------------------------------------

    PluginProcessor.h
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

#include "Model/ConfigModel.h"
#include "Dsp/MatrixEngine.h"
#include "Dsp/MeasurementEngine.h"
#include "Dsp/SplMeterEngine.h"
#include "Model/Workspace.h"

//==============================================================================
class SuperMoToAudioProcessor  : public juce::AudioProcessor,
                                 private juce::AudioProcessorValueTreeState::Listener,
                                 private juce::ValueTree::Listener,
                                 private smt::ConfigModel::Listener
{
public:
    //==============================================================================
    SuperMoToAudioProcessor();
    ~SuperMoToAudioProcessor() override;

    //==============================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;

    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    //==============================================================================
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    //==============================================================================
    const juce::String getName() const override;

    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    //==============================================================================
    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int index, const juce::String& newName) override;

    //==============================================================================
    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // State format version. Stamped on apvts.state itself rather than on the
    // getStateInformation() wrapper root, so it travels through BOTH
    // serialization surfaces: the host session, and the preset XML files
    // PresetManager writes straight from apvts.copyState().
    //
    //   0 (absent) written before the version property existed. Could be
    //              either the original layout ("Configurations" a sibling of
    //              the parameters, under the wrapper root) or the current one,
    //              so setStateInformation still tells them apart structurally.
    //   1          "Configurations" and the per-output EmbeddedAudio FIR slots
    //              live inside apvts.state.
    //
    // Bump this when the meaning of the tree changes, and add the migration in
    // setStateInformation. A state from a *newer* version is still loaded, not
    // refused: the tree is self-describing enough that a partial restore beats
    // an empty one.
    static constexpr int currentStateVersion = 1;
    static const juce::Identifier stateVersionProperty;     // "stateVersion"

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameters();
    juce::AudioProcessorValueTreeState apvts{*this, nullptr, "Parameters", createParameters()};

    // Matrix configuration model (6 configs A..F) — saved with the plugin state.
    smt::ConfigModel configModel;

    // Audio engine: 6 config matrices + per-output FIR correction.
    smt::MatrixEngine engine { configModel };

    // Part 2: measurement of loudspeaker responses.
    smt::MeasurementEngine measurement;

    // Part 2: SPL meter + test-signal generator (calibration window).
    smt::SplMeterEngine splMeter;

    // Selected microphone input for the measurement part (0-based), set by
    // the calibration GUI before starting a run.
    std::atomic<int> measurementMicChannel { 0 };

    // The work in progress of each pane (analyses, their settings, a
    // background batch), which must survive the editor being closed. Declared
    // after the model and the engine it refers to, so it is destroyed first.
    // Message thread only; not saved with the session.
    smt::Workspace workspace { configModel, engine };

    // Factory (BinaryData XML) + user preset banks over the APVTS state.
    fxme::PresetManager& getPresetManager() noexcept    { return *presetManager; }

private:
    // Exclusive mode: engaging one of A..F releases the others.
    void parameterChanged (const juce::String& parameterID, float newValue) override;

    //==========================================================================
    // Presets / self-contained state. PresetManager round-trips only
    // apvts.state, so the ConfigModel tree ("Configurations" child) and the
    // per-output FIR impulses (fxme::EmbeddedAudio, FLAC+Base64) are mirrored
    // into it on every model change; a preset or session then carries the
    // whole plugin. Message thread throughout.
    static juce::String firSlot (int out)   { return "outFir" + juce::String (out + 1); }

    void syncConfigToState();       // model -> "Configurations" child
    void embedChangedFirFiles();    // firPath changes -> embedded audio slots
    void restoreFromApvtsState();   // state -> model + FIR engines
    void stampStateVersion();       // ensure apvts.state carries the version

    // ConfigModel::Listener — mirrors every model edit into apvts.state.
    void modelChanged() override;

    // ValueTree::Listener on apvts.state — replaceState() (preset load, host
    // session restore) redirects the tree; restore the model from it.
    void valueTreeRedirected (juce::ValueTree&) override;

    std::unique_ptr<fxme::PresetManager> presetManager;
    std::array<juce::String, smt::numChannels> lastFirPaths;   // embed diffing
    bool restoringState = false;    // silences modelChanged during a restore

    // One pass of the audio path over at most preparedBlockSize samples.
    // processBlock() slices anything longer, so every scratch buffer downstream
    // (inputCopy, the engine's and the meters') is guaranteed big enough and
    // nothing has to allocate or grow on the audio thread.
    void processChunk (juce::AudioBuffer<float>& buffer, int numSamples);

    juce::AudioBuffer<float> inputCopy;
    int preparedBlockSize = 0;      // samplesPerBlock from prepareToPlay

    //==============================================================================
    JUCE_DECLARE_WEAK_REFERENCEABLE (SuperMoToAudioProcessor)
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SuperMoToAudioProcessor)
};
