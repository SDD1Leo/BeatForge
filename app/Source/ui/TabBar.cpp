#include "ui/TabBar.h"
#include "ui/Theme.h"

namespace beatforge
{
    namespace
    {
        constexpr int tabPadding = 10;   // 20 px between labels
        juce::Font tabFont() { return juce::Font (theme::font (13.0f, juce::Font::bold)); }
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
        // Spec 12.5: label over a 2 px underline spanning the label; the active
        // tab is bright with a purple underline, the others muted.
        for (int i = 0; i < names.size(); ++i)
        {
            const auto r = tabBounds (i).reduced (tabPadding, 0);
            const bool active = i == current;

            g.setColour (active ? theme::textPrimary : (i == hovered ? theme::textSecondary : theme::textMuted));
            g.setFont (tabFont());
            g.drawText (names[i], r.withTrimmedBottom (10), juce::Justification::centredBottom);

            if (active)
            {
                g.setColour (theme::purple);
                g.fillRect (r.withTop (getHeight() - 2));
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
