/*
  ------------------------------------------------------------------------------
    GroupAnalysisSession.h

    Everything Group analysis is working on, kept by the processor so that it
    outlives the editor: the speaker group and its engines, the shared
    correction-design settings, the loaded measurement folder and its run
    selection, and the background runner with the status and progress of the
    batch it is running. GroupAnalysisComponent is only a view of it.

    Closing the editor therefore loses nothing, and a batch in flight (a folder
    load, a re-analysis, an Apply & export) runs to completion without it: the
    runner's callbacks land here, not in a component, and a view open at the
    time, or opened later, is told through Listener.

    Message thread only, like the component it was taken out of. The one
    exception is the background jobs, each of which touches exactly one engine
    (see buildReloadJobs), while isBusy() is true and every view keeps its
    engine-reading controls disabled.

    Not saved with the host session: the working state lives as long as the
    plugin instance.

    Author: Olivier Doaré, github.com/odoare
    Licenced under the GNU AGPL Version 3.0, or commercial terms (LICENSE.md)
    SPDX-License-Identifier: AGPL-3.0-or-later OR LicenseRef-FXME-Commercial
  ------------------------------------------------------------------------------
*/

#pragma once

#include <JuceHeader.h>
#include <set>
#include <vector>
#include "ConfigModel.h"
#include "../Dsp/MatrixEngine.h"
#include "../Dsp/MeasurementFolder.h"
#include "../Dsp/SpeakerGroupAnalysis.h"

namespace smt
{

/** The Group analysis controls, as values. The view writes a field and calls
    GroupAnalysisSession::settingsChanged(); the session pushes them into the
    engines when it can. Combo boxes are stored by item id, editable ones by
    their text. */
struct GroupAnalysisSettings
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
    bool phaseLimited = true;           // linear phase: phase corrected through the crossover region only
    int firLength = 4096;
    juce::String crossoverText = "80 Hz";
    bool subInverted = false;
    float subTrimMs = 0.0f;

    // Export.
    juce::String prefix;
    bool figures = true;

    // Display only.
    int previewId = 1;                  // speaker i + 1, or count + 1 for the sub
    int levelRefId = 1;                 // Normalized, Absolute dB, dB SPL
    bool showIr = false;                // View: impulse response
    float viewLowHz = 0.0f;             // plot frequency window, 0 = its default
    float viewHighHz = 0.0f;
};

class GroupAnalysisSession
{
public:
    GroupAnalysisSession (ConfigModel& configModel, MatrixEngine& matrixEngine);
    ~GroupAnalysisSession();

    //==========================================================================
    /** Told whenever the session changes on its own account: a batch starts or
        finishes, a folder or a run selection is applied, the status changes.
        Not for the view's own edits, which it has already shown. Message
        thread. */
    struct Listener
    {
        virtual ~Listener() = default;
        virtual void groupSessionChanged() = 0;
    };

    void addListener (Listener* l)          { listeners.add (l); }
    void removeListener (Listener* l)       { listeners.remove (l); }

    //==========================================================================
    SpeakerGroupAnalysis group;
    GroupAnalysisSettings settings;

    /** A batch owns the engines: from the moment it is started to the end of
        its completion (figures and finalizeApply included). While it is true,
        a view must not read any engine. */
    bool isBusy() const noexcept            { return busy; }

    /** The running batch's progress, 0..1, for a juce::ProgressBar to poll. */
    double progress = 0.0;

    const juce::String& getStatus() const noexcept  { return status; }
    void setStatus (const juce::String& text);

    //==========================================================================
    // Settings

    /** The view changed a field of `settings`. Engines follow at the next
        applySettings(). */
    void settingsChanged() noexcept         { settingsDirty = true; }

    /** Pushes the settings into every engine and the sub trim into the group.
        Returns false, and keeps them pending, while a batch owns the engines;
        the batch applies them when it ends. Cheap when nothing moved: the
        engine setters return early on unchanged values. */
    bool applySettings();

    /** The window size and the TF method both invalidate the analysis, so
        each re-analyzes every loaded set in the background. */
    void setWindowSize (int size);
    void setSweepMethod (bool sweep);

    void setNumSpeakers (int n);
    void setSubEnabled (bool enabled);

    /** Whether the sweep method is offered: the files the first speaker holds
        carry a sweep identity in the folder's manifest. */
    bool isSweepAvailable() const noexcept  { return sweepAvailable; }

    /** The calibration settings.micCalSource selects. */
    const fxme::MicCalibration& activeMicCal() const;
    bool hasFolderMicCal() const noexcept   { return folderMicCal.isValid(); }

    //==========================================================================
    // Loading

    /** Loads a measurement folder (scan, run selection defaults, files,
        assignments, mic cal, TF method) and analyzes it in the background.
        `keepPrefix` leaves settings.prefix alone even when it is empty (the
        user is typing one). */
    void loadMeasurementFolder (const juce::File& dir, bool keepPrefix);

    /** Files picked by hand, for one speaker or for the sub. Both withdraw the
        run selection until the next folder load. */
    void loadSpeakerFiles (int speaker, const juce::Array<juce::File>& files);
    void loadSubFiles (const juce::Array<juce::File>& files);

    /** Re-analyzes every loaded set at the current settings. */
    void reanalyzeAll();

    //==========================================================================
    // Measurement runs of the loaded folder

    /** A run that contributed files to the scan, or the files no run lists
        (number 0). Runs that left nothing Group analysis loads (System runs,
        which record inputs) are not offered at all. */
    struct SelectableRun
    {
        int number = 0;
        const MeasurementRunInfo* info = nullptr;   // into the loaded folder; null for 0
        juce::StringArray channels;                 // "1", "2", ..., "sub"
    };

    std::vector<SelectableRun> selectableRuns() const;
    int keptRunCount() const;
    bool canSelectRuns() const noexcept             { return runsSelectable; }
    const std::set<int>& getExcludedRuns() const noexcept { return excludedRuns; }

    /** Why this exclusion cannot be applied, or empty when it can: every
        loaded speaker, and the sub when the folder has one, must keep a file. */
    juce::String refusalFor (const std::set<int>& excluded) const;

    /** Re-loads every folder row with these runs left out, in the background,
        and re-runs Compute alignment afterwards if it had been run. */
    void applyRunSelection (const std::set<int>& excluded);

    static juce::String modeLabel (const juce::String& mode);
    static juce::String timeLabel (const juce::String& iso8601);

    //==========================================================================
    // Alignment and export

    void computeAlignment();

    /** Exports every assigned speaker's correction into `dir`, writes delays,
        FIRs and the sub's polarity onto the outputs, and the report (with
        figures when settings.figures). Finishes even if the editor closes. */
    void applyAndExport (const juce::File& dir);

    //==========================================================================
    // Display context

    /** The sweep identity for one file set, from the folder manifest (invalid,
        hence Welch, for files without one). */
    AnalysisEngine::SweepInfo sweepInfoFor (const juce::Array<juce::File>& files) const;

    /** The dBFS -> dB SPL offset (the folder's calibration, else the
        machine-wide one) and the set's stimulus level from the manifest.
        False when either is unknown. */
    bool getSplContext (const juce::Array<juce::File>& files,
                        float& offsetDb, float& levelDb) const;

private:
    void notify();
    void startBatch (std::vector<fxme::BackgroundTaskRunner::Job> jobs,
                     const juce::String& runningText,
                     std::function<void()> onDone);
    void endBatch (const juce::String& finalStatus);
    std::vector<fxme::BackgroundTaskRunner::Job> buildReloadJobs();
    void pushSettingsTo (AnalysisEngine& e, bool isSub) const;
    MeasurementFolderInfo runInfoForSub (const juce::Array<juce::File>& subFiles) const;
    std::set<int> defaultExcludedRuns() const;
    juce::StringArray describeExcludedRuns() const;
    void updateSweepAvailability (const juce::Array<juce::File>& firstSpeakerFiles,
                                  bool selectWhenAvailable);
    void forgetFolderRuns();

    /** What adoptFolderInfo() took from the manifest beside a hand-picked set. */
    struct AdoptedFolderInfo
    {
        bool micCal = false;         // the folder carried one and it is now in force
        bool methodChanged = false;  // the TF method moved, so every other set is stale
    };
    AdoptedFolderInfo adoptFolderInfo (const juce::Array<juce::File>& files);

    // The sub's analysis range is capped here regardless of the shared Range
    // control; see pushSettingsTo in the .cpp for why, and for the 500 Hz
    // floor that makes the effective cap higher than it reads.
    static constexpr float subMaxRangeHz = 300.0f;

    ConfigModel& model;
    MatrixEngine& engine;

    // Created by the first batch: its pool starts threads, which a plugin
    // instance that never runs Group analysis (or a host's plugin scan)
    // should not pay for. Destroyed before `group`, whose engines its jobs use.
    std::unique_ptr<fxme::BackgroundTaskRunner> runner;
    bool busy = false;
    bool settingsDirty = true;          // push once before the first use
    juce::String status;

    fxme::MicCalibration folderMicCal;      // embedded in the loaded folder
    MeasurementFolderInfo folderInfo;       // its manifest (SPL cal, runs)
    bool sweepAvailable = false;

    // The loaded folder as scanned (every run), and the runs left out of the
    // files the rows hold.
    MeasurementFolderContents loadedFolder;
    juce::File loadedFolderDir;
    std::set<int> excludedRuns;
    bool runsSelectable = false;        // false once a row or the sub is loaded by hand
    bool alignmentComputed = false;     // Compute alignment run since the folder loaded

    juce::ListenerList<Listener> listeners;

    JUCE_DECLARE_WEAK_REFERENCEABLE (GroupAnalysisSession)
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GroupAnalysisSession)
};

} // namespace smt
