/*
  ------------------------------------------------------------------------------
    AnalysisSession.cpp

    Author: Olivier Doaré, github.com/odoare
    Licenced under the GNU AGPL Version 3.0, or commercial terms (LICENSE.md)
    SPDX-License-Identifier: AGPL-3.0-or-later OR LicenseRef-FXME-Commercial
  ------------------------------------------------------------------------------
*/

#include "AnalysisSession.h"
#include "../AppSettings.h"

namespace smt
{

void AnalysisSession::applySettings()
{
    // Index 0..6 -> Off, 1/24, 1/12, 1/6, 1/3, 1/2, 1 oct.
    static const float fractions[] = { 0.0f, 1.0f / 24.0f, 1.0f / 12.0f,
                                       1.0f / 6.0f, 1.0f / 3.0f, 1.0f / 2.0f, 1.0f };
    engine.setSmoothing (fractions[juce::jlimit (0, 6, settings.smoothLowId  - 1)],
                         fractions[juce::jlimit (0, 6, settings.smoothHighId - 1)]);
    engine.setAnalysisRange (settings.lowFreqText.getFloatValue(),
                             settings.highFreqText.getFloatValue());
    engine.setCorrectionLevel (settings.correctionLevel);
    engine.setMaxBoostDb (settings.maxBoostDb);
    engine.setPhaseType (settings.minimumPhase ? AnalysisEngine::PhaseType::minimum
                                               : AnalysisEngine::PhaseType::linear);
    engine.setCrossoverHz (settings.crossoverText.getFloatValue());
    engine.setSubPolarityInverted (settings.subInverted);
    engine.setTimeAlignMs (settings.mainsDelayMs);
    engine.setMicCalibration (activeMicCal());
}

void AnalysisSession::setWindowSize (int size)
{
    settings.windowSize = size;
    engine.setWindowSize (size);
    if (hasFiles())
        reanalyze();
}

void AnalysisSession::setSweepMethod (bool sweep)
{
    settings.sweepMethod = sweep;
    if (hasFiles())
        reanalyze();
}

const fxme::MicCalibration& AnalysisSession::activeMicCal() const
{
    static const fxme::MicCalibration none;
    switch (settings.micCalSource)
    {
        case 2:  return folderMicCal;
        case 3:  return none;
        default: return sharedMicCalibration();
    }
}

//==============================================================================
void AnalysisSession::loadFiles (const juce::Array<juce::File>& files)
{
    if (files.isEmpty())
        return;

    loadedFiles = files;
    subFiles.clear();           // paired with the previous main set
    recommendedAlignMs = 0.0f;

    // A measurement folder carries its metadata (mic cal, SPL cal, per-file
    // run parameters) in measurement.xml next to the wavs; adopt an embedded
    // mic cal automatically (still overridable via the source selector).
    folderInfo = readMeasurementFolderInfo (loadedFiles[0].getParentDirectory());
    folderMicCal.clear();
    const bool haveFolderCal = folderInfo.hasMicCal()
        && folderMicCal.loadFromText (folderInfo.micCalText, folderInfo.micCalName);
    if (haveFolderCal)
        settings.micCalSource = 2;
    else if (settings.micCalSource == 2)
        settings.micCalSource = 1;

    // Sweep runs (with the sweep identity in the manifest) get the Farina
    // deconvolution automatically; anything else falls back to Welch.
    sweepAvailable = currentSweepInfo().isValid();
    settings.sweepMethod = sweepAvailable;

    reanalyze();
}

int AnalysisSession::loadSubFiles (const juce::Array<juce::File>& files)
{
    if (files.isEmpty() || ! engine.hasData())
        return 0;

    subFiles = files;
    const int ok = engine.loadSubFiles (subFiles);

    status = ok > 0
        ? juce::String (ok) + " sub measurement(s) aligned for phase integration."
        : juce::String ("No sub file could be analyzed (need stereo wavs at the main's rate/length).");

    // Auto-detect the main/sub time offset (recommendation only; the user
    // drives the Mains delay to apply it).
    recommendedAlignMs = ok > 0 ? engine.estimateMainSubOffsetMs() : 0.0f;
    return ok;
}

void AnalysisSession::reanalyze()
{
    if (loadedFiles.isEmpty())
        return;

    applySettings();
    engine.setWindowSize (settings.windowSize);
    engine.setSweepInfo (currentSweepInfo());
    engine.setTfMethod (settings.sweepMethod ? AnalysisEngine::TfMethod::sweep
                                             : AnalysisEngine::TfMethod::welch);
    const int ok = engine.loadFiles (loadedFiles);

    status = ok > 0
        ? juce::String (ok) + " measurement(s) analyzed at "
            + juce::String (engine.getSampleRate() / 1000.0, 1) + " kHz."
        : juce::String ("No file could be analyzed (need stereo wavs longer than the window).");

    // Loading the main set clears the sub pairing; put it back.
    if (ok > 0 && ! subFiles.isEmpty())
    {
        const int subOk = engine.loadSubFiles (subFiles);
        recommendedAlignMs = subOk > 0 ? engine.estimateMainSubOffsetMs() : 0.0f;
        status << " " << juce::String (subOk) << " sub measurement(s) re-aligned.";
    }
}

//==============================================================================
// The loaded files' sweep identity from the folder manifest (invalid when the
// run was not a sweep or no manifest was found).
AnalysisEngine::SweepInfo AnalysisSession::currentSweepInfo() const
{
    AnalysisEngine::SweepInfo s;
    if (loadedFiles.isEmpty())
        return s;
    const auto it = folderInfo.fileRuns.find (loadedFiles[0].getFileName());
    if (it != folderInfo.fileRuns.end() && it->second.signal == "sweep")
    {
        s.f1 = it->second.sweepF1;
        s.f2 = it->second.sweepF2;
        s.L  = it->second.sweepL;
    }
    return s;
}

bool AnalysisSession::getSplContext (float& offsetDb, float& levelDb) const
{
    if (folderInfo.splCalibrated)
        offsetDb = folderInfo.splOffsetDb;
    else if (isSplCalibrated())
        offsetDb = getSplOffsetDb();
    else
        return false;

    if (loadedFiles.isEmpty())
        return false;
    const auto it = folderInfo.fileRuns.find (loadedFiles[0].getFileName());
    if (it == folderInfo.fileRuns.end())
        return false;
    levelDb = it->second.levelDb;
    return true;
}

} // namespace smt
