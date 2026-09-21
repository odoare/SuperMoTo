/*
  ------------------------------------------------------------------------------
    TargetCurveComponent.h

    The Target view: the monitor target curve the whole rig is aimed at, its
    named curves on disk, and a plot of what the outputs will apply.

    Everything but the plot is one column on the left, in the order the pane is
    used: the curve bank (TargetCurveStore) with its factory and user curves
    and the Save / Save as / Rename / Delete that act on the user's only, the
    switch that engages the curve, the four values it is made of, and the
    caveat under all of them. The plot fills the rest, on the same
    log-frequency grid as the analyzer and the analysis panes
    (fxme::SpectrumDisplay, with its badges off since there is no spectrum
    behind it).

    The shape is a property of the configuration and lives in the ConfigModel,
    so it is saved with the session and with presets. Whether it is engaged is
    an APVTS parameter instead, so that the A/B a target curve has to be judged
    by can be automated or bound to a key. See TargetCurve.h for what the
    curve is and why it is applied as a layer on every output rather than
    baked into the correction filters.

    Author: Olivier Doaré, github.com/odoare
    Licenced under the GNU AGPL Version 3.0, or commercial terms (LICENSE.md)
    SPDX-License-Identifier: AGPL-3.0-or-later OR LicenseRef-FXME-Commercial
  ------------------------------------------------------------------------------
*/

#pragma once

#include <JuceHeader.h>     // fxme::SpectrumDisplay, FxmeSlider and FxmeButton
#include "../Model/ConfigModel.h"
#include "../Model/TargetCurveStore.h"
#include "../Dsp/TargetCurve.h"
#include "../Theme.h"
#include "../Tooltips.h"

//==============================================================================
/** The curve on the shared log-frequency display. No taps: everything is
    drawn over the grid through paintOverTraces(). */
class TargetCurvePlot : public fxme::SpectrumDisplay
{
public:
    TargetCurvePlot()
    {
        setColours (SuperMoToTheme::spectrumColours());
        setBadgesVisible (false);       // nothing analysing behind this one
        setFftSizeLocked (true);
        setDbRange (-16.0f, 8.0f);
        setFreqWindow (20.0f, 20000.0f);
    }

    void setCurve (const smt::TargetCurve& c)
    {
        curve = c;
        repaint();
    }

    /** Draw the applied curve (the one with the output attenuation in it) as
        well as the shape. Off by default: the shape is what a target curve is
        talked about in, and the offset is only a level. */
    void setShowApplied (bool shouldShow)
    {
        showApplied = shouldShow;
        repaint();
    }

protected:
    void paintOverTraces (juce::Graphics& g, juce::Rectangle<float> plot) override
    {
        const smt::TargetCurveShape shape (curve);

        if (showApplied && shape.offsetDb() < -0.01)
        {
            g.setColour (SuperMoToTheme::dimText.withAlpha (0.55f));
            g.strokePath (curvePath (shape, plot, (float) shape.offsetDb()),
                          juce::PathStrokeType (1.2f));
        }

        g.setColour (SuperMoToTheme::fir);
        g.strokePath (curvePath (shape, plot, 0.0f), juce::PathStrokeType (2.0f));
    }

private:
    juce::Path curvePath (const smt::TargetCurveShape& shape,
                          juce::Rectangle<float> plot, float offsetDb) const
    {
        juce::Path p;
        const int steps = juce::jmax (2, (int) plot.getWidth());

        for (int i = 0; i <= steps; ++i)
        {
            const float x = plot.getX() + plot.getWidth() * (float) i / (float) steps;
            const float y = dbToY ((float) shape.gainDb (xToFreq (x, plot)) + offsetDb, plot);
            i == 0 ? p.startNewSubPath (x, y) : p.lineTo (x, y);
        }
        return p;
    }

    smt::TargetCurve curve;
    bool showApplied = false;
};

//==============================================================================
class TargetCurveComponent : public juce::Component,
                             private smt::ConfigModel::Listener
{
public:
    TargetCurveComponent (smt::ConfigModel& m, juce::AudioProcessorValueTreeState& s)
        : model (m), apvts (s)
    {
        model.addListener (this);

        title.setText ("Monitor target curve", juce::dontSendNotification);
        title.setFont (juce::Font (juce::FontOptions (17.0f, juce::Font::bold)));
        title.setColour (juce::Label::textColourId, SuperMoToTheme::text);
        addAndMakeVisible (title);

        // ── The bank ─────────────────────────────────────────────────────────
        addLabel (presetLabel, "Curve");
        SuperMoToTheme::accentComboBox (presetBox, SuperMoToTheme::fir);
        presetBox.setTooltip (smt::tips::tgt::preset);
        presetBox.onChange = [this] { presetChosen(); };
        addAndMakeVisible (presetBox);

        auto initButton = [this] (juce::TextButton& b, const juce::String& text,
                                  const juce::String& tip, std::function<void()> action)
        {
            b.setButtonText (text);
            b.setColour (juce::TextButton::buttonColourId, SuperMoToTheme::panel);
            b.setTooltip (tip);
            b.onClick = std::move (action);
            addAndMakeVisible (b);
        };
        initButton (saveButton,   "Save",      smt::tips::tgt::save,   [this] { saveOver(); });
        initButton (saveAsButton, "Save as...", smt::tips::tgt::saveAs, [this] { saveAs(); });
        initButton (renameButton, "Rename",    smt::tips::tgt::rename, [this] { renameCurve(); });
        initButton (deleteButton, "Delete",    smt::tips::tgt::remove, [this] { deleteCurve(); });

        folderLabel.setFont (juce::Font (juce::FontOptions (11.0f)));
        folderLabel.setColour (juce::Label::textColourId, SuperMoToTheme::dimText);
        folderLabel.setText (smt::TargetCurveStore::directory().getFullPathName(),
                             juce::dontSendNotification);
        folderLabel.setTooltip (smt::TargetCurveStore::directory().getFullPathName());
        addAndMakeVisible (folderLabel);

        // ── The plot ─────────────────────────────────────────────────────────
        // Both the curve and, dimmed, the curve as applied: the second line is
        // the first one pulled down so the filter only ever attenuates, and
        // seeing them together is what makes that obvious.
        plot.setShowApplied (true);
        addAndMakeVisible (plot);

        // ── The values ───────────────────────────────────────────────────────
        auto initKnob = [this] (fxme::FxmeSlider& k, const juce::String& name,
                                double lo, double hi, double step, double centre,
                                const juce::String& suffix, const juce::String& tip)
        {
            k.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
            k.setName (name);
            k.setShowLabel (true);
            k.setRange (lo, hi, step);
            k.setTextValueSuffix (suffix);
            k.setCentralValue (centre);
            k.setTooltip (tip);
            SuperMoToTheme::accentSlider (k, SuperMoToTheme::fir);
            k.onValueChange = [this] { pushToModel(); };
            addAndMakeVisible (k);
        };

        initKnob (tiltKnob, "Tilt", -12.0, 3.0, 0.1, 0.0, " dB", smt::tips::tgt::tilt);
        initKnob (turnoverKnob, "Turnover", 0.0, 4000.0, 10.0, 0.0, " Hz", smt::tips::tgt::turnover);
        initKnob (bassKnob, "Bass", 0.0, 8.0, 0.1, 0.0, " dB", smt::tips::tgt::bass);
        initKnob (bassHzKnob, "Bass freq", 40.0, 300.0, 1.0, 105.0, " Hz", smt::tips::tgt::bassHz);

        // 0 means "no turnover, the tilt runs across the whole band", which is
        // worth saying rather than showing as a frequency of zero.
        turnoverKnob.textFromValueFunction = [] (double v)
        {
            return v < 5.0 ? juce::String ("off") : juce::String (juce::roundToInt (v)) + " Hz";
        };

        engageButton = std::make_unique<fxme::FxmeButton> (apvts, "Target", "Engage",
                                                           SuperMoToTheme::fir);
        // FxmeButton is a wrapper component; the tooltip belongs on the
        // ToggleButton inside it, which is the thing the mouse is over.
        engageButton->button.setTooltip (smt::tips::tgt::engage);
        addAndMakeVisible (*engageButton);

        levelLabel.setFont (juce::Font (juce::FontOptions (12.0f)));
        levelLabel.setColour (juce::Label::textColourId, SuperMoToTheme::dimText);
        levelLabel.setTooltip (smt::tips::tgt::level);
        addAndMakeVisible (levelLabel);

        // ── The caveat ───────────────────────────────────────────────────────
        note.setFont (juce::Font (juce::FontOptions (12.5f)));
        note.setColour (juce::Label::textColourId, SuperMoToTheme::dim);   // amber: a caution
        note.setJustificationType (juce::Justification::topLeft);
        note.setText ("A target curve only means something on a system that has already been "
                      "measured, corrected and time-aligned: it shapes what the correction is "
                      "aiming at, it does not correct anything itself. On an uncorrected rig it "
                      "is a tone control. Measure and align first (Calibration, then Analysis or "
                      "Group), then use this to decide how the corrected system should tilt.",
                      juce::dontSendNotification);
        addAndMakeVisible (note);

        pullFromModel();
    }

    ~TargetCurveComponent() override    { model.removeListener (this); }

    void paint (juce::Graphics& g) override
    {
        g.setColour (SuperMoToTheme::panel.withAlpha (0.7f));
        g.fillRoundedRectangle (getLocalBounds().toFloat(), 6.0f);
        g.setColour (SuperMoToTheme::panelLine);
        g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (0.5f), 6.0f, 1.0f);
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced (12);

        title.setBounds (area.removeFromTop (26).withTrimmedLeft (30));   // room for the info button
        area.removeFromTop (6);

        // One column on the left, in the order the pane is used: pick a curve,
        // engage it, adjust it, with the caveat under all of it. The plot
        // takes everything else.
        auto left = area.removeFromLeft (juce::jlimit (260, 360, area.getWidth() / 3));
        area.removeFromLeft (12);
        plot.setBounds (area);

        presetLabel.setBounds (left.removeFromTop (18));
        presetBox.setBounds (left.removeFromTop (26));
        left.removeFromTop (6);

        auto brow = left.removeFromTop (26);
        saveButton.setBounds (brow.removeFromLeft (brow.getWidth() / 2).reduced (2, 0));
        saveAsButton.setBounds (brow.reduced (2, 0));

        left.removeFromTop (4);
        brow = left.removeFromTop (26);
        renameButton.setBounds (brow.removeFromLeft (brow.getWidth() / 2).reduced (2, 0));
        deleteButton.setBounds (brow.reduced (2, 0));

        left.removeFromTop (2);
        folderLabel.setBounds (left.removeFromTop (30));

        left.removeFromTop (10);
        auto erow = left.removeFromTop (28);
        engageButton->setBounds (erow.removeFromLeft (110).reduced (2));
        levelLabel.setBounds (erow.reduced (6, 0));

        // Two rows of two: four knobs side by side in this width would be
        // narrower than their own read-outs.
        left.removeFromTop (6);
        const int knobH = juce::jlimit (74, 96, (left.getHeight() - 96) / 2);
        for (int row = 0; row < 2; ++row)
        {
            auto krow = left.removeFromTop (knobH);
            const int w = krow.getWidth() / 2;
            (row == 0 ? tiltKnob : bassKnob).setBounds (krow.removeFromLeft (w).reduced (4, 0));
            (row == 0 ? turnoverKnob : bassHzKnob).setBounds (krow.reduced (4, 0));
        }

        left.removeFromTop (10);
        note.setBounds (left);
    }

private:
    //==========================================================================
    void modelChanged() override        { pullFromModel(); }

    void pullFromModel()
    {
        const auto c = model.getTargetCurve();

        updating = true;
        tiltKnob.setValue (c.tiltDb, juce::dontSendNotification);
        turnoverKnob.setValue (c.turnoverHz, juce::dontSendNotification);
        bassKnob.setValue (c.bassDb, juce::dontSendNotification);
        bassHzKnob.setValue (c.bassHz, juce::dontSendNotification);
        updating = false;

        plot.setCurve (c);
        refreshBank (c);
        refreshLevel (c);
    }

    smt::TargetCurve collect() const
    {
        smt::TargetCurve c;
        c.tiltDb     = (float) tiltKnob.getValue();
        c.turnoverHz = turnoverKnob.getValue() < 5.0 ? 0.0f : (float) turnoverKnob.getValue();
        c.bassDb     = (float) bassKnob.getValue();
        c.bassHz     = (float) bassHzKnob.getValue();
        return c;
    }

    void pushToModel()
    {
        if (updating)
            return;
        model.setTargetCurve (collect());   // comes back through modelChanged()
    }

    /** Rebuilds the selector and shows which stored curve the values are, or
        "(edited)" when they are none of them. */
    void refreshBank (const smt::TargetCurve& c)
    {
        presetBox.clear (juce::dontSendNotification);

        const auto& entries = store.getEntries();
        bool headerDone = false, userHeaderDone = false;

        for (int i = 0; i < (int) entries.size(); ++i)
        {
            const auto& e = entries[(size_t) i];
            if (e.factory && ! headerDone)       { presetBox.addSectionHeading ("Factory"); headerDone = true; }
            if (! e.factory && ! userHeaderDone) { presetBox.addSectionHeading ("User");    userHeaderDone = true; }
            presetBox.addItem (e.name, i + 1);
        }

        const int match = store.indexOfCurve (c);
        if (match >= 0)
        {
            selectedName = entries[(size_t) match].name;
            presetBox.setSelectedId (match + 1, juce::dontSendNotification);
        }
        else
        {
            presetBox.addSeparator();
            presetBox.addItem (selectedName.isEmpty() ? juce::String ("(edited)")
                                                      : selectedName + " (edited)", editedId);
            presetBox.setSelectedId (editedId, juce::dontSendNotification);
        }

        const int sel = store.indexOfName (selectedName);
        saveButton.setEnabled (store.isUserIndex (sel));
        renameButton.setEnabled (store.isUserIndex (sel));
        deleteButton.setEnabled (store.isUserIndex (sel));
    }

    void refreshLevel (const smt::TargetCurve& c)
    {
        const smt::TargetCurveShape shape (c);
        levelLabel.setText (shape.offsetDb() < -0.005
                                ? juce::String (shape.offsetDb(), 1) + " dB out"
                                : juce::String ("0.0 dB out"),
                            juce::dontSendNotification);
    }

    void presetChosen()
    {
        const int id = presetBox.getSelectedId();
        if (id <= 0 || id == editedId)
            return;

        const auto& entries = store.getEntries();
        const int index = id - 1;
        if (index < 0 || index >= (int) entries.size())
            return;

        selectedName = entries[(size_t) index].name;
        model.setTargetCurve (entries[(size_t) index].curve);
    }

    void saveOver()
    {
        const int index = store.indexOfName (selectedName);
        if (! store.isUserIndex (index))
            return;
        store.save (selectedName, model.getTargetCurve());
        pullFromModel();
    }

    void saveAs()
    {
        promptForName ("Save target curve", selectedName.isEmpty() ? "My curve" : selectedName,
                       [this] (const juce::String& name)
                       {
                           if (store.save (name, model.getTargetCurve()) >= 0)
                               selectedName = name;
                           pullFromModel();
                       });
    }

    void renameCurve()
    {
        const int index = store.indexOfName (selectedName);
        if (! store.isUserIndex (index))
            return;

        promptForName ("Rename target curve", selectedName,
                       [this, index] (const juce::String& name)
                       {
                           if (store.rename (index, name))
                               selectedName = name;
                           pullFromModel();
                       });
    }

    void deleteCurve()
    {
        const int index = store.indexOfName (selectedName);
        if (! store.isUserIndex (index))
            return;

        juce::AlertWindow::showOkCancelBox (
            juce::MessageBoxIconType::WarningIcon, "Delete target curve",
            "Delete the curve \"" + selectedName + "\"?", {}, {}, this,
            juce::ModalCallbackFunction::create ([this, index] (int result)
            {
                if (result != 1)
                    return;
                store.remove (index);
                selectedName.clear();
                pullFromModel();
            }));
    }

    void promptForName (const juce::String& boxTitle, const juce::String& initial,
                        std::function<void (const juce::String&)> onOk)
    {
        auto* aw = new juce::AlertWindow (boxTitle, "Curve name:",
                                          juce::MessageBoxIconType::NoIcon, this);
        aw->addTextEditor ("name", initial);
        aw->addButton ("OK",     1, juce::KeyPress (juce::KeyPress::returnKey));
        aw->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));

        aw->enterModalState (true,
            juce::ModalCallbackFunction::create ([aw, onOk] (int result)
            {
                const auto name = aw->getTextEditorContents ("name").trim();
                if (result == 1 && name.isNotEmpty())
                    onOk (name);
            }),
            true);
    }

    void addLabel (juce::Label& l, const juce::String& text)
    {
        l.setText (text, juce::dontSendNotification);
        l.setFont (juce::Font (juce::FontOptions (11.0f)));
        l.setColour (juce::Label::textColourId, SuperMoToTheme::dimText);
        addAndMakeVisible (l);
    }

    static constexpr int editedId = 10000;

    smt::ConfigModel& model;
    juce::AudioProcessorValueTreeState& apvts;
    smt::TargetCurveStore store;

    juce::String selectedName;
    bool updating = false;

    juce::Label title, presetLabel, folderLabel, levelLabel, note;
    juce::ComboBox presetBox;
    juce::TextButton saveButton, saveAsButton, renameButton, deleteButton;
    TargetCurvePlot plot;
    fxme::FxmeSlider tiltKnob, turnoverKnob, bassKnob, bassHzKnob;
    std::unique_ptr<fxme::FxmeButton> engageButton;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TargetCurveComponent)
};
