/*
  ------------------------------------------------------------------------------
    ConfigToolSettings.h

    The Config tool's controls, as values, kept by the processor so that they
    outlive the editor: the rig being described (layout, Ambisonics order and
    speaker count), each speaker row, and how it is written. ConfigToolComponent
    restores its controls from them and writes each change back.

    Not saved with the host session: the values live as long as the plugin
    instance. Message thread only.

    Author: Olivier Doaré, github.com/odoare
    Licenced under the GNU AGPL Version 3.0, or commercial terms (LICENSE.md)
    SPDX-License-Identifier: AGPL-3.0-or-later OR LicenseRef-FXME-Commercial
  ------------------------------------------------------------------------------
*/

#pragma once

#include <JuceHeader.h>
#include <vector>
#include "ConfigModel.h"

namespace smt
{

/** Combo boxes are stored by item id. */
struct ConfigToolSettings
{
    // The rig.
    int layoutId = 2;                   // 2.0, 2.1, 4.0, 4.1, 5.1, 7.1, Ambisonics
    int orderId = 1;                    // Ambisonics order
    int numSpeakers = 8;                // Ambisonics speaker count

    /** One speaker row. Its name, and whether it is the sub, come from the
        layout; these are the values the user can edit. */
    struct Speaker
    {
        int inputId = 0;                // input c (1-based) is id c, numChannels + 1 the
                                        //   sum of mains (a sub); unused for Ambisonics
        int outputId = 1;               // output c (1-based) is id c
        float gainDb = 0.0f;            // the trim, for Ambisonics
        float azimuthDeg = 0.0f;        // Ambisonics only
        float elevationDeg = 0.0f;
        float radiusM = 2.0f;
    };

    /** The rows of the rig above, in order. Reset to the rig's defaults when
        the layout, the order or the count changes; empty until the first view
        fills it. */
    std::vector<Speaker> speakers;

    // How it is written.
    int targetId = 1;                   // configuration A..F
    bool bassManagement = true;
    float crossoverHz = 80.0f;
    bool writeRadiusGain = true;        // Ambisonics
    bool writeRadiusDelay = true;

    juce::String status;                // the status line as last shown
};

} // namespace smt
