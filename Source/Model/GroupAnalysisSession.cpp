/*
  ------------------------------------------------------------------------------
    GroupAnalysisSession.cpp

    Author: Olivier Doaré, github.com/odoare
    Licenced under the GNU AGPL Version 3.0, or commercial terms (LICENSE.md)
    SPDX-License-Identifier: AGPL-3.0-or-later OR LicenseRef-FXME-Commercial
  ------------------------------------------------------------------------------
*/

#include "GroupAnalysisSession.h"
#include "../AppSettings.h"
#include "../Components/ReportFigures.h"

namespace smt
{

GroupAnalysisSession::GroupAnalysisSession (ConfigModel& configModel, MatrixEngine& matrixEngine)
    : model (configModel), engine (matrixEngine)
{
}

// The runner is declared after the group, so it is destroyed first: its
// destructor waits for the job in flight, which may still be using an engine.
GroupAnalysisSession::~GroupAnalysisSession() = default;

void GroupAnalysisSession::notify()
{
    listeners.call ([] (Listener& l) { l.groupSessionChanged(); });
}

void GroupAnalysisSession::setStatus (const juce::String& text)
{
    status = text;
    notify();
}

//==============================================================================
// Batches

void GroupAnalysisSession::startBatch (std::vector<fxme::BackgroundTaskRunner::Job> jobs,
                                       const juce::String& runningText,
                                       std::function<void()> onDone)
{
    busy = true;
    progress = 0.0;
    status = runningText;
    notify();       // views disable every control that reaches an engine

    if (runner == nullptr)
        runner = std::make_unique<fxme::BackgroundTaskRunner>();

    runner->runJobs (std::move (jobs),
                    [this] (float p) { progress = (double) p; },
                    std::move (onDone));
}

void GroupAnalysisSession::endBatch (const juce::String& finalStatus)
{
    busy = false;
    status = finalStatus;

    // A setting moved while the batch owned the engines was kept for now.
    if (settingsDirty)
        applySettings();

    notify();
}

/** One background job per engine with files, each touching only that engine:
    a speaker's files in the order pairSubFiles gives, followed by its sub
    pairing, and the sub on its own. Never group.loadSubFiles(), which fans out
    across every speaker's engine and would race with the per-speaker jobs.
    Each engine's sweep identity is set here, on the message thread. */
std::vector<fxme::BackgroundTaskRunner::Job> GroupAnalysisSession::buildReloadJobs()
{
    const auto subFiles = group.subEntry().files;
    const bool haveSub = ! subFiles.isEmpty();
    const auto info = haveSub ? runInfoForSub (subFiles) : MeasurementFolderInfo();

    std::vector<fxme::BackgroundTaskRunner::Job> jobs;
    for (int i = 0; i < group.getNumSpeakers(); ++i)
    {
        const auto files = group.speaker (i).files;
        if (files.isEmpty())
            continue;
        group.speaker (i).engine->setSweepInfo (sweepInfoFor (files));

        const auto pairing = pairSubFiles (files, haveSub ? subFiles : juce::Array<juce::File>(), info);
        jobs.push_back ([this, i, pairing, haveSub]
        {
            auto& e = *group.speaker (i).engine;
            e.loadFiles (pairing.speaker);
            if (haveSub)
                e.loadSubFiles (pairing.sub);
        });
    }
    if (haveSub)
    {
        group.subEntry().engine->setSweepInfo (sweepInfoFor (subFiles));
        jobs.push_back ([this, subFiles] { group.subEntry().engine->loadFiles (subFiles); });
    }
    return jobs;
}

//==============================================================================
// Settings

// Pushes the shared correction-design settings onto one engine: fanned out to
// every engine in the group by applySettings(), and pushed right before
// loading a fresh engine's files, so an engine touched for the first time
// reflects the current settings rather than AnalysisEngine's own defaults.
//
// isSub caps the analysis range to subMaxRangeHz regardless of the shared
// Range control: the sub never gets a correction FIR exported/applied (see
// SpeakerGroupAnalysis), but its curves are still computed and previewable,
// and above a subwoofer's real passband a measurement is just noise, so
// band-limiting keeps that preview (and the otherwise-unused correction the
// engine still computes internally) meaningful rather than fitting noise up
// to the shared speaker range's top end.
//
// NOTE: the effective cap is 500 Hz, not 300. AnalysisEngine::setAnalysisRange
// floors the high edge at max(lowHz * 1.1, 500), so the 300 asked for here is
// raised to 500 for every usable low edge. The sub's preview band is therefore
// 500 Hz wide at the top, and the group report states the engine's actual
// value rather than this one. Lowering that floor is the only way to make this
// constant bite.
void GroupAnalysisSession::pushSettingsTo (AnalysisEngine& e, bool isSub) const
{
    e.setTfMethod (settings.sweepMethod ? AnalysisEngine::TfMethod::sweep
                                        : AnalysisEngine::TfMethod::welch);
    e.setWindowSize (settings.windowSize);
    static const float fractions[] = { 0.0f, 1.0f / 24.0f, 1.0f / 12.0f,
                                       1.0f / 6.0f, 1.0f / 3.0f, 1.0f / 2.0f, 1.0f };
    e.setSmoothing (fractions[juce::jlimit (0, 6, settings.smoothLowId  - 1)],
                    fractions[juce::jlimit (0, 6, settings.smoothHighId - 1)]);
    e.setCorrectionLevel (settings.correctionLevel);
    e.setMaxBoostDb (settings.maxBoostDb);
    e.setPhaseType (settings.minimumPhase ? AnalysisEngine::PhaseType::minimum
                                          : AnalysisEngine::PhaseType::linear);
    const float lowHz  = settings.lowFreqText.getFloatValue();
    const float highHz = settings.highFreqText.getFloatValue();
    e.setAnalysisRange (lowHz, isSub ? juce::jmin (highHz, subMaxRangeHz) : highHz);
    e.setCrossoverHz (settings.crossoverText.getFloatValue());
    e.setSubPolarityInverted (settings.subInverted);
    e.setMicCalibration (activeMicCal());
}

bool GroupAnalysisSession::applySettings()
{
    if (busy)
    {
        settingsDirty = true;
        return false;
    }

    group.forEachEngine ([this] (AnalysisEngine& e, bool isSub) { pushSettingsTo (e, isSub); });
    // A no-op when the value has not moved, so this costs nothing when another
    // setting brought us here.
    group.setSubTrimMs (settings.subTrimMs);
    settingsDirty = false;
    return true;
}

void GroupAnalysisSession::setWindowSize (int size)
{
    if (busy)
        return;
    settings.windowSize = size;
    applySettings();
    reanalyzeAll();
}

void GroupAnalysisSession::setSweepMethod (bool sweep)
{
    if (busy)
        return;
    settings.sweepMethod = sweep;
    applySettings();
    reanalyzeAll();
}

void GroupAnalysisSession::setNumSpeakers (int n)
{
    group.setNumSpeakers (n);
    // Slots coming back into the group may have missed setting changes made
    // while they were out of it.
    settingsChanged();
    applySettings();
}

void GroupAnalysisSession::setSubEnabled (bool enabled)
{
    group.setSubEnabled (enabled);
}

const fxme::MicCalibration& GroupAnalysisSession::activeMicCal() const
{
    static const fxme::MicCalibration none;
    switch (settings.micCalSource)
    {
        case 2:  return folderMicCal;
        case 3:  return none;
        default: return sharedMicCalibration();
    }
}

void GroupAnalysisSession::updateSweepAvailability (const juce::Array<juce::File>& firstSpeakerFiles,
                                                    bool selectWhenAvailable)
{
    sweepAvailable = sweepInfoFor (firstSpeakerFiles).isValid();
    if (selectWhenAvailable)
        settings.sweepMethod = sweepAvailable;
    else if (! sweepAvailable)
        settings.sweepMethod = false;
}

//==============================================================================
// Loading

void GroupAnalysisSession::loadMeasurementFolder (const juce::File& dir, bool keepPrefix)
{
    if (busy)
        return;

    const auto contents = scanMeasurementFolder (dir);
    if (! contents.ok)
    {
        setStatus (contents.error);
        return;
    }

    // Suggest the folder's name as the export prefix, but never overwrite one
    // the user has already typed.
    if (settings.prefix.trim().isEmpty() && ! keepPrefix)
        settings.prefix = juce::File::createLegalFileName (dir.getFileName());

    // Adopt the folder's embedded mic calibration when it carries one (still
    // overridable via the source selector). Before the pushes below, so the
    // engines get it.
    folderInfo = contents.info;
    folderMicCal.clear();
    const bool haveFolderCal = folderInfo.hasMicCal()
        && folderMicCal.loadFromText (folderInfo.micCalText, folderInfo.micCalName);
    if (haveFolderCal)
        settings.micCalSource = 2;
    else if (settings.micCalSource == 2)
        settings.micCalSource = 1;

    // FIR runs measure the speaker through its correction, not the bare
    // speaker, so they start left out (see defaultExcludedRuns). The
    // unfiltered scan is kept for the run selection.
    loadedFolder = contents;
    loadedFolderDir = dir;
    excludedRuns = defaultExcludedRuns();
    // Without a run log (a folder from before measurement.xml) every file
    // would sit in one row that cannot be unchecked.
    runsSelectable = ! contents.info.runs.empty() && ! selectableRuns().empty();
    alignmentComputed = false;
    const auto used = withoutRuns (contents, excludedRuns);
    group.setExcludedRunsDescription (describeExcludedRuns());

    const int n = juce::jmin ((int) used.speakers.size(), SpeakerGroupAnalysis::maxSpeakers);
    const bool haveSub = used.sub.channelNumber >= 0 && ! used.sub.files.isEmpty();

    // Reflect the folder's contents: the sub switch on when the folder has a
    // subwoofer, off when it doesn't.
    group.setSubEnabled (haveSub);

    // A trim from a previous set means nothing for this one.
    settings.subTrimMs = 0.0f;
    group.setSubTrimMs (0.0f);

    // Files and assignments before the row count, so no view refresh can show
    // the new count with the previous folder's rows.
    for (int i = 0; i < n; ++i)
    {
        const auto& c = used.speakers[(size_t) i];
        auto& entry = group.speaker (i);
        entry.files = c.files;
        entry.assignedOutput = c.channelNumber - 1;
    }
    if (n > 0)
        group.setNumSpeakers (n);

    // Sweep runs get the Farina deconvolution automatically. Decided before
    // the pushes below, which read the method.
    updateSweepAvailability (n > 0 ? used.speakers[0].files : juce::Array<juce::File>(), true);

    for (int i = 0; i < n; ++i)
        pushSettingsTo (*group.speaker (i).engine, false);

    // A folder without a sub also clears the previous one, files and
    // analysis, so the reload below cannot pair the new speakers with a
    // subwoofer from another folder.
    if (haveSub)
    {
        group.subEntry().files = used.sub.files;
        group.subEntry().assignedOutput = used.sub.channelNumber - 1;
        pushSettingsTo (*group.subEntry().engine, true);
    }
    else
    {
        group.subEntry().files.clear();
        group.subEntry().engine->clear();
    }
    settingsChanged();      // the rest of the group follows when the load ends

    auto jobs = buildReloadJobs();
    if (jobs.empty())
    {
        setStatus ("No speaker measurement files found in " + dir.getFileName() + ".");
        return;
    }

    const auto runningText = "Loading " + juce::String ((int) jobs.size())
                           + " measurement set(s) from " + dir.getFileName() + "...";
    startBatch (std::move (jobs), runningText, [this, n, haveSub, haveFolderCal]
    {
        const int leftOut = (int) selectableRuns().size() - keptRunCount();
        endBatch (juce::String (n) + " speaker(s)" + (haveSub ? " + sub" : "")
                  + " loaded from the measurement folder."
                  + (haveFolderCal ? " Embedded mic cal applied." : "")
                  + (leftOut > 0 ? " " + juce::String (leftOut) + " FIR run(s) left out, see Runs."
                                 : juce::String()));
    });
}

// Files picked by hand still come from a measurement folder, and that folder's
// manifest is as much a part of the analysis as the wavs are: it carries the
// microphone calibration the design divides out, and the sweep identity that
// decides between Welch and the Farina deconvolution. loadMeasurementFolder()
// adopts both from its own scan, and AnalysisSession::loadFiles() does the
// same in the Analysis pane; the two per-row loaders below used to do neither,
// so the same files loaded through them were analyzed with whatever the pane
// was last left on -- a different microphone calibration and a different
// estimator from the Analysis pane, on the same measurements. Nothing in
// either pane showed that; the curves simply disagreed at the band edges.
//
// A folder with no manifest adopts nothing rather than clearing what is in
// force, so a row picked out of a folder from before measurement.xml does not
// silently drop the calibration the rest of the group is using.
GroupAnalysisSession::AdoptedFolderInfo
GroupAnalysisSession::adoptFolderInfo (const juce::Array<juce::File>& files)
{
    AdoptedFolderInfo adopted;
    if (files.isEmpty())
        return adopted;

    auto info = readMeasurementFolderInfo (files[0].getParentDirectory());
    if (! info.manifestFound)
        return adopted;

    folderInfo = std::move (info);
    folderMicCal.clear();
    adopted.micCal = folderInfo.hasMicCal()
        && folderMicCal.loadFromText (folderInfo.micCalText, folderInfo.micCalName);

    // Still overridable from the source selector, like the folder load's.
    if (adopted.micCal)
        settings.micCalSource = 2;
    else if (settings.micCalSource == 2)
        settings.micCalSource = 1;      // the folder that carried one is gone

    const bool wasSweep = settings.sweepMethod;
    updateSweepAvailability (files, true);
    adopted.methodChanged = settings.sweepMethod != wasSweep;
    return adopted;
}

void GroupAnalysisSession::loadSpeakerFiles (int speaker, const juce::Array<juce::File>& files)
{
    if (busy || files.isEmpty() || speaker < 0 || speaker >= SpeakerGroupAnalysis::maxSpeakers)
        return;

    forgetFolderRuns();         // this row no longer comes from the folder
    const auto adopted = adoptFolderInfo (files);
    applySettings();            // both are shared, so every engine gets them

    std::vector<fxme::BackgroundTaskRunner::Job> jobs;
    juce::String running;

    if (adopted.methodChanged)
    {
        // The manifest asks for the other estimator, and the sets already
        // loaded were analyzed with the one it replaces, so they go again too.
        // buildReloadJobs() reads the rows' files, hence the assignment here,
        // on the message thread, before any job runs.
        group.speaker (speaker).files = files;
        jobs = buildReloadJobs();
        running = "Re-analyzing " + juce::String ((int) jobs.size())
                + " measurement set(s) with the "
                + juce::String (settings.sweepMethod ? "sweep" : "Welch") + " method...";
    }
    else
    {
        group.speaker (speaker).engine->setSweepInfo (sweepInfoFor (files));
        jobs.push_back ([this, speaker, files] { group.loadSpeakerFiles (speaker, files); });
        running = group.speaker (speaker).label + ": analyzing...";
    }

    const juce::String cal = adopted.micCal ? " Embedded mic cal applied." : juce::String();

    startBatch (std::move (jobs), running,
                [this, speaker, cal]
                {
                    endBatch (group.speaker (speaker).label + ": "
                              + juce::String (group.speaker (speaker).engine->getNumCurves())
                              + " file(s) analyzed." + cal);
                });
}

void GroupAnalysisSession::loadSubFiles (const juce::Array<juce::File>& files)
{
    if (busy || files.isEmpty())
        return;

    forgetFolderRuns();         // the sub no longer comes from the folder
    const auto adopted = adoptFolderInfo (files);
    applySettings();

    std::vector<fxme::BackgroundTaskRunner::Job> jobs;
    juce::String running;

    if (adopted.methodChanged)
    {
        group.subEntry().files = files;
        jobs = buildReloadJobs();       // re-pairs every speaker with the sub as well
        running = "Re-analyzing " + juce::String ((int) jobs.size())
                + " measurement set(s) with the "
                + juce::String (settings.sweepMethod ? "sweep" : "Welch") + " method...";
    }
    else
    {
        group.subEntry().engine->setSweepInfo (sweepInfoFor (files));

        // Pairs each speaker with the sub by run when both sets are files of
        // the loaded folder. A copy, taken here: the job runs elsewhere.
        const auto info = runInfoForSub (files);
        jobs.push_back ([this, files, info] { group.loadSubFiles (files, info); });
        running = "Sub: analyzing...";
    }

    const juce::String cal = adopted.micCal ? " Embedded mic cal applied." : juce::String();

    startBatch (std::move (jobs), running,
                [this, cal]
                {
                    endBatch ("Sub: " + juce::String (group.subEntry().engine->getNumCurves())
                              + " file(s) analyzed." + cal);
                });
}

// Window-size and TF-method changes clear each engine's data, so every loaded
// file set is re-run at the new setting, one job per engine (see
// buildReloadJobs, so no two jobs ever touch the same engine).
void GroupAnalysisSession::reanalyzeAll()
{
    if (busy)
        return;

    auto jobs = buildReloadJobs();
    if (jobs.empty())
        return;

    const auto runningText = "Re-analyzing " + juce::String ((int) jobs.size())
                           + " measurement set(s)...";
    startBatch (std::move (jobs), runningText, [this] { endBatch ("Re-analysis complete."); });
}

/** The manifest to pair speakers with the sub by run, when the sub's files and
    every active speaker's files come from the loaded folder, and an empty one
    otherwise (load-order pairing): run numbers are looked up by file name, and
    names repeat from one folder to the next. */
MeasurementFolderInfo GroupAnalysisSession::runInfoForSub (const juce::Array<juce::File>& subFiles) const
{
    auto inLoadedFolder = [this] (const juce::Array<juce::File>& files)
    {
        if (loadedFolderDir == juce::File())
            return false;
        for (const auto& f : files)
            if (f.getParentDirectory() != loadedFolderDir)
                return false;
        return true;
    };

    if (! inLoadedFolder (subFiles))
        return {};
    for (int i = 0; i < group.getNumSpeakers(); ++i)
        if (! inLoadedFolder (group.speaker (i).files))
            return {};
    return folderInfo;
}

//==============================================================================
// Measurement runs

std::vector<GroupAnalysisSession::SelectableRun> GroupAnalysisSession::selectableRuns() const
{
    std::vector<SelectableRun> result;
    if (! loadedFolder.ok)
        return result;

    const int numSpeakers = juce::jmin ((int) loadedFolder.speakers.size(),
                                        SpeakerGroupAnalysis::maxSpeakers);
    auto channelsOf = [&] (int number)
    {
        juce::StringArray channels;
        auto has = [&] (const juce::Array<juce::File>& files)
        {
            for (const auto& f : files)
                if (loadedFolder.info.runNumberOf (f.getFileName()) == number)
                    return true;
            return false;
        };
        for (int i = 0; i < numSpeakers; ++i)
            if (has (loadedFolder.speakers[(size_t) i].files))
                channels.add (juce::String (loadedFolder.speakers[(size_t) i].channelNumber));
        if (has (loadedFolder.sub.files))
            channels.add ("sub");
        return channels;
    };

    for (const auto& run : loadedFolder.info.runs)
        if (auto channels = channelsOf (run.info.number); ! channels.isEmpty())
            result.push_back ({ run.info.number, &run.info, channels });

    if (auto channels = channelsOf (0); ! channels.isEmpty())
        result.push_back ({ 0, nullptr, channels });

    return result;
}

/** FIR runs measure the speaker through its correction, which would bias a
    correction designed from them. System runs would too, but they record
    inputs, which the folder scan never loads. Both start left out, unless that
    would leave a channel with no file, in which case nothing is (a folder of
    verification runs only is still loadable as it is). */
std::set<int> GroupAnalysisSession::defaultExcludedRuns() const
{
    std::set<int> excluded;
    for (const auto& run : selectableRuns())
        if (run.info != nullptr && (run.info->mode == "fir" || run.info->mode == "system"))
            excluded.insert (run.number);

    return refusalFor (excluded).isEmpty() ? excluded : std::set<int>();
}

int GroupAnalysisSession::keptRunCount() const
{
    int kept = 0;
    for (const auto& run : selectableRuns())
        kept += excludedRuns.count (run.number) == 0 ? 1 : 0;
    return kept;
}

juce::String GroupAnalysisSession::refusalFor (const std::set<int>& excluded) const
{
    const auto used = withoutRuns (loadedFolder, excluded);
    const int numSpeakers = juce::jmin ((int) used.speakers.size(),
                                        SpeakerGroupAnalysis::maxSpeakers);
    for (int i = 0; i < numSpeakers; ++i)
        if (used.speakers[(size_t) i].files.isEmpty())
            return "Channel " + juce::String (used.speakers[(size_t) i].channelNumber)
                 + " would have no measurement left";

    if (! loadedFolder.sub.files.isEmpty() && used.sub.files.isEmpty())
        return "The sub would have no measurement left";

    return {};
}

juce::String GroupAnalysisSession::modeLabel (const juce::String& mode)
{
    if (mode == "dry")    return "Dry";
    if (mode == "fir")    return "FIR";
    if (mode == "system") return "System";
    return mode;
}

juce::String GroupAnalysisSession::timeLabel (const juce::String& iso8601)
{
    return iso8601.isEmpty() ? juce::String()
                             : juce::Time::fromISO8601 (iso8601).toString (true, true, false, true);
}

/** The report's lines for the runs currently left out. */
juce::StringArray GroupAnalysisSession::describeExcludedRuns() const
{
    juce::StringArray lines;
    for (const auto& run : selectableRuns())
    {
        if (excludedRuns.count (run.number) == 0)
            continue;
        if (run.info == nullptr)
        {
            lines.add ("Files not listed in measurement.xml (channels "
                       + run.channels.joinIntoString (", ") + ")");
            continue;
        }
        juce::String line = "Run " + juce::String (run.number) + ", "
                          + timeLabel (run.info->time) + ", "
                          + modeLabel (run.info->mode) + ", "
                          + run.info->signal + ", channels "
                          + run.channels.joinIntoString (", ");
        if (run.info->comment.isNotEmpty())
            line << ": \"" << run.info->comment << "\"";
        lines.add (line);
    }
    return lines;
}

/** Files loaded by hand no longer come from the folder, and re-applying a run
    selection would overwrite them, so the selection is withdrawn until the
    next folder load. The report keeps listing what was left out of the rows
    that did come from it. */
void GroupAnalysisSession::forgetFolderRuns()
{
    runsSelectable = false;
}

void GroupAnalysisSession::applyRunSelection (const std::set<int>& newExcluded)
{
    if (! runsSelectable || busy)
        return;
    if (newExcluded == excludedRuns)
    {
        setStatus ("Run selection unchanged.");
        return;
    }
    if (refusalFor (newExcluded).isNotEmpty())
        return;     // the dialog refuses it already; a guard, not a message

    excludedRuns = newExcluded;
    group.setExcludedRunsDescription (describeExcludedRuns());
    const auto used = withoutRuns (loadedFolder, excludedRuns);

    const int n = juce::jmin ((int) used.speakers.size(), SpeakerGroupAnalysis::maxSpeakers);
    for (int i = 0; i < n; ++i)
        group.speaker (i).files = used.speakers[(size_t) i].files;
    if (! loadedFolder.sub.files.isEmpty())
        group.subEntry().files = used.sub.files;

    // The estimator follows what is left: leaving out every sweep run takes
    // the Farina method away. Otherwise the user's choice stands.
    updateSweepAvailability (n > 0 ? used.speakers[0].files : juce::Array<juce::File>(), false);
    applySettings();

    auto jobs = buildReloadJobs();
    if (jobs.empty())
    {
        notify();
        return;
    }

    startBatch (std::move (jobs), "Re-analyzing with the selected runs...", [this]
    {
        // Before endBatch, which notifies: the rows must show the new
        // alignment, never the one of the previous selection.
        if (alignmentComputed)
            group.computeAlignment();

        endBatch (juce::String (keptRunCount()) + " of "
                  + juce::String ((int) selectableRuns().size())
                  + " run(s) kept, group re-analyzed"
                  + (alignmentComputed ? " and alignment recomputed." : "."));
    });
}

//==============================================================================
// Alignment and export

void GroupAnalysisSession::computeAlignment()
{
    if (busy)
        return;

    applySettings();        // a trim set moments ago must be in first
    group.computeAlignment();
    alignmentComputed = true;
    setStatus ("Alignment and level match computed.");
}

void GroupAnalysisSession::applyAndExport (const juce::File& dir)
{
    if (busy || dir == juce::File())
        return;

    // Whatever is exported must reflect the settings as they stand now.
    applySettings();

    const int firLength = settings.firLength;
    const int n = group.getNumSpeakers();

    // Snapshot on the message thread: the export jobs and finalizeApply must
    // all use the same prefix.
    const auto prefix = juce::File::createLegalFileName (settings.prefix.trim());
    const bool wantFigures = settings.figures;

    // One job per speaker, each rendering and writing only its own IR
    // (background-safe: exportSpeakerIR touches only that speaker's own const
    // engine and the filesystem). The results are applied to the model on the
    // message thread in finalizeApply(), once every job has finished.
    auto exportOk = std::make_shared<std::vector<char>> ((size_t) n, 0);
    std::vector<fxme::BackgroundTaskRunner::Job> jobs;
    jobs.reserve ((size_t) n);
    for (int i = 0; i < n; ++i)
        jobs.push_back ([this, i, dir, firLength, prefix, exportOk]
        {
            (*exportOk)[(size_t) i] = group.exportSpeakerIR (i, dir, firLength, prefix) ? 1 : 0;
        });

    startBatch (std::move (jobs), "Exporting...",
        [this, dir, firLength, prefix, wantFigures, exportOk]
        {
            const std::vector<bool> ok (exportOk->begin(), exportOk->end());

            // Back on the message thread with every job done, so painting the
            // engines into figures is safe here. The session stays busy until
            // finish() has run. A named local rather than an init-capture: MSVC
            // does not carry an outer lambda's captured `this` into a nested
            // init-capture's initializer.
            const juce::WeakReference<GroupAnalysisSession> weak (this);

            auto finish = [weak, dir, firLength, prefix, wantFigures, ok]
            {
                auto* s = weak.get();
                if (s == nullptr)
                    return;

                SpeakerGroupAnalysis::ReportFigures figs;
                if (wantFigures)
                    figs = renderGroupFigures (s->group, dir, prefix, firLength);

                const auto result = s->group.finalizeApply (ok, dir, firLength, s->model,
                                                            s->engine, prefix, figs);
                s->endBatch (result.error.isNotEmpty()
                                 ? result.error
                                 : juce::String (result.numApplied)
                                     + " speaker(s) applied & exported to "
                                     + dir.getFileName() + "/ as "
                                     + SpeakerGroupAnalysis::namePrefix (prefix)
                                     + "speaker*_correction.wav + "
                                     + result.reportFile.getFileName()
                                     + (wantFigures ? " (with figures)" : ""));
            };

            // Rendering blocks the message thread for a moment, so let the
            // "rendering" status paint first.
            if (wantFigures)
            {
                setStatus ("Rendering figures...");
                juce::MessageManager::callAsync (std::move (finish));
            }
            else
            {
                finish();
            }
        });
}

//==============================================================================
// Display context

AnalysisEngine::SweepInfo GroupAnalysisSession::sweepInfoFor (const juce::Array<juce::File>& files) const
{
    AnalysisEngine::SweepInfo s;
    if (files.isEmpty())
        return s;
    const auto it = folderInfo.fileRuns.find (files[0].getFileName());
    if (it != folderInfo.fileRuns.end() && it->second.signal == "sweep")
    {
        s.f1 = it->second.sweepF1;
        s.f2 = it->second.sweepF2;
        s.L  = it->second.sweepL;
    }
    return s;
}

bool GroupAnalysisSession::getSplContext (const juce::Array<juce::File>& files,
                                          float& offsetDb, float& levelDb) const
{
    if (folderInfo.splCalibrated)
        offsetDb = folderInfo.splOffsetDb;
    else if (isSplCalibrated())
        offsetDb = getSplOffsetDb();
    else
        return false;

    if (files.isEmpty())
        return false;
    const auto it = folderInfo.fileRuns.find (files[0].getFileName());
    if (it == folderInfo.fileRuns.end())
        return false;
    levelDb = it->second.levelDb;
    return true;
}

} // namespace smt
