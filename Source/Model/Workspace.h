/*
  ------------------------------------------------------------------------------
    Workspace.h

    What the user is working on in one plugin instance, owned by the processor
    so that closing the editor loses none of it. Each pane keeps its own part
    here and its component is a view of it.

    Held in memory for the life of the instance only: none of it is written to
    the host session or to presets.

    Message thread only.

    Author: Olivier Doaré, github.com/odoare
    Licenced under the GNU LGPL Version 3.0
    SPDX-License-Identifier: LGPL-3.0-or-later
  ------------------------------------------------------------------------------
*/

#pragma once

#include <JuceHeader.h>
#include "ConfigModel.h"
#include "GroupAnalysisSession.h"

namespace smt
{

class Workspace
{
public:
    Workspace (ConfigModel& configModel, MatrixEngine& matrixEngine)
        : groupAnalysis (configModel, matrixEngine)
    {
    }

    /** The Group analysis pane: speaker group, settings, loaded folder, and
        the background batch, which runs on with the editor closed. */
    GroupAnalysisSession groupAnalysis;

    JUCE_DECLARE_NON_COPYABLE (Workspace)
};

} // namespace smt
