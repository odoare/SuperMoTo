/*
  ------------------------------------------------------------------------------

    PluginProcessor.h
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

#include "Model/ConfigModel.h"
#include "Dsp/MatrixEngine.h"
#include "Dsp/MeasurementEngine.h"
#include "Dsp/SplMeterEngine.h"

//==============================================================================
class SuperMoToAudioProcessor  : public juce::AudioProcessor,
                                 private juce::AudioProcessorValueTreeState::Listener
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

private:
    // Exclusive mode: engaging one of A..F releases the others.
    void parameterChanged (const juce::String& parameterID, float newValue) override;

    juce::AudioBuffer<float> inputCopy;

    //==============================================================================
    JUCE_DECLARE_WEAK_REFERENCEABLE (SuperMoToAudioProcessor)
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SuperMoToAudioProcessor)
};
