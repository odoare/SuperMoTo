/*
  ------------------------------------------------------------------------------

    PluginProcessor.cpp
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

#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
SuperMoToAudioProcessor::SuperMoToAudioProcessor()
     : AudioProcessor (BusesProperties()
                       .withInput  ("Input",  juce::AudioChannelSet::discreteChannels (smt::numChannels), true)
                       .withOutput ("Output", juce::AudioChannelSet::discreteChannels (smt::numChannels), true))
{
    for (int c = 0; c < smt::numConfigs; ++c)
        apvts.addParameterListener (smt::configName (c), this);
}

SuperMoToAudioProcessor::~SuperMoToAudioProcessor()
{
    for (int c = 0; c < smt::numConfigs; ++c)
        apvts.removeParameterListener (smt::configName (c), this);
}

void SuperMoToAudioProcessor::parameterChanged (const juce::String& parameterID, float newValue)
{
    // Exclusive mode: when a config engages, release the others. The writes
    // happen on the message thread (this callback can come from any thread).
    if (newValue < 0.5f || apvts.getRawParameterValue ("Exclusive")->load() < 0.5f)
        return;

    juce::MessageManager::callAsync (
        [safeThis = juce::WeakReference<SuperMoToAudioProcessor> (this), parameterID]
        {
            if (safeThis == nullptr)
                return;
            for (int c = 0; c < smt::numConfigs; ++c)
            {
                const auto name = smt::configName (c);
                if (name == parameterID)
                    continue;
                if (auto* param = safeThis->apvts.getParameter (name))
                    if (param->getValue() > 0.5f)
                        param->setValueNotifyingHost (0.0f);
            }
        });
}

//==============================================================================
const juce::String SuperMoToAudioProcessor::getName() const
{
    return ProjectInfo::projectName;
}

bool SuperMoToAudioProcessor::acceptsMidi() const            { return false; }
bool SuperMoToAudioProcessor::producesMidi() const           { return false; }
bool SuperMoToAudioProcessor::isMidiEffect() const           { return false; }
double SuperMoToAudioProcessor::getTailLengthSeconds() const { return 0.0; }

int SuperMoToAudioProcessor::getNumPrograms()                { return 1; }
int SuperMoToAudioProcessor::getCurrentProgram()             { return 0; }
void SuperMoToAudioProcessor::setCurrentProgram (int)        {}
const juce::String SuperMoToAudioProcessor::getProgramName (int)        { return {}; }
void SuperMoToAudioProcessor::changeProgramName (int, const juce::String&) {}

//==============================================================================
void SuperMoToAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    inputCopy.setSize (smt::numChannels, samplesPerBlock);
    engine.prepare (sampleRate, samplesPerBlock);
    engine.updateFirFiles();
    measurement.prepare (sampleRate, samplesPerBlock);
    splMeter.prepare (sampleRate, samplesPerBlock);
}

void SuperMoToAudioProcessor::releaseResources()
{
}

bool SuperMoToAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::discreteChannels (smt::numChannels)
        && layouts.getMainInputChannelSet()  == juce::AudioChannelSet::discreteChannels (smt::numChannels);
}

void SuperMoToAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int n = buffer.getNumSamples();
    const int numIn = juce::jmin (getTotalNumInputChannels(), smt::numChannels);

    if (inputCopy.getNumSamples() < n)
        inputCopy.setSize (smt::numChannels, n, false, false, true);

    // Keep a copy of the inputs: the engine overwrites the buffer.
    for (int c = 0; c < smt::numChannels; ++c)
    {
        if (c < numIn)
            inputCopy.copyFrom (c, 0, buffer, c, 0, n);
        else
            inputCopy.clear (c, 0, n);
    }

    // Engaged configurations A..F (also needed by the full-system measurement /
    // SPL tests, which run the engine themselves).
    std::array<bool, smt::numConfigs> configActive {};
    for (int c = 0; c < smt::numConfigs; ++c)
        configActive[(size_t) c] = apvts.getRawParameterValue (smt::configName (c))->load() > 0.5f;

    // Part 2: while a measurement runs, it owns the audio entirely.
    const int micChannel = juce::jlimit (0, smt::numChannels - 1,
                                         measurementMicChannel.load());
    if (measurement.process (inputCopy.getReadPointer (micChannel), buffer, n, engine, configActive))
        return;

    // Part 2: SPL meter. It taps the selected mic for RMS and, when a test
    // generator is engaged, owns the audio (emitting on the chosen channels).
    const int splMicChannel = juce::jlimit (0, smt::numChannels - 1,
                                            splMeter.getMicChannel());
    if (splMeter.process (inputCopy.getReadPointer (splMicChannel), buffer, n, engine, configActive))
        return;

    // Mono fold-down of the first input pair (like MoTo).
    if (apvts.getRawParameterValue ("Mono")->load() > 0.5f)
    {
        auto* l = inputCopy.getWritePointer (0);
        auto* r = inputCopy.getWritePointer (1);
        for (int i = 0; i < n; ++i)
        {
            const float m = 0.5f * (l[i] + r[i]);
            l[i] = r[i] = m;
        }
    }

    const float masterGain =
        juce::Decibels::decibelsToGain (apvts.getRawParameterValue ("Level")->load())
        * (apvts.getRawParameterValue ("Mute")->load() > 0.5f ? 0.0f : 1.0f)
        * (apvts.getRawParameterValue ("Dim")->load() > 0.5f ? 0.5f : 1.0f);

    engine.process (inputCopy.getArrayOfReadPointers(), buffer, n, configActive, masterGain);
}

//==============================================================================
bool SuperMoToAudioProcessor::hasEditor() const
{
    return true;
}

juce::AudioProcessorEditor* SuperMoToAudioProcessor::createEditor()
{
    return new SuperMoToAudioProcessorEditor (*this);
}

//==============================================================================
void SuperMoToAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    juce::ValueTree root ("SuperMoToState");
    root.addChild (apvts.copyState(), -1, nullptr);
    root.addChild (configModel.toValueTree(), -1, nullptr);

    juce::MemoryOutputStream mos (destData, true);
    root.writeToStream (mos);
}

void SuperMoToAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto root = juce::ValueTree::readFromData (data, sizeInBytes);
    if (! root.isValid())
        return;

    auto params = root.getChildWithName (apvts.state.getType());
    if (params.isValid())
        apvts.replaceState (params);

    auto configs = root.getChildWithName ("Configurations");
    if (configs.isValid())
        configModel.restoreFromValueTree (configs);

    juce::MessageManager::callAsync ([safeThis = juce::WeakReference<SuperMoToAudioProcessor> (this)]
    {
        if (safeThis != nullptr)
            safeThis->engine.updateFirFiles();
    });
}

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout SuperMoToAudioProcessor::createParameters()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "Level", 1 }, "Level",
        juce::NormalisableRange<float> (-60.0f, 6.0f, 0.1f, 1.0f), 0.0f));

    for (int c = 0; c < smt::numConfigs; ++c)
        layout.add (std::make_unique<juce::AudioParameterBool> (
            juce::ParameterID { smt::configName (c), 1 }, smt::configName (c), c == 0));

    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "Mute", 1 }, "Mute", false));
    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "Dim", 1 }, "Dim", false));
    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "Mono", 1 }, "Mono", false));
    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "Exclusive", 1 }, "Exclusive", true));

    return layout;
}

//==============================================================================
// This creates new instances of the plugin..
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SuperMoToAudioProcessor();
}
