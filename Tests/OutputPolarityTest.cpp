/*
  ------------------------------------------------------------------------------
    OutputPolarityTest.cpp

    Offline check of the per-output polarity switch (OutputSettings::phaseInvert):
      - an output whose only non-default setting is its polarity is saved,
      - it survives a ValueTree round trip, directly and through XML text as a
        session or preset file stores it,
      - a state without the property (saved before it existed) loads with the
        polarity off and every other field intact,
      - MatrixEngine::process applies it as the sign of the output trim, and a
        flip ramps through zero on the trim smoothing rather than stepping,
      - MatrixEngine::processOutputChainOnly (the "FIR" measurement mode and the
        SPL meter) applies it too.

    Run: build target SuperMoToOutputTests and execute it; exits 0 on success.

    Author: Olivier Doaré, github.com/odoare
    (c) 2023-2026 Olivier Doaré
    Licenced under the GNU AGPL Version 3.0, or commercial terms (LICENSE.md)
    SPDX-License-Identifier: AGPL-3.0-or-later OR LicenseRef-FXME-Commercial
  ------------------------------------------------------------------------------
*/

#include <JuceHeader.h>
#include "../Source/Model/ConfigModel.h"
#include "../Source/Dsp/MatrixEngine.h"
#include <iostream>
#include <memory>

static bool g_ok = true;

static bool check (bool cond, const char* what)
{
    std::cout << (cond ? "  OK  " : "  FAIL ") << what << "\n";
    g_ok = g_ok && cond;
    return cond;
}

static bool approx (double a, double b, double tol, const char* what)
{
    const bool good = std::abs (a - b) <= tol;
    std::cout << (good ? "  OK  " : "  FAIL ") << what << ": " << a
              << " (expected " << b << " +/- " << tol << ")\n";
    g_ok = g_ok && good;
    return good;
}

//==============================================================================
static void testSerialisation()
{
    std::cout << "Serialisation\n";

    smt::OutputSettings onlyInverted;
    onlyInverted.phaseInvert = true;
    check (! onlyInverted.isDefault(), "an output with only its polarity set is not default");

    // Output 4 inverted and nothing else; output 6 trimmed but not inverted.
    // The models are large (every frame of every configuration), so heap.
    auto a = std::make_unique<smt::ConfigModel>();
    a->setOutput (3, onlyInverted);
    auto trimmed = a->getOutput (5);
    trimmed.gainDb = -3.0f;
    a->setOutput (5, trimmed);

    const auto tree = a->toValueTree();

    auto b = std::make_unique<smt::ConfigModel>();
    b->restoreFromValueTree (tree);
    check (b->getOutput (3).phaseInvert,   "round trip: inverted output stays inverted");
    check (! b->getOutput (5).phaseInvert, "round trip: trimmed output stays normal");
    check (! b->getOutput (0).phaseInvert, "round trip: untouched output stays normal");

    // As a session or preset file holds it: XML text, properties as strings.
    const auto xml = juce::parseXML (tree.toXmlString());
    check (xml != nullptr, "state parses back from XML text");
    if (xml != nullptr)
    {
        auto c = std::make_unique<smt::ConfigModel>();
        c->restoreFromValueTree (juce::ValueTree::fromXml (*xml));
        check (c->getOutput (3).phaseInvert,   "XML round trip: inverted output stays inverted");
        check (! c->getOutput (5).phaseInvert, "XML round trip: trimmed output stays normal");
    }

    // A state saved before outputs had a polarity: strip the property.
    auto legacy = tree.createCopy();
    for (auto child : legacy)
        if (child.hasType ("Output"))
            child.removeProperty ("phaseInvert", nullptr);

    auto d = std::make_unique<smt::ConfigModel>();
    d->restoreFromValueTree (legacy);
    check (! d->getOutput (3).phaseInvert, "legacy state: polarity loads off");
    check (! d->getOutput (5).phaseInvert, "legacy state: trimmed output loads normal");
    approx (d->getOutput (5).gainDb, -3.0, 1.0e-6, "legacy state: trim still loads");
}

//==============================================================================
static void testEngine()
{
    std::cout << "Engine\n";

    constexpr double sr = 48000.0;
    constexpr int blockSize = 512;
    constexpr float input = 0.5f;
    const float trimGain = juce::Decibels::decibelsToGain (-6.0f);
    const std::array<bool, smt::numConfigs> engaged { true, false, false, false, false, false };

    // Configuration A: input 1 to outputs 1 and 2 at 0 dB. Output 2 is trimmed
    // by -6 dB and inverted.
    auto model = std::make_unique<smt::ConfigModel>();
    model->setMatrixSize (1, 2);
    smt::FrameSettings route;
    route.active = true;
    model->setFrame (0, 0, 0, route);
    model->setFrame (0, 0, 1, route);

    auto out2 = model->getOutput (1);
    out2.gainDb = -6.0f;
    out2.phaseInvert = true;
    model->setOutput (1, out2);

    auto engine = std::make_unique<smt::MatrixEngine> (*model);
    engine->prepare (sr, blockSize);

    juce::AudioBuffer<float> in (smt::numChannels, blockSize);
    in.clear();
    for (int s = 0; s < blockSize; ++s)
        in.setSample (0, s, input);
    juce::AudioBuffer<float> out (smt::numChannels, blockSize);

    auto run = [&] (int blocks)
    {
        for (int b = 0; b < blocks; ++b)
            engine->process (in.getArrayOfReadPointers(), out, blockSize, engaged, 1.0f);
    };

    // Well past the 50 ms trim and master ramps (2400 samples at 48 kHz).
    run (20);
    approx (out.getSample (0, blockSize - 1), input, 1.0e-4,
            "steady state: normal output keeps its sign");
    approx (out.getSample (1, blockSize - 1), -input * trimGain, 1.0e-4,
            "steady state: inverted output is the negated, trimmed input");

    // Flip output 2 back. The next block must move towards the new sign in
    // small steps: no sample-to-sample jump anywhere near the full swing.
    out2.phaseInvert = false;
    model->setOutput (1, out2);

    float prev = out.getSample (1, blockSize - 1);
    run (1);
    float largestStep = 0.0f;
    for (int s = 0; s < blockSize; ++s)
    {
        const float v = out.getSample (1, s);
        largestStep = juce::jmax (largestStep, std::abs (v - prev));
        prev = v;
    }
    const float mid = out.getSample (1, blockSize - 1);
    check (mid > -input * trimGain + 0.01f && mid < input * trimGain - 0.01f,
           "flip: one block in, the output is still ramping");
    check (largestStep < 0.01f, "flip: no step between consecutive samples");

    run (20);
    approx (out.getSample (1, blockSize - 1), input * trimGain, 1.0e-4,
            "flip: settles on the normal, trimmed input");

    // processOutputChainOnly, as the "FIR" measurement mode calls it: trim and
    // polarity apply at once (no smoothing on that path).
    out2.phaseInvert = true;
    model->setOutput (1, out2);

    juce::AudioBuffer<float> chain (2, blockSize);
    for (int ch = 0; ch < 2; ++ch)
        for (int s = 0; s < blockSize; ++s)
            chain.setSample (ch, s, input);
    engine->processOutputChainOnly (chain, 0, blockSize, true);
    engine->processOutputChainOnly (chain, 1, blockSize, true);
    approx (chain.getSample (0, 0), input, 1.0e-6, "output chain only: normal output");
    approx (chain.getSample (1, 0), -input * trimGain, 1.0e-6,
            "output chain only: inverted output, from the first sample");
}

//==============================================================================
int main()
{
    testSerialisation();
    testEngine();

    std::cout << (g_ok ? "ALL TESTS PASSED\n" : "TESTS FAILED\n");
    return g_ok ? 0 : 1;
}
