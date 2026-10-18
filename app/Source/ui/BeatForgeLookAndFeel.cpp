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
              theme::purple,        // highlightedFill
              theme::textPrimary    // menuText
          })
    {
        using namespace juce;
        const std::pair<int, Colour> palette[] {
            { ResizableWindow::backgroundColourId,          theme::background },
            { DocumentWindow::textColourId,                 theme::textPrimary },

            { TextButton::buttonColourId,                   theme::bgRaised },
            { TextButton::buttonOnColourId,                 theme::purple },
            { TextButton::textColourOffId,                  theme::textPrimary },
            { TextButton::textColourOnId,                   theme::textPrimary },

            { ComboBox::backgroundColourId,                 theme::control },
            { ComboBox::outlineColourId,                    theme::border },
            { ComboBox::textColourId,                       theme::textPrimary },
            { ComboBox::arrowColourId,                      theme::textSecondary },
            { ComboBox::focusedOutlineColourId,             theme::purple },

            { PopupMenu::backgroundColourId,                theme::bgSurface },
            { PopupMenu::textColourId,                      theme::textPrimary },
            { PopupMenu::highlightedBackgroundColourId,     theme::purpleDim },
            { PopupMenu::highlightedTextColourId,           theme::textPrimary },
            { PopupMenu::headerTextColourId,                theme::textSecondary },

            { Label::textColourId,                          theme::textPrimary },
            { TextEditor::backgroundColourId,               theme::control },
            { TextEditor::textColourId,                     theme::textPrimary },
            { TextEditor::outlineColourId,                  theme::border },
            { TextEditor::focusedOutlineColourId,           theme::purple },
            { TextEditor::highlightColourId,                theme::purple.withAlpha (0.45f) },
            { CaretComponent::caretColourId,                theme::cyan },

            { ListBox::backgroundColourId,                  theme::panel },
            { ListBox::outlineColourId,                     theme::border },
            { ScrollBar::thumbColourId,                     theme::borderStrong },
            { ScrollBar::trackColourId,                     theme::bgSurface },

            { Slider::backgroundColourId,                   theme::bgControl },
            { Slider::trackColourId,                        theme::purple },
            { Slider::thumbColourId,                        theme::textPrimary },
            { Slider::textBoxTextColourId,                  theme::textPrimary },
            { Slider::textBoxBackgroundColourId,            theme::control },
            { Slider::textBoxOutlineColourId,               theme::border },

            { ToggleButton::textColourId,                   theme::textPrimary },
            { ToggleButton::tickColourId,                   theme::purple },
            { ToggleButton::tickDisabledColourId,           theme::textSecondary },

            { TooltipWindow::backgroundColourId,            theme::bgRaised },
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
    // Stock widgets that ask for the default sans-serif get the embedded Inter.
    juce::Typeface::Ptr BeatForgeLookAndFeel::getTypefaceForFont (const juce::Font& font)
    {
        if (font.getTypefaceName() == juce::Font::getDefaultSansSerifFontName())
            return font.isBold() ? theme::typefaces::interSemiBold() : theme::typefaces::interRegular();
        if (font.getTypefaceName() == juce::Font::getDefaultMonospacedFontName())
            return font.isBold() ? theme::typefaces::monoMedium() : theme::typefaces::monoRegular();
        return LookAndFeel_V4::getTypefaceForFont (font);
    }

    //==============================================================================
    juce::Font BeatForgeLookAndFeel::getTextButtonFont (juce::TextButton&, int buttonHeight)
    {
        return juce::Font (theme::font (juce::jmin (13.0f, (float) buttonHeight * 0.5f), juce::Font::bold));
    }

    void BeatForgeLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& button,
                                                     const juce::Colour& backgroundColour,
                                                     bool highlighted, bool down)
    {
        auto bounds = button.getLocalBounds().toFloat().reduced (0.5f);
        const bool on = button.getToggleState();
        const auto onColour = button.findColour (juce::TextButton::buttonOnColourId);

        // Off: the button's surface with a quiet outline (Secondary, or Ghost when
        // transparent). On: its signal colour, filled, no outline (Primary).
        auto fill = on ? onColour : backgroundColour;
        if (button.isEnabled())
        {
            if (down)
                fill = fill.isTransparent() ? theme::bgRaised : fill.darker (0.15f);
            else if (highlighted)
                fill = fill.isTransparent() ? theme::bgSurface
                     : (on || fill != theme::bgRaised) ? fill.brighter (0.15f) : theme::bgControl;
        }
        else
        {
            fill = fill.withMultipliedAlpha (0.4f);
        }

        g.setColour (fill);
        g.fillRoundedRectangle (bounds, theme::controlRadius);

        if (! on)
        {
            g.setColour (highlighted ? theme::borderStrong : (backgroundColour.isTransparent() ? theme::borderSubtle : theme::border));
            g.drawRoundedRectangle (bounds, theme::controlRadius, theme::borderWidth);
        }
    }

    //==============================================================================
    juce::Font BeatForgeLookAndFeel::getComboBoxFont (juce::ComboBox& box)
    {
        return juce::Font (theme::font (juce::jmin (13.0f, (float) box.getHeight() * 0.5f), juce::Font::bold));
    }

    void BeatForgeLookAndFeel::drawComboBox (juce::Graphics& g, int width, int height, bool isButtonDown,
                                             int, int, int, int, juce::ComboBox& box)
    {
        const auto bounds = juce::Rectangle<float> (0.0f, 0.0f, (float) width, (float) height).reduced (0.5f);
        const bool hot = box.isMouseOver (true) || isButtonDown || box.isPopupActive();

        g.setColour (isButtonDown ? theme::bgRaised.darker (0.15f) : (hot ? theme::bgControl : theme::bgRaised));
        g.fillRoundedRectangle (bounds, theme::controlRadius);
        g.setColour (box.isPopupActive() ? theme::purple : (hot ? theme::borderStrong : theme::border));
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
        return juce::Font (theme::font (13.0f));
    }

    // JUCE's version boldens the menu font, which looks the embedded Inter up by
    // family and style and trips a FreeType assertion for static (non-variable)
    // fonts. Use the Semi Bold face directly instead.
    void BeatForgeLookAndFeel::drawPopupMenuSectionHeader (juce::Graphics& g, const juce::Rectangle<int>& area,
                                                           const juce::String& sectionName)
    {
        g.setFont (theme::captionFont (10.0f));
        g.setColour (theme::textMuted);
        g.drawFittedText (sectionName.toUpperCase(), area.getX() + 12, area.getY(), area.getWidth() - 16,
                          (int) ((float) area.getHeight() * 0.8f), juce::Justification::bottomLeft, 1);
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

        // 4 px bar in the slider's colour up to the value, bg/control after it,
        // and a white thumb ringed in the slider's colour.
        const juce::PathStrokeType stroke (thickness, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);
        const auto signal = slider.isEnabled() ? colour : theme::textMuted;
        g.setColour (theme::bgControl);
        g.strokePath (groove, stroke);
        g.setColour (signal);
        g.strokePath (fill, stroke);

        const float radius = (float) getSliderThumbRadius (slider) + (slider.isMouseButtonDown() ? 1.0f : (hot ? 0.5f : 0.0f));
        const auto knob = juce::Rectangle<float> (radius * 2.0f, radius * 2.0f).withCentre (thumb);
        g.setColour (theme::textPrimary);
        g.fillEllipse (knob);
        g.setColour (signal);
        g.drawEllipse (knob.reduced (1.0f), 2.0f);
    }

    //==============================================================================
    namespace
    {
        // Tooltips wrap at this width and grow to fit (several lines are fine).
        constexpr float tooltipWrap = 420.0f;

        juce::TextLayout tooltipLayout (const juce::String& text)
        {
            juce::AttributedString as;
            as.append (text, juce::Font (theme::font (13.0f)), theme::textPrimary);
            as.setWordWrap (juce::AttributedString::byWord);
            juce::TextLayout layout;
            layout.createLayout (as, tooltipWrap);
            return layout;
        }
    }

    juce::Rectangle<int> BeatForgeLookAndFeel::getTooltipBounds (const juce::String& text, juce::Point<int> screenPos,
                                                               juce::Rectangle<int> parentArea)
    {
        const auto layout = tooltipLayout (text);
        float widest = 0.0f;
        for (int i = 0; i < layout.getNumLines(); ++i)
            widest = juce::jmax (widest, layout.getLine (i).getLineBoundsX().getLength());
        const int w = (int) std::ceil (widest) + 22;
        const int h = (int) std::ceil (layout.getHeight()) + 12;

        return juce::Rectangle<int> (screenPos.x > parentArea.getCentreX() ? screenPos.x - (w + 12) : screenPos.x + 24,
                                     screenPos.y > parentArea.getCentreY() ? screenPos.y - (h + 6) : screenPos.y + 6,
                                     w, h).constrainedWithin (parentArea);
    }

    void BeatForgeLookAndFeel::drawTooltip (juce::Graphics& g, const juce::String& text, int width, int height)
    {
        const auto bounds = juce::Rectangle<float> ((float) width, (float) height);
        g.setColour (theme::bgRaised);
        g.fillRoundedRectangle (bounds, theme::controlRadius);
        g.setColour (theme::border);
        g.drawRoundedRectangle (bounds.reduced (0.5f), theme::controlRadius, theme::borderWidth);
        tooltipLayout (text).draw (g, bounds.reduced (11.0f, 6.0f));
    }

    //==============================================================================
    juce::Font BeatForgeLookAndFeel::getAlertWindowTitleFont()   { return juce::Font (theme::font (17.0f, juce::Font::bold)); }
    juce::Font BeatForgeLookAndFeel::getAlertWindowMessageFont() { return juce::Font (theme::font (14.5f)); }
    juce::Font BeatForgeLookAndFeel::getAlertWindowFont()        { return juce::Font (theme::font (14.0f)); }

    void BeatForgeLookAndFeel::drawAlertBox (juce::Graphics& g, juce::AlertWindow& alert,
                                             const juce::Rectangle<int>&, juce::TextLayout& textLayout)
    {
        const auto bounds = alert.getLocalBounds().toFloat();
        g.setColour (theme::panel);
        g.fillRoundedRectangle (bounds, theme::cornerRadius);
        g.setColour (theme::borderStrong);
        g.drawRoundedRectangle (bounds.reduced (0.5f), theme::cornerRadius, theme::borderWidth);

        // Compact badge instead of JUCE's oversized clipped icon. The text column
        // starts at x = 80 either way, matching AlertWindow's own layout.
        constexpr int iconSpace = 80;
        const auto type = alert.getAlertType();

        if (type != juce::MessageBoxIconType::NoIcon)
        {
            const auto colour = type == juce::MessageBoxIconType::WarningIcon  ? theme::amber
                              : type == juce::MessageBoxIconType::QuestionIcon ? theme::purple
                                                                                : theme::cyan;
            const auto glyph = type == juce::MessageBoxIconType::WarningIcon  ? "!"
                             : type == juce::MessageBoxIconType::QuestionIcon ? "?"
                                                                               : "i";
            const auto badge = juce::Rectangle<float> (24.0f, 26.0f, 36.0f, 36.0f);
            g.setColour (colour.withAlpha (0.16f));
            g.fillEllipse (badge);
            g.setColour (colour);
            g.drawEllipse (badge.reduced (0.75f), 1.5f);
            g.setFont (theme::font (20.0f, juce::Font::bold));
            g.drawText (glyph, badge, juce::Justification::centred);
        }

        const auto left = type != juce::MessageBoxIconType::NoIcon ? iconSpace : 0;
        textLayout.draw (g, juce::Rectangle<int> (left, 30, alert.getWidth() - left,
                                                  alert.getHeight() - getAlertWindowButtonHeight() - 20).toFloat());
    }

    //==============================================================================
    void BeatForgeLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height, float sliderPos,
                                                 float startAngle, float endAngle, juce::Slider& slider)
    {
        // Spec 12.1: a 270-degree ring (bg/control) with the value arc in the
        // knob's colour, a raised cap at 64 % of the size and a white wedge pointer.
        // Ranges that straddle zero (pan) draw their arc from 12 o'clock.
        const auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat();
        const auto size = juce::jmin (bounds.getWidth(), bounds.getHeight());
        const auto centre = bounds.getCentre();
        const float ringWidth = juce::jmax (2.5f, size * 0.5f * 0.16f);
        const float arcRadius = size * 0.5f - ringWidth * 0.5f - 0.5f;
        const auto signal = slider.findColour (juce::Slider::rotarySliderFillColourId);
        const bool hot = slider.isMouseOverOrDragging() && slider.isEnabled();
        const auto angle = startAngle + sliderPos * (endAngle - startAngle);
        const bool bipolar = slider.getMinimum() < 0.0 && slider.getMaximum() > 0.0;
        const auto from = bipolar ? juce::MathConstants<float>::twoPi : startAngle;

        juce::Path track, value;
        track.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f, startAngle, endAngle, true);
        value.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f, juce::jmin (from, angle), juce::jmax (from, angle), true);
        const juce::PathStrokeType stroke (ringWidth, juce::PathStrokeType::curved, juce::PathStrokeType::butt);
        g.setColour (theme::bgControl);
        g.strokePath (track, stroke);
        g.setColour (slider.isEnabled() ? (slider.isMouseButtonDown() || hot ? signal.brighter (0.15f) : signal) : theme::textMuted);
        g.strokePath (value, stroke);

        // Cap with a soft shadow (black @ 0.5, y 2, blur 4).
        const float capRadius = size * 0.32f;
        const auto cap = juce::Rectangle<float> (capRadius * 2.0f, capRadius * 2.0f).withCentre (centre);
        juce::Path capPath;
        capPath.addEllipse (cap);
        juce::DropShadow (theme::shadow.withAlpha (0.5f), 4, { 0, 2 }).drawForPath (g, capPath);
        g.setColour (hot ? theme::bgControl : theme::bgRaised);
        g.fillPath (capPath);
        g.setColour (theme::borderStrong);
        g.drawEllipse (cap.reduced (0.5f), 1.0f);

        // Wedge pointer from 45 % of the cap radius to its edge.
        const auto along = [&] (float r, float a) {
            return juce::Point<float> (centre.x + std::sin (a) * r, centre.y - std::cos (a) * r);
        };
        const float halfWidth = juce::jmax (1.0f, capRadius * 0.09f);
        const auto side = juce::MathConstants<float>::halfPi;
        juce::Path wedge;
        const auto inner = along (capRadius * 0.45f, angle);
        const auto outer = along (capRadius - 2.0f, angle);
        wedge.startNewSubPath (inner + along (halfWidth * 0.6f, angle + side) - centre);
        wedge.lineTo (outer + along (halfWidth, angle + side) - centre);
        wedge.lineTo (outer + along (halfWidth, angle - side) - centre);
        wedge.lineTo (inner + along (halfWidth * 0.6f, angle - side) - centre);
        wedge.closeSubPath();
        g.setColour (slider.isEnabled() ? theme::textPrimary : theme::textMuted);
        g.fillPath (wedge);
    }

    juce::Label* BeatForgeLookAndFeel::createSliderTextBox (juce::Slider& slider)
    {
        auto* label = LookAndFeel_V4::createSliderTextBox (slider);
        label->setFont (juce::Font (theme::numericFont (11.5f)));
        label->setColour (juce::Label::textColourId, theme::textPrimary);
        label->setColour (juce::Label::backgroundColourId, juce::Colours::transparentBlack);
        label->setColour (juce::Label::outlineColourId, juce::Colours::transparentBlack);
        return label;
    }
}
