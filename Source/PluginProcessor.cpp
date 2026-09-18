/*
  ------------------------------------------------------------------------------

    PluginProcessor.cpp
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

#include "PluginProcessor.h"
#include "PluginEditor.h"

const juce::Identifier SuperMoToAudioProcessor::stateVersionProperty { "stateVersion" };

//==============================================================================
SuperMoToAudioProcessor::SuperMoToAudioProcessor()
     : AudioProcessor (BusesProperties()
                       .withInput  ("Input",  juce::AudioChannelSet::discreteChannels (smt::numChannels), true)
                       .withOutput ("Output", juce::AudioChannelSet::discreteChannels (smt::numChannels), true))
{
    for (int c = 0; c < smt::numConfigs; ++c)
        apvts.addParameterListener (smt::configName (c), this);

    // Presets: stamp the version and mirror the model into apvts.state before
    // the PresetManager attaches its dirty-tracking listener, so a fresh
    // instance starts clean.
    stampStateVersion();
    syncConfigToState();
    configModel.addListener (this);
    apvts.state.addListener (this);

    engine.embeddedIrProvider = [this] (int out)
    {
        return fxme::EmbeddedAudio::createReader (apvts.state, firSlot (out));
    };

    presetManager = std::make_unique<fxme::PresetManager> (
        apvts,
        fxme::PresetManager::getDefaultUserPresetDirectory ("FXMechanics", "SuperMoTo"),
        BinaryData::namedResourceList,
        BinaryData::namedResourceListSize,
        BinaryData::getNamedResource);
}

SuperMoToAudioProcessor::~SuperMoToAudioProcessor()
{
    apvts.state.removeListener (this);
    configModel.removeListener (this);
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
    // A validating host (auval, and Logic during its plugin scan) may call this
    // with zero values. Everything below sizes buffers, filter coefficients and
    // smoothing ramps from them, so stay unprepared instead: processBlock
    // returns early while preparedBlockSize is 0, and the real call follows.
    if (sampleRate <= 0.0 || samplesPerBlock <= 0)
    {
        preparedBlockSize = 0;
        return;
    }

    preparedBlockSize = samplesPerBlock;
    inputCopy.setSize (smt::numChannels, preparedBlockSize);
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
    // Fixed discrete I/O at the matrix maximum on both sides: this sidesteps
    // per-host channel-count negotiation entirely (VST3/AU discrete-layout
    // discovery is inconsistent across hosts). Unused channels are simply
    // left disconnected in the host; the matrix size selector in the editor
    // chooses how many of the 32 are actually routed.
    const auto fixed = juce::AudioChannelSet::discreteChannels (smt::numChannels);

    return layouts.getMainInputChannelSet()  == fixed
        && layouts.getMainOutputChannelSet() == fixed;
}

void SuperMoToAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int n = buffer.getNumSamples();
    const int maxN = preparedBlockSize;

    if (maxN <= 0)
        return;                 // processBlock before prepareToPlay

    if (n <= maxN)
    {
        processChunk (buffer, n);
        return;
    }

    // A host that sends a longer block than the one it declared in
    // prepareToPlay would otherwise overrun every scratch buffer downstream:
    // inputCopy here, MatrixEngine::outScratch, FrameProcessor::scratch and the
    // SPL generator's inScratch are all sized for maxN and then indexed up to n
    // unchecked. Growing them on the fly would allocate on the audio thread (and
    // re-preparing would reset every filter and delay line), so slice the block
    // instead: no allocation, no state reset, no overrun.
    const int numCh = juce::jmin (buffer.getNumChannels(), smt::numChannels);
    float* chans[smt::numChannels];

    for (int start = 0; start < n; start += maxN)
    {
        const int len = juce::jmin (maxN, n - start);

        for (int c = 0; c < numCh; ++c)
            chans[c] = buffer.getWritePointer (c) + start;

        juce::AudioBuffer<float> window (chans, numCh, len);
        processChunk (window, len);
    }
}

void SuperMoToAudioProcessor::processChunk (juce::AudioBuffer<float>& buffer, int n)
{
    jassert (n <= inputCopy.getNumSamples());   // guaranteed by processBlock

    const int numIn = juce::jmin (getTotalNumInputChannels(),
                                  buffer.getNumChannels(), smt::numChannels);

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
// apvts.state carries the "Configurations" tree and the embedded FIR audio as
// children (kept in sync by modelChanged), so the parameter tree alone is the
// whole plugin state — and so are the presets PresetManager writes from it.

// Idempotent on purpose: writing only when the value actually differs means the
// common case (a state that is already current) touches nothing, so this cannot
// flag the preset as dirty or depend on the order the apvts.state listeners run
// in. It writes only when migrating an older state forward, which genuinely is
// a change to the state.
void SuperMoToAudioProcessor::stampStateVersion()
{
    if ((int) apvts.state.getProperty (stateVersionProperty, 0) != currentStateVersion)
        apvts.state.setProperty (stateVersionProperty, currentStateVersion, nullptr);
}

void SuperMoToAudioProcessor::syncConfigToState()
{
    auto tree = configModel.toValueTree();
    auto existing = apvts.state.getChildWithName (tree.getType());
    if (existing.isValid())
    {
        if (existing.isEquivalentTo (tree))
            return;
        apvts.state.removeChild (existing, nullptr);
    }
    apvts.state.appendChild (tree, nullptr);
}

void SuperMoToAudioProcessor::embedChangedFirFiles()
{
    for (int o = 0; o < smt::numChannels; ++o)
    {
        const auto path = configModel.getOutput (o).firPath;
        if (path == lastFirPaths[(size_t) o])
            continue;
        lastFirPaths[(size_t) o] = path;

        if (path.isEmpty())
            fxme::EmbeddedAudio::removeEmbedded (apvts.state, firSlot (o));
        else if (juce::File (path).existsAsFile())
            fxme::EmbeddedAudio::embedFile (apvts.state, firSlot (o), juce::File (path));
        // A path whose file is gone keeps the previous embedded impulse: it
        // may be the only remaining copy (state restored on another machine).
    }
}

void SuperMoToAudioProcessor::modelChanged()
{
    if (restoringState)
        return;
    embedChangedFirFiles();
    syncConfigToState();
}

void SuperMoToAudioProcessor::valueTreeRedirected (juce::ValueTree&)
{
    restoreFromApvtsState();
}

void SuperMoToAudioProcessor::restoreFromApvtsState()
{
    {
        const juce::ScopedValueSetter<bool> svs (restoringState, true);

        auto configs = apvts.state.getChildWithName ("Configurations");
        if (configs.isValid())
            configModel.restoreFromValueTree (configs);
    }

    for (int o = 0; o < smt::numChannels; ++o)
        lastFirPaths[(size_t) o] = configModel.getOutput (o).firPath;

    // replaceState() reseats the tree wholesale, so a state or preset written
    // before versioning existed leaves apvts.state without the property. Put it
    // back, or the next preset saved from this (now migrated) state would be
    // unversioned again.
    stampStateVersion();

    // Forced: a preset can carry a different embedded impulse under an
    // unchanged firPath.
    engine.updateFirFiles (true);
}

//==============================================================================
void SuperMoToAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    // The version normally rides along inside apvts.state already (stamped in
    // the constructor and after every restore); setting it on the copy as well
    // is idempotent and makes a saved session self-describing whatever happened
    // to the live tree. It is the same property at the same path, so the file
    // still carries exactly one version number.
    auto params = apvts.copyState();
    params.setProperty (stateVersionProperty, currentStateVersion, nullptr);

    // The wrapper root is kept for compatibility with pre-preset-system
    // states, which stored "Configurations" as a second child.
    juce::ValueTree root ("SuperMoToState");
    root.addChild (params, -1, nullptr);

    juce::MemoryOutputStream mos (destData, true);
    root.writeToStream (mos);
}

void SuperMoToAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto root = juce::ValueTree::readFromData (data, sizeInBytes);
    if (! root.isValid())
        return;

    auto params = root.getChildWithName (apvts.state.getType());

    // Read before replaceState(), while this is still unambiguously the incoming
    // tree. 0 means the state predates the version property.
    const int version = params.isValid()
                            ? (int) params.getProperty (stateVersionProperty, 0)
                            : 0;

    if (params.isValid())
        apvts.replaceState (params);    // valueTreeRedirected restores the
                                        // model + FIRs from the new tree

    // Original layout: "Configurations" next to the parameters instead of inside
    // them. Restoring it re-mirrors (and re-embeds) via modelChanged. Only ever
    // possible in an unversioned state, so from version 1 on there is nothing to
    // probe for and a stale sibling cannot be picked up by mistake.
    if (version == 0)
    {
        auto configs = root.getChildWithName ("Configurations");
        if (configs.isValid())
            configModel.restoreFromValueTree (configs);
    }

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
