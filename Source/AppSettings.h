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

} // namespace smt
