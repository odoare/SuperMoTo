/*
  ------------------------------------------------------------------------------
    EditorSettings.h

    The editor's own state and the matrix view's, kept by the processor so
    that they outlive the editor: which page is up, how compact the window is
    and how large when it is not, the configuration shown in the matrix, the
    selected frame or output, and the analyzer's view.

    Per instance. The page and the compact mode are also written machine-wide
    (AppSettings) as the editor changes them, and a new instance's first
    editor starts from those; the hover tooltips stay a machine-wide
    preference only.

    Not saved with the host session: the values live as long as the plugin
    instance. Message thread only.

    Author: Olivier Doaré, github.com/odoare
    Licenced under the GNU AGPL Version 3.0, or commercial terms (LICENSE.md)
    SPDX-License-Identifier: AGPL-3.0-or-later OR LicenseRef-FXME-Commercial
  ------------------------------------------------------------------------------
*/

#pragma once

#include <JuceHeader.h>
#include <optional>

namespace smt
{

struct EditorSettings
{
    /** Set by the first editor, which takes the page and the compact mode
        from the machine-wide settings. */
    bool opened = false;

    /** The startup splash has had its turn. Here rather than on the editor so
        that it plays once per plugin instance, not every time the window is
        reopened. */
    bool splashShown = false;

    int view = 0;                       // SuperMoToAudioProcessorEditor::View
    int compactMode = 0;                // SuperMoToAudioProcessorEditor::Compact
    int expandedWidth = 1280;           // the window in the full layout
    int expandedHeight = 820;

    // Matrix view.
    int editConfig = 0;                 // configuration shown and edited, 0..5 = A..F

    /** The engaged configurations when the editor closed, bit c for
        configuration c. An open editor follows a configuration being engaged;
        if one was engaged while the editor was closed (automation, a preset),
        the next editor shows it instead of editConfig. */
    int engagedConfigs = 0;

    int selectedIn = -1, selectedOut = -1;  // the selected frame, in editConfig
    int selectedOutput = -1;                // the selected output (strip)
    bool editingOutput = false;             // the detail panel shows the output editor

    std::optional<fxme::SpectrumDisplay::ViewState> analyzerView;
};

} // namespace smt
