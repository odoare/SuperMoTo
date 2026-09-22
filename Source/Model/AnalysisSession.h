/*
  ------------------------------------------------------------------------------
    AnalysisSession.h

    Everything the single-speaker Analysis pane is working on, kept by the
    processor so that it outlives the editor: the analysis engine with the
    measurements it has analyzed, the files they came from (main and sub), the
    folder manifest found next to them, and the pane's settings.
    AnalysisComponent is only a view of it.

    Every operation here is synchronous and runs on the message thread, as the
    pane always did, so unlike GroupAnalysisSession there is no batch to
    follow and no listener: the one view calls, then redraws.

    Not saved with the host session: the working state lives as long as the
    plugin instance.

    Author: Olivier Doaré, github.com/odoare
    Licenced under the GNU AGPL Version 3.0, or commercial terms (LICENSE.md)
    SPDX-License-Identifier: AGPL-3.0-or-later OR LicenseRef-FXME-Commercial
  ------------------------------------------------------------------------------
*/

#pragma once

#include <JuceHeader.h>
#include "../Dsp/AnalysisEngine.h"
#include "../Dsp/MeasurementFolder.h"

namespace smt
{

/** The Analysis controls, as values. Combo boxes are stored by item id,
    editable ones by their text. */
struct AnalysisSettings
{
    // How the measurements become transfer functions.
    int windowSize = 65536;
    bool sweepMethod = false;           // TF: "Sweep (Farina)" rather than Welch
    int smoothLowId = 4;                // Off, 1/24, 1/12, 1/6, 1/3, 1/2, 1 oct
    int smoothHighId = 4;
    juce::String lowFreqText = "20 Hz";
    juce::String highFreqText = "20000 Hz";
    int micCalSource = 1;               // 1 global, 2 the folder's, 3 none

    // How the correction is designed from them.
    float correctionLevel = 1.0f;
    float maxBoostDb = 12.0f;
    bool minimumPhase = false;
    int firLength = 4096;
    int assignOutputId = 1;             // 1 (none), output o is id o + 2

    // Subwoofer integration.
    juce::String crossoverText = "80 Hz";
    bool subInverted = false;
    float mainsDelayMs = 0.0f;
    bool applyBulkDelay = false;

    // Display only.
    int levelRefId = 1;                 // Normalized, Absolute dB, dB SPL
    int displayId = 1;                  // View: 1 frequency, 2 impulse, 3 raw impulse
    bool irInDb = false;                // that view's amplitude axis
    float viewLowHz = 0.0f;             // plot frequency window, 0 = its default
    float viewHighHz = 0.0f;
};

class AnalysisSession
{
public:
    AnalysisSession() = default;

    AnalysisEngine engine;
    AnalysisSettings settings;

    /** The last status line, so a reopened view shows where things stood. */
    juce::String status;

    /** Main/sub offset estimated when the sub set was loaded (a recommendation
        only: the Mains delay setting is what the correction assumes). */
    float recommendedAlignMs = 0.0f;

    //==========================================================================
    /** Pushes the settings that do not invalidate the analysis (smoothing,
        range, level, boost, phase type, crossover, sub polarity, mains delay,
        mic cal) into the engine. Cheap when nothing moved: the engine setters
        return early on unchanged values. */
    void applySettings();

    /** The window size and the TF method invalidate the analysis: each
        re-analyzes the loaded files (and the sub set) when there are any. */
    void setWindowSize (int size);
    void setSweepMethod (bool sweep);

    //==========================================================================
    /** Loads one speaker's measurements: reads the manifest next to them,
        adopts its mic cal and sweep identity, and analyzes. A sub set loaded
        before is dropped, since it was paired with the previous files. */
    void loadFiles (const juce::Array<juce::File>& files);

    /** Loads the sub set onto the analyzed main set and estimates the offset.
        Returns the number of sub files analyzed. */
    int loadSubFiles (const juce::Array<juce::File>& files);

    /** Re-runs the analysis of the loaded files at the current settings, and
        re-pairs the sub set, which loading the main set clears. */
    void reanalyze();

    bool hasFiles() const noexcept              { return ! loadedFiles.isEmpty(); }

    /** Whether the TF method can be the sweep: the loaded files carry a sweep
        identity in their folder's manifest. */
    bool isSweepAvailable() const noexcept      { return sweepAvailable; }

    /** The calibration settings.micCalSource selects. */
    const fxme::MicCalibration& activeMicCal() const;
    bool hasFolderMicCal() const noexcept       { return folderMicCal.isValid(); }

    /** The dBFS -> dB SPL offset (the folder's calibration, else the
        machine-wide one) and the loaded run's stimulus level from the
        manifest. False when either is unknown. */
    bool getSplContext (float& offsetDb, float& levelDb) const;

private:
    AnalysisEngine::SweepInfo currentSweepInfo() const;

    juce::Array<juce::File> loadedFiles, subFiles;
    MeasurementFolderInfo folderInfo;       // manifest next to the loaded files
    fxme::MicCalibration folderMicCal;      // embedded in it
    bool sweepAvailable = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AnalysisSession)
};

} // namespace smt
