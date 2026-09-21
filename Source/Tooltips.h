/*
  ------------------------------------------------------------------------------
    Tooltips.h

    Every hover-help string in the plugin, in one place.

    Sub-namespace per pane; `shared` for anything used by more than one.

    Author: Olivier Doaré, github.com/odoare
    Licenced under the GNU AGPL Version 3.0, or commercial terms (LICENSE.md)
    SPDX-License-Identifier: AGPL-3.0-or-later OR LicenseRef-FXME-Commercial
  ------------------------------------------------------------------------------
*/

#pragma once

namespace smt::tips
{

//==============================================================================
/** Top bar and the bottom view switcher (PluginEditor). */
namespace bar
{
    inline constexpr auto config =
        "Engage this speaker configuration (A..F). The matrix view follows whichever is "
        "engaged. With Exclusive off, several can play at once and their outputs sum.";

    inline constexpr auto exclusive =
        "Exclusive: engaging a configuration releases the others. Off, configurations sum, "
        "which is how a configuration carrying only a subwoofer can be added to any of the "
        "others.";

    inline constexpr auto mute    = "Mute the master output.";
    inline constexpr auto dim     = "Dim the master output by 6 dB.";
    inline constexpr auto mono    = "Sum to mono. The quickest check for phase cancellation "
                                    "between left and right.";
    inline constexpr auto level   = "Master level (dB). Right-click to type a value.";

    inline constexpr auto collapseOff  = "Compact view: the output strip only";
    inline constexpr auto collapseMini = "Mini view: master controls and output meters";
    inline constexpr auto collapseBack = "Back to the full editor";

    inline constexpr auto editConfig =
        "Show this configuration in the matrix without engaging it, so it can be edited while "
        "another one plays.";

    inline constexpr auto ins  =
        "Number of matrix inputs (rows). Two is the stereo mixing case; raise it when the "
        "source itself is multichannel: 6 for 5.1, 8 for 7.1, 12 for a 7.1.4 immersive bed, "
        "or 4 / 9 / 16 for first / second / third-order Ambisonics. The Config tool sets it "
        "for you when you apply a layout.";
    inline constexpr auto outs = "Number of matrix outputs (columns), i.e. speakers.";

    inline constexpr auto viewMatrix   = "Routing matrix, per-output chain and the analyzer.";
    inline constexpr auto viewConfig   = "Build a standard speaker layout (stereo, 5.1, "
                                         "Ambisonics...) and write it into a configuration.";
    inline constexpr auto viewCal      = "Play test signals, record the microphone and write a "
                                         "measurement folder. Also the SPL-meter calibration.";
    inline constexpr auto viewAnalysis = "Design a correction for ONE speaker from a set of "
                                         "measurements.";
    inline constexpr auto viewGroup    = "Align and correct a whole speaker set at once, "
                                         "subwoofer included.";
    inline constexpr auto viewPresets  = "Browse, save and organise presets.";

    inline constexpr auto tooltips =
        "Show hover help on every control. Turn it off once the layout is familiar; the text "
        "is otherwise the only in-app explanation of what each control does.";
}

//==============================================================================
/** Matrix view: the grid, the output strip and the two detail editors. */
namespace mtx
{
    // ── Output editor ────────────────────────────────────────────────────────
    inline constexpr auto outPhase =
        "Invert the polarity of this output, whatever feeds it. Use it for the speaker "
        "as wired (a subwoofer against the mains) and keep the crosspoint switch for "
        "one route of one configuration. Group analysis writes it on the subwoofer "
        "output from Invert sub. Key: P.";
    inline constexpr auto outFir =
        "Apply this output's FIR correction. Greyed out until an impulse response is loaded.";
    inline constexpr auto outSpectrum =
        "Show this output's sum on the analyzer.";
    inline constexpr auto outLoadIr =
        "Load an impulse response wav as this output's correction filter. Resampled to the "
        "session rate if needed. The Analysis panes can write and assign one for you.";
    inline constexpr auto outClearIr =
        "Remove this output's impulse response. Its delay, trim and EQ are kept.";
    inline constexpr auto outDescription =
        "What this output drives, as this preset names it: \"Genelec 8030 Left\", "
        "\"Surround Right\". Shown when hovering the output in the matrix and in "
        "Measurement & calibration, and written into a measurement folder's manifest.";
    inline constexpr auto outTrim =
        "Output trim (dB). Right-click to type a value.";
    inline constexpr auto outDelay =
        "Output delay (ms), for time-aligning this speaker with the others. The Analysis panes "
        "write this value when a correction is applied. Right-click to type a value.";

    // ── Frame editor (one crosspoint) ────────────────────────────────────────
    inline constexpr auto frameActive =
        "Route this input to this output.";
    inline constexpr auto framePhase =
        "Invert the polarity of this crosspoint: one route, in this configuration only. "
        "To invert a speaker whatever feeds it, use Phase inv. on its output.";
    inline constexpr auto frameSpectrum =
        "Show this crosspoint on the analyzer.";
    inline constexpr auto frameLevel =
        "Crosspoint gain (dB). Can also be set by dragging the cell vertically in the matrix. "
        "Right-click to type a value.";

    // -- Band EQ (shared by both editors) -------------------------------
    inline constexpr auto bandOn =
        "Enable this filter band.";
    inline constexpr auto bandType =
        "Filter shape:\n"
        "Lowpass / Highpass: cut everything above / below the frequency.\n"
        "Bandpass: keep a band around it.\n"
        "Band: a peaking / notch bell, the only shape that uses Gain.";
    inline constexpr auto bandOrder =
        "Filter steepness: 2nd order is 12 dB/octave, 4th is 24 dB/octave. Ignored by the "
        "Band (bell) shape.";
    inline constexpr auto bandFreq =
        "Corner or centre frequency of this band.";
    inline constexpr auto bandQ =
        "Resonance. 0.707 is the flattest (Butterworth) response; higher is narrower and "
        "peakier.";
    inline constexpr auto bandGain =
        "Boost or cut, for the Band (bell) shape only.";
}

//==============================================================================
/** Speaker configuration tool. */
namespace cfg
{
    inline constexpr auto layout =
        "Standard layout to build. The rows below show the resulting speakers and let you "
        "assign each to a physical output before writing.";
    inline constexpr auto order =
        "Ambisonics order. The number of inputs the matrix needs is (order+1)^2: 4, 9 or 16.";
    inline constexpr auto target =
        "Which configuration (A..F) Apply writes into. Everything already in it is replaced.";
    inline constexpr auto bassManagement =
        "Route the low frequencies of every channel to the subwoofer and high-pass the mains "
        "at the crossover. Off, each speaker is sent full range and the sub gets only the LFE "
        "channel, if the layout has one.";
    inline constexpr auto crossover =
        "Bass-management crossover frequency: mains are high-passed and the subwoofer "
        "low-passed here.";
    inline constexpr auto apply =
        "Write these speakers, their routing, gains and delays into the target configuration.";
    inline constexpr auto numSpk =
        "How many loudspeakers the Ambisonics decoder feeds. More speakers give a more even "
        "decode but need at least (order+1)^2 to be well determined.";
    inline constexpr auto writeGain =
        "Also write the inverse-distance gain trim derived from each speaker's radius. Turn "
        "off to keep trims already set by the Analysis panes.";
    inline constexpr auto writeDelay =
        "Also write the delay that time-aligns each speaker to the farthest one, from its "
        "radius. Turn off to keep delays already set by the Analysis panes.";
    inline constexpr auto loadIem =
        "Import an IEM AllRADecoder .json as the decode matrix, instead of the built-in "
        "sampling decoder. The speaker directions and the matrix both come from the file.";
    inline constexpr auto rowName   = "Speaker name from the layout. Read only.";
    inline constexpr auto rowOutput = "Physical output this speaker is wired to.";
    inline constexpr auto rowGain   = "Extra gain written for this speaker, on top of anything "
                                      "the decoder computes.";
    inline constexpr auto rowRadius = "Distance from the listening position, in metres. Feeds "
                                      "the radius gain and delay above.";
    inline constexpr auto rowInput  = "Matrix input this Ambisonics component is taken from.";
    inline constexpr auto rowAz     = "Azimuth of this loudspeaker, degrees, 0 straight ahead and "
                                      "positive to the left.";
    inline constexpr auto rowEl     = "Elevation of this loudspeaker, degrees above the horizontal "
                                      "plane.";
}

//==============================================================================
/** Measurement & calibration pane. */
namespace cal
{
    inline constexpr auto micInput =
        "Hardware input the measurement microphone is plugged into. Its signal is recorded as "
        "channel 2 of every measurement file.";
    inline constexpr auto micCalValue =
        "REW / miniDSP / FRD text calibration; divided out of the "
        "live spectrum and the analysis measurements. Copied into the measurement folder's "
        "measurement.xml, so a set carries its own calibration.";
    inline constexpr auto micCalLoad =
        "Load a microphone calibration file (frequency / dB, optionally / phase).";
    inline constexpr auto micCalClear =
        "Use no microphone correction.";
    inline constexpr auto outputs =
        "Which outputs to measure. Each is played and recorded in turn, one file per output "
        "per position.";
    inline constexpr auto signal =
        "Test signal:\n"
        "White noise: works with any analysis, needs a longer capture for the same "
        "signal-to-noise ratio.\n"
        "Log sweep: also enables the synchronized-sweep analysis (the whole band from one sweep, "
        "harmonic distortion kept out of the response and shown as its own curves). It runs all the way to Nyquist over a whole number of "
        "octaves, which is what keeps the deconvolved impulse response free of ringing around "
        "its peak. Its parameters are written into measurement.xml.";
    inline constexpr auto duration =
        "Length of each capture. Longer is quieter (more averaging) but slower; a sweep needs "
        "enough length for its harmonics to separate.";
    inline constexpr auto level =
        "Playback level of the test signal. Loud enough to sit well above the room noise, "
        "quiet enough not to drive the speaker into distortion.";
    inline constexpr auto subEnabled =
        "Whether this measurement run includes a subwoofer channel. When "
        "off, no channel is tagged as sub even if one is picked below.";
    inline constexpr auto subChannel =
        "Which output channel is the subwoofer. Its files are named "
        "sub_pos<N>.wav instead of ch<channel>_pos<N>.wav, so Group analysis's "
        "\"Load measurement folder...\" recognizes it automatically. Not used "
        "in Full system mode (there the toggled channels are plugin inputs).";
    inline constexpr auto path =
        "Folder the measurement files are written into. One folder per speaker set: it also "
        "receives measurement.xml and readme_measurement.md.";
    inline constexpr auto browse =
        "Choose the measurement folder.";
    inline constexpr auto comment =
        "General comment for this measurement folder, written to "
        "measurement.xml and readme_measurement.md at the end of every "
        "run (edit it between runs and the next save rewrites it). "
        "Pre-filled from the folder's existing manifest.";
    inline constexpr auto runComment =
        "One-line comment for the next run, logged with that run's "
        "entry in the manifests.";
    inline constexpr auto run =
        "Play and record every selected output once, at the current microphone position. Move "
        "the microphone and run again for each position you want to average.";

    // -- SPL calibration ------------------------------------------------------
    inline constexpr auto meterOn =
        "Show the live level of the measurement microphone, so a real SPL meter can be read "
        "next to it.";
    inline constexpr auto splWindow =
        "Averaging window of the level read-out. Longer is steadier and easier to compare "
        "against a hand-held meter.";
    inline constexpr auto splSine =
        "Play a steady sine through the selected outputs, as the reference tone to read on the "
        "hand-held meter.";
    inline constexpr auto splSineAmp =
        "Level of that reference tone. Loud enough to sit above the room noise, well below "
        "anything that would strain the speaker.";
    inline constexpr auto splSineFreq =
        "Frequency of the reference tone. 1 kHz is the usual choice: it is where microphones "
        "and SPL meters agree best.";
    inline constexpr auto measureType =
        "What is measured:\n"
        "Dry outputs: the speaker alone, correction bypassed. What the analysis wants.\n"
        "Outputs + FIR: the speaker through its loaded correction, to verify one.\n"
        "Full system: the whole plugin from its inputs, matrix included.";
}

//==============================================================================
/** Anything explained the same way in both the Analysis and Group panes. */
namespace shared
{
    inline constexpr auto micCalInfo =
        "Microphone calibration is divided out of the measurements. "
        "Load it in the Measurement & Calibration pane.";

    inline constexpr auto tfMethod =
        "Transfer-function estimation:\n"
        "Welch: averaged cross/auto spectra (any stimulus).\n"
        "Sweep (Farina): deconvolution by the synchronized sweep's analytic "
        "inverse (Novak et al. 2015) (full-band response with true "
        "phase in one shot, plus the harmonic-distortion curves H2, H3...). "
        "Needs the sweep parameters from the folder's measurement.xml; "
        "auto-selected when available.";

    inline constexpr auto levelRef =
        "Level reference of the measured curves:\n"
        "Normalized: 0 dB = 200 Hz - 2 kHz mean of the average.\n"
        "Absolute dB: recorded level per unit of stimulus level "
        "(no normalization).\n"
        "dB SPL: estimated SPL during the measurement (needs an SPL "
        "calibration and the run's stimulus level from measurement.xml); "
        "exact for sweep runs, approximate for noise.\n"
        "The correction curve is always absolute dB.";

    inline constexpr auto phaseType =
        "Linear: corrects magnitude and phase (incl. subwoofer alignment), "
        "adds firLength/2 latency.\n"
        "Min phase: magnitude only, near-zero latency, no phase correction "
        "or subwoofer alignment (for tracking).\n"
        "Min phase also switches the subwoofer integration off entirely: the crossover "
        "all-pass lives in the phase, which this mode discards. The bulk time alignment "
        "still happens, since that is an output delay rather than part of the filter.";

    inline constexpr auto windowWelch =
        "Length of the Welch analysis segments, in samples. Longer = finer frequency "
        "resolution and more of the room's decay, shorter = smoother and more anechoic.";

    inline constexpr auto windowGate =
        "Length of the impulse response kept after the sweep deconvolution, in samples "
        "(a rectangular gate from the start of the IR). Longer = finer frequency "
        "resolution and more of the room's decay, shorter = smoother and more anechoic. "
        "It is not a Welch window in this mode: the sweep gives one IR in a single shot, "
        "with no segment averaging.";

    inline constexpr auto smoothLow =
        "Smoothing of the low frequencies (<= 100 Hz), as a fraction of an octave. Fine "
        "enough to resolve room modes; the fraction is interpolated up to the HF setting "
        "across the spectrum. Complex smoothing, so it moves phase as well as magnitude.";

    inline constexpr auto smoothHigh =
        "Smoothing of the high frequencies (>= 10 kHz). Broad up here: above the room's "
        "transition frequency only the trend is meaningful, and correcting individual "
        "reflections would hold at one microphone position and nowhere else.";

    inline constexpr auto rangeLow =
        "Lowest frequency the correction acts on. Below it the filter fades to unity rather "
        "than trying to compensate output the speaker does not have.";
    inline constexpr auto rangeHigh =
        "Highest frequency the correction acts on.";

    inline constexpr auto correctionLevel =
        "How much of the computed correction to apply, 0 (bypass) to 1 (full). Interpolated "
        "in the log domain, so 0.5 is half the correction in dB.";

    inline constexpr auto maxBoost =
        "Ceiling on how much the correction may boost, with a soft knee below it. Room nulls "
        "cannot be filled by boosting; without a ceiling the filter would spend all its "
        "headroom trying.";

    inline constexpr auto firLength =
        "Length of the exported correction filter, in samples. Longer reaches lower (see the "
        "read-out beside it) at the cost of latency in linear-phase mode.";

    inline constexpr auto firInfo =
        "How low the exported FIR can still shape the response. A filter of "
        "N taps resolves the spectrum to fs/N, so two bins (2*fs/N) is the "
        "lowest frequency at which it can place a correction at all. Below "
        "it the filter is simply too short, whatever the analysis says. The "
        "analysis Range setting bounds the correction from below as well; "
        "whichever is higher wins.";

    inline constexpr auto crossover =
        "Crossover frequency between the mains and the subwoofer. Sets where the phase "
        "alignment acts, and the band the crossover-region estimates are read over.";

    inline constexpr auto subInvert =
        "Treat the subwoofer as polarity-inverted when designing the phase alignment. Try it "
        "when the crossover region sums to a dip that the delay alone will not fix. The "
        "subwoofer output must then be inverted too: Group analysis writes it there on "
        "Apply & export, and in the Analysis pane you set Phase inv. on that output yourself.";
}

//==============================================================================
/** Single-speaker Analysis pane. */
namespace ana
{
    inline constexpr auto micCalSource =
        "Microphone calibration source: the global curve loaded in "
        "the Measurement pane, the curve embedded in the loaded "
        "files' measurement.xml, or none.";

    inline constexpr auto load =
        "Load one speaker's measurement files: the same speaker recorded at several "
        "microphone positions. They are averaged coherently after each one's own arrival "
        "delay is removed.";

    inline constexpr auto loadSub =
        "Load the subwoofer measured at the SAME positions, in the same order. Each is "
        "anchored on the paired main measurement, which is what preserves the main-to-sub "
        "timing and makes the crossover alignment possible.";

    inline constexpr auto mainsDelay =
        "Bulk delay you will apply to the main output, measured against the subwoofer. The "
        "correction then only has to carry what is left over, which keeps the FIR short. The "
        "sub curve on the plot moves with it, so the crossover region can be lined up by eye.";

    inline constexpr auto applyDelay =
        "On Export correction IR + assign, also set that output's "
        "delay to the Mains-delay value (the bulk time-alignment "
        "the correction was designed for). Negative values clamp to 0.";

    inline constexpr auto display =
        "Impulse response traces:\n"
        "measured: the speaker as it is now.\n"
        "corrected: the speaker predicted with the exported FIR loaded, at the "
        "chosen length and Phase type.\n"
        "+ sub: the same, summed with the subwoofer at the Mains-delay offset "
        "and at the level it was measured at (this pane does no level matching).\n\n"
        "The sum is for ONE main. Feeding a mono subwoofer from a stereo pair raises its "
        "share of the sum, so apply about 3 dB more attenuation to the sub in the real "
        "system (up to 6 dB for content correlated between L and R).";
}

//==============================================================================
/** Group analysis pane. */
namespace grp
{
    inline constexpr auto micCalSource =
        "Microphone calibration source: the global curve loaded in "
        "the Measurement pane, the curve embedded in the measurement "
        "folder's measurement.xml, or none.";

    inline constexpr auto loadFolder =
        "Load an entire measurement set written by the Measurement & "
        "calibration pane's folder-based capture: reads measurement.xml (or "
        "readme_measurement.md for older folders) to find the channels and the "
        "subwoofer, and loads every speaker's (and the sub's) position files in "
        "one step. FIR runs are left out at first, see Runs.";

    inline constexpr auto runs =
        "Choose which measurement runs of the loaded folder feed the analysis. "
        "Each run is one microphone position. Unchecked runs are left out of "
        "every speaker and the sub, and OK re-analyzes the group, recomputing "
        "the alignment if it had been computed. FIR runs start unchecked "
        "because they measure the speaker through its correction. Needs a "
        "folder with measurement.xml, and is withdrawn once files are loaded "
        "by hand.";

    inline constexpr auto count =
        "How many speakers are in this group. Slots keep their files when the count is "
        "reduced and restored.";

    inline constexpr auto subEnabled =
        "Whether this group has a subwoofer. When off, the Sub row is "
        "disabled and the subwoofer is excluded from alignment and from "
        "Apply & export (its own settings/data are kept, not cleared).";

    inline constexpr auto compute =
        "Derive every speaker's delay from its measured arrival time, referred to the most "
        "distant driver, and its suggested level trim. Also refreshes what each correction "
        "assumes about the subwoofer. Run it again after changing any shared setting: the "
        "delays and trims do not follow on their own.";

    inline constexpr auto apply =
        "Design and export one correction FIR per speaker, write the delays and the FIR paths "
        "onto the assigned outputs, write the subwoofer's delay and polarity (Invert sub) "
        "onto its output, and save a markdown report of the whole run.";

    inline constexpr auto prefix =
        "Optional name prefix for everything Apply & export writes: "
        "<prefix>_speaker1_correction.wav, <prefix>_report.md. Lets "
        "several alignments (rooms, groups, takes) live in one folder "
        "instead of overwriting each other (which also keeps "
        "the FIR files of previously applied outputs intact). "
        "Pre-filled from the measurement folder's name.";

    inline constexpr auto figures =
        "Also render each speaker's frequency-response and "
        "impulse-response plots as PNGs into <prefix>_figs/ and embed "
        "them in the report. Adds a few seconds to Apply & export for "
        "a large group.";

    inline constexpr auto preview =
        "Which speaker of the group the plot below shows.";

    inline constexpr auto rowLoad =
        "Load this speaker's measurement files by hand, instead of taking them from a "
        "measurement folder.";
    inline constexpr auto rowOutput =
        "Physical output this speaker is wired to. Apply & export writes its delay and FIR "
        "here.";
    inline constexpr auto subOutput =
        "Physical output the subwoofer is wired to. Apply & export writes its delay and its "
        "polarity (Invert sub) here, never a FIR.";
    inline constexpr auto rowDelay =
        "Delay that will be applied to this output, from the measured arrival times: enough "
        "to bring this speaker into line with the most distant one.";

    inline constexpr auto rowTrim =
        "Suggested level-matching trim: how much to attenuate this "
        "output to match the quietest speaker in the group, from the "
        "corrected mid-band (500 Hz - 2 kHz, the SMPTE ST 2095-1 "
        "calibration band) level. Informational, apply it "
        "yourself via the output's Trim in the matrix view.";

    inline constexpr auto subName =
        "Delay and polarity only (no correction FIR is designed for the "
        "subwoofer, above its passband a measurement is just noise).";

    inline constexpr auto subTrimLevel =
        "Suggested level trim for the subwoofer, read over half an "
        "octave either side of the crossover (where the sub and the "
        "mains overlap) rather than the mid-band used for the "
        "speakers, which the sub does not reach. Matches the sub to "
        "ONE corrected main: with a stereo pair driven together "
        "their sum is about 3 dB higher, so allow for that. "
        "Informational, apply it yourself via the "
        "output's Trim in the matrix view.";

    inline constexpr auto subTrim =
        "Offset applied to the subwoofer against the rest of the group, in ms. Positive is "
        "the sub later, negative is the sub earlier; the whole group slides so that the "
        "earliest output sits at zero, which is why a positive trim shows up as less delay "
        "on the mains rather than more on the sub. Every speaker's crossover alignment "
        "follows, so the plot updates as you drag.";

    inline constexpr auto subSuggest =
        "Set the trim from the measured crossover-band phase slope "
        "instead of the arrival times. The two disagree (generally slightly) "
        "by the "
        "subwoofer's own group delay (its low-pass and box "
        "alignment), which arrival times cannot see.";

    inline constexpr auto display =
        "Impulse response traces:\n"
        "measured: the speaker as it is now.\n"
        "corrected: the speaker predicted with the exported FIR loaded, at the "
        "chosen length and Phase type.\n"
        "+ sub (1 main): the same, summed with the subwoofer at its aligned "
        "delay and its suggested level trim.\n\n"
        "The sum is for ONE main. Feeding a mono subwoofer from a stereo pair raises "
        "its share of the sum, so apply about 3 dB more attenuation to the sub in the "
        "real system (up to 6 dB for content correlated between L and R).";
}

} // namespace smt::tips
