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
    inline const juce::Colour dimText     { 0xff9a9aa8 };

    inline const juce::Colour master      { 0xff007070 };   // dark cyan (master level)
    inline const juce::Colour mute        { 0xffe05858 };
    inline const juce::Colour dim         { 0xffd9b13a };
    inline const juce::Colour mono        { 0xff8aa0b8 };
    inline const juce::Colour exclusive   { 0xff00ffff };
    inline const juce::Colour fir         { 0xff35c0c0 };   // teal (FIR correction)
    inline const juce::Colour spectrum    { 0xff8fc73e };   // lime (analyzer)
    inline const juce::Colour measure     { 0xffe0586f };   // rose (calibration)

    inline const juce::Colour configEngage { 0xff00ffff };  // cyan: A..F engage / edit

    // Plots, analyzers and meters.
    inline const juce::Colour plotBackground   { 0xff000000 };   // plot / meter background
    inline const juce::Colour grid             { 0x66555555 };   // faint grid line
    inline const juce::Colour gridZero         { 0xcc555555 };   // 0 dB / 0 deg grid line
    inline const juce::Colour curveAverage     { 0xffffffff };   // analysis average curve
    inline const juce::Colour curveMeasurement { 0xff808080 };   // individual measurements
    inline const juce::Colour selection        { 0xe6ffffff };   // selected matrix frame
    inline const juce::Colour meterOk          { 0xff3b9d3b };   // VU below 0 dB
    inline const juce::Colour meterClip        { 0xffff0000 };   // VU over 0 dB

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
