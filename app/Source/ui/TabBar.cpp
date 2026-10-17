#include "ui/TabBar.h"
#include "ui/Theme.h"

namespace beatforge
{
    namespace
    {
        constexpr int tabPadding = 16;
        juce::Font tabFont() { return juce::Font (theme::font (13.5f, juce::Font::bold)); }
    }

    TabBar::TabBar (juce::StringArray tabNames) : names (std::move (tabNames))
    {
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
    }

    void TabBar::setCurrentTab (int index, juce::NotificationType notification)
    {
        index = juce::jlimit (0, names.size() - 1, index);
        if (index == current)
            return;

        current = index;
        repaint();
        if (notification != juce::dontSendNotification && onChange)
            onChange (current);
    }

    int TabBar::getPreferredWidth() const
    {
        int w = 0;
        for (auto& n : names)
            w += juce::GlyphArrangement::getStringWidthInt (tabFont(), n) + tabPadding * 2;
        return w;
    }

    juce::Rectangle<int> TabBar::tabBounds (int index) const
    {
        int x = 0;
        for (int i = 0; i < names.size(); ++i)
        {
            const int w = juce::GlyphArrangement::getStringWidthInt (tabFont(), names[i]) + tabPadding * 2;
            if (i == index)
                return { x, 0, w, getHeight() };
            x += w;
        }
        return {};
    }

    int TabBar::tabAt (juce::Point<int> p) const
    {
        for (int i = 0; i < names.size(); ++i)
            if (tabBounds (i).contains (p))
                return i;
        return -1;
    }

    void TabBar::paint (juce::Graphics& g)
    {
        g.setColour (theme::border);
        g.drawHorizontalLine (getHeight() - 1, 0.0f, (float) getWidth());

        for (int i = 0; i < names.size(); ++i)
        {
            const auto r = tabBounds (i);
            const bool active = i == current;

            g.setColour (active ? theme::textPrimary : (i == hovered ? theme::textPrimary.withAlpha (0.8f) : theme::textSecondary));
            g.setFont (tabFont());
            g.drawText (names[i], r.withTrimmedBottom (3), juce::Justification::centred);

            if (active)
            {
                g.setColour (theme::violet);
                g.fillRoundedRectangle (r.withTop (getHeight() - 3).reduced (8, 0).toFloat(), 1.5f);
            }
        }
    }

    void TabBar::mouseDown (const juce::MouseEvent& e)
    {
        if (const auto i = tabAt (e.getPosition()); i >= 0)
            setCurrentTab (i);
    }

    void TabBar::mouseMove (const juce::MouseEvent& e)
    {
        const auto i = tabAt (e.getPosition());
        if (i != hovered) { hovered = i; repaint(); }
    }

    void TabBar::mouseExit (const juce::MouseEvent&)
    {
        hovered = -1;
        repaint();
    }
}
