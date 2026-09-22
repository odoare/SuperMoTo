/*
  ------------------------------------------------------------------------------
    SplashScreenComponent.h

    The splash: a drawn card (no artwork file but the company logo) with the
    acoustic blueprint grid, the product name, a main-versus-subwoofer wave
    pair whose phase offset closes as it comes up, and the FX-Mechanics logo
    over a link to the site.

    Two ways up, both driven by the owner:
      - at startup, timed — it goes by itself after `displaySeconds`;
      - from the top bar's logo, where it stays until it is clicked.
    Clicking it always dismisses it, except on the logo and the link at the
    bottom, which open fx-mechanics.com and leave the card up.

    Policy lives with the owner, as it does for fxme::SplashOverlay: this
    component knows how to appear and when it has finished, not when it should
    be shown. PluginEditor shows it once per plugin instance (a flag on
    smt::EditorSettings, which lives on the processor and so outlives the
    window), only when the editor is in its full layout, and hides it again
    from onFinished.

    Author: Olivier Doaré, github.com/odoare
    Licenced under the GNU AGPL Version 3.0, or commercial terms (LICENSE.md)
    SPDX-License-Identifier: AGPL-3.0-or-later OR LicenseRef-FXME-Commercial
  ------------------------------------------------------------------------------
*/

#pragma once

#include <JuceHeader.h>

#include <utility>

class SplashScreenComponent : public juce::Component,
                              private juce::Timer
{
public:
    /** The size the card is drawn for. The owner gives it these bounds; the
        drawing is laid out for them rather than for an arbitrary rectangle. */
    static constexpr int designWidth = 600, designHeight = 380;

    /** How long the timed (startup) card stays up, and how long the two waves
        take to pull into phase. The alignment finishes first: it is the card
        arriving, not a progress report — nothing is being measured. */
    static constexpr double displaySeconds = 3.0, alignSeconds = 2.0;

    static constexpr const char* websiteUrl = "https://fx-mechanics.com";

    SplashScreenComponent()
    {
        setSize (designWidth, designHeight);
        setInterceptsMouseClicks (true, false);     // eat clicks while up
        setMouseCursor (juce::MouseCursor::PointingHandCursor);

        logo = juce::ImageCache::getFromMemory (BinaryData::logo686_png,
                                                BinaryData::logo686_pngSize);
    }

    ~SplashScreenComponent() override
    {
        stopTimer();
    }

    /** Told once, when the card has had its time or a click has dismissed it.
        The owner hides or deletes the splash from here. */
    std::function<void()> onFinished;

    /** Starts the animation from the beginning and makes the splash visible.
        With `autoDismiss` it goes by itself after displaySeconds; without, it
        stays until clicked. Calling it while one is up restarts it. */
    void show (bool autoDismiss)
    {
        timed = autoDismiss;
        startTime = juce::Time::getMillisecondCounterHiRes();
        setVisible (true);
        toFront (false);                            // over the editor, never focused
        startTimerHz (60);                          // the waves run the whole time
    }

    /** Takes the card down as a click would, and tells the owner. Does
        nothing if it is not up. */
    void dismiss()
    {
        if (! isTimerRunning())
            return;

        stopTimer();
        if (onFinished != nullptr)
            onFinished();
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        // The company mark is the one live thing on the card; everywhere else
        // dismisses, which is what the pointing hand promises.
        if (brandArea.contains (e.position))
            juce::URL (websiteUrl).launchInDefaultBrowser();
        else
            dismiss();
    }

    void mouseMove (const juce::MouseEvent& e) override
    {
        const bool over = brandArea.contains (e.position);

        if (over != linkHot)
        {
            linkHot = over;
            repaint();
        }
    }

    void mouseExit (const juce::MouseEvent&) override
    {
        if (std::exchange (linkHot, false))
            repaint();
    }

    void paint (juce::Graphics& g) override
    {
        auto bounds = getLocalBounds().toFloat();

        // 1. Dark slate background and the radial acoustic blueprint grid
        g.fillAll (juce::Colour::fromRGB (18, 20, 23));
        drawAcousticGrid (g, bounds);

        // 2. The card's content, top to bottom. Every block takes its own
        //    strip, so nothing can end up drawn over anything else.
        auto body = bounds.reduced (24.0f);

        drawTitle    (g, body.removeFromTop (52.0f));
        drawTagline  (g, body.removeFromTop (24.0f));
        body.removeFromTop (12.0f);
        drawPhaseAlignmentAnimation (g, body.removeFromTop (108.0f));
        body.removeFromTop (8.0f);
        drawBranding (g, body.removeFromTop (100.0f));

        // 3. Version, from the build rather than typed in, as the top bar's
        //    is; and, on the card that waits for a click, how to close it.
        g.setFont (styledFont ("Inter", 12.0f, juce::Font::plain));
        g.setColour (juce::Colour::fromRGB (110, 116, 128));

        auto footer = bounds.withTop (bounds.getBottom() - 28.0f).reduced (20.0f, 0.0f);
        g.drawText ("v" + juce::String (ProjectInfo::versionString),
                    footer, juce::Justification::centredLeft);

        if (! timed)
            g.drawText ("click to close", footer, juce::Justification::centredRight);

        // 4. The card's edge, so it reads as one over whatever is behind it.
        g.setColour (juce::Colour::fromRGB (0, 229, 255).withAlpha (0.25f));
        g.drawRoundedRectangle (bounds.reduced (0.5f), 6.0f, 1.0f);
    }

private:
    static constexpr float waveAmplitude = 28.0f;

    juce::Image logo;
    double startTime = 0.0;
    double elapsed = 0.0;       // seconds since show(): drives the waves
    bool timed = true;          // false when the card waits for a click
    bool linkHot = false;
    juce::Rectangle<float> brandArea;   // logo + link, as last painted

    void timerCallback() override
    {
        elapsed = (juce::Time::getMillisecondCounterHiRes() - startTime) / 1000.0;
        repaint();

        if (timed && elapsed >= displaySeconds)
            dismiss();
    }

    /** The requested family when the machine has it, the default sans when it
        does not. Without this the fallback is silent, and a splash drawn in a
        font nobody chose is worse than one drawn in the system's own. */
    static juce::Font styledFont (const char* family, float height, int styleFlags)
    {
        static const auto available = juce::Font::findAllTypefaceNames();
        const juce::String name (family);
        return juce::Font (juce::FontOptions (available.contains (name)
                                                  ? name
                                                  : juce::Font::getDefaultSansSerifFontName(),
                                              height, styleFlags));
    }

    void drawAcousticGrid (juce::Graphics& g, juce::Rectangle<float> bounds)
    {
        auto center = bounds.getCentre();
        const float reach = juce::jmax (bounds.getWidth(), bounds.getHeight());
        g.setColour (juce::Colour::fromRGB (50, 56, 64).withAlpha (0.6f));

        // Concentric acoustic diffusion circles
        for (float r = 40.0f; r < bounds.getWidth() * 0.6f; r += 35.0f)
            g.drawEllipse (center.x - r, center.y - r - 20.0f, r * 2.0f, r * 2.0f, 1.0f);

        // Polar angle lines, long enough to leave the card at any size
        for (int angle = 0; angle < 360; angle += 30)
        {
            auto rad = juce::degreesToRadians (static_cast<float> (angle));
            juce::Point<float> p (center.x + std::cos (rad) * reach,
                                  center.y - 20.0f + std::sin (rad) * reach);
            g.drawLine (center.x, center.y - 20.0f, p.x, p.y, 0.7f);
        }
    }

    void drawTitle (juce::Graphics& g, juce::Rectangle<float> area)
    {
        // "SUPER" in white, "MoTo" in cyan.
        const auto titleFont = styledFont ("Space Grotesk", 34.0f, juce::Font::bold);

        juce::AttributedString titleText;
        titleText.setJustification (juce::Justification::centred);
        titleText.append ("SUPER", titleFont, juce::Colours::white);
        titleText.append ("MoTo", titleFont, juce::Colour::fromRGB (0, 229, 255));
        titleText.draw (g, area);
    }

    void drawTagline (juce::Graphics& g, juce::Rectangle<float> area)
    {
        g.setFont (styledFont ("Inter", 14.0f, juce::Font::plain));
        g.setColour (juce::Colour::fromRGB (160, 166, 178));
        g.drawText ("ROOM CORRECTION  /  SPEAKER MANAGER  /  PHASE ALIGNMENT",
                    area, juce::Justification::centred, false);
    }

    void drawPhaseAlignmentAnimation (juce::Graphics& g, juce::Rectangle<float> area)
    {
        auto center = area.getCentre();
        const float waveWidth = area.getWidth() * 0.7f;
        const float startX = center.x - waveWidth * 0.5f;
        const float cy = center.y;

        // The offset closes over alignSeconds and then holds: the mains and
        // the sub arriving together, which is what the plugin is for. The
        // waves keep travelling afterwards, so the card that waits for a
        // click is never a still image.
        const float t = juce::jlimit (0.0f, 1.0f, (float) (elapsed / alignSeconds));
        const float closed = t * t * (3.0f - 2.0f * t);             // smoothstep
        const float phaseOffset = (1.0f - closed) * juce::MathConstants<float>::pi * 1.2f;
        const float timeMod = (float) elapsed * 3.0f;

        juce::Path subWave;     // amber subwoofer wave
        juce::Path mainsWave;   // cyan mains wave

        subWave.startNewSubPath (startX, cy);
        mainsWave.startNewSubPath (startX, cy);

        for (float x = 0; x <= waveWidth; x += 2.0f)
        {
            const float normX = x / waveWidth;
            const float envelope = std::sin (normX * juce::MathConstants<float>::pi);

            subWave.lineTo   (startX + x, cy + std::sin (normX * 12.0f + timeMod)
                                                  * waveAmplitude * envelope);
            mainsWave.lineTo (startX + x, cy + std::sin (normX * 12.0f + timeMod + phaseOffset)
                                                  * waveAmplitude * envelope);
        }

        g.setColour (juce::Colour::fromRGB (255, 145, 0).withAlpha (0.7f));
        g.strokePath (subWave, juce::PathStrokeType (2.0f));

        g.setColour (juce::Colour::fromRGB (0, 229, 255).withAlpha (0.85f));
        g.strokePath (mainsWave, juce::PathStrokeType (2.0f));
    }

    /** The company mark: the logo over the site address, both live. The area
        they occupy is remembered for the hit test, so it follows the layout
        rather than being written down twice. */
    void drawBranding (juce::Graphics& g, juce::Rectangle<float> area)
    {
        auto logoArea = area.removeFromTop (72.0f);
        auto linkArea = area;

        if (logo.isValid())
        {
            const float h = logoArea.getHeight();
            const float w = h * (float) logo.getWidth() / (float) logo.getHeight();
            logoArea = logoArea.withSizeKeepingCentre (w, h);
            g.drawImage (logo, logoArea, juce::RectanglePlacement::centred);
        }
        else
        {
            logoArea = {};      // nothing drawn, nothing to click
        }

        const auto linkFont = styledFont ("Inter", 16.0f, juce::Font::plain);
        g.setFont (linkFont);
        g.setColour (juce::Colour::fromRGB (0, 229, 255).withAlpha (linkHot ? 1.0f : 0.8f));

        const juce::String address ("fx-mechanics.com");
        g.drawText (address, linkArea, juce::Justification::centred, false);

        const float textW = juce::GlyphArrangement::getStringWidth (linkFont, address);
        auto underline = linkArea.withSizeKeepingCentre (textW, linkArea.getHeight());

        if (linkHot)
            g.fillRect (underline.getX(), underline.getCentreY() + linkFont.getHeight() * 0.42f,
                        textW, 1.0f);

        brandArea = logoArea.getUnion (underline);
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SplashScreenComponent)
};
