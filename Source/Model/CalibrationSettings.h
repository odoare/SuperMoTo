/*
  ------------------------------------------------------------------------------
    CalibrationSettings.h

    The Measurement & calibration pane's controls, as values, kept by the
    processor so that they outlive the editor. CalibrationComponent restores
    its controls from them and writes each change back.

    Nothing else is needed for that pane: a measurement in progress already
    lives in MeasurementEngine, the generator and the meter in SplMeterEngine,
    and the mic and SPL calibrations are machine-wide (AppSettings).

    Not saved with the host session: the values live as long as the plugin
    instance. Message thread only.

    Author: Olivier Doaré, github.com/odoare
    Licenced under the GNU LGPL Version 3.0
    SPDX-License-Identifier: LGPL-3.0-or-later
  ------------------------------------------------------------------------------
*/

#pragma once

#include <JuceHeader.h>
#include <array>
#include "ConfigModel.h"

namespace smt
{

/** Combo boxes are stored by item id. */
struct CalibrationSettings
{
    // What is measured.
    int micInputId = 1;                             // input c (1-based) is id c
    MeasureMode mode = MeasureMode::dryOutput;
    std::array<bool, numChannels> channels {};      // outputs, or inputs in System mode
    bool subEnabled = false;
    int subChannelId = 1;                           // 1 (none), output o (0-based) is id o + 2

    // The stimulus.
    int signalId = 2;                               // 1 white noise, 2 log sweep
    float durationS = 10.0f;
    float levelDb = -12.0f;

    // Where it is written, and the comments that go with it.
    juce::String folder;                            // the last browsed folder until the first view
    juce::String generalComment;                    // saved into the manifest by the next run
    juce::File commentFolder;                       // the folder generalComment was read from
    juce::String runComment;

    // SPL meter and test generator.
    bool meterOn = false;
    int rmsWindowId = 3;                            // 50 ms, 100 ms, 300 ms, 1 s
    bool sineOn = false;
    float sineAmpDb = -12.0f;
    float sineFreqHz = 1000.0f;
    bool noiseOn = false;
    float noiseAmpDb = -12.0f;
    float splReferenceDb = 85.0f;                   // the last reading typed from a real SPL meter

    // Status line.
    juce::String status;                            // as last shown
    juce::String measurementStatus;                 // MeasurementEngine's, when last shown

    /** Set by the first view, which fills in what depends on the machine
        rather than on the instance (the folder). */
    bool opened = false;
};

} // namespace smt
