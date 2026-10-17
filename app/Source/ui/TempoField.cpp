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

    void TempoField::paint (juce::Graphics& g)
    {
        auto bounds = getLocalBounds().toFloat().reduced (0.5f);
        g.setColour (theme::background);
        g.fillRoundedRectangle (bounds, theme::cornerRadius);
        g.setColour (dragging ? theme::accent : theme::border);
        g.drawRoundedRectangle (bounds, theme::cornerRadius, 1.0f);

        if (editor != nullptr && editor->isVisible())
            return;

        auto area = getLocalBounds().reduced (10, 4);
        auto label = area.removeFromRight (30);

        g.setColour (theme::textPrimary);
        g.setFont (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 18.0f, juce::Font::plain));
        g.drawText (juce::String (tempo, 2), area, juce::Justification::centredRight);

        g.setColour (theme::textMuted);
        g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
        g.drawText ("BPM", label, juce::Justification::centredRight);
    }

    void TempoField::resized()
    {
        if (editor != nullptr)
            editor->setBounds (getLocalBounds().reduced (4));
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
        editor->setFont (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 18.0f, juce::Font::plain));
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
