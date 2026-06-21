/*
  ------------------------------------------------------------------------------
    ConfigToolComponent.h

    Configuration tool for standard speaker layouts (2.0, 2.1, 4.0, 4.1, 5.1,
    7.1) and periphonic Ambisonics (orders 1..3). A convenient way to fill one
    of the A..F matrix configurations.

    For channel-based layouts: pick which plugin inputs/outputs carry each
    speaker, the gains, the bass-management crossover and the subwoofer routing,
    then Apply writes the corresponding frames into the model. The sum of the
    main channels is sent lowpassed to the subwoofer output; mains are
    highpassed when bass management is enabled.

    For Ambisonics: pick the azimuth/elevation and output of each loudspeaker.
    Apply builds the AmbiX (ACN / SN3D) max-rE sampling decoder and writes the
    harmonic -> speaker decode gains as frames (B-format taken as inputs
    1..(order+1)^2). With bass management, the omnidirectional W component is
    lowpassed to the subwoofer and the speakers are highpassed.

    Further per-frame tuning (and FIR correction curves per output) is then done
    in the matrix view.

    Author: Olivier Doaré, github.com/odoare
    Licenced under the GNU LGPL Version 3.0
    SPDX-License-Identifier: LGPL-3.0-or-later
  ------------------------------------------------------------------------------
*/

#pragma once

#include <JuceHeader.h>
#include <algorithm>
#include <cmath>
#include "../Model/ConfigModel.h"
#include "../Dsp/AmbisonicsDecode.h"
#include "../Theme.h"

class ConfigToolComponent : public juce::Component
{
public:
    explicit ConfigToolComponent (smt::ConfigModel& m) : model (m)
    {
        title.setText ("Speaker configuration tool", juce::dontSendNotification);
        title.setFont (juce::Font (17.0f, juce::Font::bold));
        title.setColour (juce::Label::textColourId, SuperMoToTheme::text);
        addAndMakeVisible (title);

        layoutBox.addItemList ({ "Stereo 2.0", "Stereo 2.1", "Quad 4.0", "Quad 4.1", "5.1", "7.1",
                                 "Ambisonics 1st order", "Ambisonics 2nd order",
                                 "Ambisonics 3rd order" }, 1);
        SuperMoToTheme::accentComboBox (layoutBox, SuperMoToTheme::master);
        layoutBox.onChange = [this] { syncAmbiControls(); rebuildRows(); };
        addAndMakeVisible (layoutBox);
        addLabel (layoutLabel, "Layout");

        targetBox.addItemList ({ "A", "B", "C", "D", "E", "F" }, 1);
        SuperMoToTheme::accentComboBox (targetBox, SuperMoToTheme::master);
        targetBox.setSelectedId (1, juce::dontSendNotification);
        addAndMakeVisible (targetBox);
        addLabel (targetLabel, "Write to config");

        bassManagement.setButtonText ("Bass management (highpass the mains)");
        SuperMoToTheme::accentToggleButton (bassManagement, SuperMoToTheme::fir);
        bassManagement.setToggleState (true, juce::dontSendNotification);
        addAndMakeVisible (bassManagement);

        crossover.setSliderStyle (juce::Slider::LinearHorizontal);
        crossover.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        crossover.setRange (40.0, 300.0, 1.0);
        crossover.setValue (80.0, juce::dontSendNotification);
        crossover.setDoubleClickReturnValue (true, 80.0);
        crossover.setTextValueSuffix (" Hz");
        SuperMoToTheme::accentSlider (crossover, SuperMoToTheme::fir);
        addAndMakeVisible (crossover);
        addLabel (crossoverLabel, "Crossover");

        applyButton.setButtonText ("Apply to configuration");
        applyButton.setColour (juce::TextButton::buttonColourId, SuperMoToTheme::master.darker (0.8f));
        applyButton.onClick = [this] { apply(); };
        addAndMakeVisible (applyButton);

        status.setColour (juce::Label::textColourId, SuperMoToTheme::spectrum);
        addAndMakeVisible (status);

        // Ambisonics-only controls (shown via syncAmbiControls): the number of
        // speaker outputs and whether the radius delay compensation is written.
        numSpkSlider.setSliderStyle (juce::Slider::LinearHorizontal);
        numSpkSlider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        numSpkSlider.setRange (4.0, (double) smt::numChannels, 1.0);
        numSpkSlider.onValueChange = [this] { rebuildRows(); };
        SuperMoToTheme::accentSlider (numSpkSlider, SuperMoToTheme::master);
        addChildComponent (numSpkSlider);
        addLabel (numSpkLabel, "Speakers");
        numSpkLabel.setVisible (false);

        writeDelayToggle.setButtonText ("Write delay compensation");
        writeDelayToggle.setToggleState (true, juce::dontSendNotification);
        SuperMoToTheme::accentToggleButton (writeDelayToggle, SuperMoToTheme::dim);
        addChildComponent (writeDelayToggle);

        layoutBox.setSelectedId (2);    // default to 2.1 — builds the rows
    }

    void paint (juce::Graphics& g) override
    {
        g.setColour (SuperMoToTheme::panel.withAlpha (0.7f));
        g.fillRoundedRectangle (getLocalBounds().toFloat(), 6.0f);
        g.setColour (SuperMoToTheme::panelLine);
        g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (0.5f), 6.0f, 1.0f);

        // Column headers for the speaker rows
        if (! rows.empty())
        {
            g.setColour (SuperMoToTheme::dimText);
            g.setFont (11.0f);
            auto r = rowArea (0).translated (0, -16);
            if (isAmbisonics())
            {
                auto cols = splitRowAmbi (r);
                g.drawText ("Speaker",    cols[0], juce::Justification::centredLeft);
                g.drawText ("Azimuth",    cols[1], juce::Justification::centred);
                g.drawText ("Elevation",  cols[2], juce::Justification::centred);
                g.drawText ("Radius (m)", cols[3], juce::Justification::centred);
                g.drawText ("Output",     cols[4], juce::Justification::centred);
                g.drawText ("Trim (dB)",  cols[5], juce::Justification::centred);
            }
            else
            {
                auto cols = splitRow (r);
                g.drawText ("Speaker",   cols[0], juce::Justification::centredLeft);
                g.drawText ("Input",     cols[1], juce::Justification::centred);
                g.drawText ("Output",    cols[2], juce::Justification::centred);
                g.drawText ("Gain (dB)", cols[3], juce::Justification::centred);
            }
        }
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced (14);
        title.setBounds (area.removeFromTop (26).withTrimmedLeft (30));   // room for the info button
        area.removeFromTop (6);

        const bool ambi = isAmbisonics();

        auto top = area.removeFromTop (24);
        layoutLabel.setBounds (top.removeFromLeft (50));
        layoutBox.setBounds (top.removeFromLeft (160));
        top.removeFromLeft (20);
        targetLabel.setBounds (top.removeFromLeft (100));
        targetBox.setBounds (top.removeFromLeft (60));
        if (ambi)
        {
            top.removeFromLeft (24);
            numSpkLabel.setBounds (top.removeFromLeft (60));
            numSpkSlider.setBounds (top.removeFromLeft (150));
            top.removeFromLeft (16);
            writeDelayToggle.setBounds (top.removeFromLeft (230));
        }

        area.removeFromTop (8);
        auto bm = area.removeFromTop (24);
        bassManagement.setBounds (bm.removeFromLeft (300));
        crossoverLabel.setBounds (bm.removeFromLeft (70));
        crossover.setBounds (bm);

        rowsTop = area.getY() + 24;

        // Shrink the row height when there are many speakers so they always fit
        // above the Apply button (32 rows would otherwise overflow).
        const int n = juce::jmax (1, (int) rows.size());
        const int avail = getHeight() - 44 - rowsTop;     // 44 = Apply row + margin
        rowH = juce::jlimit (18, 28, avail / n);

        for (size_t i = 0; i < rows.size(); ++i)
        {
            auto r = rowArea ((int) i);
            if (ambi)
            {
                auto cols = splitRowAmbi (r);
                rows[i]->name.setBounds (cols[0]);
                rows[i]->az.setBounds (cols[1].reduced (2, 1));
                rows[i]->el.setBounds (cols[2].reduced (2, 1));
                rows[i]->radius.setBounds (cols[3].reduced (2, 1));
                rows[i]->output.setBounds (cols[4].reduced (4, 1));
                rows[i]->gain.setBounds (cols[5]);
            }
            else
            {
                auto cols = splitRow (r);
                rows[i]->name.setBounds (cols[0]);
                rows[i]->input.setBounds (cols[1].reduced (4, 1));
                rows[i]->output.setBounds (cols[2].reduced (4, 1));
                rows[i]->gain.setBounds (cols[3]);
            }
        }

        auto bottom = getLocalBounds().reduced (14).removeFromBottom (30);
        applyButton.setBounds (bottom.removeFromLeft (220));
        bottom.removeFromLeft (12);
        status.setBounds (bottom);
    }

private:
    struct SpeakerDef { juce::String name; int in, out; bool isSub; float gain;
                        float az = 0.0f, el = 0.0f, radius = 2.0f; };

    struct Row
    {
        juce::Label name;
        juce::ComboBox input, output;
        fxme::FxmeSlider gain, az, el, radius;
        bool isSub = false;
    };

    static constexpr double speedOfSound = 343.0;   // m/s, for radius delay

    void addLabel (juce::Label& l, const juce::String& text)
    {
        l.setText (text, juce::dontSendNotification);
        l.setFont (juce::Font (12.0f));
        l.setColour (juce::Label::textColourId, SuperMoToTheme::dimText);
        addAndMakeVisible (l);
    }

    bool isAmbisonics() const   { return layoutBox.getSelectedId() >= 7; }
    int  ambiOrder() const      { return layoutBox.getSelectedId() - 6; }   // 1..3

    int defaultSpeakerCount (int order) const { return order == 1 ? 8 : order == 2 ? 12 : 16; }
    int currentSpeakerCount() const           { return juce::jmax (1, (int) numSpkSlider.getValue()); }

    // Shows/updates the ambisonics-only controls when the layout changes. The
    // speaker count is clamped to at least the number of harmonics and reset to
    // the order's canonical default.
    void syncAmbiControls()
    {
        const bool ambi = isAmbisonics();
        numSpkSlider.setVisible (ambi);
        numSpkLabel.setVisible (ambi);
        writeDelayToggle.setVisible (ambi);

        if (ambi)
        {
            const int order = ambiOrder();
            numSpkSlider.setRange ((double) smt::ambisonics::numHarmonics (order),
                                   (double) smt::numChannels, 1.0);
            numSpkSlider.setValue ((double) defaultSpeakerCount (order), juce::dontSendNotification);
            numSpkSlider.setDoubleClickReturnValue (true, (double) defaultSpeakerCount (order));
        }
        resized();
    }

    juce::Rectangle<int> rowArea (int i) const
    {
        return { 14, rowsTop + i * rowH, getWidth() - 28, rowH - 2 };
    }

    std::array<juce::Rectangle<int>, 4> splitRow (juce::Rectangle<int> r) const
    {
        std::array<juce::Rectangle<int>, 4> cols;
        cols[0] = r.removeFromLeft (130);
        cols[1] = r.removeFromLeft (90);
        cols[2] = r.removeFromLeft (90);
        cols[3] = r;
        return cols;
    }

    std::array<juce::Rectangle<int>, 6> splitRowAmbi (juce::Rectangle<int> r) const
    {
        std::array<juce::Rectangle<int>, 6> cols;
        cols[0] = r.removeFromLeft (90);
        cols[1] = r.removeFromLeft (110);
        cols[2] = r.removeFromLeft (110);
        cols[3] = r.removeFromLeft (110);
        cols[4] = r.removeFromLeft (80);
        cols[5] = r;
        return cols;
    }

    std::vector<SpeakerDef> layoutDefs() const
    {
        switch (layoutBox.getSelectedId())
        {
            default:
            case 1: return { { "Left", 0, 0, false, 0.0f }, { "Right", 1, 1, false, 0.0f } };
            case 2: return { { "Left", 0, 0, false, 0.0f }, { "Right", 1, 1, false, 0.0f },
                             { "Subwoofer", -1, 2, true, 0.0f } };
            case 3: return { { "Left", 0, 0, false, 0.0f }, { "Right", 1, 1, false, 0.0f },
                             { "Left surround", 2, 2, false, 0.0f }, { "Right surround", 3, 3, false, 0.0f } };
            case 4: return { { "Left", 0, 0, false, 0.0f }, { "Right", 1, 1, false, 0.0f },
                             { "Left surround", 2, 2, false, 0.0f }, { "Right surround", 3, 3, false, 0.0f },
                             { "Subwoofer", -1, 4, true, 0.0f } };
            case 5: return { { "Left", 0, 0, false, 0.0f }, { "Right", 1, 1, false, 0.0f },
                             { "Centre", 2, 2, false, 0.0f }, { "Subwoofer / LFE", 3, 3, true, 0.0f },
                             { "Left surround", 4, 4, false, 0.0f }, { "Right surround", 5, 5, false, 0.0f } };
            case 6: return { { "Left", 0, 0, false, 0.0f }, { "Right", 1, 1, false, 0.0f },
                             { "Centre", 2, 2, false, 0.0f }, { "Subwoofer / LFE", 3, 3, true, 0.0f },
                             { "Left surround", 4, 4, false, 0.0f }, { "Right surround", 5, 5, false, 0.0f },
                             { "Left back", 6, 6, false, 0.0f }, { "Right back", 7, 7, false, 0.0f } };
        }
    }

    // Default periphonic loudspeaker rigs. For the order's canonical count a
    // hand-tuned regular layout is used; for any other count a near-uniform
    // spherical Fibonacci lattice is generated. The user edits the
    // azimuth/elevation/radius to match the real installation.
    std::vector<SpeakerDef> ambiDefs (int order, int count) const
    {
        std::vector<SpeakerDef> defs;
        auto add = [&] (float az, float el)
        {
            const int idx = (int) defs.size();
            defs.push_back ({ "Spk " + juce::String (idx + 1), -1, idx, false, 0.0f, az, el });
        };

        if (count == defaultSpeakerCount (order))
        {
            if (order == 1)
            {
                // 8-speaker cube (a spherical 3-design).
                const float e = 35.2644f;
                for (float az : { 45.0f, 135.0f, 225.0f, 315.0f }) add (az,  e);
                for (float az : { 45.0f, 135.0f, 225.0f, 315.0f }) add (az, -e);
            }
            else if (order == 2)
            {
                // 12-speaker icosahedron (pentagonal antiprism + poles, a 5-design).
                const float e = 26.5651f;
                add (0.0f, 90.0f);
                for (float az : { 0.0f, 72.0f, 144.0f, 216.0f, 288.0f }) add (az,  e);
                for (float az : { 36.0f, 108.0f, 180.0f, 252.0f, 324.0f }) add (az, -e);
                add (0.0f, -90.0f);
            }
            else
            {
                // 16-speaker dome: two staggered rings of 8.
                for (int k = 0; k < 8; ++k) add (k * 45.0f,           30.0f);
                for (int k = 0; k < 8; ++k) add (k * 45.0f + 22.5f,  -30.0f);
            }
        }
        else
        {
            // Spherical Fibonacci lattice: near-uniform directions for any count.
            constexpr double pi = juce::MathConstants<double>::pi;
            const double ga = pi * (3.0 - std::sqrt (5.0));    // golden angle
            for (int i = 0; i < count; ++i)
            {
                const double z  = 1.0 - 2.0 * ((double) i + 0.5) / (double) count;
                const double th = ga * (double) i;
                const double az = std::atan2 (std::sin (th), std::cos (th)) * 180.0 / pi;
                const double el = std::asin (juce::jlimit (-1.0, 1.0, z)) * 180.0 / pi;
                add ((float) az, (float) el);
            }
        }
        return defs;
    }

    void rebuildRows()
    {
        rows.clear();
        const bool ambi = isAmbisonics();
        const auto defs = ambi ? ambiDefs (ambiOrder(), currentSpeakerCount()) : layoutDefs();

        for (const auto& def : defs)
        {
            auto row = std::make_unique<Row>();
            row->isSub = def.isSub;

            row->name.setText (def.name, juce::dontSendNotification);
            row->name.setColour (juce::Label::textColourId, SuperMoToTheme::text);
            addAndMakeVisible (row->name);

            for (int c = 1; c <= smt::numChannels; ++c)
                row->output.addItem (juce::String (c), c);
            row->output.setSelectedId (def.out + 1, juce::dontSendNotification);
            SuperMoToTheme::accentComboBox (row->output, SuperMoToTheme::master);
            addAndMakeVisible (row->output);

            row->gain.setSliderStyle (juce::Slider::LinearHorizontal);
            row->gain.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
            row->gain.setRange (-60.0, 12.0, 0.1);
            row->gain.setValue (def.gain, juce::dontSendNotification);
            row->gain.setDoubleClickReturnValue (true, 0.0);
            SuperMoToTheme::accentSlider (row->gain, SuperMoToTheme::master);
            addAndMakeVisible (row->gain);

            if (ambi)
            {
                auto initAngle = [this] (juce::Slider& s, double lo, double hi, double val)
                {
                    s.setSliderStyle (juce::Slider::LinearHorizontal);
                    s.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
                    s.setRange (lo, hi, 0.5);
                    s.setValue (val, juce::dontSendNotification);
                    s.setDoubleClickReturnValue (true, val);
                    s.setTextValueSuffix (juce::String::fromUTF8 ("\xc2\xb0"));
                    SuperMoToTheme::accentSlider (s, SuperMoToTheme::master);
                    addAndMakeVisible (s);
                };
                initAngle (row->az, -180.0, 180.0, def.az);
                initAngle (row->el,  -90.0,  90.0, def.el);

                row->radius.setSliderStyle (juce::Slider::LinearHorizontal);
                row->radius.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
                row->radius.setRange (0.3, 15.0, 0.05);
                row->radius.setValue (def.radius, juce::dontSendNotification);
                row->radius.setDoubleClickReturnValue (true, def.radius);
                row->radius.setTextValueSuffix (" m");
                SuperMoToTheme::accentSlider (row->radius, SuperMoToTheme::master);
                addAndMakeVisible (row->radius);
            }
            else
            {
                for (int c = 1; c <= smt::numChannels; ++c)
                    row->input.addItem (juce::String (c), c);
                // The subwoofer can take the sum of all mains instead of an input.
                if (def.isSub)
                    row->input.addItem ("Sum of mains", smt::numChannels + 1);
                row->input.setSelectedId (def.isSub && def.in < 0 ? smt::numChannels + 1
                                                                  : def.in + 1,
                                          juce::dontSendNotification);
                SuperMoToTheme::accentComboBox (row->input, SuperMoToTheme::master);
                addAndMakeVisible (row->input);
            }

            rows.push_back (std::move (row));
        }
        resized();
        repaint();
    }

    // Writes the bass-management crossover onto an output's first EQ band
    // (highpass for a main, lowpass for the sub). Outputs are global, so this
    // describes the physical speaker regardless of the configuration.
    void setOutputCrossover (int out, smt::FilterType type, float fc, bool on)
    {
        if (out < 0 || out >= smt::numChannels)
            return;
        auto s = model.getOutput (out);
        auto& b = s.bands[0];
        b.on    = on;
        b.type  = (int) type;
        b.order = 4;
        b.freq  = fc;
        model.setOutput (out, s);
    }

    // Sets up a periphonic loudspeaker output: the radius compensation (trim,
    // and the delay unless writeDelay is false) and the highpass crossover.
    // Skipping the delay preserves any existing alignment already on the output.
    void setSpeakerOutput (int out, float fc, bool bm, float gainDb, float delayMs, bool writeDelay)
    {
        if (out < 0 || out >= smt::numChannels)
            return;
        auto s = model.getOutput (out);
        s.gainDb  = gainDb;
        if (writeDelay)
            s.delayMs = delayMs;
        auto& b = s.bands[0];
        b.on    = bm;
        b.type  = (int) smt::FilterType::highpass;
        b.order = 4;
        b.freq  = fc;
        model.setOutput (out, s);
    }

    void apply()
    {
        if (isAmbisonics()) { applyAmbisonics(); return; }

        const int target = targetBox.getSelectedId() - 1;
        if (target < 0)
            return;

        model.clearConfig (target);

        const float fc = (float) crossover.getValue();
        const bool bm = bassManagement.getToggleState();

        // Collect mains for the "sum of mains" sub feed.
        std::vector<std::pair<int, int>> mains;   // {input, output}
        for (const auto& row : rows)
            if (! row->isSub)
                mains.push_back ({ row->input.getSelectedId() - 1,
                                   row->output.getSelectedId() - 1 });

        for (const auto& row : rows)
        {
            const int out = row->output.getSelectedId() - 1;
            const float gain = (float) row->gain.getValue();

            // Frames are pure routing now; the crossover lives on the output.
            auto makeFrame = [&] (float gainDb)
            {
                smt::FrameSettings f;
                f.active = true;
                f.gainDb = gainDb;
                return f;
            };

            if (! row->isSub)
            {
                model.setFrame (target, row->input.getSelectedId() - 1, out, makeFrame (gain));
                setOutputCrossover (out, smt::FilterType::highpass, fc, bm);   // main highpass
            }
            else
            {
                // Subwoofer: either one dedicated input (LFE) or the sum of all
                // main inputs; the output lowpass does the bass management.
                const bool sumOfMains = row->input.getSelectedId() == smt::numChannels + 1;

                if (sumOfMains)
                {
                    for (const auto& [mi, mo] : mains)
                    {
                        juce::ignoreUnused (mo);
                        model.setFrame (target, mi, out, makeFrame (gain));
                    }
                }
                else
                {
                    model.setFrame (target, row->input.getSelectedId() - 1, out, makeFrame (gain));
                }
                setOutputCrossover (out, smt::FilterType::lowpass, fc, bm);     // sub lowpass
            }
        }

        status.setText ("Written to configuration " + smt::configName (target)
                        + juce::String::fromUTF8 (" \xe2\x80\x94 fine-tune it in the matrix view."),
                        juce::dontSendNotification);
    }

    void applyAmbisonics()
    {
        const int target = targetBox.getSelectedId() - 1;
        if (target < 0)
            return;

        model.clearConfig (target);

        const int order = ambiOrder();
        const int H = smt::ambisonics::numHarmonics (order);     // 4 / 9 / 16
        const float fc = (float) crossover.getValue();
        const bool bm = bassManagement.getToggleState();
        const bool writeDelay = writeDelayToggle.getToggleState();

        // Gather the loudspeaker directions, outputs, trims and radii.
        std::vector<smt::SpeakerDir> dirs;
        std::vector<int> outs;
        std::vector<float> trims, radii;
        for (const auto& row : rows)
        {
            dirs.push_back ({ (float) row->az.getValue(), (float) row->el.getValue() });
            outs.push_back (row->output.getSelectedId() - 1);
            trims.push_back ((float) row->gain.getValue());
            radii.push_back ((float) row->radius.getValue());
        }

        const auto D = smt::ambisonics::decodeMatrix (order, dirs);   // L x H, signed

        int maxOut = 0;
        for (int o : outs)
            maxOut = std::max (maxOut, o);

        // Radius compensation, referenced to the farthest speaker: closer
        // speakers are delayed (so all arrive together at the centre) and
        // attenuated (inverse-distance law). This never boosts or asks for a
        // negative delay.
        float rMax = 0.3f;
        for (float r : radii)
            rMax = std::max (rMax, r);

        // One frame per non-negligible decode coefficient: input = harmonic
        // (ACN index), output = speaker. The per-speaker trim folds into the
        // frame gain; the coefficient sign becomes a polarity flip. The radius
        // gain/delay go on the speaker's output.
        for (size_t s = 0; s < dirs.size(); ++s)
        {
            const int out = outs[s];
            for (int j = 0; j < H; ++j)
            {
                const double coeff = D[s][(size_t) j];
                if (std::abs (coeff) < 1.0e-4)
                    continue;

                smt::FrameSettings f;
                f.active = true;
                f.gainDb = (float) (20.0 * std::log10 (std::abs (coeff))) + trims[s];
                f.phaseInvert = coeff < 0.0;
                model.setFrame (target, j, out, f);
            }

            const float r = juce::jmax (0.3f, radii[s]);
            const float radiusGainDb  = juce::jlimit (-24.0f, 0.0f,
                                                      (float) (20.0 * std::log10 (r / rMax)));
            const float radiusDelayMs = juce::jlimit (0.0f, smt::maxDelayMs,
                                                      (float) ((rMax - r) / speedOfSound * 1000.0));
            setSpeakerOutput (out, fc, bm, radiusGainDb, radiusDelayMs, writeDelay);   // crossover + radius comp
        }

        int neededOuts = maxOut + 1;

        if (bm)
        {
            // Bass management: the omnidirectional W component (ACN 0) feeds the
            // subwoofer on the next free output, lowpassed by the output EQ.
            const int subOut = juce::jmin (maxOut + 1, smt::numChannels - 1);
            neededOuts = juce::jmax (neededOuts, subOut + 1);

            smt::FrameSettings f;
            f.active = true;
            model.setFrame (target, 0, subOut, f);
            setOutputCrossover (subOut, smt::FilterType::lowpass, fc, true);
        }

        // Grow the visible/processed matrix to expose the B-format inputs and
        // the speaker (+ sub) outputs.
        model.setMatrixSize (juce::jmax (model.getNumIns(), H),
                             juce::jmax (model.getNumOuts(), neededOuts));

        const juce::String emdash = juce::String::fromUTF8 ("\xe2\x80\x94");
        status.setText ("Ambisonics order " + juce::String (order) + ", "
                        + juce::String ((int) dirs.size()) + " speakers ("
                        + juce::String (H) + " B-format inputs) " + emdash + " config "
                        + smt::configName (target)
                        + (writeDelay ? ", radius delay+trim" : ", radius trim only (delays kept)")
                        + (bm ? ", sub = W lowpass." : ", no bass management."),
                        juce::dontSendNotification);
    }

    smt::ConfigModel& model;

    juce::Label title, layoutLabel, targetLabel, crossoverLabel, numSpkLabel, status;
    juce::ComboBox layoutBox, targetBox;
    juce::ToggleButton bassManagement, writeDelayToggle;
    fxme::FxmeSlider crossover, numSpkSlider;
    juce::TextButton applyButton;

    std::vector<std::unique_ptr<Row>> rows;
    int rowsTop = 120;
    int rowH = 28;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ConfigToolComponent)
};
