/*
  ------------------------------------------------------------------------------
    InfoButton.h

    Small round "i" help button. When clicked it shows a CallOutBox with a
    title and a scrollable body of help / shortcuts for the current page. The
    editor reuses one instance, re-pointing its content per view.

    Author: Olivier Doaré, github.com/odoare
    Licenced under the GNU LGPL Version 3.0
    SPDX-License-Identifier: LGPL-3.0-or-later
  ------------------------------------------------------------------------------
*/

#pragma once

#include <JuceHeader.h>
#include "../Theme.h"

class InfoButton : public juce::Button
{
public:
    InfoButton() : juce::Button ("info")
    {
        setTooltip ("Help & shortcuts for this page");
    }

    void setInfo (juce::String titleText, juce::String bodyText)
    {
        infoTitle = std::move (titleText);
        infoBody  = std::move (bodyText);
    }

    void paintButton (juce::Graphics& g, bool over, bool down) override
    {
        auto b = getLocalBounds().toFloat().reduced (1.0f);
        const float d = juce::jmin (b.getWidth(), b.getHeight());
        const auto circle = juce::Rectangle<float> (d, d).withCentre (b.getCentre());

        g.setColour (down ? SuperMoToTheme::measure.darker (0.3f)
                   : over ? SuperMoToTheme::measure
                          : SuperMoToTheme::measure.darker (0.6f));
        g.fillEllipse (circle);
        g.setColour (SuperMoToTheme::text);
        g.setFont (juce::Font (d * 0.64f, juce::Font::bold | juce::Font::italic));
        g.drawText ("i", circle, juce::Justification::centred);
    }

    void clicked() override
    {
        if (infoBody.isEmpty())
            return;
        juce::CallOutBox::launchAsynchronously (
            std::make_unique<Content> (infoTitle, infoBody),
            getScreenBounds(), nullptr);
    }

private:
    struct Content : public juce::Component
    {
        Content (const juce::String& t, const juce::String& body)
        {
            title.setText (t, juce::dontSendNotification);
            title.setFont (juce::Font (16.0f, juce::Font::bold));
            title.setColour (juce::Label::textColourId, SuperMoToTheme::text);
            addAndMakeVisible (title);

            text.setMultiLine (true, true);
            text.setReadOnly (true);
            text.setScrollbarsShown (true);
            text.setCaretVisible (false);
            text.setPopupMenuEnabled (false);
            text.setColour (juce::TextEditor::backgroundColourId, juce::Colours::transparentBlack);
            text.setColour (juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
            text.setColour (juce::TextEditor::focusedOutlineColourId, juce::Colours::transparentBlack);
            text.setColour (juce::TextEditor::textColourId, SuperMoToTheme::text);
            text.setFont (juce::Font (13.0f));
            text.setText (body, juce::dontSendNotification);
            addAndMakeVisible (text);

            setSize (470, 430);
        }

        void paint (juce::Graphics& g) override
        {
            g.fillAll (SuperMoToTheme::panel);
            g.setColour (SuperMoToTheme::panelLine);
            g.drawRect (getLocalBounds(), 1);
        }

        void resized() override
        {
            auto r = getLocalBounds().reduced (16, 14);
            title.setBounds (r.removeFromTop (24));
            r.removeFromTop (6);
            text.setBounds (r);
        }

        juce::Label title;
        juce::TextEditor text;
    };

    juce::String infoTitle, infoBody;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (InfoButton)
};
