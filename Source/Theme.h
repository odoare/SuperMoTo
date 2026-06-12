/*
  ------------------------------------------------------------------------------
    Theme.h

    SuperMoTo colour scheme, after the MechanOdd / FxmeFX pattern: dark
    diagonal-gradient backdrop, accents applied to the slider colour IDs the
    FxmeLookAndFeel reads. Matrix input rows are tinted with a rotating hue
    so a frame's colour identifies its input at a glance.

    Author: Olivier Doaré, github.com/odoare
    Licenced under the GNU LGPL Version 3.0
    SPDX-License-Identifier: LGPL-3.0-or-later
  ------------------------------------------------------------------------------
*/

#pragma once

#include <JuceHeader.h>
#include "Model/ConfigModel.h"

namespace SuperMoToTheme
{
    inline void paintBackground (juce::Graphics& g, juce::Rectangle<float> b)
    {
        const auto base = juce::Colour::fromFloatRGBA (0.15f, 0.15f, 0.25f, 1.0f);
        juce::ColourGradient grad (base.darker().darker().darker(), b.getBottomLeft(),
                                   base, b.getTopRight(), false);
        g.setGradientFill (grad);
        g.fillRect (b);
    }

    inline const juce::Colour panel       { 0xff20202c };
    inline const juce::Colour panelLine   { 0xff3a3a4c };
    inline const juce::Colour text        { 0xffd8d8e0 };
    inline const juce::Colour dimText     { 0xff8a8a98 };

    inline const juce::Colour master      { 0xffe8833a };   // orange (master level)
    inline const juce::Colour mute        { 0xffe05858 };
    inline const juce::Colour dim         { 0xffd9b13a };
    inline const juce::Colour mono        { 0xff8aa0b8 };
    inline const juce::Colour exclusive   { 0xffb164d6 };
    inline const juce::Colour fir         { 0xff35c0c0 };   // teal (FIR correction)
    inline const juce::Colour spectrum    { 0xff8fc73e };   // lime (analyzer)
    inline const juce::Colour measure     { 0xffe0586f };   // rose (calibration)

    // One accent per configuration A..F.
    inline juce::Colour configColour (int c) noexcept
    {
        static const juce::Colour col[] = { juce::Colour (0xffe8833a),   // A orange
                                            juce::Colour (0xff35c0c0),   // B teal
                                            juce::Colour (0xff8fc73e),   // C lime
                                            juce::Colour (0xff4a90e0),   // D azure
                                            juce::Colour (0xffd06fd0),   // E magenta
                                            juce::Colour (0xffd9b13a) }; // F gold
        return col[(size_t) juce::jlimit (0, 5, c)];
    }

    // Matrix input-row accents: rotate the hue over the 16 inputs.
    inline juce::Colour inputColour (int in) noexcept
    {
        return juce::Colour::fromHSV ((float) in / (float) smt::numChannels,
                                      0.55f, 0.85f, 1.0f);
    }

    // Analyzer trace colours (frame taps).
    inline juce::Colour frameTraceColour (int slot) noexcept
    {
        return juce::Colour::fromHSV (0.05f + 0.61f * (float) slot, 0.8f, 0.95f, 1.0f);
    }

    inline juce::Colour outputTraceColour (int out) noexcept
    {
        return juce::Colour::fromHSV ((float) out / (float) smt::numChannels,
                                      0.35f, 0.95f, 1.0f);
    }

    inline void accentSlider (juce::Slider& s, juce::Colour accent)
    {
        s.setColour (juce::Slider::rotarySliderFillColourId,    juce::Colour (0xff2b2b2b));
        s.setColour (juce::Slider::rotarySliderOutlineColourId, accent.darker (1.6f));
        s.setColour (juce::Slider::trackColourId,               accent);
        s.setColour (juce::Slider::thumbColourId,               accent.brighter (0.4f));
        s.setColour (juce::Slider::backgroundColourId,          juce::Colour (0xff2b2b2b));
    }

    inline void accentToggleButton (juce::ToggleButton& b, juce::Colour accent)
    {
        b.setColour (juce::ToggleButton::tickColourId,         accent);
        b.setColour (juce::ToggleButton::tickDisabledColourId, accent.withAlpha (0.4f));
        b.setColour (juce::ToggleButton::textColourId,         text);
    }

    inline void accentComboBox (juce::ComboBox& c, juce::Colour accent)
    {
        c.setColour (juce::ComboBox::outlineColourId, accent.darker());
        c.setColour (juce::ComboBox::arrowColourId,   accent.brighter (0.3f));
        c.setColour (juce::ComboBox::backgroundColourId, panel);
        c.setColour (juce::ComboBox::textColourId, text);
    }
}
