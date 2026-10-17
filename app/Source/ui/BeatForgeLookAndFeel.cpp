#include "ui/BeatForgeLookAndFeel.h"
#include "ui/Theme.h"

namespace beatforge
{
    BeatForgeLookAndFeel::BeatForgeLookAndFeel()
        : juce::LookAndFeel_V4 (juce::LookAndFeel_V4::ColourScheme {
              theme::background,    // windowBackground
              theme::panel,         // widgetBackground
              theme::panelRaised,   // menuBackground
              theme::border,        // outline
              theme::textPrimary,   // defaultText
              theme::control,       // defaultFill
              theme::textPrimary,   // highlightedText
              theme::violet,        // highlightedFill
              theme::textPrimary    // menuText
          })
    {
        setDefaultSansSerifTypefaceName (theme::sansTypeface);

        using namespace juce;
        const std::pair<int, Colour> palette[] {
            { ResizableWindow::backgroundColourId,          theme::background },
            { DocumentWindow::textColourId,                 theme::textPrimary },

            { TextButton::buttonColourId,                   theme::control },
            { TextButton::buttonOnColourId,                 theme::violet },
            { TextButton::textColourOffId,                  theme::textPrimary },
            { TextButton::textColourOnId,                   theme::textPrimary },

            { ComboBox::backgroundColourId,                 theme::control },
            { ComboBox::outlineColourId,                    theme::border },
            { ComboBox::textColourId,                       theme::textPrimary },
            { ComboBox::arrowColourId,                      theme::textSecondary },
            { ComboBox::focusedOutlineColourId,             theme::violet },

            { PopupMenu::backgroundColourId,                theme::panelRaised },
            { PopupMenu::textColourId,                      theme::textPrimary },
            { PopupMenu::highlightedBackgroundColourId,     theme::violet.withAlpha (0.35f) },
            { PopupMenu::highlightedTextColourId,           theme::textPrimary },
            { PopupMenu::headerTextColourId,                theme::textSecondary },

            { Label::textColourId,                          theme::textPrimary },
            { TextEditor::backgroundColourId,               theme::control },
            { TextEditor::textColourId,                     theme::textPrimary },
            { TextEditor::outlineColourId,                  theme::border },
            { TextEditor::focusedOutlineColourId,           theme::violet },
            { TextEditor::highlightColourId,                theme::violet.withAlpha (0.45f) },
            { CaretComponent::caretColourId,                theme::cyan },

            { ListBox::backgroundColourId,                  theme::panel },
            { ListBox::outlineColourId,                     theme::border },
            { ScrollBar::thumbColourId,                     theme::borderStrong },

            { Slider::backgroundColourId,                   theme::control },
            { Slider::trackColourId,                        theme::violet },
            { Slider::thumbColourId,                        theme::textPrimary },
            { Slider::textBoxTextColourId,                  theme::textPrimary },
            { Slider::textBoxBackgroundColourId,            theme::control },
            { Slider::textBoxOutlineColourId,               theme::border },

            { ToggleButton::textColourId,                   theme::textPrimary },
            { ToggleButton::tickColourId,                   theme::violet },
            { ToggleButton::tickDisabledColourId,           theme::textSecondary },

            { TooltipWindow::backgroundColourId,            theme::panelRaised },
            { TooltipWindow::textColourId,                  theme::textPrimary },
            { TooltipWindow::outlineColourId,               theme::border },

            { AlertWindow::backgroundColourId,              theme::panel },
            { AlertWindow::textColourId,                    theme::textPrimary },
            { AlertWindow::outlineColourId,                 theme::border },
        };

        for (auto& [id, colour] : palette)
            setColour (id, colour);
    }

    //==============================================================================
    juce::Font BeatForgeLookAndFeel::getTextButtonFont (juce::TextButton&, int buttonHeight)
    {
        return juce::Font (theme::font (juce::jmin (14.0f, (float) buttonHeight * 0.52f), juce::Font::bold));
    }

    void BeatForgeLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& button,
                                                     const juce::Colour& backgroundColour,
                                                     bool highlighted, bool down)
    {
        auto bounds = button.getLocalBounds().toFloat().reduced (0.5f);
        const bool on = button.getToggleState();
        const auto onColour = button.findColour (juce::TextButton::buttonOnColourId);

        // Off: quiet control surface. On: the button's signal colour, filled.
        auto fill = on ? onColour : backgroundColour;
        if (button.isEnabled())
        {
            if (down)             fill = fill.darker (0.2f);
            else if (highlighted) fill = fill.brighter (on ? 0.1f : 0.12f);
        }
        else
        {
            fill = fill.withMultipliedAlpha (0.5f);
        }

        g.setColour (fill);
        g.fillRoundedRectangle (bounds, theme::controlRadius);

        g.setColour (on ? onColour.brighter (0.25f) : (highlighted ? theme::borderStrong : theme::border));
        g.drawRoundedRectangle (bounds, theme::controlRadius, theme::borderWidth);
    }

    //==============================================================================
    juce::Font BeatForgeLookAndFeel::getComboBoxFont (juce::ComboBox& box)
    {
        return juce::Font (theme::font (juce::jmin (14.0f, (float) box.getHeight() * 0.5f)));
    }

    void BeatForgeLookAndFeel::drawComboBox (juce::Graphics& g, int width, int height, bool isButtonDown,
                                             int, int, int, int, juce::ComboBox& box)
    {
        const auto bounds = juce::Rectangle<float> (0.0f, 0.0f, (float) width, (float) height).reduced (0.5f);
        const bool hot = box.isMouseOver (true) || isButtonDown || box.isPopupActive();

        g.setColour (isButtonDown ? theme::control.darker (0.2f) : theme::control);
        g.fillRoundedRectangle (bounds, theme::controlRadius);
        g.setColour (box.isPopupActive() ? theme::violet : (hot ? theme::borderStrong : theme::border));
        g.drawRoundedRectangle (bounds, theme::controlRadius, theme::borderWidth);

        // Chevron.
        const auto arrow = juce::Rectangle<float> ((float) width - 20.0f, 0.0f, 12.0f, (float) height)
                               .withSizeKeepingCentre (8.0f, 4.5f);
        juce::Path chevron;
        chevron.startNewSubPath (arrow.getX(), arrow.getY());
        chevron.lineTo (arrow.getCentreX(), arrow.getBottom());
        chevron.lineTo (arrow.getRight(), arrow.getY());
        g.setColour (hot ? theme::textPrimary : theme::textSecondary);
        g.strokePath (chevron, juce::PathStrokeType (1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    juce::Font BeatForgeLookAndFeel::getPopupMenuFont()
    {
        return juce::Font (theme::font (14.0f));
    }

    //==============================================================================
    int BeatForgeLookAndFeel::getSliderThumbRadius (juce::Slider& slider)
    {
        return slider.isHorizontal() || slider.isVertical() ? 6 : LookAndFeel_V4::getSliderThumbRadius (slider);
    }

    void BeatForgeLookAndFeel::drawLinearSlider (juce::Graphics& g, int x, int y, int width, int height,
                                                 float sliderPos, float minSliderPos, float maxSliderPos,
                                                 juce::Slider::SliderStyle style, juce::Slider& slider)
    {
        if (style != juce::Slider::LinearHorizontal && style != juce::Slider::LinearVertical)
            return LookAndFeel_V4::drawLinearSlider (g, x, y, width, height, sliderPos, minSliderPos, maxSliderPos, style, slider);

        const bool horizontal = style == juce::Slider::LinearHorizontal;
        const bool hot = slider.isMouseOverOrDragging() && slider.isEnabled();
        const auto colour = slider.findColour (juce::Slider::trackColourId);
        const float thickness = 4.0f;

        // Thin groove, value fill in the slider's signal colour, round thumb.
        juce::Point<float> start, end, thumb;
        if (horizontal)
        {
            const auto cy = (float) y + (float) height * 0.5f;
            start = { (float) x, cy };
            end = { (float) (x + width), cy };
            thumb = { sliderPos, cy };
        }
        else
        {
            const auto cx = (float) x + (float) width * 0.5f;
            start = { cx, (float) (y + height) };
            end = { cx, (float) y };
            thumb = { cx, sliderPos };
        }

        juce::Path groove, fill;
        groove.startNewSubPath (start);
        groove.lineTo (end);
        fill.startNewSubPath (start);
        fill.lineTo (thumb);

        const juce::PathStrokeType stroke (thickness, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);
        g.setColour (theme::control.brighter (0.15f));
        g.strokePath (groove, stroke);
        g.setColour (slider.isEnabled() ? colour : theme::textSecondary);
        g.strokePath (fill, stroke);

        const float radius = (float) getSliderThumbRadius (slider) + (slider.isMouseButtonDown() ? 1.5f : (hot ? 1.0f : 0.0f));
        g.setColour (slider.isEnabled() ? colour : theme::textSecondary);
        g.fillEllipse (juce::Rectangle<float> (radius * 2.0f, radius * 2.0f).withCentre (thumb));
        g.setColour (theme::background);
        g.fillEllipse (juce::Rectangle<float> (radius * 0.8f, radius * 0.8f).withCentre (thumb));
    }

    //==============================================================================
    juce::Rectangle<int> BeatForgeLookAndFeel::getTooltipBounds (const juce::String& text, juce::Point<int> screenPos,
                                                               juce::Rectangle<int> parentArea)
    {
        const auto font = juce::Font (theme::font (13.0f));
        const int w = juce::jmin (360, juce::GlyphArrangement::getStringWidthInt (font, text) + 20);
        const int h = 26;

        return juce::Rectangle<int> (screenPos.x > parentArea.getCentreX() ? screenPos.x - (w + 12) : screenPos.x + 24,
                                     screenPos.y > parentArea.getCentreY() ? screenPos.y - (h + 6) : screenPos.y + 6,
                                     w, h).constrainedWithin (parentArea);
    }

    void BeatForgeLookAndFeel::drawTooltip (juce::Graphics& g, const juce::String& text, int width, int height)
    {
        const auto bounds = juce::Rectangle<float> ((float) width, (float) height);
        g.setColour (theme::panelRaised);
        g.fillRoundedRectangle (bounds, theme::controlRadius);
        g.setColour (theme::borderStrong);
        g.drawRoundedRectangle (bounds.reduced (0.5f), theme::controlRadius, theme::borderWidth);
        g.setColour (theme::textPrimary);
        g.setFont (theme::font (13.0f));
        g.drawFittedText (text, bounds.toNearestInt().reduced (10, 2), juce::Justification::centredLeft, 1);
    }
}
