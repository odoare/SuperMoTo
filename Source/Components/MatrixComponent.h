/*
  ------------------------------------------------------------------------------
    MatrixComponent.h

    The 16x16 monitoring matrix view. Rows are inputs, columns are outputs.
    Each frame (crosspoint) shows its state (active, gain, filter, delay,
    phase, analyzer checkbox) and a small vu-meter. The strip at the TOP
    shows the per-output chain: trim, FIR correction state and output
    vu-meter; the column numbers run along the bottom edge.

    When the component is sized down to outputStripH (collapsed editor),
    only the output strip is shown and the grid is skipped entirely.

    Interactions
      frame:  click = select (opens in the frame editor)
              double-click = toggle active
              vertical drag = gain
              alt+click = toggle analyzer trace
              right-click = context menu
      output strip: double-click = toggle FIR, vertical drag = trim,
              alt+click = analyzer trace, right-click = load/clear IR

    Author: Olivier Doaré, github.com/odoare
    Licenced under the GNU LGPL Version 3.0
    SPDX-License-Identifier: LGPL-3.0-or-later
  ------------------------------------------------------------------------------
*/

#pragma once

#include <JuceHeader.h>
#include "../Model/ConfigModel.h"
#include "../Dsp/MatrixEngine.h"
#include "../Theme.h"

class MatrixComponent : public juce::Component,
                        private juce::Timer,
                        private smt::ConfigModel::Listener
{
public:
    MatrixComponent (smt::ConfigModel& model, smt::MatrixEngine& engine);
    ~MatrixComponent() override;

    void paint (juce::Graphics&) override;

    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;

    /** Which configuration (A..F) is displayed/edited. */
    void setEditConfig (int config);
    int getEditConfig() const noexcept          { return editConfig; }

    /** Called when the user selects a frame (in, out) — the editor shows it
        in the frame editor panel. */
    std::function<void (int in, int out)> onFrameSelected;

    void setSelectedFrame (int in, int out)     { selIn = in; selOut = out; repaint(); }

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

    smt::ConfigModel& model;
    smt::MatrixEngine& engine;

    int editConfig = 0;
    int selIn = -1, selOut = -1;

    // Drag state
    bool draggingFrame = false, draggingOutput = false;
    int dragIn = -1, dragOut = -1;
    float dragStartGain = 0.0f;

    std::unique_ptr<juce::FileChooser> fileChooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MatrixComponent)
};
