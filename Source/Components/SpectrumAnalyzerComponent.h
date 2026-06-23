/*
  ------------------------------------------------------------------------------
    SpectrumAnalyzerComponent.h

    Monitoring-part spectrum analyzer: a SpectrumDisplay pre-configured with
    one trace per tap of the engine's SpectrumBus — matrix frame signals
    (checkbox in each frame) and output sums (checkbox in each output strip).
    The FFT/windowing/averaging lives in SpectrumAnalyzer; the drawing in
    SpectrumDisplay. This class only maps taps to colours and labels.

    Author: Olivier Doaré, github.com/odoare
    Licenced under the GNU LGPL Version 3.0
    SPDX-License-Identifier: LGPL-3.0-or-later
  ------------------------------------------------------------------------------
*/

#pragma once

#include <JuceHeader.h>     // fxme::SpectrumDisplay via the FxmeTools module umbrella
#include "../Dsp/MatrixEngine.h"
#include "../Theme.h"

class SpectrumAnalyzerComponent : public fxme::SpectrumDisplay
{
public:
    explicit SpectrumAnalyzerComponent (smt::MatrixEngine& e)
    {
        setDbRange (-100.0f, 10.0f);
        setColours (SuperMoToTheme::spectrumColours());

        auto& bus = e.getSpectrumBus();
        for (int t = 0; t < smt::numSpectrumTaps; ++t)
        {
            TraceConfig c;
            c.tap = &bus.tap (t);

            if (t < smt::numChannels)
            {
                c.colour    = SuperMoToTheme::outputTraceColour (t);
                c.thickness = 1.6f;
                c.label     = [t] { return "Out " + juce::String (t + 1); };
            }
            else
            {
                const int slot = t - smt::numChannels;
                c.colour    = SuperMoToTheme::frameTraceColour (slot);
                c.thickness = 1.1f;
                c.label     = [this, slot]
                {
                    return frameLabelProvider != nullptr ? frameLabelProvider (slot)
                                                         : "Frame " + juce::String (slot + 1);
                };
            }

            addTrace (std::move (c));
        }
    }

    /** Resolves a frame trace's display label from the model routing. */
    std::function<juce::String (int frameSlot)> frameLabelProvider;
};
