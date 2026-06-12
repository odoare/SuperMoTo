/*
  ------------------------------------------------------------------------------
    ConfigModel.cpp — serialization of the matrix configurations.

    Author: Olivier Doaré, github.com/odoare
    Licenced under the GNU LGPL Version 3.0
    SPDX-License-Identifier: LGPL-3.0-or-later
  ------------------------------------------------------------------------------
*/

#include "ConfigModel.h"

namespace smt
{

static const juce::Identifier idConfigurations ("Configurations");
static const juce::Identifier idConfiguration  ("Configuration");
static const juce::Identifier idFrame          ("Frame");
static const juce::Identifier idOutput         ("Output");

juce::ValueTree ConfigModel::toValueTree() const
{
    decltype (frames) fr;
    decltype (outputs) outs;
    copyAll (fr, outs);

    juce::ValueTree tree (idConfigurations);
    tree.setProperty ("numIns", numIns.load(), nullptr);
    tree.setProperty ("numOuts", numOuts.load(), nullptr);

    for (int c = 0; c < numConfigs; ++c)
    {
        juce::ValueTree cfg (idConfiguration);
        cfg.setProperty ("index", c, nullptr);

        for (int i = 0; i < numChannels; ++i)
            for (int o = 0; o < numChannels; ++o)
            {
                const auto& f = fr[(size_t) c][(size_t) i][(size_t) o];
                if (f.isDefault())
                    continue;       // keep the state compact

                juce::ValueTree vf (idFrame);
                vf.setProperty ("in", i, nullptr);
                vf.setProperty ("out", o, nullptr);
                vf.setProperty ("active", f.active, nullptr);
                vf.setProperty ("gain", f.gainDb, nullptr);
                vf.setProperty ("filterOn", f.filterOn, nullptr);
                vf.setProperty ("filterType", f.filterType, nullptr);
                vf.setProperty ("filterOrder", f.filterOrder, nullptr);
                vf.setProperty ("filterFreq", f.filterFreq, nullptr);
                vf.setProperty ("filterQ", f.filterQ, nullptr);
                vf.setProperty ("phaseInvert", f.phaseInvert, nullptr);
                vf.setProperty ("delayMs", f.delayMs, nullptr);
                vf.setProperty ("spectrum", f.spectrum, nullptr);
                cfg.addChild (vf, -1, nullptr);
            }

        tree.addChild (cfg, -1, nullptr);
    }

    for (int o = 0; o < numChannels; ++o)
    {
        const auto& s = outs[(size_t) o];
        if (s.isDefault())
            continue;

        juce::ValueTree vo (idOutput);
        vo.setProperty ("index", o, nullptr);
        vo.setProperty ("gain", s.gainDb, nullptr);
        vo.setProperty ("firOn", s.firOn, nullptr);
        vo.setProperty ("firPath", s.firPath, nullptr);
        vo.setProperty ("spectrum", s.spectrum, nullptr);
        tree.addChild (vo, -1, nullptr);
    }

    return tree;
}

void ConfigModel::restoreFromValueTree (const juce::ValueTree& tree)
{
    if (! tree.hasType (idConfigurations))
        return;

    numIns.store (juce::jlimit (1, numChannels, (int) tree.getProperty ("numIns", numChannels)));
    numOuts.store (juce::jlimit (1, numChannels, (int) tree.getProperty ("numOuts", numChannels)));

    {
        const juce::SpinLock::ScopedLockType sl (lock);

        for (auto& cfg : frames)
            for (auto& row : cfg)
                for (auto& f : row)
                    f = FrameSettings();
        for (auto& o : outputs)
            o = OutputSettings();

        for (int ci = 0; ci < tree.getNumChildren(); ++ci)
        {
            auto child = tree.getChild (ci);

            if (child.hasType (idConfiguration))
            {
                const int c = (int) child.getProperty ("index", -1);
                if (c < 0 || c >= numConfigs)
                    continue;

                for (int fi = 0; fi < child.getNumChildren(); ++fi)
                {
                    auto vf = child.getChild (fi);
                    if (! vf.hasType (idFrame))
                        continue;

                    const int i = (int) vf.getProperty ("in", -1);
                    const int o = (int) vf.getProperty ("out", -1);
                    if (i < 0 || i >= numChannels || o < 0 || o >= numChannels)
                        continue;

                    FrameSettings f;
                    f.active      = (bool)  vf.getProperty ("active", f.active);
                    f.gainDb      = (float) (double) vf.getProperty ("gain", f.gainDb);
                    f.filterOn    = (bool)  vf.getProperty ("filterOn", f.filterOn);
                    f.filterType  = (int)   vf.getProperty ("filterType", f.filterType);
                    f.filterOrder = (int)   vf.getProperty ("filterOrder", f.filterOrder);
                    f.filterFreq  = (float) (double) vf.getProperty ("filterFreq", f.filterFreq);
                    f.filterQ     = (float) (double) vf.getProperty ("filterQ", f.filterQ);
                    f.phaseInvert = (bool)  vf.getProperty ("phaseInvert", f.phaseInvert);
                    f.delayMs     = (float) (double) vf.getProperty ("delayMs", f.delayMs);
                    f.spectrum    = (bool)  vf.getProperty ("spectrum", f.spectrum);
                    frames[(size_t) c][(size_t) i][(size_t) o] = f;
                }
            }
            else if (child.hasType (idOutput))
            {
                const int o = (int) child.getProperty ("index", -1);
                if (o < 0 || o >= numChannels)
                    continue;

                OutputSettings s;
                s.gainDb   = (float) (double) child.getProperty ("gain", s.gainDb);
                s.firOn    = (bool)  child.getProperty ("firOn", s.firOn);
                s.firPath  = child.getProperty ("firPath", s.firPath).toString();
                s.spectrum = (bool)  child.getProperty ("spectrum", s.spectrum);
                outputs[(size_t) o] = s;
            }
        }
    }

    bumpAndNotify();
}

} // namespace smt
