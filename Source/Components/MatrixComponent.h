/*
  ------------------------------------------------------------------------------
    MatrixComponent.h

    The monitoring matrix view. Rows are inputs, columns are outputs. Each
    frame (crosspoint) shows its routing state (active, gain, phase, analyzer
    checkbox) and a small vu-meter. The strip at the TOP shows the per-output
    (per-speaker) chain: trim, EQ / delay tags, FIR correction state and output
    vu-meter; the column numbers run along the bottom edge. Clicking an output
    cell opens it in the output editor.

    When the component is sized down to outputStripH (collapsed editor),
    only the output strip is shown and the grid is skipped entirely.

    Interactions
      frame:  click = select (opens in the frame editor)
              double-click = toggle active
              vertical drag = gain
              alt+click = toggle analyzer trace
              right-click = context menu
      output strip: click = select (opens in the output editor),
              double-click = toggle FIR, vertical drag = trim,
              alt+click = analyzer trace, right-click = load/clear IR

    Author: Olivier Doaré, github.com/odoare
    Licenced under the GNU AGPL Version 3.0, or commercial terms (LICENSE.md)
    SPDX-License-Identifier: AGPL-3.0-or-later OR LicenseRef-FXME-Commercial
  ------------------------------------------------------------------------------
*/

#pragma once

#include <JuceHeader.h>
#include "../Model/ConfigModel.h"
#include "../Dsp/MatrixEngine.h"
#include "../Theme.h"

class MatrixComponent : public juce::Component,
                        public juce::TooltipClient,
                        private juce::Timer,
                        private smt::ConfigModel::Listener
{
public:
    MatrixComponent (smt::ConfigModel& model, smt::MatrixEngine& engine);
    ~MatrixComponent() override;

    void paint (juce::Graphics&) override;

    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;

    /** Arrow keys move the selection across the grid and the output strip
        (with wrap-around); single keys act on the selected frame/output. */
    bool keyPressed (const juce::KeyPress&) override;

    /** Tooltip for the cell currently under the mouse (e.g. latency comp). */
    juce::String getTooltip() override;

    /** Which configuration (A..F) is displayed/edited. */
    void setEditConfig (int config);
    int getEditConfig() const noexcept          { return editConfig; }

    /** Called when the user selects a frame (in, out) — the editor shows it
        in the frame editor panel. */
    std::function<void (int in, int out)> onFrameSelected;

    /** Called when the user selects an output (top strip) — the editor shows it
        in the output editor panel. */
    std::function<void (int out)> onOutputSelected;

    void setSelectedFrame (int in, int out)     { selIn = in; selOut = out; repaint(); }
    void setSelectedOutput (int out)            { selStrip = out; selIn = selOut = -1; repaint(); }

    static constexpr int outputStripH = 54;     // height of the output strip

private:
    void timerCallback() override               { repaint(); }

    void modelChanged() override
    {
        // Drop the selection if a matrix-size shrink hid the selected frame.
        if (selIn >= model.getNumIns() || selOut >= model.getNumOuts())
        {
            selIn = selOut = -1;
            if (onFrameSelected != nullptr)
                onFrameSelected (-1, -1);
        }
        if (selStrip >= model.getNumOuts())
        {
            selStrip = -1;
            if (onOutputSelected != nullptr)
                onOutputSelected (-1);
        }
        repaint();
    }

    // Geometry
    static constexpr int labelW = 30;       // input labels column
    static constexpr int headerH = 18;      // output numbers row (bottom)

    juce::Rectangle<int> gridArea() const;
    bool isGridVisible() const              { return gridArea().getHeight() > 20; }
    juce::Rectangle<int> frameBounds (int in, int out) const;
    juce::Rectangle<int> outputCellBounds (int out) const;
    bool hitTest (juce::Point<int> pos, int& in, int& out, bool& isOutputStrip) const;

    void showFrameMenu (int in, int out);
    void showOutputMenu (int out);
    void loadIrForOutput (int out);

    // Select a cell for keyboard navigation. row < 0 = output strip (col is the
    // output); otherwise the frame at (row = input, col = output). Fires the
    // matching selection callback so the detail editor follows.
    void selectCell (int row, int col);

    smt::ConfigModel& model;
    smt::MatrixEngine& engine;

    int editConfig = 0;
    int selIn = -1, selOut = -1;
    int selStrip = -1;          // selected output (top strip), -1 = none

    // Drag state
    bool draggingFrame = false, draggingOutput = false;
    int dragIn = -1, dragOut = -1;
    float dragStartGain = 0.0f;

    // Cell under the mouse, for tooltips (-1 = none, else output index).
    int hoverOut = -1;

    std::unique_ptr<juce::FileChooser> fileChooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MatrixComponent)
};
