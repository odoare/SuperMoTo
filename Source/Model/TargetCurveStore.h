/*
  ------------------------------------------------------------------------------
    TargetCurveStore.h

    The named target curves on disk: a small bank of four numbers each, kept
    as one xml file per curve in

        <application data>/FXMechanics/SuperMoTo/target_curves/

    next to the plugin's preset folder. Separate from fxme::PresetManager on
    purpose: that one round-trips the whole plugin state through the APVTS,
    while a target curve is a handful of values that has to be swappable
    without touching anything else about the rig.

    Factory curves are ordinary files in the same folder, marked factory="1",
    and any that is missing is written again by refresh(). They are not
    editable through the GUI (Save, Rename and Delete act on user curves
    only), so the rewrite is invisible unless the folder is edited by hand.

    Author: Olivier Doaré, github.com/odoare
    Licenced under the GNU AGPL Version 3.0, or commercial terms (LICENSE.md)
    SPDX-License-Identifier: AGPL-3.0-or-later OR LicenseRef-FXME-Commercial
  ------------------------------------------------------------------------------
*/

#pragma once

#include <JuceHeader.h>
#include <algorithm>
#include <vector>
#include "../AppSettings.h"
#include "../Dsp/TargetCurve.h"

namespace smt
{

class TargetCurveStore
{
public:
    struct Entry
    {
        juce::String name;
        juce::File file;
        bool factory = false;
        TargetCurve curve;
    };

    TargetCurveStore() { refresh(); }

    /** Where the curves live. */
    static juce::File directory()
    {
        return superMoToDataFolder().getChildFile ("target_curves");
    }

    /** Writes any factory curve the folder is missing, then reads it: factory
        curves first, then the user's, each set alphabetical. */
    void refresh()
    {
        auto dir = directory();
        dir.createDirectory();

        for (const auto& f : factorySet())
            if (! fileFor (dir, f.name).existsAsFile())
                write (fileFor (dir, f.name), f.name, f.curve, true);

        entries.clear();

        for (const auto& file : dir.findChildFiles (juce::File::findFiles, false, "*.xml"))
        {
            Entry e;
            if (read (file, e))
                entries.push_back (std::move (e));
        }

        std::sort (entries.begin(), entries.end(), [] (const Entry& a, const Entry& b)
        {
            if (a.factory != b.factory)
                return a.factory;           // factory first
            return a.name.compareIgnoreCase (b.name) < 0;
        });
    }

    const std::vector<Entry>& getEntries() const noexcept   { return entries; }

    int indexOfName (const juce::String& name) const
    {
        for (int i = 0; i < (int) entries.size(); ++i)
            if (entries[(size_t) i].name.equalsIgnoreCase (name))
                return i;
        return -1;
    }

    /** The curve a set of values came from, or -1 when it matches none: what
        the selector shows as "(edited)". */
    int indexOfCurve (const TargetCurve& c) const
    {
        for (int i = 0; i < (int) entries.size(); ++i)
            if (entries[(size_t) i].curve == c)
                return i;
        return -1;
    }

    /** Creates or overwrites a user curve, and returns its index afterwards.
        Refuses to write over a factory one. */
    int save (const juce::String& name, const TargetCurve& c)
    {
        const auto clean = name.trim();
        if (clean.isEmpty())
            return -1;

        if (const int existing = indexOfName (clean); existing >= 0)
            if (entries[(size_t) existing].factory)
                return -1;

        if (! write (fileFor (directory(), clean), clean, c, false))
            return -1;

        refresh();
        return indexOfName (clean);
    }

    bool rename (int index, const juce::String& newName)
    {
        const auto clean = newName.trim();
        if (! isUserIndex (index) || clean.isEmpty() || indexOfName (clean) >= 0)
            return false;

        const auto& e = entries[(size_t) index];
        const auto curve = e.curve;
        if (! write (fileFor (directory(), clean), clean, curve, false))
            return false;

        e.file.deleteFile();
        refresh();
        return true;
    }

    bool remove (int index)
    {
        if (! isUserIndex (index))
            return false;

        const bool ok = entries[(size_t) index].file.deleteFile();
        refresh();
        return ok;
    }

    bool isUserIndex (int index) const
    {
        return index >= 0 && index < (int) entries.size() && ! entries[(size_t) index].factory;
    }

private:
    struct Factory { juce::String name; TargetCurve curve; };

    /** The curves that ship. Flat first, then the range the literature spans:
        EBU Tech 3276's mask allows 1 dB/octave above 2 kHz, and the Harman
        in-room target is about 1 dB/octave across the band with a bass shelf
        near 105 Hz. */
    static std::vector<Factory> factorySet()
    {
        return {
            { "Flat",                   TargetCurve { 0.0f,   0.0f,    0.0f, 105.0f } },
            { "Gentle -3 dB",           TargetCurve { -3.0f,  0.0f,    0.0f, 105.0f } },
            { "Moderate -4 dB",         TargetCurve { -4.0f,  0.0f,    0.0f, 105.0f } },
            { "Strong -6 dB",           TargetCurve { -6.0f,  0.0f,    0.0f, 105.0f } },
            { "Harman-like",            TargetCurve { -10.0f, 0.0f,    3.0f, 105.0f } },
            { "Flat to 1 kHz, -6 dB",   TargetCurve { -6.0f,  1000.0f, 0.0f, 105.0f } },
        };
    }

    static juce::File fileFor (const juce::File& dir, const juce::String& name)
    {
        return dir.getChildFile (juce::File::createLegalFileName (name) + ".xml");
    }

    static bool write (const juce::File& file, const juce::String& name,
                       const TargetCurve& c, bool factory)
    {
        juce::XmlElement xml ("SuperMoToTargetCurve");
        xml.setAttribute ("name", name);
        if (factory)
            xml.setAttribute ("factory", 1);
        xml.setAttribute ("tiltDb", (double) c.tiltDb);
        xml.setAttribute ("turnoverHz", (double) c.turnoverHz);
        xml.setAttribute ("bassDb", (double) c.bassDb);
        xml.setAttribute ("bassHz", (double) c.bassHz);
        return xml.writeTo (file);
    }

    static bool read (const juce::File& file, Entry& out)
    {
        const auto xml = juce::parseXML (file);
        if (xml == nullptr || ! xml->hasTagName ("SuperMoToTargetCurve"))
            return false;

        out.file = file;
        out.name = xml->getStringAttribute ("name", file.getFileNameWithoutExtension());
        out.factory = xml->getIntAttribute ("factory", 0) != 0;
        out.curve.tiltDb     = (float) xml->getDoubleAttribute ("tiltDb", 0.0);
        out.curve.turnoverHz = (float) xml->getDoubleAttribute ("turnoverHz", 0.0);
        out.curve.bassDb     = (float) xml->getDoubleAttribute ("bassDb", 0.0);
        out.curve.bassHz     = (float) xml->getDoubleAttribute ("bassHz", 105.0);
        return out.name.isNotEmpty();
    }

    std::vector<Entry> entries;
};

} // namespace smt
