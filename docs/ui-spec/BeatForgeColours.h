// BeatForgeColours.h
// Colour tokens for the BeatForge UI revamp.
// Source: Figma file "BeatForge UI Revamp" -> variable collection "BeatForge Tokens" (mode: Dark)
// https://www.figma.com/design/mguWCRHCzNCM5ZbFtdzjJU
//
// Token names map 1:1 to the Figma variables (e.g. bg/panel -> Colours::bgPanel).
// Translucent fills used in the design are listed in the spec (SPEC.md, section 3.2);
// build them with .withAlpha() from these base colours rather than adding new tokens.

#pragma once

#include <juce_graphics/juce_graphics.h>
#include <juce_gui_basics/juce_gui_basics.h>

namespace beatforge::Colours
{
    // ---- Backgrounds (darkest -> lightest) --------------------------------
    inline const juce::Colour bgApp      { 0xff060914 }; // window background behind panels
    inline const juce::Colour bgInset    { 0xff080C1C }; // timeline, graph displays, readouts, meters
    inline const juce::Colour bgPanel    { 0xff0B1022 }; // header, status bar, main panels
    inline const juce::Colour bgSurface  { 0xff111733 }; // cards: track headers, modules, buttons
    inline const juce::Colour bgRaised   { 0xff171E40 }; // selected track header, secondary button, off step cell
    inline const juce::Colour bgControl  { 0xff1F2752 }; // knob track, slider track, M/S off, accent-group step cell

    // ---- Borders -----------------------------------------------------------
    inline const juce::Colour borderSubtle  { 0xff1A2146 }; // panel outlines, 16th grid lines
    inline const juce::Colour borderDefault { 0xff262F5E }; // dividers, beat lines, control outlines
    inline const juce::Colour borderStrong  { 0xff3A4580 }; // bar lines, ruler ticks, knob cap outline

    // ---- Text ----------------------------------------------------------------
    inline const juce::Colour textPrimary   { 0xffECEEFF };
    inline const juce::Colour textSecondary { 0xff9BA2CC };
    inline const juce::Colour textMuted     { 0xff5D6492 }; // section labels, units, scale numbers

    // ---- Accents -------------------------------------------------------------
    inline const juce::Colour accentPurple    { 0xffA855F7 }; // primary accent: play, playhead, active tab, oscillator
    inline const juce::Colour accentPurpleDim { 0xff2E1A5C }; // fill behind active toggles (loop, keyboard MIDI, selected wave)
    inline const juce::Colour accentPink      { 0xffEC4899 }; // envelope, Drums track, Kick row
    inline const juce::Colour accentCyan      { 0xff22D3EE }; // filter, Closed Hat row, position readout, Solo on
    inline const juce::Colour accentBlue      { 0xff3B82F6 }; // Bass track
    inline const juce::Colour accentOrange    { 0xffF59E0B }; // Perc track, Snare row
    inline const juce::Colour accentTeal      { 0xff2DD4BF }; // Open Hat row
    inline const juce::Colour accentViolet    { 0xff8B5CF6 }; // Lead track, Clap row

    // ---- Status --------------------------------------------------------------
    inline const juce::Colour statusGreen { 0xff22C55E }; // audio device OK dot
    inline const juce::Colour statusRed   { 0xffF43F5E }; // record button
    inline const juce::Colour statusAmber { 0xffFBBF24 }; // unsaved dot, Mute on

    // ---- Track colour cycle --------------------------------------------------
    // New tracks take the next colour in this order (matches the mockup).
    inline const juce::Colour trackPalette[] {
        accentPink, accentBlue, accentViolet, accentOrange, accentCyan, accentTeal
    };
    inline constexpr int trackPaletteSize = (int) (sizeof (trackPalette) / sizeof (trackPalette[0]));

    inline juce::Colour trackColour (int trackIndex)
    {
        return trackPalette[((trackIndex % trackPaletteSize) + trackPaletteSize) % trackPaletteSize];
    }

    // ---- Drum lane colours (step sequencer rows) ------------------------------
    inline const juce::Colour kick      = accentPink;
    inline const juce::Colour snare     = accentOrange;
    inline const juce::Colour closedHat = accentCyan;
    inline const juce::Colour openHat   = accentTeal;
    inline const juce::Colour clap      = accentViolet;

    // ---- Meter gradient (bottom -> top) --------------------------------------
    inline juce::ColourGradient meterGradient (juce::Rectangle<float> bar)
    {
        juce::ColourGradient g (accentCyan, bar.getBottomLeft(), accentPink, bar.getTopLeft(), false);
        g.addColour (0.65, accentViolet);
        return g;
    }

    // ---- Apply to a LookAndFeel_V4 -------------------------------------------
    // Sets the stock JUCE colour IDs so untouched widgets already look on-theme.
    inline void applyTo (juce::LookAndFeel_V4& lf)
    {
        using namespace juce;

        lf.setColourScheme ({
            bgPanel,        // windowBackground
            bgSurface,      // widgetBackground
            bgInset,        // menuBackground
            borderDefault,  // outline
            textPrimary,    // defaultText
            bgControl,      // defaultFill
            textPrimary,    // highlightedText
            accentPurple,   // highlightedFill
            textPrimary     // menuText
        });

        lf.setColour (ResizableWindow::backgroundColourId, bgApp);

        lf.setColour (TextButton::buttonColourId,   bgRaised);
        lf.setColour (TextButton::buttonOnColourId, accentPurple);
        lf.setColour (TextButton::textColourOffId,  textPrimary);
        lf.setColour (TextButton::textColourOnId,   textPrimary);

        lf.setColour (Slider::rotarySliderFillColourId,    accentPurple);
        lf.setColour (Slider::rotarySliderOutlineColourId, bgControl);
        lf.setColour (Slider::thumbColourId,               textPrimary);
        lf.setColour (Slider::trackColourId,               accentPurple);
        lf.setColour (Slider::backgroundColourId,          bgControl);
        lf.setColour (Slider::textBoxTextColourId,         textSecondary);
        lf.setColour (Slider::textBoxBackgroundColourId,   juce::Colours::transparentBlack);
        lf.setColour (Slider::textBoxOutlineColourId,      juce::Colours::transparentBlack);

        lf.setColour (ComboBox::backgroundColourId, bgRaised);
        lf.setColour (ComboBox::outlineColourId,    borderDefault);
        lf.setColour (ComboBox::textColourId,       textPrimary);
        lf.setColour (ComboBox::arrowColourId,      textSecondary);

        lf.setColour (PopupMenu::backgroundColourId,            bgSurface);
        lf.setColour (PopupMenu::textColourId,                  textPrimary);
        lf.setColour (PopupMenu::highlightedBackgroundColourId, accentPurpleDim);
        lf.setColour (PopupMenu::highlightedTextColourId,       textPrimary);

        lf.setColour (Label::textColourId, textPrimary);

        lf.setColour (TabbedButtonBar::tabTextColourId,   textMuted);
        lf.setColour (TabbedButtonBar::frontTextColourId, textPrimary);

        lf.setColour (ScrollBar::thumbColourId, borderStrong);
        lf.setColour (ScrollBar::trackColourId, bgSurface);

        lf.setColour (TooltipWindow::backgroundColourId, bgRaised);
        lf.setColour (TooltipWindow::textColourId,       textPrimary);
        lf.setColour (TooltipWindow::outlineColourId,    borderDefault);
    }
}
