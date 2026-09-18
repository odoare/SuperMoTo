/*
  ------------------------------------------------------------------------------
    MeasurementFolderTest.cpp

    Offline check of the measurement-folder reader and the run helpers behind
    Group analysis's "Runs..." dialog (Source/Dsp/MeasurementFolder):
      - the run log reads back oldest first, numbered, with time, comment,
        mode and file names, and every file maps to its run,
      - withoutRuns() removes a run's files from every speaker and the sub,
        and run 0 stands for the files no run lists,
      - pairSubFiles() pairs a speaker with the sub by run, not by position
        number (the per-channel counters drift apart once a speaker misses a
        run), then pairs what is left in load order, and leaves hand-picked
        files in load order.

    The folder is written to a temporary directory: empty wav files (only the
    names matter here) and a measurement.xml laid out as MeasurementEngine
    writes it.

    Run: build target SuperMoToFolderTests and execute it; exits 0 on success.

    Author: Olivier Doaré, github.com/odoare
    (c) 2023-2026 Olivier Doaré
    Licenced under the GNU AGPL Version 3.0, or commercial terms (LICENSE.md)
    SPDX-License-Identifier: AGPL-3.0-or-later OR LicenseRef-FXME-Commercial
  ------------------------------------------------------------------------------
*/

#include <JuceHeader.h>
#include "../Source/Dsp/MeasurementFolder.h"
#include <iostream>

static bool g_ok = true;

static bool check (bool cond, const juce::String& what)
{
    std::cout << (cond ? "  OK  " : "  FAIL ") << what << "\n";
    g_ok = g_ok && cond;
    return cond;
}

static juce::String names (const juce::Array<juce::File>& files)
{
    juce::StringArray s;
    for (const auto& f : files)
        s.add (f.getFileNameWithoutExtension());
    return s.joinIntoString (" ");
}

//==============================================================================
// The folder. Positions are counted per channel, as MeasurementEngine does, so
// ch02 missing run 2 puts its position numbers one behind ch01 and the sub.
//
//   run 1  dry  "centre"  ch01_pos001  ch02_pos001  sub_pos001
//   run 2  dry  "left"    ch01_pos002               sub_pos002
//   run 3  dry  "right"   ch01_pos003  ch02_pos002  sub_pos003
//   run 4  fir  "verify"  ch01_pos004  ch02_pos003
//   (none)                             ch02_pos004            <- added by hand

struct RunSpec
{
    const char* time;
    const char* mode;
    const char* comment;
    juce::StringArray files;
};

static juce::File writeFolder()
{
    const auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory)
                         .getNonexistentChildFile ("SuperMoToFolderTest", {}, false);
    dir.createDirectory();

    const std::vector<RunSpec> runs {
        { "2026-09-14T10:00:00.000+02:00", "dry", "centre", { "ch01_pos001.wav", "ch02_pos001.wav", "sub_pos001.wav" } },
        { "2026-09-14T10:05:00.000+02:00", "dry", "left",   { "ch01_pos002.wav", "sub_pos002.wav" } },
        { "2026-09-14T10:10:00.000+02:00", "dry", "right",  { "ch01_pos003.wav", "ch02_pos002.wav", "sub_pos003.wav" } },
        { "2026-09-14T11:00:00.000+02:00", "fir", "verify", { "ch01_pos004.wav", "ch02_pos003.wav" } },
    };

    juce::XmlElement root ("SuperMoToMeasurements");
    root.setAttribute ("version", 1);
    root.setAttribute ("subChannel", 3);
    for (int ch : { 1, 2, 3 })
        root.createNewChildElement ("Channel")->setAttribute ("number", ch);

    // Newest first, as the writer stores them.
    for (auto it = runs.rbegin(); it != runs.rend(); ++it)
    {
        auto* run = root.createNewChildElement ("Run");
        run->setAttribute ("time", it->time);
        run->setAttribute ("mode", it->mode);
        run->setAttribute ("signal", "sweep");
        run->setAttribute ("comment", it->comment);
        for (const auto& name : it->files)
        {
            run->createNewChildElement ("File")->setAttribute ("name", name);
            dir.getChildFile (name).create();
        }
    }
    root.writeTo (dir.getChildFile ("measurement.xml"));

    dir.getChildFile ("ch02_pos004.wav").create();
    return dir;
}

//==============================================================================
static void testReader (const juce::File& dir)
{
    std::cout << "Reader\n";

    const auto info = smt::readMeasurementFolderInfo (dir);
    check (info.manifestFound, "manifest found");
    check (info.runs.size() == 4, "four runs");
    if (info.runs.size() != 4)
        return;

    check (info.runs[0].info.number == 1 && info.runs[0].info.comment == "centre",
           "oldest run first, numbered 1");
    check (info.runs[3].info.number == 4 && info.runs[3].info.mode == "fir",
           "newest run last, numbered 4, mode kept");
    check (info.runs[1].files.size() == 2, "run 2 lists its two files");
    check (info.runs[2].info.time == "2026-09-14T10:10:00.000+02:00", "time kept as written");
    check (info.runNumberOf ("ch02_pos002.wav") == 3, "ch02_pos002 belongs to run 3");
    check (info.runNumberOf ("sub_pos002.wav") == 2, "sub_pos002 belongs to run 2");
    check (info.runNumberOf ("ch02_pos004.wav") == 0, "a file no run lists maps to 0");

    const auto contents = smt::scanMeasurementFolder (dir);
    check (contents.ok, "folder scans");
    check (contents.speakers.size() == 2, "two speakers");
    if (contents.speakers.size() == 2)
    {
        check (names (contents.speakers[0].files) == "ch01_pos001 ch01_pos002 ch01_pos003 ch01_pos004",
               "ch01 files, by position");
        check (names (contents.speakers[1].files) == "ch02_pos001 ch02_pos002 ch02_pos003 ch02_pos004",
               "ch02 files, by position, the unlisted one included");
    }
    check (names (contents.sub.files) == "sub_pos001 sub_pos002 sub_pos003", "sub files");
}

static void testWithoutRuns (const juce::File& dir)
{
    std::cout << "withoutRuns\n";

    const auto contents = smt::scanMeasurementFolder (dir);
    if (contents.speakers.size() != 2)
    {
        check (false, "scan precondition");
        return;
    }

    const auto noFir = smt::withoutRuns (contents, { 4 });
    check (names (noFir.speakers[0].files) == "ch01_pos001 ch01_pos002 ch01_pos003",
           "run 4 leaves ch01");
    check (names (noFir.speakers[1].files) == "ch02_pos001 ch02_pos002 ch02_pos004",
           "run 4 leaves ch02, the unlisted file kept");
    check (names (noFir.sub.files) == "sub_pos001 sub_pos002 sub_pos003", "run 4 had no sub file");

    const auto noLeft = smt::withoutRuns (contents, { 2 });
    check (names (noLeft.speakers[0].files) == "ch01_pos001 ch01_pos003 ch01_pos004",
           "run 2 leaves ch01");
    check (names (noLeft.speakers[1].files) == "ch02_pos001 ch02_pos002 ch02_pos003 ch02_pos004",
           "run 2 never measured ch02");
    check (names (noLeft.sub.files) == "sub_pos001 sub_pos003", "run 2 leaves the sub");

    const auto noUnlisted = smt::withoutRuns (contents, { 0, 4 });
    check (names (noUnlisted.speakers[1].files) == "ch02_pos001 ch02_pos002",
           "run 0 removes the unlisted file");

    const auto all = smt::withoutRuns (contents, { 1, 2, 3, 4, 0 });
    check (all.speakers.size() == 2 && all.speakers[1].files.isEmpty(),
           "an emptied channel is kept, empty");
}

static void testPairing (const juce::File& dir)
{
    std::cout << "pairSubFiles\n";

    const auto contents = smt::scanMeasurementFolder (dir);
    if (contents.speakers.size() != 2)
    {
        check (false, "scan precondition");
        return;
    }
    const auto& info = contents.info;
    const auto used = smt::withoutRuns (contents, { 0, 4 });

    // ch02 has runs 1 and 3; the sub runs 1, 2 and 3. By position number,
    // ch02_pos002 would meet sub_pos002 (run 2, another mic position).
    const auto p = smt::pairSubFiles (used.speakers[1].files, used.sub.files, info);
    check (p.pairedByRun == 2 && p.pairedInOrder == 0, "ch02: two pairs, both by run");
    check (names (p.speaker) == "ch02_pos001 ch02_pos002", "ch02: speaker order");
    check (names (p.sub) == "sub_pos001 sub_pos003", "ch02: pos002 pairs with sub_pos003 (run 3)");
    check (p.unpairedSubFiles == 1 && p.unpairedSpeakerFiles == 0, "ch02: sub_pos002 dropped");

    // With the unlisted file: run pairs first, then the leftovers in order.
    const auto q = smt::pairSubFiles (smt::withoutRuns (contents, { 4 }).speakers[1].files,
                                      contents.sub.files, info);
    check (q.pairedByRun == 2 && q.pairedInOrder == 1, "unlisted file paired in order");
    check (names (q.speaker) == "ch02_pos001 ch02_pos002 ch02_pos004", "leftover speaker file last");
    check (names (q.sub) == "sub_pos001 sub_pos003 sub_pos002", "leftover sub file last");

    // ch01 measured with the sub every time, plus a FIR run: three run pairs,
    // the FIR file left without a partner.
    const auto r = smt::pairSubFiles (contents.speakers[0].files, contents.sub.files, info);
    check (r.pairedByRun == 3 && r.unpairedSpeakerFiles == 1, "ch01: three by run, one unpaired");
    check (names (r.speaker) == "ch01_pos001 ch01_pos002 ch01_pos003 ch01_pos004",
           "ch01: order unchanged when it already matches");

    // Hand-picked files, no manifest: load order, untouched.
    const auto h = smt::pairSubFiles (contents.speakers[0].files, contents.sub.files, {});
    check (h.pairedByRun == 0 && h.pairedInOrder == 3, "no manifest: load order");
    check (names (h.speaker) == names (contents.speakers[0].files)
               && names (h.sub) == names (contents.sub.files),
           "no manifest: both lists unchanged");

    // A sub measured in runs of its own shares no run with the speakers:
    // everything pairs in order, as before runs were read.
    smt::MeasurementFolderInfo separate;
    auto runOf = [&separate] (const char* name, int number)
    {
        smt::MeasurementRunInfo ri;
        ri.number = number;
        separate.fileRuns[name] = ri;
    };
    runOf ("ch01_pos001.wav", 1); runOf ("sub_pos001.wav", 2);
    runOf ("ch01_pos002.wav", 3); runOf ("sub_pos002.wav", 4);
    const juce::Array<juce::File> sp { dir.getChildFile ("ch01_pos001.wav"), dir.getChildFile ("ch01_pos002.wav") };
    const juce::Array<juce::File> sb { dir.getChildFile ("sub_pos001.wav"), dir.getChildFile ("sub_pos002.wav") };
    const auto s = smt::pairSubFiles (sp, sb, separate);
    check (s.pairedByRun == 0 && s.pairedInOrder == 2, "sub in its own runs: paired in order");
    check (names (s.sub) == "sub_pos001 sub_pos002", "sub in its own runs: order unchanged");

    // A run listing two files for one side is no key for either of them.
    smt::MeasurementFolderInfo twice;
    for (auto* name : { "ch01_pos001.wav", "ch01_pos002.wav", "sub_pos002.wav" })
    {
        smt::MeasurementRunInfo ri;
        ri.number = 1;
        twice.fileRuns[name] = ri;
    }
    const auto t = smt::pairSubFiles (sp, juce::Array<juce::File> { dir.getChildFile ("sub_pos002.wav") }, twice);
    check (t.pairedByRun == 0 && t.pairedInOrder == 1, "a repeated run is not used as a key");
}

static void testCaptureNames()
{
    std::cout << "parseCaptureName\n";

    juce::String tag;
    int ch = 0, pos = 0;
    check (smt::parseCaptureName ("ch07_pos012", tag, ch, pos) && tag == "ch" && ch == 7 && pos == 12,
           "ch07_pos012");
    check (smt::parseCaptureName ("sub_pos003", tag, ch, pos) && tag == "sub" && ch == -1 && pos == 3,
           "sub_pos003");
    check (smt::parseCaptureName ("in02_pos001", tag, ch, pos) && tag == "in" && ch == 2,
           "in02_pos001");
    check (! smt::parseCaptureName ("measurement", tag, ch, pos), "an arbitrary name is refused");
}

//==============================================================================
int main()
{
    const auto dir = writeFolder();

    testCaptureNames();
    testReader (dir);
    testWithoutRuns (dir);
    testPairing (dir);

    dir.deleteRecursively();

    std::cout << (g_ok ? "ALL TESTS PASSED\n" : "TESTS FAILED\n");
    return g_ok ? 0 : 1;
}
