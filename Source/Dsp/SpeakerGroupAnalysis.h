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
    potentially harmful. The sub only ever contributes a time-alignment delay
    and its polarity.

    Most methods are file-based (not real time) and expected on the message
    thread, EXCEPT exportSpeakerIR(): it's deliberately const and touches only
    its own entry's engine (read-only) and the filesystem, so callers may run
    one per speaker on a background thread (e.g. fxme::BackgroundTaskRunner)
    as long as nothing else concurrently touches the same entry — see
    GroupAnalysisSession for the intended pattern (disable UI, dispatch,
    call finalizeApply() on the message thread once every job has finished).

    Author: Olivier Doaré, github.com/odoare
    Licenced under the GNU AGPL Version 3.0, or commercial terms (LICENSE.md)
    SPDX-License-Identifier: AGPL-3.0-or-later OR LicenseRef-FXME-Commercial
  ------------------------------------------------------------------------------
*/

#pragma once

#include <JuceHeader.h>
#include "AnalysisEngine.h"
#include "MatrixEngine.h"
#include "MeasurementFolder.h"
#include "../Model/ConfigModel.h"
#include <algorithm>
#include <cmath>
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

    // Band used for the SUBWOOFER's level suggestion: half an octave either
    // side of the crossover, i.e. [fx/sqrt2, fx*sqrt2]. That is where the two
    // sources overlap and where their relative level decides whether the
    // crossover region sums flat, sags or humps. The mid-band above is useless
    // here: the sub has no output in it.
    static constexpr float subMatchHalfWidth = 1.41421356f;   // sqrt(2)

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
        // Crossover-band main-vs-sub offset from this speaker's own phase slope
        // (AnalysisEngine::estimateMainSubOffsetMs). A different measurement
        // from alignedDelayMs, not a refinement of it — see
        // doc/note_on_delay_processing. Set by computeAlignment(); 0 with no sub.
        float crossoverOffsetMs = 0.0f;

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
        crossover phase-alignment integration.

        Each speaker is paired with the sub by pairSubFiles(): by measurement
        run when `info` is the manifest of the folder both sets come from, in
        load order otherwise (pass a default info for files picked by hand). A
        speaker whose engine does not already hold its files in the paired
        order is re-analyzed in that order first, since AnalysisEngine anchors
        the i-th sub file on its i-th curve. */
    int loadSubFiles (const juce::Array<juce::File>& files,
                      const MeasurementFolderInfo& info = {})
    {
        sub.files = files;
        const int ok = sub.engine->loadFiles (files);

        for (int i = 0; i < activeCount; ++i)
        {
            auto& s = speakers[(size_t) i];
            if (! s.hasData())
                continue;

            const auto pairing = pairSubFiles (s.files, files, info);
            if (! holdsInOrder (*s.engine, pairing.speaker))
                s.engine->loadFiles (pairing.speaker);
            s.engine->loadSubFiles (pairing.sub);
        }

        return ok;
    }

    /** Whether the engine's curves are exactly these files, in this order
        (AnalysisEngine names each curve after its file). */
    static bool holdsInOrder (const AnalysisEngine& engine, const juce::Array<juce::File>& files)
    {
        if (engine.getNumCurves() != files.size())
            return false;
        for (int i = 0; i < files.size(); ++i)
            if (engine.getCurveName (i) != files[i].getFileName())
                return false;
        return true;
    }

    /** One line per measurement run left out of the loaded files, listed in
        the report's group settings. Empty when nothing was left out. */
    void setExcludedRunsDescription (const juce::StringArray& lines)   { excludedRuns = lines; }
    const juce::StringArray& getExcludedRunsDescription() const noexcept { return excludedRuns; }

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

    //==========================================================================
    // Subwoofer trim. The group's applied delays come from arrival times; the
    // main-vs-sub offset that actually governs summation at the crossover is a
    // different quantity (a group delay), and the two disagree by the sub's own
    // filter delay. This one signed control shifts the subwoofer against the
    // whole group so the user can set that offset by ear or from the
    // crossover-band estimate, without breaking the alignment identity below.
    // Rationale and derivation: doc/note_on_delay_processing.

    /** Extra delay given to the subwoofer, in ms. Positive pushes the sub
        later; negative brings it forward, and when that would take its output
        delay below zero the whole group is pushed back instead (see
        computeAlignment) so the relative alignment is always achievable.
        Re-derives the alignment, so the corrections follow immediately. */
    void setSubTrimMs (float ms)
    {
        ms = juce::jlimit (-smt::maxDelayMs, smt::maxDelayMs, ms);
        if (ms == subTrimMs)
            return;
        subTrimMs = ms;
        computeAlignment();
    }

    float getSubTrimMs() const noexcept                 { return subTrimMs; }

    /** The trim that would put every speaker's assumed main-vs-sub offset on
        the crossover-band estimate instead of the arrival-time one. Working
        through computeAlignment's algebra, a speaker's assumed offset is
        T_i = (d_sub - d_i) - subTrim, so matching it to that speaker's
        estimate tau_i needs subTrim = (d_sub - d_i) - tau_i: exactly the
        disagreement between the two estimators. Returned as the median over
        the loaded speakers (it should be near-constant across them, since the
        geometric part cancels; a wide spread means one of the two estimates is
        unreliable). Zero when there is no sub or no speaker carries one. */
    float getSuggestedSubTrimMs() const
    {
        if (! subEnabled || ! sub.hasData())
            return 0.0f;

        const float dSub = sub.engine->getPropagationDelayMs();
        std::vector<float> v;
        v.reserve ((size_t) activeCount);
        for (int i = 0; i < activeCount; ++i)
        {
            const auto& s = speakers[(size_t) i];
            if (s.hasData() && s.engine->hasSub())
                v.push_back ((dSub - s.engine->getPropagationDelayMs())
                             - s.engine->estimateMainSubOffsetMs());
        }
        if (v.empty())
            return 0.0f;

        auto mid = v.begin() + (long) (v.size() / 2);
        std::nth_element (v.begin(), mid, v.end());
        return juce::jlimit (-smt::maxDelayMs, smt::maxDelayMs, *mid);
    }

    /** Whether the crossover-band estimate behind getSuggestedSubTrimMs() can
        be trusted at the current settings. It is read off a COMPLEX-smoothed
        average, and a smoothing window of width beta octaves at frequency f
        rotates the phasor of a residual delay tau by about
        2*pi*tau*beta*f*ln2 across the window; as that approaches pi the vector
        average collapses and the phase slope becomes meaningless. Checked at
        the top of the fit band (2 x crossover), where it is worst. */
    bool isCrossoverEstimateReliable() const
    {
        if (! subEnabled || ! sub.hasData())
            return false;

        // The fraction in force at the top of the fit band, NOT the larger of
        // the two end-point settings: they are anchors at 100 Hz and 10 kHz and
        // the value between them is log-interpolated, so at 170 Hz the HF
        // setting contributes about a tenth. Using the maximum made heavy HF
        // smoothing — which does nothing at a subwoofer crossover — declare the
        // estimate unusable.
        const float f    = 2.0f * sub.engine->getCrossoverHz();
        const float beta = sub.engine->smoothingFractionAt ((double) f);
        if (beta <= 0.0f)
            return true;                    // no smoothing here, nothing to collapse

        const float tau  = std::abs (getSuggestedSubTrimMs()) * 0.001f;
        const float rot  = 2.0f * juce::MathConstants<float>::pi
                             * tau * beta * f * std::log (2.0f);
        return rot < 0.5f * juce::MathConstants<float>::pi;   // half the collapse point
    }

    //==========================================================================
    /** Computes each loaded entry's (speakers + sub) delay relative to the
        most-distant one — the farthest driver gets 0 ms, everything else is
        pushed back to match it, so all delays end up non-negative. Then
        re-derives each speaker's subwoofer phase-alignment from the delay it
        will be given RELATIVE TO THE SUB (AnalysisEngine::setTimeAlignMs),
        since that is what the all-pass has to leave as residual.

        The relative part matters. setTimeAlignMs is the main's delay measured
        against the subwoofer, so the value to push is a_i - a_sub, not a_i:
        the sub receives its own bulk delay here too, and passing a_i alone
        designs the crossover all-pass for an offset wrong by exactly a_sub —
        zero only when the sub happens to be the most distant driver, and up to
        a full phase inversion at the crossover when it is not. See
        doc/note_on_delay_processing for the derivation.

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

        // Only differences matter acoustically, so the whole set is free to
        // slide in time; the useful choice is the one with the least latency,
        // i.e. the one where the earliest entry sits at zero. Compute the raw
        // (possibly negative) delays first, then shift everything by the
        // minimum. Without this the trim only ever ADDS: asking for the sub
        // 9 ms later would leave the mains at 30.6 ms and push the sub to 9.2,
        // where subtracting 9.2 from the mains gives the same alignment for
        // 9.2 ms less latency.
        const bool haveSub = subEnabled && sub.hasData();

        auto rawDelay = [&] (const Entry& e, float extra)
        {
            return maxDelay - e.engine->getPropagationDelayMs() + extra;
        };

        float groupShift = 0.0f;
        bool  first = true;
        auto considerRaw = [&] (const Entry& e, float extra)
        {
            if (! e.hasData())
                return;
            const float r = rawDelay (e, extra);
            groupShift = first ? r : juce::jmin (groupShift, r);
            first = false;
        };
        for (int i = 0; i < activeCount; ++i)
            considerRaw (speakers[(size_t) i], 0.0f);
        if (haveSub)
            considerRaw (sub, subTrimMs);
        groupShift = first ? 0.0f : -groupShift;    // bring the earliest to zero

        auto apply = [&] (Entry& e, float extra)
        {
            if (! e.hasData())
            {
                e.alignedDelayMs = 0.0f;
                return;
            }
            e.alignedDelayMs = juce::jlimit (0.0f, smt::maxDelayMs,
                                             rawDelay (e, extra) + groupShift);
        };

        // The sub first: every speaker's assumed offset is measured against it.
        if (haveSub)
            apply (sub, subTrimMs);
        else
            sub.alignedDelayMs = 0.0f;

        // Excluded from the alignment means it receives no delay, so the
        // speakers' offsets are then measured against zero.
        const float aSub = haveSub ? sub.alignedDelayMs : 0.0f;

        for (int i = 0; i < activeCount; ++i)
        {
            auto& s = speakers[(size_t) i];
            apply (s, 0.0f);
            s.crossoverOffsetMs = s.hasData() && s.engine->hasSub()
                                      ? s.engine->estimateMainSubOffsetMs() : 0.0f;
            if (s.hasData() && s.engine->hasSub())
                s.engine->setTimeAlignMs (s.alignedDelayMs - aSub);
        }

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

        computeSubLevelMatch();
    }

    /** Subwoofer level suggestion: how much to trim the sub so that, through
        the crossover region, it plays at the same level as a main will once
        that main's correction and its own suggested trim are applied.

        Two asymmetries with the speakers' version, both deliberate. The band is
        centred on the crossover rather than the mid-band, because that is the
        only place the sub and the mains both have output. And the sub's level
        is read UNCORRECTED (getBandLevelDb's second argument): the group never
        designs a correction FIR for the subwoofer, so the level that matters is
        the one it already has, whereas each main is measured as it will play
        after correction plus its own trim.

        Matched against ONE main, not their sum. With several mains driven
        together the acoustic sum through the crossover is higher (about 3 dB
        for an uncorrelated pair, up to 6 dB for correlated content), so a user
        running a stereo pair into a mono sub will usually want a few dB less
        than this figure. Informational either way: never written to an output. */
    void computeSubLevelMatch()
    {
        sub.bandLevelDb = 0.0f;
        sub.suggestedTrimDb = 0.0f;

        if (! subEnabled || ! sub.hasData())
            return;

        const float fx = sub.engine->getCrossoverHz();
        if (fx <= 0.0f)
            return;

        const float lo = fx / subMatchHalfWidth;
        const float hi = fx * subMatchHalfWidth;

        // The sub as it will actually play: no correction FIR is exported for it.
        sub.bandLevelDb = sub.engine->getBandLevelDb (lo, hi, false);

        // The mains as they will play: corrected, plus the trim each is being
        // told to apply. Averaged in POWER across the loaded speakers, so the
        // reference is what one main plays, not what several sum to.
        double acc = 0.0;
        int n = 0;
        for (int i = 0; i < activeCount; ++i)
        {
            const auto& sp = speakers[(size_t) i];
            if (! sp.hasData())
                continue;
            const double lvl = (double) sp.engine->getBandLevelDb (lo, hi, true)
                             + (double) sp.suggestedTrimDb;
            acc += std::pow (10.0, lvl / 10.0);
            ++n;
        }
        if (n == 0)
            return;

        const double mainsDb = 10.0 * std::log10 (juce::jmax (1.0e-12, acc / (double) n));
        sub.suggestedTrimDb = (float) (mainsDb - (double) sub.bandLevelDb);
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
        speakers) onto each assigned output, writes the sub's delay and
        polarity, calls matrixEngine.updateFirFiles() once if anything changed,
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

        // Every shared setting below reads off sub.engine, which is fine because
        // they are pushed uniformly — EXCEPT the analysis range. The sub's high
        // edge is deliberately capped below the shared Range control (see
        // GroupAnalysisSession::subMaxRangeHz and the class doc), so reading
        // the range from the sub would report the sub's narrower band as if it
        // were the group's. The speakers carry the range the correction was
        // actually designed with.
        const AnalysisEngine& rangeEngine = activeCount > 0 ? *speakers[0].engine
                                                            : *sub.engine;

        result.report << "# SuperMoTo multi-speaker alignment report\n\n"
                       << juce::Time::getCurrentTime().toString (true, true) << "\n\n"
                       << "## Group settings\n\n"
                       << "- Window size: " << sub.engine->getWindowSize() << " samples\n"
                       << "- Smoothing: " << smoothingLabel (sub.engine->getSmoothingLow())
                       << " (LF) / " << smoothingLabel (sub.engine->getSmoothingHigh()) << " (HF)\n"
                       << "- Correction level: " << juce::String (sub.engine->getCorrectionLevel(), 2) << "\n"
                       << "- Max boost: " << juce::String (sub.engine->getMaxBoostDb(), 1) << " dB\n"
                       << "- Analysis range: " << juce::String (rangeEngine.getAnalysisLowHz(), 0)
                       << " Hz - " << juce::String (rangeEngine.getAnalysisHighHz(), 0)
                       << " Hz (speakers; the subwoofer's own range is narrower, "
                          "reported with it below)\n"
                       << "- Crossover: " << juce::String (sub.engine->getCrossoverHz(), 0) << " Hz"
                       << (sub.engine->getSubPolarityInverted() ? " (sub inverted)" : "") << "\n"
                       << "- Sub trim: " << juce::String (subTrimMs, 2)
                           << " ms (offset applied to the subwoofer against the group; "
                              "the correction assumes each speaker's delay measured "
                              "against the sub)\n"
                       << "- Phase type: "
                       << (sub.engine->getPhaseType() == AnalysisEngine::PhaseType::minimum
                               ? "Minimum phase" : "Linear phase") << "\n"
                       << "- FIR length: " << firLengthSamples << " samples\n"
                       << "- Level-match band: " << juce::String (levelMatchLowHz, 0)
                       << " Hz - " << juce::String (levelMatchHighHz, 0)
                       << " Hz (per SMPTE ST 2095-1)\n";
        if (filePrefix.trim().isNotEmpty())
            result.report << "- File prefix: `" << filePrefix.trim() << "`\n";
        if (! excludedRuns.isEmpty())
        {
            result.report << "- Measurement runs left out of the analysis:\n";
            for (const auto& line : excludedRuns)
                result.report << "    - " << line << "\n";
        }
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

        // Sub: delay and polarity, never a FIR (see class doc).
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
            result.report << "- Analysis range: "
                           << juce::String (sub.engine->getAnalysisLowHz(), 0) << " Hz - "
                           << juce::String (sub.engine->getAnalysisHighHz(), 0)
                           << " Hz (capped below the group's: above a subwoofer's real "
                              "passband a broadband measurement is just noise)\n"
                           << "- Crossover-band level ("
                           << juce::String (sub.engine->getCrossoverHz() / subMatchHalfWidth, 0)
                           << " - "
                           << juce::String (sub.engine->getCrossoverHz() * subMatchHalfWidth, 0)
                           << " Hz, uncorrected): "
                           << juce::String (sub.bandLevelDb, 1) << " dB\n"
                           << "- Suggested level trim: " << juce::String (sub.suggestedTrimDb, 1)
                           << " dB (to match ONE corrected main through the crossover; with "
                              "several mains driven together their sum is higher, so allow a "
                              "few dB less. Informational, not written to the output)\n"
                           << "- Measured propagation delay: "
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
                // The polarity goes with the delay, both ways: the design
                // assumed the sub at exactly this sign against the Dry
                // measurement, which the output chain does not touch. A
                // crosspoint inverted on a route into this output would still
                // flip it back.
                const bool inverted = sub.engine->getSubPolarityInverted();
                auto settings = configModel.getOutput (sub.assignedOutput);
                settings.delayMs = sub.alignedDelayMs;
                settings.phaseInvert = inverted;
                configModel.setOutput (sub.assignedOutput, settings);
                result.report << "- Assigned to output " << (sub.assignedOutput + 1)
                               << " \xe2\x80\x94 time-alignment delay and polarity "
                                  "(no correction FIR is designed for the subwoofer).\n"
                               << "- Output polarity: "
                               << (inverted ? "inverted" : "normal")
                               << " (Invert sub). A crosspoint polarity switch on a route "
                                  "into this output inverts it again.\n\n";
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
    float subTrimMs = 0.0f;     // user offset on the sub's applied delay
    juce::StringArray excludedRuns;     // report lines, see setExcludedRunsDescription

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SpeakerGroupAnalysis)
};

} // namespace smt
