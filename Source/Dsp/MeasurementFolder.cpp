/*
  ------------------------------------------------------------------------------
    MeasurementFolder.cpp

    Author: Olivier Doaré, github.com/odoare
    Licenced under the GNU AGPL Version 3.0, or commercial terms (LICENSE.md)
    SPDX-License-Identifier: AGPL-3.0-or-later OR LicenseRef-FXME-Commercial
  ------------------------------------------------------------------------------
*/

#include "MeasurementFolder.h"
#include <algorithm>

namespace smt
{

bool parseCaptureName (const juce::String& stem, juce::String& tag,
                       int& channelNumber, int& position)
{
    const auto posIdx = stem.lastIndexOf ("_pos");
    if (posIdx < 0)
        return false;

    position = stem.substring (posIdx + 4).getIntValue();
    const auto head = stem.substring (0, posIdx);

    if (head == "sub")          { tag = "sub"; channelNumber = -1; return true; }
    if (head.startsWith ("ch")) { tag = "ch";  channelNumber = head.substring (2).getIntValue(); return true; }
    if (head.startsWith ("in")) { tag = "in";  channelNumber = head.substring (2).getIntValue(); return true; }
    return false;
}

static void sortFilesByPosition (juce::Array<juce::File>& files)
{
    std::sort (files.begin(), files.end(), [] (const juce::File& a, const juce::File& b)
    {
        juce::String tag; int chan = -1, posA = 0, posB = 0;
        parseCaptureName (a.getFileNameWithoutExtension(), tag, chan, posA);
        parseCaptureName (b.getFileNameWithoutExtension(), tag, chan, posB);
        return posA < posB;
    });
}

MeasurementFolderInfo readMeasurementFolderInfo (const juce::File& folder)
{
    MeasurementFolderInfo info;
    const auto xml = juce::parseXML (folder.getChildFile ("measurement.xml"));
    if (xml == nullptr || ! xml->hasTagName ("SuperMoToMeasurements"))
        return info;

    info.manifestFound = true;

    if (auto* c = xml->getChildByName ("GeneralComment"))
        info.generalComment = c->getAllSubText().trim();
    if (auto* sc = xml->getChildByName ("SplCalibration"))
    {
        info.splCalibrated = true;
        info.splOffsetDb = (float) sc->getDoubleAttribute ("offsetDb");
    }
    if (auto* mc = xml->getChildByName ("MicCalibration"))
    {
        info.micCalName = mc->getStringAttribute ("name");
        info.micCalText = mc->getAllSubText();
    }

    // Runs are stored newest first. Read them in that order, then reverse so
    // they number chronologically.
    for (auto* run : xml->getChildWithTagNameIterator ("Run"))
    {
        MeasurementRun entry;
        auto& r = entry.info;
        r.time          = run->getStringAttribute ("time");
        r.comment       = run->getStringAttribute ("comment");
        r.mode          = run->getStringAttribute ("mode");
        r.signal        = run->getStringAttribute ("signal");
        r.durationS     = (float) run->getDoubleAttribute ("durationS");
        r.levelDb       = (float) run->getDoubleAttribute ("levelDb");
        r.sweepF1       = run->getDoubleAttribute ("sweepF1");
        r.sweepF2       = run->getDoubleAttribute ("sweepF2");
        r.sweepL        = run->getDoubleAttribute ("sweepL");
        r.splCalibrated = run->hasAttribute ("splOffsetDb");
        r.splOffsetDb   = (float) run->getDoubleAttribute ("splOffsetDb");
        r.micCalName    = run->getStringAttribute ("micCal");

        for (auto* f : run->getChildWithTagNameIterator ("File"))
            entry.files.add (f->getStringAttribute ("name"));

        info.runs.push_back (std::move (entry));
    }
    std::reverse (info.runs.begin(), info.runs.end());
    for (size_t k = 0; k < info.runs.size(); ++k)
        info.runs[k].info.number = (int) k + 1;

    // Newest run first again, so emplace keeps the most recent run for a file
    // name that also appears in an older one.
    for (auto it = info.runs.rbegin(); it != info.runs.rend(); ++it)
        for (const auto& name : it->files)
            info.fileRuns.emplace (name, it->info);

    return info;
}

MeasurementFolderContents scanMeasurementFolder (const juce::File& folder)
{
    MeasurementFolderContents result;
    result.info = readMeasurementFolderInfo (folder);

    // The channel set and sub identity come from a manifest; the actual file
    // lists are always re-derived from the wavs on disk below, so a stale or
    // hand-edited manifest can't misorder the position pairing.
    std::vector<int> channels;
    int subChannel = -1;    // 1-based, -1 = none
    bool haveManifest = false;

    // Preferred: the machine-readable measurement.xml (written alongside the
    // readme by MeasurementEngine::writeManifests()).
    if (auto xml = juce::parseXML (folder.getChildFile ("measurement.xml")))
    {
        if (xml->hasTagName ("SuperMoToMeasurements"))
        {
            const int sc = xml->getIntAttribute ("subChannel", 0);
            subChannel = sc > 0 ? sc : -1;
            for (auto* c : xml->getChildWithTagNameIterator ("Channel"))
            {
                const int ch = c->getIntAttribute ("number", 0);
                if (ch > 0 && std::find (channels.begin(), channels.end(), ch) == channels.end())
                    channels.push_back (ch);
            }
            haveManifest = true;
        }
    }

    // Fallback for folders recorded before measurement.xml existed: parse the
    // human-readable readme's "Channels:" / "Sub channel:" lines.
    const juce::File readme = folder.getChildFile ("readme_measurement.md");
    if (! haveManifest && readme.existsAsFile())
    {
        const auto text = readme.loadFileAsString();
        auto lineStartingWith = [&text] (const juce::String& prefix) -> juce::String
        {
            for (const auto& line : juce::StringArray::fromLines (text))
                if (line.startsWith (prefix))
                    return line.substring (prefix.length()).trim();
            return {};
        };

        const auto channelsLine = lineStartingWith ("Channels:");
        const auto subLine      = lineStartingWith ("Sub channel:");

        if (channelsLine.isNotEmpty())
        {
            subChannel = subLine.equalsIgnoreCase ("none") ? -1 : subLine.getIntValue();
            if (subChannel <= 0)
                subChannel = -1;

            juce::StringArray channelTokens;
            channelTokens.addTokens (channelsLine, ",", "");
            for (auto& tok : channelTokens)
            {
                const int ch = tok.trim().getIntValue();
                if (ch > 0 && std::find (channels.begin(), channels.end(), ch) == channels.end())
                    channels.push_back (ch);
            }
            haveManifest = true;
        }
    }

    if (! haveManifest)
    {
        result.error = "No measurement.xml or readable readme_measurement.md found in "
                       + folder.getFullPathName() + ".";
        return result;
    }

    for (int ch : channels)
    {
        if (ch == subChannel)
            continue;

        auto files = folder.findChildFiles (juce::File::findFiles, false,
                                            "ch" + juce::String (ch).paddedLeft ('0', 2) + "_pos*.wav");
        if (files.isEmpty())
            continue;
        sortFilesByPosition (files);

        MeasurementFolderContents::Channel c;
        c.channelNumber = ch;
        c.files = files;
        result.speakers.push_back (c);
    }
    std::sort (result.speakers.begin(), result.speakers.end(),
              [] (const MeasurementFolderContents::Channel& a, const MeasurementFolderContents::Channel& b)
              { return a.channelNumber < b.channelNumber; });

    if (subChannel > 0)
    {
        auto files = folder.findChildFiles (juce::File::findFiles, false, "sub_pos*.wav");
        if (! files.isEmpty())
        {
            sortFilesByPosition (files);
            result.sub.channelNumber = subChannel;
            result.sub.files = files;
        }
    }

    result.ok = true;
    return result;
}

MeasurementFolderContents withoutRuns (const MeasurementFolderContents& contents,
                                       const std::set<int>& excludedRuns)
{
    auto result = contents;
    if (excludedRuns.empty())
        return result;

    auto drop = [&] (juce::Array<juce::File>& files)
    {
        files.removeIf ([&] (const juce::File& f)
        {
            return excludedRuns.count (contents.info.runNumberOf (f.getFileName())) > 0;
        });
    };

    for (auto& c : result.speakers)
        drop (c.files);
    drop (result.sub.files);
    return result;
}

SubPairing pairSubFiles (const juce::Array<juce::File>& speakerFiles,
                         const juce::Array<juce::File>& subFiles,
                         const MeasurementFolderInfo& info)
{
    // Each file's run, or 0 when it has none that can serve as a key: not in
    // the manifest, or a run that lists two files for this side (never written
    // by MeasurementEngine, but a hand-edited folder should not pair wrongly).
    auto keysOf = [&info] (const juce::Array<juce::File>& files)
    {
        std::vector<int> keys;
        std::map<int, int> count;
        for (const auto& f : files)
        {
            keys.push_back (info.runNumberOf (f.getFileName()));
            ++count[keys.back()];
        }
        for (auto& k : keys)
            if (k > 0 && count[k] > 1)
                k = 0;
        return keys;
    };

    const auto speakerKeys = keysOf (speakerFiles);
    const auto subKeys = keysOf (subFiles);

    std::map<int, int> subIndexOfRun;
    for (int i = 0; i < subFiles.size(); ++i)
        if (subKeys[(size_t) i] > 0)
            subIndexOfRun[subKeys[(size_t) i]] = i;

    SubPairing result;
    std::vector<bool> subTaken ((size_t) subFiles.size(), false);
    juce::Array<juce::File> speakerLeft;

    // Same run, same microphone position.
    for (int i = 0; i < speakerFiles.size(); ++i)
    {
        const auto it = speakerKeys[(size_t) i] > 0 ? subIndexOfRun.find (speakerKeys[(size_t) i])
                                                    : subIndexOfRun.end();
        if (it != subIndexOfRun.end())
        {
            result.speaker.add (speakerFiles[i]);
            result.sub.add (subFiles[it->second]);
            subTaken[(size_t) it->second] = true;
            ++result.pairedByRun;
        }
        else
        {
            speakerLeft.add (speakerFiles[i]);
        }
    }

    // The rest, in load order.
    juce::Array<juce::File> subLeft;
    for (int i = 0; i < subFiles.size(); ++i)
        if (! subTaken[(size_t) i])
            subLeft.add (subFiles[i]);

    const int inOrder = juce::jmin (speakerLeft.size(), subLeft.size());
    for (int i = 0; i < inOrder; ++i)
    {
        result.speaker.add (speakerLeft[i]);
        result.sub.add (subLeft[i]);
    }
    for (int i = inOrder; i < speakerLeft.size(); ++i)
        result.speaker.add (speakerLeft[i]);

    result.pairedInOrder = inOrder;
    result.unpairedSpeakerFiles = speakerLeft.size() - inOrder;
    result.unpairedSubFiles = subLeft.size() - inOrder;
    return result;
}

juce::String readMeasurementGeneralComment (const juce::File& folder)
{
    return readMeasurementFolderInfo (folder).generalComment;
}

} // namespace smt
