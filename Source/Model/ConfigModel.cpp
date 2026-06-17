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
static const juce::Identifier idBand           ("Band");
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
                vf.setProperty ("phaseInvert", f.phaseInvert, nullptr);
                vf.setProperty ("delayMs", f.delayMs, nullptr);
                vf.setProperty ("spectrum", f.spectrum, nullptr);

                for (int bi = 0; bi < numFrameBands; ++bi)
                {
                    const auto& b = f.bands[(size_t) bi];
                    juce::ValueTree vb (idBand);
                    vb.setProperty ("on", b.on, nullptr);
                    vb.setProperty ("type", b.type, nullptr);
                    vb.setProperty ("order", b.order, nullptr);
                    vb.setProperty ("freq", b.freq, nullptr);
                    vb.setProperty ("q", b.q, nullptr);
                    vb.setProperty ("gain", b.gainDb, nullptr);
                    vf.addChild (vb, -1, nullptr);
                }
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
                    f.phaseInvert = (bool)  vf.getProperty ("phaseInvert", f.phaseInvert);
                    f.delayMs     = (float) (double) vf.getProperty ("delayMs", f.delayMs);
                    f.spectrum    = (bool)  vf.getProperty ("spectrum", f.spectrum);

                    // Backward compatibility: old single-filter format -> band 0.
                    if (vf.hasProperty ("filterOn"))
                    {
                        auto& b = f.bands[0];
                        b.on     = (bool)  vf.getProperty ("filterOn", false);
                        b.type   = (int)   vf.getProperty ("filterType", b.type);
                        b.order  = (int)   vf.getProperty ("filterOrder", b.order);
                        b.freq   = (float) (double) vf.getProperty ("filterFreq", b.freq);
                        b.q      = (float) (double) vf.getProperty ("filterQ", b.q);
                    }

                    // Current format: one child per band.
                    int bi = 0;
                    for (int ci = 0; ci < vf.getNumChildren() && bi < numFrameBands; ++ci)
                    {
                        auto vb = vf.getChild (ci);
                        if (! vb.hasType (idBand))
                            continue;
                        auto& b = f.bands[(size_t) bi++];
                        b.on     = (bool)  vb.getProperty ("on", b.on);
                        b.type   = (int)   vb.getProperty ("type", b.type);
                        b.order  = (int)   vb.getProperty ("order", b.order);
                        b.freq   = (float) (double) vb.getProperty ("freq", b.freq);
                        b.q      = (float) (double) vb.getProperty ("q", b.q);
                        b.gainDb = (float) (double) vb.getProperty ("gain", b.gainDb);
                    }
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
