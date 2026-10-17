#include "ui/TempoField.h"
#include "ui/Theme.h"

#include <beatforge/time/TempoMap.h>

namespace beatforge
{
    TempoField::TempoField()
    {
        setTooltip ("Tempo: drag up/down or scroll (Shift = fine), double-click to type");
        setMouseCursor (juce::MouseCursor::UpDownResizeCursor);
    }

    void TempoField::setTempo (double bpm, juce::NotificationType notification)
    {
        bpm = time::clampTempo (std::round (bpm * 100.0) / 100.0);

        if (juce::exactlyEqual (bpm, tempo))
            return;

        tempo = bpm;
        repaint();

        if (notification != juce::dontSendNotification && onTempoChange)
            onTempoChange (tempo);
    }

    // Drawn as a segment of the transport readout strip: value over caption.
    void TempoField::paint (juce::Graphics& g)
    {
        if (isMouseOver() || dragging)
        {
            g.setColour (theme::control.withAlpha (0.6f));
            g.fillRect (getLocalBounds().reduced (1));
        }

        if (editor != nullptr && editor->isVisible())
            return;

        auto area = getLocalBounds().reduced (4, 4);
        auto caption = area.removeFromBottom (12);

        g.setColour (theme::textPrimary);
        g.setFont (theme::numericFont (20.0f));
        g.drawText (juce::String (tempo, 2), area, juce::Justification::centred);

        g.setColour (dragging ? theme::violet : theme::textSecondary);
        g.setFont (theme::captionFont (9.5f));
        g.drawText ("BPM", caption, juce::Justification::centred);
    }

    void TempoField::resized()
    {
        if (editor != nullptr)
            editor->setBounds (getLocalBounds().reduced (6, 8));
    }

    void TempoField::mouseDown (const juce::MouseEvent&)
    {
        dragStartTempo = tempo;
        dragging = true;
        repaint();
    }

    void TempoField::mouseDrag (const juce::MouseEvent& e)
    {
        // 1 BPM per 3 px; Shift for 0.1 BPM steps.
        const bool fine = e.mods.isShiftDown();
        const double steps = std::round (-e.getDistanceFromDragStartY() / 3.0);
        const double step = fine ? 0.1 : 1.0;
        const double base = fine ? dragStartTempo : std::round (dragStartTempo);
        setTempo (base + steps * step);
    }

    void TempoField::mouseUp (const juce::MouseEvent&)
    {
        dragging = false;
        repaint();
    }

    void TempoField::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
    {
        const double direction = wheel.deltaY > 0 ? 1.0 : (wheel.deltaY < 0 ? -1.0 : 0.0);
        const double step = e.mods.isShiftDown() ? 0.1 : 1.0;
        setTempo ((e.mods.isShiftDown() ? tempo : std::round (tempo)) + direction * step);
    }

    void TempoField::mouseDoubleClick (const juce::MouseEvent&)
    {
        dragging = false;
        showEditor();
    }

    void TempoField::showEditor()
    {
        editor = std::make_unique<juce::TextEditor>();
        editor->setFont (theme::numericFont (20.0f));
        editor->setJustification (juce::Justification::centred);
        editor->setInputRestrictions (6, "0123456789.");
        editor->setText (juce::String (tempo, 2), false);
        editor->selectAll();
        editor->onReturnKey = [this] { closeEditor (true); };
        editor->onFocusLost = [this] { closeEditor (true); };
        editor->onEscapeKey = [this] { closeEditor (false); };
        addAndMakeVisible (*editor);
        resized();
        editor->grabKeyboardFocus();
        repaint();
    }

    void TempoField::closeEditor (bool commit)
    {
        if (editor == nullptr || ! editor->isVisible())
            return;

        const auto text = editor->getText().trim();
        editor->setVisible (false);

        if (commit && text.isNotEmpty() && text.containsOnly ("0123456789."))
            setTempo (text.getDoubleValue());

        // The editor is still inside its own callback here; delete it afterwards.
        juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<TempoField> (this)]
        {
            if (safe == nullptr)
                return;

            safe->editor = nullptr;
            safe->repaint();

            if (safe->onEditFinished)
                safe->onEditFinished();
        });
    }
}
