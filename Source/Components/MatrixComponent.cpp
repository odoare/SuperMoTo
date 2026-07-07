/*
  ------------------------------------------------------------------------------
    MatrixComponent.cpp

    Author: Olivier Doaré, github.com/odoare
    Licenced under the GNU LGPL Version 3.0
    SPDX-License-Identifier: LGPL-3.0-or-later
  ------------------------------------------------------------------------------
*/

#include "MatrixComponent.h"
#include "../AppSettings.h"

using namespace smt;

MatrixComponent::MatrixComponent (ConfigModel& m, MatrixEngine& e)
    : model (m), engine (e)
{
    model.addListener (this);
    startTimerHz (20);      // vu-meter refresh
    setOpaque (false);
    setWantsKeyboardFocus (true);   // arrow-key navigation + single-key actions
}

MatrixComponent::~MatrixComponent()
{
    model.removeListener (this);
}

//==============================================================================
juce::Rectangle<int> MatrixComponent::gridArea() const
{
    // Output strip on top, column numbers along the bottom.
    return getLocalBounds().withTrimmedLeft (labelW).withTrimmedTop (outputStripH)
                           .withTrimmedBottom (headerH);
}

juce::Rectangle<int> MatrixComponent::frameBounds (int in, int out) const
{
    const auto g = gridArea();
    const float cw = (float) g.getWidth() / (float) model.getNumOuts();
    const float ch = (float) g.getHeight() / (float) model.getNumIns();
    return juce::Rectangle<float> ((float) g.getX() + (float) out * cw,
                                   (float) g.getY() + (float) in * ch,
                                   cw, ch).toNearestInt().reduced (1);
}

juce::Rectangle<int> MatrixComponent::outputCellBounds (int out) const
{
    const float cw = (float) (getWidth() - labelW) / (float) model.getNumOuts();
    return juce::Rectangle<float> ((float) labelW + (float) out * cw,
                                   2.0f,
                                   cw, (float) outputStripH - 4.0f).toNearestInt().reduced (1);
}

bool MatrixComponent::hitTest (juce::Point<int> pos, int& in, int& out, bool& isOutputStrip) const
{
    const int numIns = model.getNumIns();
    const int numOuts = model.getNumOuts();
    const float cw = (float) (getWidth() - labelW) / (float) numOuts;

    if (pos.y < outputStripH)
    {
        out = (int) ((float) (pos.x - labelW) / cw);
        isOutputStrip = true;
        in = -1;
        return out >= 0 && out < numOuts && pos.x >= labelW;
    }

    const auto g = gridArea();
    if (! isGridVisible() || ! g.contains (pos))
        return false;

    const float ch = (float) g.getHeight() / (float) numIns;
    out = (int) ((float) (pos.x - g.getX()) / cw);
    in  = (int) ((float) (pos.y - g.getY()) / ch);
    isOutputStrip = false;
    return in >= 0 && in < numIns && out >= 0 && out < numOuts;
}

//==============================================================================
void MatrixComponent::paint (juce::Graphics& g)
{
    const auto grid = gridArea();
    const int numIns = model.getNumIns();
    const int numOuts = model.getNumOuts();
    const float cw = (float) grid.getWidth() / (float) numOuts;
    const float ch = (float) grid.getHeight() / (float) numIns;
    const bool gridVisible = isGridVisible();

    g.setFont (11.0f);

    // Output strip (always shown, also in collapsed mode)
    g.setFont (10.0f);
    for (int o = 0; o < numOuts; ++o)
    {
        const auto r = outputCellBounds (o).toFloat();
        const auto s = model.getOutput (o);
        const bool hasIr = engine.getFir (o).hasImpulse();

        g.setColour (SuperMoToTheme::panel);
        g.fillRoundedRectangle (r, 3.0f);
        if (o == selStrip)
        {
            g.setColour (SuperMoToTheme::selection);
            g.drawRoundedRectangle (r.reduced (0.5f), 3.0f, 1.4f);
        }
        else
        {
            g.setColour (SuperMoToTheme::panelLine);
            g.drawRoundedRectangle (r.reduced (0.5f), 3.0f, 0.8f);
        }

        // Trim value
        g.setColour (SuperMoToTheme::text);
        g.drawText (juce::String (s.gainDb, 1), r.toNearestInt().removeFromTop (14),
                    juce::Justification::centred);

        // FIR state
        g.setColour (s.firOn && hasIr ? SuperMoToTheme::fir
                     : hasIr          ? SuperMoToTheme::fir.darker (1.2f)
                                      : SuperMoToTheme::dimText.darker (0.8f));
        g.drawText ("FIR", r.toNearestInt().withTrimmedTop (13).removeFromTop (12),
                    juce::Justification::centred);

        // EQ / delay indicators (bottom-left, above the meter).
        juce::String tag;
        if (s.anyBandOn())     tag << "EQ";
        if (s.delayMs > 0.0f)  tag << (tag.isEmpty() ? "" : " ") << "D";
        if (tag.isNotEmpty())
        {
            g.setColour (SuperMoToTheme::fir.brighter (0.2f));
            g.setFont (8.0f);
            g.drawText (tag, r.toNearestInt().withTrimmedLeft (3)
                              .withTop ((int) r.getBottom() - 16).withHeight (8),
                        juce::Justification::centredLeft);
        }

        // Output vu-meter (horizontal, at the bottom of the cell)
        const float lvl = engine.getOutputLevelDb (o);
        const float w = juce::jmap (juce::jlimit (-60.0f, 6.0f, lvl),
                                    -60.0f, 6.0f, 0.0f, r.getWidth() - 6.0f);
        g.setColour (SuperMoToTheme::plotBackground.withAlpha (0.6f));
        g.fillRect (r.getX() + 3.0f, r.getBottom() - 8.0f, r.getWidth() - 6.0f, 5.0f);
        g.setColour (lvl > 0.0f ? SuperMoToTheme::meterClip : SuperMoToTheme::meterOk);
        g.fillRect (r.getX() + 3.0f, r.getBottom() - 8.0f, w, 5.0f);

        if (s.spectrum)
        {
            g.setColour (SuperMoToTheme::spectrum);
            g.fillEllipse (r.getX() + 2.0f, r.getY() + 2.0f, 5.0f, 5.0f);
        }

        // Latency-compensation LED (top-right): lit when the engine delayed this
        // output to align it with the longest output FIR, OR when this output's
        // own manual delay is self-absorbing its own FIR's latency. Hover for
        // the amount (and which one it is).
        if (engine.getOutputLatencyCompSamples (o) > 0
            || engine.getOutputSelfAbsorbedSamples (o) > 0)
        {
            const juce::Rectangle<float> led (r.getRight() - 7.0f, r.getY() + 2.0f, 5.0f, 5.0f);
            g.setColour (SuperMoToTheme::dim);
            g.fillEllipse (led);
            g.setColour (SuperMoToTheme::dim.brighter (0.6f));
            g.drawEllipse (led, 0.5f);
        }
    }

    if (! gridVisible)
        return;

    g.setFont (11.0f);

    // Output numbers (bottom row) and input numbers (left column)
    for (int o = 0; o < numOuts; ++o)
    {
        g.setColour (SuperMoToTheme::dimText);
        g.drawText (juce::String (o + 1),
                    grid.getX() + (int) ((float) o * cw), grid.getBottom(), (int) cw, headerH,
                    juce::Justification::centred);
    }
    for (int i = 0; i < numIns; ++i)
    {
        g.setColour (SuperMoToTheme::inputColour (i).withAlpha (0.9f));
        g.drawText (juce::String (i + 1),
                    0, grid.getY() + (int) ((float) i * ch), labelW - 6, (int) ch,
                    juce::Justification::centredRight);
    }

    // Frames
    for (int i = 0; i < numIns; ++i)
    {
        for (int o = 0; o < numOuts; ++o)
        {
            const auto r = frameBounds (i, o).toFloat();
            const auto f = model.getFrame (editConfig, i, o);
            const auto inCol = SuperMoToTheme::inputColour (i);

            // Cell background
            g.setColour (f.active ? inCol.withAlpha (0.28f)
                                  : SuperMoToTheme::panel.withAlpha (0.55f));
            g.fillRoundedRectangle (r, 3.0f);

            if (i == selIn && o == selOut)
            {
                g.setColour (SuperMoToTheme::selection);
                g.drawRoundedRectangle (r.reduced (0.5f), 3.0f, 1.4f);
            }
            else
            {
                g.setColour (SuperMoToTheme::panelLine.withAlpha (0.6f));
                g.drawRoundedRectangle (r.reduced (0.5f), 3.0f, 0.8f);
            }

            if (! f.active && ! f.spectrum)
                continue;

            // Gain
            if (f.active)
            {
                g.setColour (SuperMoToTheme::text);
                g.setFont (juce::jmin (11.0f, r.getHeight() * 0.42f));
                g.drawText (juce::String (f.gainDb, 1),
                            r.toNearestInt().withTrimmedBottom ((int) (r.getHeight() * 0.35f)),
                            juce::Justification::centred);

                // Indicators (delay lives on the output strip; EQ can now be on
                // the frame too, in addition to the output's own EQ).
                g.setFont (8.0f);
                juce::String tags;
                if (f.phaseInvert)      tags << juce::String::fromUTF8 ("\xc3\x98");
                if (f.anyBandOn())      tags << (tags.isEmpty() ? "" : " ") << "EQ";
                g.setColour (inCol.brighter (0.4f));
                g.drawText (tags, (int) r.getX() + 2, (int) (r.getBottom() - 11.0f),
                            (int) r.getWidth() - 10, 9, juce::Justification::centredLeft);

                // Frame vu-meter: thin bar on the right edge
                const float lvl = engine.getFrameLevelDb (editConfig, i, o);
                const float h = juce::jmap (juce::jlimit (-60.0f, 6.0f, lvl),
                                            -60.0f, 6.0f, 0.0f, r.getHeight() - 4.0f);
                g.setColour (SuperMoToTheme::plotBackground.withAlpha (0.5f));
                g.fillRect (r.getRight() - 4.0f, r.getY() + 2.0f, 2.5f, r.getHeight() - 4.0f);
                g.setColour (lvl > 0.0f ? SuperMoToTheme::meterClip
                                        : inCol.brighter (0.6f));
                g.fillRect (r.getRight() - 4.0f, r.getBottom() - 2.0f - h, 2.5f, h);
            }

            // Analyzer checkbox marker
            if (f.spectrum)
            {
                g.setColour (SuperMoToTheme::spectrum);
                g.fillEllipse (r.getX() + 2.0f, r.getY() + 2.0f, 5.0f, 5.0f);
            }
        }
    }
}

//==============================================================================
void MatrixComponent::mouseDown (const juce::MouseEvent& e)
{
    int in = -1, out = -1;
    bool strip = false;
    if (! hitTest (e.getPosition(), in, out, strip))
        return;

    draggingFrame = draggingOutput = false;

    if (strip)
    {
        if (e.mods.isPopupMenu())            { showOutputMenu (out); return; }

        auto s = model.getOutput (out);
        if (e.mods.isAltDown())
        {
            s.spectrum = ! s.spectrum;
            model.setOutput (out, s);
            return;
        }

        // Select the output for the output editor, and prepare a trim drag.
        selStrip = out;
        selIn = selOut = -1;
        if (onOutputSelected != nullptr)
            onOutputSelected (out);

        draggingOutput = true;
        dragOut = out;
        dragStartGain = s.gainDb;
        repaint();
        return;
    }

    if (e.mods.isPopupMenu())                { showFrameMenu (in, out); return; }

    auto f = model.getFrame (editConfig, in, out);

    if (e.mods.isAltDown())
    {
        f.spectrum = ! f.spectrum;
        model.setFrame (editConfig, in, out, f);
        return;
    }

    selIn = in; selOut = out;
    selStrip = -1;
    if (onFrameSelected != nullptr)
        onFrameSelected (in, out);

    draggingFrame = true;
    dragIn = in; dragOut = out;
    dragStartGain = f.gainDb;
    repaint();
}

void MatrixComponent::mouseDrag (const juce::MouseEvent& e)
{
    const float dy = (float) -e.getDistanceFromDragStartY();
    if (std::abs (dy) < 3.0f)
        return;

    if (draggingFrame)
    {
        auto f = model.getFrame (editConfig, dragIn, dragOut);
        if (! f.active)
            return;
        f.gainDb = juce::jlimit (-60.0f, 12.0f, dragStartGain + dy * 0.15f);
        model.setFrame (editConfig, dragIn, dragOut, f);
    }
    else if (draggingOutput)
    {
        auto s = model.getOutput (dragOut);
        s.gainDb = juce::jlimit (-60.0f, 12.0f, dragStartGain + dy * 0.15f);
        model.setOutput (dragOut, s);
    }
}

void MatrixComponent::mouseDoubleClick (const juce::MouseEvent& e)
{
    int in = -1, out = -1;
    bool strip = false;
    if (! hitTest (e.getPosition(), in, out, strip))
        return;

    if (strip)
    {
        auto s = model.getOutput (out);
        s.firOn = ! s.firOn;
        model.setOutput (out, s);
        return;
    }

    auto f = model.getFrame (editConfig, in, out);
    f.active = ! f.active;
    model.setFrame (editConfig, in, out, f);

    selIn = in; selOut = out;
    selStrip = -1;
    if (onFrameSelected != nullptr)
        onFrameSelected (in, out);
}

void MatrixComponent::selectCell (int row, int col)
{
    col = juce::jlimit (0, model.getNumOuts() - 1, col);

    if (row < 0)
    {
        selStrip = col;
        selIn = selOut = -1;
        if (onOutputSelected != nullptr)
            onOutputSelected (col);
    }
    else
    {
        selIn = juce::jlimit (0, model.getNumIns() - 1, row);
        selOut = col;
        selStrip = -1;
        if (onFrameSelected != nullptr)
            onFrameSelected (selIn, selOut);
    }
    repaint();
}

bool MatrixComponent::keyPressed (const juce::KeyPress& key)
{
    const int numIns = model.getNumIns();
    const int numOuts = model.getNumOuts();
    if (numOuts <= 0)
        return false;

    // ── Navigation: arrows move the selection (output strip = row -1), wrapping.
    const bool left  = key.isKeyCode (juce::KeyPress::leftKey);
    const bool right = key.isKeyCode (juce::KeyPress::rightKey);
    const bool up    = key.isKeyCode (juce::KeyPress::upKey);
    const bool down  = key.isKeyCode (juce::KeyPress::downKey);

    if (left || right || up || down)
    {
        // Current position. row -1 = output strip, 0..numIns-1 = input rows.
        int row, col;
        if      (selStrip >= 0) { row = -1;    col = selStrip; }
        else if (selIn >= 0)    { row = selIn; col = selOut; }
        else                    { row = -1;    col = 0; }       // nothing selected yet

        if (left || right)
        {
            col = (col + (right ? 1 : -1) + numOuts) % numOuts;
        }
        else
        {
            // Vertical list folds the strip in as index 0, inputs as 1..numIns.
            const int rows = numIns + 1;
            int idx = (row + 1 + (down ? 1 : -1) + rows) % rows;
            row = idx - 1;
        }

        selectCell (row, col);
        return true;
    }

    // ── Single-key actions on the current selection ─────────────────────────
    const juce_wchar ch = juce::CharacterFunctions::toLowerCase (key.getTextCharacter());
    const bool plus  = ch == '+' || ch == '=' || key.isKeyCode (juce::KeyPress::numberPadAdd);
    const bool minus = ch == '-' || ch == '_' || key.isKeyCode (juce::KeyPress::numberPadSubtract);

    if (selIn >= 0 && selOut >= 0)          // a frame is selected
    {
        auto f = model.getFrame (editConfig, selIn, selOut);
        bool handled = true;
        if      (ch == 'a')  f.active      = ! f.active;
        else if (ch == 'p')  f.phaseInvert = ! f.phaseInvert;
        else if (ch == 'n')  f.spectrum    = ! f.spectrum;
        else if (plus)       f.gainDb = juce::jlimit (-60.0f, 12.0f, f.gainDb + 0.1f);
        else if (minus)      f.gainDb = juce::jlimit (-60.0f, 12.0f, f.gainDb - 0.1f);
        else if (ch >= '1' && ch < juce_wchar ('1' + smt::numFrameBands))
            f.bands[(size_t) (ch - '1')].on = ! f.bands[(size_t) (ch - '1')].on;
        else                 handled = false;

        if (handled)
        {
            model.setFrame (editConfig, selIn, selOut, f);
            return true;
        }
        return false;
    }

    if (selStrip >= 0)                       // an output (top strip) is selected
    {
        auto s = model.getOutput (selStrip);
        bool handled = true, firChanged = false;
        if      (ch == 'n')  s.spectrum = ! s.spectrum;
        else if (ch == 'f') { s.firOn = ! s.firOn; firChanged = true; }
        else if (plus)       s.gainDb = juce::jlimit (-60.0f, 12.0f, s.gainDb + 0.1f);
        else if (minus)      s.gainDb = juce::jlimit (-60.0f, 12.0f, s.gainDb - 0.1f);
        else if (ch >= '1' && ch < juce_wchar ('1' + smt::numOutputBands))
            s.bands[(size_t) (ch - '1')].on = ! s.bands[(size_t) (ch - '1')].on;
        else                 handled = false;

        if (handled)
        {
            model.setOutput (selStrip, s);
            if (firChanged)
                engine.updateFirFiles();
            return true;
        }
        return false;
    }

    return false;
}

void MatrixComponent::mouseMove (const juce::MouseEvent& e)
{
    int in = -1, out = -1;
    bool strip = false;
    const int newHover = (hitTest (e.getPosition(), in, out, strip) && strip) ? out : -1;
    if (newHover != hoverOut)
        hoverOut = newHover;        // tooltip text follows the hovered output
}

juce::String MatrixComponent::getTooltip()
{
    if (hoverOut < 0 || hoverOut >= model.getNumOuts())
        return {};

    const int comp     = engine.getOutputLatencyCompSamples (hoverOut);
    const int absorbed = engine.getOutputSelfAbsorbedSamples (hoverOut);
    if (comp <= 0 && absorbed <= 0)
        return {};

    juce::String text = "Output " + juce::String (hoverOut + 1) + ":";

    // This output's own manual delay is paying for part (or all) of its own
    // FIR's latency, so less (or no) delay had to be added elsewhere to keep
    // everything aligned — see the Delay control: only the reduced amount
    // shown here is actually applied, the rest is "free" from the FIR itself.
    if (absorbed > 0)
    {
        const float absorbedMs = engine.getOutputSelfAbsorbedMs (hoverOut);
        const float shownMs = model.getOutput (hoverOut).delayMs;
        const float appliedMs = juce::jmax (0.0f, shownMs - absorbedMs);
        text << "\nThis output's own FIR latency absorbs " + juce::String (absorbedMs, 2)
             + " ms from its Delay control (" + juce::String (appliedMs, 2)
             + " ms actually applied of the " + juce::String (shownMs, 2)
             + " ms shown), reducing overall system latency.";
    }

    if (comp > 0)
        text << "\n+" + juce::String (engine.getOutputLatencyCompMs (hoverOut), 2) + " ms ("
             + juce::String (comp) + " samples) added to stay aligned with the longest output FIR.";

    return text;
}

//==============================================================================
void MatrixComponent::setEditConfig (int config)
{
    editConfig = juce::jlimit (0, numConfigs - 1, config);
    repaint();
}

void MatrixComponent::showFrameMenu (int in, int out)
{
    auto f = model.getFrame (editConfig, in, out);
    juce::PopupMenu menu;
    menu.addItem (1, "Active", true, f.active);
    menu.addItem (2, "Phase invert", true, f.phaseInvert);
    menu.addItem (3, "Show on analyzer", true, f.spectrum);
    menu.addSeparator();
    menu.addItem (4, "Reset frame");

    menu.showMenuAsync (juce::PopupMenu::Options(),
        [this, in, out] (int result)
        {
            if (result == 0)
                return;
            auto fr = model.getFrame (editConfig, in, out);
            switch (result)
            {
                case 1: fr.active = ! fr.active; break;
                case 2: fr.phaseInvert = ! fr.phaseInvert; break;
                case 3: fr.spectrum = ! fr.spectrum; break;
                case 4: fr = smt::FrameSettings(); break;
                default: break;
            }
            model.setFrame (editConfig, in, out, fr);
        });
}

void MatrixComponent::showOutputMenu (int out)
{
    auto s = model.getOutput (out);
    juce::PopupMenu menu;
    menu.addItem (1, "FIR correction on", engine.getFir (out).hasImpulse(), s.firOn);
    menu.addItem (2, "Load impulse response...");
    menu.addItem (3, "Clear impulse response", s.firPath.isNotEmpty());
    menu.addItem (4, "Show on analyzer", true, s.spectrum);
    menu.addSeparator();
    menu.addItem (5, "Reset trim");

    menu.showMenuAsync (juce::PopupMenu::Options(),
        [this, out] (int result)
        {
            if (result == 0)
                return;
            auto so = model.getOutput (out);
            switch (result)
            {
                case 1: so.firOn = ! so.firOn; break;
                case 2: loadIrForOutput (out); return;
                case 3: so.firPath.clear(); so.firOn = false; break;
                case 4: so.spectrum = ! so.spectrum; break;
                case 5: so.gainDb = 0.0f; break;
                default: break;
            }
            model.setOutput (out, so);
            engine.updateFirFiles();
        });
}

void MatrixComponent::loadIrForOutput (int out)
{
    fileChooser = std::make_unique<juce::FileChooser> (
        "Load impulse response for output " + juce::String (out + 1),
        smt::getLastBrowseDir(), "*.wav");

    fileChooser->launchAsync (juce::FileBrowserComponent::openMode
                              | juce::FileBrowserComponent::canSelectFiles,
        [this, out] (const juce::FileChooser& fc)
        {
            const auto file = fc.getResult();
            if (! file.existsAsFile())
                return;
            smt::setLastBrowseDir (file);
            auto so = model.getOutput (out);
            so.firPath = file.getFullPathName();
            so.firOn = true;
            model.setOutput (out, so);
            engine.updateFirFiles();
        });
}
