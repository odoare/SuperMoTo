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
    inline const juce::Colour panel       { 0xff20202c };
    inline const juce::Colour panelLine   { 0xff3a3a4c };
    inline const juce::Colour text        { 0xffd8d8e0 };
    inline const juce::Colour dimText     { 0xff9a9aa8 };

    inline const juce::Colour master      { 0xff007070 };   // dark cyan (master level)
    inline const juce::Colour mute        { 0xffe05858 };
    inline const juce::Colour dim         { 0xffd9b13a };
    inline const juce::Colour mono        { 0xff8aa0b8 };
    // Time alignment (output delay, mains delay, sub trim). Its own colour
    // rather than `dim`, which is a bright gold: the look-and-feel draws the
    // slider's value in white over the filled track, and white on that gold is
    // 2.0:1, well under the 4.5:1 WCAG wants for body text. This violet is
    // 5.7:1, in line with `master` (5.9:1), which is the readable benchmark
    // already in the palette.
    inline const juce::Colour delay       { 0xff5a2f9f };
    inline const juce::Colour exclusive   { 0xff00ffff };
    inline const juce::Colour fir         { 0xff35c0c0 };   // teal (FIR correction)
    inline const juce::Colour spectrum    { 0xff8fc73e };   // lime (analyzer)
    inline const juce::Colour measure     { 0xffe0586f };   // rose (calibration)

    inline const juce::Colour configEngage { 0xff00ffff };  // cyan: A..F engage / edit

    // The lit body of a selected fxme::AccentToggle (the view and Edit A..F
    // buttons). A brightened master rather than master itself: AccentToggle draws
    // its "on" text in black, which is only 3.6:1 against 0xff007070 but 6.9:1
    // against this. configEngage is already bright enough to use raw.
    inline const juce::Colour viewSelected { master.brighter (0.5f) };

    // The window backdrop, from FxmeTools so every plugin in the family shares
    // one: near-black with only a whisper of the accent, which is what keeps a
    // set of differently-tinted plugins looking like one product. The
    // *Component* variant rather than paintTintedBackground because this editor
    // fills the whole plugin window itself (there is no separate effect
    // component below it), and its gradient runs corner to corner, so it reads
    // the same across the whole 1100x720 to 2400x1600 resize range.
    inline void paintBackground (juce::Graphics& g, juce::Rectangle<float> b)
    {
        fxme::paintComponentBackground (g, b, master);
    }

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

    // Palette handed to the reusable fxme::SpectrumDisplay so its grid/labels
    // match the SuperMoTo theme (trace colours stay per-trace).
    inline fxme::SpectrumDisplay::Colours spectrumColours()
    {
        fxme::SpectrumDisplay::Colours c;
        c.plotBackground = plotBackground;
        c.grid           = grid;
        c.gridZero       = gridZero;
        c.text           = text;
        c.dimText        = dimText;
        c.panelLine      = panelLine;
        return c;
    }

    // Palette handed to the reusable fxme::WaveformDisplay (impulse-response
    // and time-domain views) so its grid/labels match the spectrum plots.
    inline fxme::WaveformDisplay::Colours waveformColours()
    {
        fxme::WaveformDisplay::Colours c;
        c.plotBackground = plotBackground;
        c.grid           = grid;
        c.gridZero       = gridZero;
        c.text           = text;
        c.dimText        = dimText;
        c.panelLine      = panelLine;
        return c;
    }

    // Palette handed to the reusable fxme::SplMeterComponent bar.
    inline fxme::SplMeterComponent::Colours splMeterColours()
    {
        fxme::SplMeterComponent::Colours c;
        c.background = plotBackground;
        c.outline    = panelLine;
        c.low        = spectrum;
        c.mid        = dim;
        c.high       = mute;
        c.label      = dimText;
        c.readout    = text;
        return c;
    }

    // Palette handed to the reusable fxme::InfoButton help button + callout.
    inline fxme::InfoButton::Colours infoButtonColours()
    {
        fxme::InfoButton::Colours c;
        c.accent    = measure;
        c.text      = text;
        c.panel     = panel;
        c.panelLine = panelLine;
        return c;
    }
}
