/*
  ------------------------------------------------------------------------------
    ReportFigures.h

    Renders the figures embedded in Group analysis's markdown report: for each
    measured entry, a frequency-response figure (the same TransferFunctionPlot
    the pane draws — measurements, average, correction, corrected, sub and the
    harmonic-distortion traces when the sweep method was used) and an
    impulse-response figure (the measured IR and the correction IR, as the
    View selector's Impulse response mode shows them).

    The display components double as figure generators: an offscreen instance
    is filled with the same data and painted into a PNG by
    fxme::saveComponentAsPng. Figures therefore look exactly like what the
    user sees on screen, dark theme included.

    Everything a run writes is named from the export prefix — the PNGs go into
    "<prefix>_figs/" beside the report, and the returned paths are relative to
    the report, so the folder can be moved or zipped and the markdown still
    resolves.

    MESSAGE THREAD ONLY (it paints components). The engines are only read, but
    they must not be concurrently written by a background batch.

    Author: Olivier Doaré, github.com/odoare
    Licenced under the GNU LGPL Version 3.0
    SPDX-License-Identifier: LGPL-3.0-or-later
  ------------------------------------------------------------------------------
*/

#pragma once

#include <JuceHeader.h>
#include "../Dsp/SpeakerGroupAnalysis.h"
#include "../Theme.h"
#include "TransferFunctionPlot.h"

namespace smt
{

struct FigureOptions
{
    int responseWidth = 1100, responseHeight = 700;  // logical size...
    int irWidth = 1100, irHeight = 420;
    float scale = 2.0f;                              // ...times this, in pixels
};

namespace figures
{

/** Fills an offscreen plot with one engine's curves over its full frequency
    window, then fits the dB axis to them. */
inline void fillResponsePlot (const AnalysisEngine& e, TransferFunctionPlot& plot)
{
    const auto freqs = TransferFunctionPlot::freqGridFor (plot.getViewLowHz(), plot.getViewHighHz());

    TransferFunctionPlot::Data d;
    for (int i = 0; i < e.getNumCurves(); ++i)
    {
        d.curveDbs.push_back (e.getCurveDb (i, freqs));
        d.curvePhases.push_back (e.getCurvePhaseDeg (i, freqs));
    }
    d.averageDb       = e.getAverageDb (freqs);
    d.correctionDb    = e.getCorrectionDb (freqs);
    d.correctedDb     = e.getCorrectedDb (freqs);
    d.averagePhase    = e.getAveragePhaseDeg (freqs);
    d.correctionPhase = e.getCorrectionPhaseDeg (freqs);
    d.correctedPhase  = e.getCorrectedPhaseDeg (freqs);
    d.subDb           = e.getSubDb (freqs);
    d.subPhase        = e.getSubPhaseDeg (freqs);
    d.hasSub          = e.hasSub();
    d.crossoverHz     = e.getCrossoverHz();
    for (int i = 0; i < e.getNumHarmonics(); ++i)
        d.harmonicDbs.push_back (e.getHarmonicDb (i, freqs));

    plot.setData (std::move (d));
    plot.fitVerticalToData();
}

/** Renders one engine's frequency-response figure. */
inline bool renderResponseFigure (const AnalysisEngine& e, const juce::File& file,
                                  const FigureOptions& options)
{
    TransferFunctionPlot plot;
    fillResponsePlot (e, plot);
    return fxme::saveComponentAsPng (plot, file, options.responseWidth,
                                     options.responseHeight, options.scale);
}

/** Renders one engine's impulse-response figure: the measured IR, plus the
    correction IR unless withCorrection is false (the subwoofer never gets a
    correction FIR — see SpeakerGroupAnalysis). */
inline bool renderIrFigure (const AnalysisEngine& e, int firLength, bool withCorrection,
                            const juce::File& file, const FigureOptions& options)
{
    const double sr = e.getSampleRate();
    if (sr <= 0.0 || firLength <= 0)
        return false;

    const auto measured = e.renderMeasuredIR (firLength);
    if (measured.getNumSamples() <= 0)
        return false;

    const int numCh = withCorrection ? 2 : 1;
    juce::AudioBuffer<float> both (numCh, firLength);
    both.clear();
    both.copyFrom (0, 0, measured, 0, 0, juce::jmin (firLength, measured.getNumSamples()));

    if (withCorrection)
    {
        const auto correction = e.renderCorrectionIR (firLength);
        if (correction.getNumSamples() > 0)
            both.copyFrom (1, 0, correction, 0, 0, juce::jmin (firLength, correction.getNumSamples()));
    }

    fxme::WaveformDisplay wave;
    wave.setColours (SuperMoToTheme::waveformColours());
    wave.setChannelColours ({ SuperMoToTheme::curveAverage, SuperMoToTheme::master });
    wave.setChannelNames (withCorrection ? juce::StringArray { "measured", "correction" }
                                         : juce::StringArray { "measured" });
    wave.setTimeOffset ((double) (firLength / 2) / sr);      // t = 0 at the IR centre
    wave.setBuffer (both, sr);

    return fxme::saveComponentAsPng (wave, file, options.irWidth, options.irHeight, options.scale);
}

} // namespace figures

/** Renders every loaded entry's figures into "<prefix>_figs/" inside
    `directory` and returns their report-relative paths, ready for
    SpeakerGroupAnalysis::finalizeApply(). Entries with no data are skipped
    (their paths stay empty). Returns an empty set if the figure directory
    cannot be created. */
inline SpeakerGroupAnalysis::ReportFigures renderGroupFigures (
    const SpeakerGroupAnalysis& group, const juce::File& directory,
    const juce::String& filePrefix, int firLength,
    const FigureOptions& options = {})
{
    SpeakerGroupAnalysis::ReportFigures figs;

    const auto folderName = SpeakerGroupAnalysis::namePrefix (filePrefix) + "figs";
    const auto figDir = directory.getChildFile (folderName);
    if (! figDir.createDirectory())
        return figs;

    // Paths in the markdown are relative to the report, which sits in
    // `directory` — so just "<prefix>_figs/<name>.png".
    auto render = [&] (const AnalysisEngine& e, const juce::String& stem, bool withCorrection,
                       juce::String& responseOut, juce::String& irOut)
    {
        const auto responseFile = figDir.getChildFile (stem + "_response.png");
        if (figures::renderResponseFigure (e, responseFile, options))
            responseOut = folderName + "/" + responseFile.getFileName();

        const auto irFile = figDir.getChildFile (stem + "_ir.png");
        if (figures::renderIrFigure (e, firLength, withCorrection, irFile, options))
            irOut = folderName + "/" + irFile.getFileName();
    };

    const int n = group.getNumSpeakers();
    figs.responsePng.resize ((size_t) n);
    figs.irPng.resize ((size_t) n);

    for (int i = 0; i < n; ++i)
    {
        const auto& s = group.speaker (i);
        if (s.hasData())
            render (*s.engine, "speaker" + juce::String (i + 1), true,
                    figs.responsePng[(size_t) i], figs.irPng[(size_t) i]);
    }

    const auto& sub = group.subEntry();
    if (group.isSubEnabled() && sub.hasData())
        render (*sub.engine, "sub", false, figs.subResponsePng, figs.subIrPng);

    return figs;
}

} // namespace smt
