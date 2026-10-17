#include "ui/SignatureField.h"
#include "ui/Theme.h"

namespace beatforge
{
    namespace
    {
        const time::TimeSignature choices[] { { 2, 4 }, { 3, 4 }, { 4, 4 }, { 5, 4 }, { 6, 8 }, { 7, 8 } };

        juce::String text (time::TimeSignature s) { return juce::String (s.numerator) + "/" + juce::String (s.denominator); }
    }

    SignatureField::SignatureField()
    {
        setTooltip ("Time signature (click to change)");
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
    }

    void SignatureField::setSignature (time::TimeSignature s)
    {
        if (! (s == signature))
        {
            signature = s;
            repaint();
        }
    }

    void SignatureField::paint (juce::Graphics& g)
    {
        if (isMouseOver() || menuOpen)
        {
            g.setColour (theme::control.withAlpha (0.6f));
            g.fillRect (getLocalBounds().reduced (1));
        }

        auto area = getLocalBounds().reduced (4, 4);
        auto caption = area.removeFromBottom (12);

        g.setColour (theme::textPrimary);
        g.setFont (theme::numericFont (20.0f));
        g.drawText (text (signature), area, juce::Justification::centred);

        g.setColour (menuOpen ? theme::violet : theme::textSecondary);
        g.setFont (theme::captionFont (9.5f));
        g.drawText ("SIG", caption, juce::Justification::centred);
    }

    void SignatureField::mouseDown (const juce::MouseEvent&)
    {
        juce::PopupMenu menu;
        for (int i = 0; i < (int) std::size (choices); ++i)
            menu.addItem (i + 1, text (choices[i]), true, choices[i] == signature);

        menuOpen = true;
        repaint();

        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMinimumWidth (getWidth()),
                            [safe = juce::Component::SafePointer<SignatureField> (this)] (int id)
        {
            if (safe == nullptr)
                return;

            safe->menuOpen = false;
            safe->repaint();

            if (id > 0 && safe->onChange)
                safe->onChange (choices[id - 1]);
        });
    }
}
