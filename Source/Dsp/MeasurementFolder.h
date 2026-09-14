/*
  ------------------------------------------------------------------------------
    MeasurementFolder.h

    Reading back a measurement folder written by MeasurementEngine: the
    capture file-name grammar, the measurement.xml manifest (calibration
    records, general comment, the run log), the folder scan Group analysis
    loads from, and the two pure helpers built on it: leaving chosen runs out
    of a scan, and pairing a speaker's files with the subwoofer's by run.

    Kept apart from MeasurementEngine (the writer, which needs the whole audio
    engine) so that everything here depends on juce_core alone and can be
    tested without the plugin's DSP.

    Author: Olivier Doaré, github.com/odoare
    Licenced under the GNU LGPL Version 3.0
    SPDX-License-Identifier: LGPL-3.0-or-later
  ------------------------------------------------------------------------------
*/

#pragma once

#include <JuceHeader.h>
#include <map>
#include <set>
#include <vector>

namespace smt
{

/** Filename grammar shared by the writer (MeasurementEngine) and the readers
    below: "ch<NN>_pos<PPP>", "in<NN>_pos<PPP>" or "sub_pos<PPP>", passed in
    without the .wav extension. tag is "ch", "in" or "sub"; channelNumber is
    1-based, -1 for "sub". False when the stem does not follow the grammar. */
bool parseCaptureName (const juce::String& stem, juce::String& tag,
                       int& channelNumber, int& position);

/** Per-run metadata recorded in measurement.xml. */
struct MeasurementRunInfo
{
    int number = 0;                 // chronological, 1 = the folder's first run
    juce::String time;              // ISO-8601, as written; empty if absent
    juce::String comment;           // the one-line run comment, may be empty
    juce::String mode;              // "dry" / "fir" / "system"
    juce::String signal;            // "sweep" / "noise"
    float durationS = 0.0f;         // stimulus length (excl. tail)
    float levelDb   = 0.0f;         // stimulus level
    double sweepF1 = 0.0;           // sweep identity (0 when not a sweep):
    double sweepF2 = 0.0;           //   phase(t) = 2*pi*f1*L*(exp(t/L) - 1)
    double sweepL  = 0.0;           //   L in seconds
    bool  splCalibrated = false;    // SPL cal in effect for that run
    float splOffsetDb   = 0.0f;     //   dB SPL = dBFS + offset
    juce::String micCalName;        // mic cal in effect for that run (name only)
};

/** One <Run> of the manifest with the capture files it lists. */
struct MeasurementRun
{
    MeasurementRunInfo info;
    juce::StringArray files;        // file names, as written in the manifest
};

/** Folder-level metadata read back from a measurement folder's
    measurement.xml: the general comment, the embedded calibration data
    (Analysis / Group analysis divide the mic curve out of the measurements,
    the SPL offset maps plot levels to true dB SPL), the run log, and each
    capture file's run parameters (the sweep identity enables
    deconvolution-based analysis). Everything is default/empty when the
    folder has no manifest. */
struct MeasurementFolderInfo
{
    bool manifestFound = false;
    juce::String generalComment;
    juce::String micCalName;        // embedded mic correction curve: display
    juce::String micCalText;        //   name + verbatim cal-file text (feed to
                                    //   MicCalibration::loadFromText)
    bool  splCalibrated = false;    // folder-level SPL calibration:
    float splOffsetDb   = 0.0f;     //   dB SPL = dBFS + offset

    /** Every run, oldest first (the manifest stores them newest first), so
        runs[k].info.number == k + 1. */
    std::vector<MeasurementRun> runs;

    /** wav name -> the run that wrote it. A name listed by several runs (a
        file deleted and measured again) maps to the newest of them. */
    std::map<juce::String, MeasurementRunInfo> fileRuns;

    bool hasMicCal() const noexcept { return micCalText.isNotEmpty(); }

    /** The number of the run that wrote this file, or 0 when no run lists it
        (a file added by hand, or a folder without a manifest). */
    int runNumberOf (const juce::String& fileName) const
    {
        const auto it = fileRuns.find (fileName);
        return it != fileRuns.end() ? it->second.number : 0;
    }
};

MeasurementFolderInfo readMeasurementFolderInfo (const juce::File& folder);

/** Reads back a folder written by MeasurementEngine: which channels were
    measured — from the machine-readable measurement.xml manifest, falling
    back to parsing readme_measurement.md's "Channels:"/"Sub channel:" lines
    for folders recorded before the XML existed — and for each, its files
    sorted by position (re-derived from the actual ch<NN>_pos<PPP>.wav /
    sub_pos<PPP>.wav files on disk, not counted from either manifest, so this
    stays correct even if a manifest and the folder ever drift).
    Used by GroupAnalysisComponent's "Load measurement folder..." button. */
struct MeasurementFolderContents
{
    struct Channel
    {
        int channelNumber = -1;         // 1-based, as written to disk/readme
        juce::Array<juce::File> files;  // sorted by ascending position
    };

    std::vector<Channel> speakers;      // ascending channel number, sub excluded
    Channel sub;                        // sub.channelNumber == -1 if none
    MeasurementFolderInfo info;         // manifest metadata (cal, comments, runs)
    bool ok = false;
    juce::String error;                 // set when !ok
};

MeasurementFolderContents scanMeasurementFolder (const juce::File& folder);

/** The same scan with every file written by one of `excludedRuns` removed,
    from each speaker and from the sub. Run numbers are
    MeasurementRunInfo::number; 0 stands for the files no run lists, so
    including it leaves those out too. Channels are kept even when they end up
    with no file, so their order and numbering never shift under a caller;
    checking for that is the caller's business. */
MeasurementFolderContents withoutRuns (const MeasurementFolderContents& contents,
                                       const std::set<int>& excludedRuns);

/** A speaker's files and the subwoofer's, ordered so that AnalysisEngine's
    load-order pairing (the i-th sub file anchored on the i-th speaker file)
    pairs files taken at the same microphone position.

    Position numbers cannot be the key: MeasurementEngine counts them per
    channel, so a speaker that missed one run is a position behind the sub
    from then on. The run is the key instead, because every channel measured
    in one run was measured at the same microphone position. Files from the
    same run are paired first. What is left on each side (a sub measured in
    runs of its own, files no run lists, or everything when `info` has no
    manifest) is then paired in load order, which is the only assumption
    available there and the behaviour from before runs were read at all.

    `speaker` holds every speaker file, paired ones first; `sub` holds their
    partners in the same order, so it is never longer than the paired part.
    Sub files left without a partner are dropped. */
struct SubPairing
{
    juce::Array<juce::File> speaker, sub;
    int pairedByRun = 0;
    int pairedInOrder = 0;
    int unpairedSpeakerFiles = 0;       // kept in `speaker`, no sub partner
    int unpairedSubFiles = 0;           // dropped from `sub`
};

SubPairing pairSubFiles (const juce::Array<juce::File>& speakerFiles,
                         const juce::Array<juce::File>& subFiles,
                         const MeasurementFolderInfo& info);

/** The general (whole-folder) comment stored in a folder's measurement.xml,
    empty when there is none. Used by the GUI to pre-fill its comment entry
    when the measurement folder changes, so an existing folder's comment is
    carried over (and not silently wiped) by the next run's manifest save. */
juce::String readMeasurementGeneralComment (const juce::File& folder);

} // namespace smt
