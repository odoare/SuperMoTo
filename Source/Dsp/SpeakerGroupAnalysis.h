/*
  ------------------------------------------------------------------------------
    SpeakerGroupAnalysis.h

    Multi-speaker extension of AnalysisEngine: owns one AnalysisEngine per
    speaker in the group plus one for the subwoofer, all sharing the same
    correction settings (window size, smoothing, correction level, max boost,
    phase type, analysis range, crossover, sub polarity — pushed uniformly via
    forEachEngine). Each entry's own measurement set gives its propagation
    delay; computeAlignment() derives the per-entry delay that time-aligns the
    whole group on the most-distant driver, then folds that delay into each
    speaker's existing subwoofer phase-alignment (AnalysisEngine::setTimeAlignMs),
    and also derives each speaker's suggested level-matching trim (mid-band
    corrected level vs the quietest speaker — see computeAlignment's doc).
    exportSpeakerIR() then designs and exports one SPEAKER's correction IR;
    finalizeApply() writes delay + FIR onto each assigned output from the
    results and returns a markdown report. The subwoofer never gets a
    correction FIR: above its real passband a broadband measurement is just
    noise (the sent/recorded cross-spectrum has no coherent content there), so
    fitting/boosting an inverse filter to it would be both pointless and
    potentially harmful. The sub only ever contributes a time-alignment delay.

    Most methods are file-based (not real time) and expected on the message
    thread, EXCEPT exportSpeakerIR(): it's deliberately const and touches only
    its own entry's engine (read-only) and the filesystem, so callers may run
    one per speaker on a background thread (e.g. fxme::BackgroundTaskRunner)
    as long as nothing else concurrently touches the same entry — see
    GroupAnalysisComponent for the intended pattern (disable UI, dispatch,
    call finalizeApply() on the message thread once every job has finished).

    Author: Olivier Doaré, github.com/odoare
    Licenced under the GNU LGPL Version 3.0
    SPDX-License-Identifier: LGPL-3.0-or-later
  ------------------------------------------------------------------------------
*/

#pragma once

#include <JuceHeader.h>
#include "AnalysisEngine.h"
#include "MatrixEngine.h"
#include "../Model/ConfigModel.h"
#include <memory>
#include <vector>

namespace smt
{

class SpeakerGroupAnalysis
{
public:
    static constexpr int maxSpeakers = 16;

    // Band used for inter-speaker level matching: mid-band, where the level
    // reading is robust — above the room's modal region, below the range
    // where loudspeaker directivity and mic orientation dominate. 500 Hz to
    // 2 kHz is the band SMPTE ST 2095-1 specifies (as band-limited pink
    // noise) for calibrating channel levels; CTA-2034-A similarly rates
    // loudspeaker sensitivity over a mid-band average (300 Hz - 3 kHz).
    static constexpr float levelMatchLowHz  = 500.0f;
    static constexpr float levelMatchHighHz = 2000.0f;

    struct Entry
    {
        Entry() : engine (std::make_unique<AnalysisEngine>()) {}

        std::unique_ptr<AnalysisEngine> engine;
        juce::String label;
        juce::Array<juce::File> files;
        int assignedOutput = -1;       // -1 = none
        float alignedDelayMs = 0.0f;   // set by computeAlignment()
        float bandLevelDb = 0.0f;      // in-band corrected level, set by computeAlignment()
        float suggestedTrimDb = 0.0f;  // <= 0, relative to the quietest speaker

        bool hasData() const noexcept { return engine->hasData(); }
    };

    // All maxSpeakers slots exist for the object's whole lifetime (so a slot's
    // loaded files/engine survive the user shrinking then re-growing the
    // count); setNumSpeakers only changes how many are considered "active"
    // (used by loadSubFiles/computeAlignment/finalizeApply/forEachEngine).
    SpeakerGroupAnalysis()
    {
        speakers.resize ((size_t) maxSpeakers);
        for (int i = 0; i < maxSpeakers; ++i)
            speakers[(size_t) i].label = "Speaker " + juce::String (i + 1);
        sub.label = "Sub";
    }

    //==========================================================================
    void setNumSpeakers (int n)                         { activeCount = juce::jlimit (1, maxSpeakers, n); }
    int getNumSpeakers() const noexcept                 { return activeCount; }

    /** Whether this group has a subwoofer at all. Off by default doesn't clear
        any already-loaded sub data — it just excludes the sub from
        computeAlignment() and finalizeApply() (see below), so flipping it back
        on resumes using whatever was loaded before. */
    void setSubEnabled (bool enabled) noexcept          { subEnabled = enabled; }
    bool isSubEnabled() const noexcept                  { return subEnabled; }

    //==========================================================================
    // Export naming. Everything an "Apply & export" run writes is named from
    // one optional file prefix, so several alignments (different groups, rooms
    // or takes) can live in the same folder without overwriting each other —
    // which matters beyond tidiness: finalizeApply() points each output's FIR
    // at the wav it exported, so a same-named re-export would silently swap
    // the filter under a previously applied output.

    /** "<prefix>_" for a non-empty prefix, empty otherwise. */
    static juce::String namePrefix (const juce::String& filePrefix)
    {
        const auto p = filePrefix.trim();
        return p.isEmpty() ? juce::String() : p + "_";
    }

    /** Speaker i's correction IR: "<prefix>_speaker<N>_correction.wav". The
        single source of truth for the name, shared by exportSpeakerIR() (which
        writes it) and finalizeApply() (which points the output at it). */
    static juce::File irFileFor (const juce::File& directory, const juce::String& filePrefix, int i)
    {
        return directory.getChildFile (namePrefix (filePrefix) + "speaker"
                                       + juce::String (i + 1) + "_correction.wav");
    }

    /** The markdown report: "<prefix>_report.md". */
    static juce::File reportFileFor (const juce::File& directory, const juce::String& filePrefix)
    {
        return directory.getChildFile (namePrefix (filePrefix) + "report.md");
    }

    //==========================================================================
    Entry& speaker (int i) noexcept                     { return speakers[(size_t) i]; }
    const Entry& speaker (int i) const noexcept         { return speakers[(size_t) i]; }
    Entry& subEntry() noexcept                          { return sub; }
    const Entry& subEntry() const noexcept              { return sub; }

    /** Loads speaker i's own multi-position measurement set. */
    int loadSpeakerFiles (int i, const juce::Array<juce::File>& files)
    {
        auto& s = speakers[(size_t) i];
        s.files = files;
        return s.engine->loadFiles (files);
    }

    /** Loads the shared subwoofer measurement set: analyzed on its own engine
        (so it gets its own correction, like any other speaker), and also fed
        to every already-loaded speaker's loadSubFiles() for the existing
        crossover phase-alignment integration. */
    int loadSubFiles (const juce::Array<juce::File>& files)
    {
        sub.files = files;
        const int ok = sub.engine->loadFiles (files);

        for (int i = 0; i < activeCount; ++i)
            if (speakers[(size_t) i].hasData())
                speakers[(size_t) i].engine->loadSubFiles (files);

        return ok;
    }

    /** Applies fn (AnalysisEngine&, bool isSub) to every engine in the group
        (speakers + sub) — used to push the shared correction-design settings
        (window size, smoothing, level, boost, phase type, range, crossover,
        sub polarity, mic cal) uniformly. The isSub flag lets the caller cap
        the sub's own analysis range instead of using the full-range speakers'
        one — see the class doc: the sub gets no correction FIR at all, but
        its (unexported) preview curve should still be confined to its real
        passband rather than the noise above it. */
    template <typename Fn>
    void forEachEngine (Fn&& fn)
    {
        for (int i = 0; i < activeCount; ++i)
            fn (*speakers[(size_t) i].engine, false);
        fn (*sub.engine, true);
    }

    /** Computes each loaded entry's (speakers + sub) delay relative to the
        most-distant one — the farthest driver gets 0 ms, everything else is
        pushed back to match it, so all delays end up non-negative. Then
        re-derives each speaker's subwoofer phase-alignment from its own
        applied delay (AnalysisEngine::setTimeAlignMs), since that alignment
        assumes the delay that will actually be applied physically.

        Also computes the LEVEL matching between the speakers (sub excluded —
        its level is a crossover-balance question, and its passband sits below
        the matching band anyway): each loaded speaker's corrected in-band
        level over levelMatchLowHz..levelMatchHighHz (see the constants' doc
        for the SMPTE ST 2095-1 rationale), and from it the suggested trim
        relative to the QUIETEST speaker — always <= 0 dB (attenuate the
        louder channels down to it, preserving headroom; the same convention
        as the delays, where the most-distant driver is the one left
        untouched). Purely informational: shown in the UI and the report,
        never written to the outputs. */
    void computeAlignment()
    {
        float maxDelay = 0.0f;
        bool any = false;
        auto consider = [&] (const Entry& e)
        {
            if (! e.hasData())
                return;
            maxDelay = any ? juce::jmax (maxDelay, e.engine->getPropagationDelayMs())
                           : e.engine->getPropagationDelayMs();
            any = true;
        };
        for (int i = 0; i < activeCount; ++i)
            consider (speakers[(size_t) i]);
        if (subEnabled)
            consider (sub);

        if (! any)
            return;

        auto apply = [&] (Entry& e)
        {
            if (! e.hasData())
            {
                e.alignedDelayMs = 0.0f;
                return;
            }
            e.alignedDelayMs = juce::jlimit (0.0f, smt::maxDelayMs,
                                             maxDelay - e.engine->getPropagationDelayMs());
        };
        for (int i = 0; i < activeCount; ++i)
        {
            auto& s = speakers[(size_t) i];
            apply (s);
            if (s.hasData() && s.engine->hasSub())
                s.engine->setTimeAlignMs (s.alignedDelayMs);
        }
        if (subEnabled)
            apply (sub);

        // Level matching (speakers only, see the method doc).
        float minLevel = 0.0f;
        bool anyLevel = false;
        for (int i = 0; i < activeCount; ++i)
        {
            auto& s = speakers[(size_t) i];
            if (! s.hasData())
            {
                s.bandLevelDb = 0.0f;
                s.suggestedTrimDb = 0.0f;
                continue;
            }
            s.bandLevelDb = s.engine->getBandLevelDb (levelMatchLowHz, levelMatchHighHz);
            minLevel = anyLevel ? juce::jmin (minLevel, s.bandLevelDb) : s.bandLevelDb;
            anyLevel = true;
        }
        for (int i = 0; i < activeCount; ++i)
        {
            auto& s = speakers[(size_t) i];
            s.suggestedTrimDb = s.hasData() ? minLevel - s.bandLevelDb : 0.0f;
        }
    }

    //==========================================================================
    struct ApplyResult
    {
        int numApplied = 0;
        juce::String report;
        juce::File reportFile;      // where the report was written
        juce::String error;
    };

    /** Optional figures for the report: the paths of already-rendered PNGs,
        RELATIVE to the report file, per entry (an empty string means "no
        figure for this one"). Rendering them means painting components, which
        is message-thread work and so belongs to the GUI layer, not here — see
        Components/ReportFigures.h; this struct is just the plumbing that
        carries the results into the markdown. */
    struct ReportFigures
    {
        std::vector<juce::String> responsePng;      // indexed by speaker
        std::vector<juce::String> irPng;
        juce::String subResponsePng, subIrPng;

        juce::String speakerResponse (int i) const
        {
            return i >= 0 && i < (int) responsePng.size() ? responsePng[(size_t) i] : juce::String();
        }
        juce::String speakerIr (int i) const
        {
            return i >= 0 && i < (int) irPng.size() ? irPng[(size_t) i] : juce::String();
        }
    };

    /** Background-safe half of "Apply & export": renders and writes speaker
        i's correction IR to irFileFor (directory, filePrefix, i). Returns
        false without touching the filesystem if the speaker has no data or
        isn't assigned to an output. Touches only this entry's own (const)
        engine and the filesystem — safe to call from a background thread, one
        job per speaker, PROVIDED nothing else touches the same speaker's
        engine concurrently (disable its controls while a batch is running).
        Call finalizeApply() on the MESSAGE THREAD once every speaker's export
        has been attempted, with the SAME prefix (the sub needs no equivalent —
        it never gets a FIR, see the class doc). */
    bool exportSpeakerIR (int i, const juce::File& directory, int firLengthSamples,
                          const juce::String& filePrefix = {}) const
    {
        const auto& e = speakers[(size_t) i];
        if (! e.hasData() || e.assignedOutput < 0)
            return false;
        return e.engine->exportCorrectionIR (irFileFor (directory, filePrefix, i), firLengthSamples);
    }

    /** Message-thread finalization: given exportOk[i] = whether
        exportSpeakerIR(i, ...) succeeded (exportOk.size() must equal
        getNumSpeakers()), writes delay (+ FIR, for successfully-exported
        speakers) onto each assigned output, writes the sub's delay-only
        entry, calls matrixEngine.updateFirFiles() once if anything changed,
        and writes the markdown report to reportFileFor (directory, filePrefix),
        alongside the wavs exportSpeakerIR already wrote (pass it the same
        prefix). */
    ApplyResult finalizeApply (const std::vector<bool>& exportOk, const juce::File& directory,
                               int firLengthSamples, ConfigModel& configModel,
                               MatrixEngine& matrixEngine,
                               const juce::String& filePrefix = {},
                               const ReportFigures& figures = {}) const
    {
        ApplyResult result;

        if (! directory.isDirectory())
        {
            result.error = "Not a valid directory.";
            return result;
        }

        result.report << "# SuperMoTo multi-speaker alignment report\n\n"
                       << juce::Time::getCurrentTime().toString (true, true) << "\n\n"
                       << "## Group settings\n\n"
                       << "- Window size: " << sub.engine->getWindowSize() << " samples\n"
                       << "- Smoothing: " << smoothingLabel (sub.engine->getSmoothingLow())
                       << " (LF) / " << smoothingLabel (sub.engine->getSmoothingHigh()) << " (HF)\n"
                       << "- Correction level: " << juce::String (sub.engine->getCorrectionLevel(), 2) << "\n"
                       << "- Max boost: " << juce::String (sub.engine->getMaxBoostDb(), 1) << " dB\n"
                       << "- Analysis range: " << juce::String (sub.engine->getAnalysisLowHz(), 0)
                       << " Hz - " << juce::String (sub.engine->getAnalysisHighHz(), 0) << " Hz\n"
                       << "- Crossover: " << juce::String (sub.engine->getCrossoverHz(), 0) << " Hz"
                       << (sub.engine->getSubPolarityInverted() ? " (sub inverted)" : "") << "\n"
                       << "- Phase type: "
                       << (sub.engine->getPhaseType() == AnalysisEngine::PhaseType::minimum
                               ? "Minimum phase" : "Linear phase") << "\n"
                       << "- FIR length: " << firLengthSamples << " samples\n"
                       << "- Level-match band: " << juce::String (levelMatchLowHz, 0)
                       << " Hz - " << juce::String (levelMatchHighHz, 0)
                       << " Hz (per SMPTE ST 2095-1)\n";
        if (filePrefix.trim().isNotEmpty())
            result.report << "- File prefix: `" << filePrefix.trim() << "`\n";
        result.report << "\n## Speakers\n\n";

        auto reportFiles = [&] (const Entry& e)
        {
            result.report << "- Files (" << e.files.size() << " position(s)):\n";
            for (const auto& f : e.files)
                result.report << "    - `" << f.getFullPathName() << "`\n";
        };

        // Figures (when the caller rendered any), embedded right after an
        // entry's numbers so they are present whether or not it ended up
        // assigned to an output.
        auto reportFigureLinks = [&] (const juce::String& label,
                                      const juce::String& responsePng,
                                      const juce::String& irPng)
        {
            if (responsePng.isEmpty() && irPng.isEmpty())
                return;

            // A prefix may contain spaces; CommonMark needs those targets
            // wrapped in angle brackets to stay one link.
            auto target = [] (const juce::String& path)
            {
                return path.containsChar (' ') ? "<" + path + ">" : path;
            };
            auto figure = [&] (const juce::String& path, const char* what)
            {
                if (path.isNotEmpty())
                    result.report << "![" << label << juce::String::fromUTF8 (" \xe2\x80\x94 ")
                                   << what << "](" << target (path) << ")\n\n";
            };

            result.report << "\n";
            figure (responsePng, "frequency response");
            figure (irPng, "impulse responses");
        };

        for (int i = 0; i < activeCount; ++i)
        {
            const auto& e = speakers[(size_t) i];
            result.report << "### " << e.label << "\n\n";

            if (! e.hasData())
            {
                result.report << "- Not measured.\n\n";
                continue;
            }
            reportFiles (e);
            result.report << "- Measured propagation delay: "
                           << juce::String (e.engine->getPropagationDelayMs(), 2) << " ms\n"
                           << "- Applied (aligned) delay: "
                           << juce::String (e.alignedDelayMs, 2) << " ms\n"
                           << "- In-band level ("
                           << juce::String (levelMatchLowHz, 0) << " Hz - "
                           << juce::String (levelMatchHighHz, 0) << " Hz, corrected): "
                           << juce::String (e.bandLevelDb, 1) << " dB\n"
                           << "- Suggested level-matching trim: "
                           << juce::String (e.suggestedTrimDb, 1)
                           << " dB (relative to the quietest speaker; "
                              "informational, not written to the output)\n";

            reportFigureLinks (e.label, figures.speakerResponse (i), figures.speakerIr (i));

            if (e.assignedOutput < 0)
            {
                result.report << "- Not assigned to an output; nothing written.\n\n";
                continue;
            }

            const bool ok = (size_t) i < exportOk.size() && exportOk[(size_t) i];
            if (! ok)
            {
                result.report << "- Export FAILED for output " << (e.assignedOutput + 1) << ".\n\n";
                continue;
            }

            const auto irFile = irFileFor (directory, filePrefix, i);
            auto settings = configModel.getOutput (e.assignedOutput);
            settings.delayMs = e.alignedDelayMs;
            settings.firPath = irFile.getFullPathName();
            settings.firOn = true;
            configModel.setOutput (e.assignedOutput, settings);

            result.report << "- Assigned to output " << (e.assignedOutput + 1)
                           << ", exported `" << irFile.getFileName() << "`\n\n";
            ++result.numApplied;
        }

        // Sub: delay only, never a FIR (see class doc).
        result.report << "### " << sub.label << "\n\n";
        if (! subEnabled)
        {
            result.report << "- Subwoofer disabled for this group; excluded from alignment "
                              "and nothing written.\n\n";
        }
        else if (! sub.hasData())
        {
            result.report << "- Not measured.\n\n";
        }
        else
        {
            reportFiles (sub);
            result.report << "- Measured propagation delay: "
                           << juce::String (sub.engine->getPropagationDelayMs(), 2) << " ms\n"
                           << "- Applied (aligned) delay: "
                           << juce::String (sub.alignedDelayMs, 2) << " ms\n";

            reportFigureLinks (sub.label, figures.subResponsePng, figures.subIrPng);

            if (sub.assignedOutput < 0)
            {
                result.report << "- Not assigned to an output; nothing written.\n\n";
            }
            else
            {
                auto settings = configModel.getOutput (sub.assignedOutput);
                settings.delayMs = sub.alignedDelayMs;
                configModel.setOutput (sub.assignedOutput, settings);
                result.report << "- Assigned to output " << (sub.assignedOutput + 1)
                               << " \xe2\x80\x94 time-alignment delay only "
                                  "(no correction FIR is designed for the subwoofer).\n\n";
                ++result.numApplied;
            }
        }

        if (result.numApplied > 0)
            matrixEngine.updateFirFiles();

        result.reportFile = reportFileFor (directory, filePrefix);
        result.reportFile.replaceWithText (result.report);
        return result;
    }

private:
    static juce::String smoothingLabel (float octaveFraction)
    {
        if (octaveFraction <= 0.0f)
            return "off";
        return "1/" + juce::String (juce::roundToInt (1.0f / octaveFraction)) + " oct";
    }

    std::vector<Entry> speakers;
    Entry sub;
    int activeCount = 2;
    bool subEnabled = true;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SpeakerGroupAnalysis)
};

} // namespace smt
