#include "ui/ProjectNameField.h"
#include "ui/Theme.h"

namespace beatforge
{
    ProjectNameField::ProjectNameField()
    {
        setTooltip ("Project name (double-click to rename)");
    }

    void ProjectNameField::setProjectName (const juce::String& name)
    {
        if (name != projectName)
        {
            projectName = name;
            repaint();
        }
    }

    void ProjectNameField::paint (juce::Graphics& g)
    {
        if (editor != nullptr)
            return;
        auto area = getLocalBounds().reduced (4, 0);

        // Project glyph: a small stack of three bars (a "song").
        auto glyph = area.removeFromLeft (14).toFloat().withSizeKeepingCentre (12.0f, 11.0f);
        g.setColour (theme::violet);
        for (int i = 0; i < 3; ++i)
            g.fillRoundedRectangle (glyph.getX(), glyph.getY() + (float) i * 4.0f, glyph.getWidth() - (float) i * 3.0f, 2.4f, 1.2f);
        area.removeFromLeft (8);

        g.setColour (theme::textPrimary);
        g.setFont (theme::font (14.5f, juce::Font::bold));
        g.drawText (projectName, area, juce::Justification::centredLeft, true);
    }

    void ProjectNameField::resized()
    {
        if (editor != nullptr)
            editor->setBounds (getLocalBounds().reduced (0, 6));
    }

    void ProjectNameField::mouseDoubleClick (const juce::MouseEvent&)
    {
        editor = std::make_unique<juce::TextEditor>();
        editor->setFont (theme::font (14.5f, juce::Font::bold));
        editor->setInputRestrictions (64);
        editor->setText (projectName, false);
        editor->selectAll();
        editor->onReturnKey = [this] { close (true); };
        editor->onEscapeKey = [this] { close (false); };
        editor->onFocusLost = [this] { close (true); };
        addAndMakeVisible (*editor);
        resized();
        editor->grabKeyboardFocus();
        repaint();
    }

    void ProjectNameField::close (bool commit)
    {
        if (editor == nullptr || ! editor->isVisible())
            return;
        const auto text = editor->getText().trim();
        editor->setVisible (false);
        if (commit && text.isNotEmpty() && text != projectName && onRename)
            onRename (text);

        juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<ProjectNameField> (this)]
        {
            if (safe == nullptr) return;
            safe->editor = nullptr;
            safe->repaint();
            if (safe->onEditFinished) safe->onEditFinished();
        });
    }
}
