/*
  ------------------------------------------------------------------------------
    AppSettings.h

    Small machine-wide settings shared by every SuperMoTo instance (and the
    standalone app), persisted in a JUCE PropertiesFile under the user's
    application-data folder. Currently just the last directory used by the
    file choosers, so loading/exporting does not start from the home folder
    every time.

    Header-only: a single shared PropertiesFile is held in a function-local
    static, so all components reach it without plumbing the processor through.

    Author: Olivier Doaré, github.com/odoare
    Licenced under the GNU LGPL Version 3.0
    SPDX-License-Identifier: LGPL-3.0-or-later
  ------------------------------------------------------------------------------
*/

#pragma once

#include <JuceHeader.h>
#include <FxmeTools/dsp/MicCalibration.h>

namespace smt
{

/** Process-wide application settings (one shared file on disk). */
inline juce::ApplicationProperties& appProperties()
{
    static juce::ApplicationProperties props;
    static const bool inited = [&]
    {
        juce::PropertiesFile::Options o;
        o.applicationName     = "SuperMoTo";
        o.filenameSuffix      = "settings";
        o.folderName          = "FXMechanics";
        o.osxLibrarySubFolder = "Application Support";
        props.setStorageParameters (o);
        return true;
    }();
    juce::ignoreUnused (inited);
    return props;
}

/** Last directory used by a load/export chooser; falls back to the home
    folder when nothing valid has been stored yet. */
inline juce::File getLastBrowseDir()
{
    if (auto* s = appProperties().getUserSettings())
    {
        const juce::File f (s->getValue ("lastBrowseDir"));
        if (f.isDirectory())
            return f;
    }
    return juce::File::getSpecialLocation (juce::File::userHomeDirectory);
}

/** Remember the directory of the given file (or the directory itself). */
inline void setLastBrowseDir (const juce::File& fileOrDir)
{
    const auto dir = fileOrDir.isDirectory() ? fileOrDir : fileOrDir.getParentDirectory();
    if (! dir.isDirectory())
        return;
    if (auto* s = appProperties().getUserSettings())
    {
        s->setValue ("lastBrowseDir", dir.getFullPathName());
        s->saveIfNeeded();
    }
}

//==============================================================================
// Interface mode: which view/panel is open and how compact the editor is, so
// it reopens the way it was left.

/** Last open view, as the editor's View enum cast to int (default 0 = matrix). */
inline int getUiView()
{
    auto* s = appProperties().getUserSettings();
    return s != nullptr ? s->getIntValue ("uiView", 0) : 0;
}

inline void setUiView (int view)
{
    if (auto* s = appProperties().getUserSettings())
    {
        s->setValue ("uiView", view);
        s->saveIfNeeded();
    }
}

/** How compact the editor was left: 0 = full, 1 = top bar + output strip,
    2 = mini (two control rows + output meters). Mirrors the editor's
    Compact enum. Falls back to the older boolean "uiCollapsed" setting so a
    window left collapsed by a previous version still reopens collapsed. */
inline int getUiCompactMode()
{
    auto* s = appProperties().getUserSettings();
    if (s == nullptr)
        return 0;
    if (s->containsKey ("uiCompactMode"))
        return juce::jlimit (0, 2, s->getIntValue ("uiCompactMode", 0));
    return s->getBoolValue ("uiCollapsed", false) ? 1 : 0;
}

inline void setUiCompactMode (int mode)
{
    if (auto* s = appProperties().getUserSettings())
    {
        s->setValue ("uiCompactMode", juce::jlimit (0, 2, mode));
        s->saveIfNeeded();
    }
}

//==============================================================================
// SPL-meter calibration: dB SPL = dBFS + offset, obtained by playing a tone,
// reading a real SPL meter and typing its value. Persisted so it survives
// closing the editor — it stays valid as long as the mic / preamp-gain /
// interface chain is unchanged.

inline bool isSplCalibrated()
{
    auto* s = appProperties().getUserSettings();
    return s != nullptr && s->getBoolValue ("splCalibrated", false);
}

inline float getSplOffsetDb()
{
    auto* s = appProperties().getUserSettings();
    return s != nullptr ? (float) s->getDoubleValue ("splOffsetDb", 0.0) : 0.0f;
}

inline void setSplCalibration (float offsetDb, bool calibrated)
{
    if (auto* s = appProperties().getUserSettings())
    {
        s->setValue ("splOffsetDb", offsetDb);
        s->setValue ("splCalibrated", calibrated);
        s->saveIfNeeded();
    }
}

//==============================================================================
// Measurement-microphone calibration. One physical mic, so a single shared
// MicCalibration is kept here and used by both the live SPL/spectrum display
// and the analysis transfer functions. The chosen file path is persisted; the
// curve is (re)loaded from it on first access.

/** The process-wide microphone calibration (loaded lazily from the stored
    path; invalid / flat when none is set). */
inline fxme::MicCalibration& sharedMicCalibration()
{
    static fxme::MicCalibration cal;
    static const bool inited = [&]
    {
        if (auto* s = appProperties().getUserSettings())
        {
            const juce::File f (s->getValue ("micCalPath"));
            if (f.existsAsFile())
                cal.loadFromFile (f);
        }
        return true;
    }();
    juce::ignoreUnused (inited);
    return cal;
}

inline juce::File getMicCalibrationFile()
{
    auto* s = appProperties().getUserSettings();
    return s != nullptr ? juce::File (s->getValue ("micCalPath")) : juce::File();
}

/** Loads (or clears, when the file is empty/missing) the shared calibration
    and persists the path. Returns true if a valid calibration is now loaded. */
inline bool setMicCalibrationFile (const juce::File& file)
{
    const bool ok = file.existsAsFile() && sharedMicCalibration().loadFromFile (file);
    if (! ok)
        sharedMicCalibration().clear();

    if (auto* s = appProperties().getUserSettings())
    {
        s->setValue ("micCalPath", ok ? file.getFullPathName() : juce::String());
        s->saveIfNeeded();
    }
    return ok;
}

} // namespace smt
