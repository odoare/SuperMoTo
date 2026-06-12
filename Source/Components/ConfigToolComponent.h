/*
  ------------------------------------------------------------------------------
    ConfigToolComponent.h

    Configuration tool for standard speaker layouts (2.0, 2.1, 4.0, 5.1,
    7.1). A convenient way to fill one of the A..F matrix configurations:
    pick which plugin inputs/outputs carry each speaker, the gains, the
    bass-management crossover and the subwoofer routing, then Apply writes
    the corresponding frames into the model. The sum of the main channels
    is sent lowpassed to the subwoofer output; mains are highpassed when
    bass management is enabled. Further per-frame tuning (and FIR
    correction curves per output) is then done in the matrix view.

    Author: Olivier Doaré, github.com/odoare
    Licenced under the GNU LGPL Version 3.0
    SPDX-License-Identifier: LGPL-3.0-or-later
  ------------------------------------------------------------------------------
*/

#pragma once

#include <JuceHeader.h>
#include "../Model/ConfigModel.h"
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

        layoutBox.addItemList ({ "Stereo 2.0", "Stereo 2.1", "Quad 4.0", "5.1", "7.1" }, 1);
        SuperMoToTheme::accentComboBox (layoutBox, SuperMoToTheme::master);
        layoutBox.onChange = [this] { rebuildRows(); };
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
        crossover.setTextBoxStyle (juce::Slider::TextBoxRight, false, 64, 18);
        crossover.setRange (40.0, 300.0, 1.0);
        crossover.setValue (80.0, juce::dontSendNotification);
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
            auto cols = splitRow (r);
            g.drawText ("Speaker", cols[0], juce::Justification::centredLeft);
            g.drawText ("Input",   cols[1], juce::Justification::centred);
            g.drawText ("Output",  cols[2], juce::Justification::centred);
            g.drawText ("Gain (dB)", cols[3], juce::Justification::centred);
        }
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced (14);
        title.setBounds (area.removeFromTop (26));
        area.removeFromTop (6);

        auto top = area.removeFromTop (24);
        layoutLabel.setBounds (top.removeFromLeft (50));
        layoutBox.setBounds (top.removeFromLeft (130));
        top.removeFromLeft (20);
        targetLabel.setBounds (top.removeFromLeft (100));
        targetBox.setBounds (top.removeFromLeft (60));

        area.removeFromTop (8);
        auto bm = area.removeFromTop (24);
        bassManagement.setBounds (bm.removeFromLeft (300));
        crossoverLabel.setBounds (bm.removeFromLeft (70));
        crossover.setBounds (bm);

        rowsTop = area.getY() + 24;
        for (size_t i = 0; i < rows.size(); ++i)
        {
            auto r = rowArea ((int) i);
            auto cols = splitRow (r);
            rows[i]->name.setBounds (cols[0]);
            rows[i]->input.setBounds (cols[1].reduced (4, 1));
            rows[i]->output.setBounds (cols[2].reduced (4, 1));
            rows[i]->gain.setBounds (cols[3]);
        }

        auto bottom = getLocalBounds().reduced (14).removeFromBottom (30);
        applyButton.setBounds (bottom.removeFromLeft (220));
        bottom.removeFromLeft (12);
        status.setBounds (bottom);
    }

private:
    struct SpeakerDef { juce::String name; int in, out; bool isSub; float gain; };

    struct Row
    {
        juce::Label name;
        juce::ComboBox input, output;
        juce::Slider gain;
        bool isSub = false;
    };

    void addLabel (juce::Label& l, const juce::String& text)
    {
        l.setText (text, juce::dontSendNotification);
        l.setFont (juce::Font (12.0f));
        l.setColour (juce::Label::textColourId, SuperMoToTheme::dimText);
        addAndMakeVisible (l);
    }

    juce::Rectangle<int> rowArea (int i) const
    {
        return { 14, rowsTop + i * 28, getWidth() - 28, 26 };
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
                             { "Centre", 2, 2, false, 0.0f }, { "Subwoofer / LFE", 3, 3, true, 0.0f },
                             { "Left surround", 4, 4, false, 0.0f }, { "Right surround", 5, 5, false, 0.0f } };
            case 5: return { { "Left", 0, 0, false, 0.0f }, { "Right", 1, 1, false, 0.0f },
                             { "Centre", 2, 2, false, 0.0f }, { "Subwoofer / LFE", 3, 3, true, 0.0f },
                             { "Left surround", 4, 4, false, 0.0f }, { "Right surround", 5, 5, false, 0.0f },
                             { "Left back", 6, 6, false, 0.0f }, { "Right back", 7, 7, false, 0.0f } };
        }
    }

    void rebuildRows()
    {
        rows.clear();
        for (const auto& def : layoutDefs())
        {
            auto row = std::make_unique<Row>();
            row->isSub = def.isSub;

            row->name.setText (def.name, juce::dontSendNotification);
            row->name.setColour (juce::Label::textColourId, SuperMoToTheme::text);
            addAndMakeVisible (row->name);

            for (int c = 1; c <= smt::numChannels; ++c)
            {
                row->input.addItem (juce::String (c), c);
                row->output.addItem (juce::String (c), c);
            }
            // The subwoofer can take the sum of all mains instead of an input.
            if (def.isSub)
                row->input.addItem ("Sum of mains", smt::numChannels + 1);

            row->input.setSelectedId (def.isSub && def.in < 0 ? smt::numChannels + 1
                                                              : def.in + 1,
                                      juce::dontSendNotification);
            row->output.setSelectedId (def.out + 1, juce::dontSendNotification);
            SuperMoToTheme::accentComboBox (row->input, SuperMoToTheme::master);
            SuperMoToTheme::accentComboBox (row->output, SuperMoToTheme::master);
            addAndMakeVisible (row->input);
            addAndMakeVisible (row->output);

            row->gain.setSliderStyle (juce::Slider::LinearHorizontal);
            row->gain.setTextBoxStyle (juce::Slider::TextBoxRight, false, 56, 18);
            row->gain.setRange (-60.0, 12.0, 0.1);
            row->gain.setValue (def.gain, juce::dontSendNotification);
            row->gain.setDoubleClickReturnValue (true, 0.0);
            SuperMoToTheme::accentSlider (row->gain, SuperMoToTheme::master);
            addAndMakeVisible (row->gain);

            rows.push_back (std::move (row));
        }
        resized();
        repaint();
    }

    void apply()
    {
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

            if (! row->isSub)
            {
                smt::FrameSettings f;
                f.active = true;
                f.gainDb = gain;
                if (bm)
                {
                    f.filterOn = true;
                    f.filterType = (int) smt::FilterType::highpass;
                    f.filterOrder = 4;
                    f.filterFreq = fc;
                }
                model.setFrame (target, row->input.getSelectedId() - 1, out, f);
            }
            else
            {
                // Subwoofer: either one dedicated input (LFE) or the lowpassed
                // sum of all main inputs.
                const bool sumOfMains = row->input.getSelectedId() == smt::numChannels + 1;

                auto makeSubFrame = [&] (float gainDb)
                {
                    smt::FrameSettings f;
                    f.active = true;
                    f.gainDb = gainDb;
                    f.filterOn = true;
                    f.filterType = (int) smt::FilterType::lowpass;
                    f.filterOrder = 4;
                    f.filterFreq = fc;
                    return f;
                };

                if (sumOfMains)
                {
                    for (const auto& [mi, mo] : mains)
                    {
                        juce::ignoreUnused (mo);
                        model.setFrame (target, mi, out, makeSubFrame (gain));
                    }
                }
                else
                {
                    model.setFrame (target, row->input.getSelectedId() - 1, out,
                                    makeSubFrame (gain));
                }
            }
        }

        status.setText ("Written to configuration " + smt::configName (target)
                        + juce::String::fromUTF8 (" \xe2\x80\x94 fine-tune it in the matrix view."),
                        juce::dontSendNotification);
    }

    smt::ConfigModel& model;

    juce::Label title, layoutLabel, targetLabel, crossoverLabel, status;
    juce::ComboBox layoutBox, targetBox;
    juce::ToggleButton bassManagement;
    juce::Slider crossover;
    juce::TextButton applyButton;

    std::vector<std::unique_ptr<Row>> rows;
    int rowsTop = 120;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ConfigToolComponent)
};
