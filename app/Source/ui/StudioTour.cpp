#include "ui/StudioTour.h"
#include "ui/Theme.h"

namespace beatforge
{
    StudioTour::StudioTour (std::vector<Step> s) : steps (std::move (s))
    {
        setWantsKeyboardFocus (true);
        setInterceptsMouseClicks (true, true);

        skipButton.setColour (juce::TextButton::buttonColourId, juce::Colours::transparentBlack);   // ghost
        nextButton.setToggleState (true, juce::dontSendNotification);                              // primary
        for (auto* b : { &skipButton, &backButton, &nextButton })
        {
            b->setWantsKeyboardFocus (false);
            addAndMakeVisible (*b);
        }
        skipButton.onClick = [this] { close(); };
        backButton.onClick = [this] { showStep (step - 1); };
        nextButton.onClick = [this] { if (step + 1 < (int) steps.size()) showStep (step + 1); else close(); };
    }

    void StudioTour::showStep (int index)
    {
        step = juce::jlimit (0, (int) steps.size() - 1, index);
        const bool last = step == (int) steps.size() - 1;
        backButton.setVisible (step > 0);
        skipButton.setVisible (! last);
        nextButton.setButtonText (step == 0 ? "Start tour" : last ? "Finish" : "Next");
        resized();
        repaint();
    }

    void StudioTour::close()
    {
        if (auto callback = onClose)
            callback();   // may delete this
    }

    void StudioTour::parentSizeChanged()
    {
        if (auto* parent = getParentComponent())
            setBounds (parent->getLocalBounds());
    }

    juce::Rectangle<int> StudioTour::spotlight() const
    {
        const auto& s = steps[(size_t) step];
        return s.target ? s.target().expanded (6) : juce::Rectangle<int>();
    }

    juce::TextLayout StudioTour::bodyLayout() const
    {
        juce::AttributedString text;
        text.append (steps[(size_t) step].body, juce::Font (theme::font (13.0f)), theme::textSecondary);
        text.setWordWrap (juce::AttributedString::byWord);
        text.setLineSpacing (3.0f);
        juce::TextLayout layout;
        layout.createLayout (text, (float) (cardWidth - 2 * padding));
        return layout;
    }

    // Below the spotlight if it fits, else above, else beside it, else centred.
    juce::Rectangle<int> StudioTour::cardBounds() const
    {
        const int height = padding + 14 + 8 + 22 + 8 + (int) std::ceil (bodyLayout().getHeight()) + 20 + 32 + padding;
        const auto area = getLocalBounds().reduced (16);
        const auto target = spotlight();
        auto card = juce::Rectangle<int> (cardWidth, height).withCentre (area.getCentre());

        if (! target.isEmpty())
        {
            const int gap = 16;
            if (target.getBottom() + gap + height <= area.getBottom())
                card = card.withPosition (target.getCentreX() - cardWidth / 2, target.getBottom() + gap);
            else if (target.getY() - gap - height >= area.getY())
                card = card.withPosition (target.getCentreX() - cardWidth / 2, target.getY() - gap - height);
            else if (target.getRight() + gap + cardWidth <= area.getRight())
                card = card.withPosition (target.getRight() + gap, target.getCentreY() - height / 2);
            else if (target.getX() - gap - cardWidth >= area.getX())
                card = card.withPosition (target.getX() - gap - cardWidth, target.getCentreY() - height / 2);
        }
        return card.constrainedWithin (area);
    }

    void StudioTour::paint (juce::Graphics& g)
    {
        // Dim everything except the spotlight.
        const auto target = spotlight().toFloat();
        juce::Path dim;
        dim.addRectangle (getLocalBounds().toFloat());
        if (! target.isEmpty())
        {
            dim.addRoundedRectangle (target, 14.0f);
            dim.setUsingNonZeroWinding (false);
        }
        g.setColour (theme::bgApp.withAlpha (0.78f));
        g.fillPath (dim);

        if (! target.isEmpty())
        {
            juce::Path ring;
            ring.addRoundedRectangle (target, 14.0f);
            {
                // The glow goes outside only; the spotlit area stays untouched.
                juce::Graphics::ScopedSaveState state (g);
                g.reduceClipRegion (dim);
                juce::DropShadow (theme::purple.withAlpha (0.6f), 14, {}).drawForPath (g, ring);
            }
            g.setColour (theme::purple);
            g.strokePath (ring, juce::PathStrokeType (2.0f));
        }

        // Card: step counter, title, text; the buttons are child components.
        const auto card = cardBounds();
        juce::Path shape;
        shape.addRoundedRectangle (card.toFloat(), 12.0f);
        juce::DropShadow (theme::shadow.withAlpha (0.6f), 24, { 0, 8 }).drawForPath (g, shape);
        g.setColour (theme::bgSurface);
        g.fillPath (shape);
        g.setColour (theme::borderStrong);
        g.drawRoundedRectangle (card.toFloat().reduced (0.5f), 12.0f, 1.0f);

        auto inner = card.reduced (padding);
        auto caption = inner.removeFromTop (14);
        g.setColour (theme::purple);
        g.setFont (theme::captionFont (10.0f));
        g.drawText ("STUDIO TOUR", caption, juce::Justification::centredLeft, false);
        g.setColour (theme::textMuted);
        g.setFont (theme::numericFont (10.0f));
        g.drawText (juce::String (step + 1) + " / " + juce::String ((int) steps.size()), caption, juce::Justification::centredRight, false);
        inner.removeFromTop (8);

        g.setColour (theme::textPrimary);
        g.setFont (theme::font (17.0f, juce::Font::bold));
        g.drawText (steps[(size_t) step].title, inner.removeFromTop (22), juce::Justification::centredLeft, true);
        inner.removeFromTop (8);

        const auto layout = bodyLayout();
        layout.draw (g, inner.removeFromTop ((int) std::ceil (layout.getHeight())).toFloat());
    }

    void StudioTour::resized()
    {
        auto row = cardBounds().reduced (padding).removeFromBottom (32);
        nextButton.setBounds (row.removeFromRight (104));
        row.removeFromRight (8);
        backButton.setBounds (row.removeFromRight (76));
        skipButton.setBounds (row.removeFromLeft (92));
    }

    bool StudioTour::keyPressed (const juce::KeyPress& key)
    {
        if (key == juce::KeyPress::escapeKey)
            close();
        else if (key == juce::KeyPress::rightKey || key == juce::KeyPress::returnKey)
            nextButton.triggerClick();
        else if (key == juce::KeyPress::leftKey && step > 0)
            showStep (step - 1);
        return true;   // nothing reaches the studio while the tour is open
    }
}
