#pragma once

#include <juce_graphics/juce_graphics.h>

#include "BinaryData.h"

// BeatForge design tokens, from the Figma file "BeatForge UI Revamp"
// (docs/ui-spec/SPEC.md and BeatForgeColours.h). Every component takes colours,
// radii and fonts from here (directly or via the LookAndFeel), so the visual
// direction can be tuned in one place.
namespace beatforge::theme
{
    // --- Backgrounds (darkest -> lightest) ---------------------------------------
    inline const juce::Colour bgApp         { 0xff060914 };   // window behind panels
    inline const juce::Colour bgInset       { 0xff080c1c };   // timeline, graphs, readouts, meters
    inline const juce::Colour bgPanel       { 0xff0b1022 };   // header, status bar, main panels
    inline const juce::Colour bgSurface     { 0xff111733 };   // cards: track headers, modules
    inline const juce::Colour bgRaised      { 0xff171e40 };   // selected header, secondary button, off step
    inline const juce::Colour bgControl     { 0xff1f2752 };   // knob/slider track, M/S off, accent-group step

    // --- Borders -----------------------------------------------------------------
    inline const juce::Colour borderSubtle  { 0xff1a2146 };   // panel outlines, 16th grid lines
    inline const juce::Colour border        { 0xff262f5e };   // dividers, beat lines, control outlines
    inline const juce::Colour borderStrong  { 0xff3a4580 };   // bar lines, ruler ticks, knob cap

    // --- Text --------------------------------------------------------------------
    inline const juce::Colour textPrimary   { 0xffeceeff };
    inline const juce::Colour textSecondary { 0xff9ba2cc };   // labels, metadata
    inline const juce::Colour textMuted     { 0xff5d6492 };   // section labels, units, scale numbers

    // --- Accents -----------------------------------------------------------------
    inline const juce::Colour purple        { 0xffa855f7 };   // primary accent: play, playhead, active tab
    inline const juce::Colour purpleDim     { 0xff2e1a5c };   // fill behind active toggles
    inline const juce::Colour pink          { 0xffec4899 };   // envelope
    inline const juce::Colour cyan          { 0xff22d3ee };   // filter, position readout, solo
    inline const juce::Colour blue          { 0xff3b82f6 };
    inline const juce::Colour orange        { 0xfff59e0b };
    inline const juce::Colour teal          { 0xff2dd4bf };
    inline const juce::Colour violet        { 0xff8b5cf6 };

    // --- Status ------------------------------------------------------------------
    inline const juce::Colour green         { 0xff22c55e };   // audio device OK
    inline const juce::Colour red           { 0xfff43f5e };   // record
    inline const juce::Colour amber         { 0xfffbbf24 };   // unsaved, mute
    inline const juce::Colour shadow        { 0xff000000 };   // drop shadows (with alpha)

    // --- Older names, kept so existing components read the new palette ----------
    inline const juce::Colour background    = bgApp;
    inline const juce::Colour panel         = bgPanel;
    inline const juce::Colour panelRaised   = bgSurface;
    inline const juce::Colour control       = bgRaised;

    // --- Semantic roles ----------------------------------------------------------
    inline const juce::Colour accent        = purple;         // toggles that are on, playing
    inline const juce::Colour playhead      = purple;
    inline const juce::Colour ok            = green;          // healthy status
    inline const juce::Colour error         = red;            // errors, clipping
    inline const juce::Colour record        = red;            // record button, recording state
    inline const juce::Colour muteOn        = amber;
    inline const juce::Colour soloOn        = cyan;

    // Level meters, bottom to top: cyan, violet at 65 %, pink at the top.
    inline const juce::Colour meterLow      = cyan;
    inline const juce::Colour meterMid      = violet;
    inline const juce::Colour meterHigh     = pink;

    inline juce::ColourGradient meterGradient (juce::Rectangle<float> bar)
    {
        juce::ColourGradient g (meterLow, bar.getBottomLeft(), meterHigh, bar.getTopLeft(), false);
        g.addColour (0.65, meterMid);
        return g;
    }

    // --- Track colours -----------------------------------------------------------
    namespace track
    {
        // Step-sequencer rows.
        inline const juce::Colour kick      = pink;
        inline const juce::Colour snare     = orange;
        inline const juce::Colour closedHat = cyan;
        inline const juce::Colour openHat   = teal;
        inline const juce::Colour clap      = violet;
        inline const juce::Colour synth     = violet;   // notes in the piano roll

        // Arrangement tracks take the next colour in creation order (never random).
        inline const juce::Colour palette[] { pink, blue, violet, orange, cyan, teal };

        inline juce::Colour forIndex (int creationIndex)
        {
            constexpr int n = (int) std::size (palette);
            return palette[(size_t) (((creationIndex % n) + n) % n)];
        }
    }

    // --- Geometry ----------------------------------------------------------------
    constexpr float panelRadius = 12.0f;    // arrangement, editor, master panels
    constexpr float cardRadius = 8.0f;      // track headers, modules
    constexpr float cornerRadius = 8.0f;    // other rounded areas
    constexpr float controlRadius = 6.0f;   // buttons, fields
    constexpr float toggleRadius = 5.0f;    // M/S toggles, step cells
    constexpr float borderWidth = 1.0f;

    // --- Typography --------------------------------------------------------------
    // Inter for text, JetBrains Mono for numbers that change while playing.
    // Both are built into the app (app/Resources/fonts) so every machine looks the same.
    namespace typefaces
    {
        inline juce::Typeface::Ptr load (const char* data, int size)
        {
            return juce::Typeface::createSystemTypefaceFor (data, (size_t) size);
        }

        inline juce::Typeface::Ptr interRegular()  { static auto t = load (BinaryData::InterRegular_ttf, BinaryData::InterRegular_ttfSize); return t; }
        inline juce::Typeface::Ptr interSemiBold() { static auto t = load (BinaryData::InterSemiBold_ttf, BinaryData::InterSemiBold_ttfSize); return t; }
        inline juce::Typeface::Ptr interBold()     { static auto t = load (BinaryData::InterBold_ttf, BinaryData::InterBold_ttfSize); return t; }
        inline juce::Typeface::Ptr monoRegular()   { static auto t = load (BinaryData::JetBrainsMonoRegular_ttf, BinaryData::JetBrainsMonoRegular_ttfSize); return t; }
        inline juce::Typeface::Ptr monoMedium()    { static auto t = load (BinaryData::JetBrainsMonoMedium_ttf, BinaryData::JetBrainsMonoMedium_ttfSize); return t; }
    }

    // Inter: plain = Regular, bold = Semi Bold (the UI's "bold").
    inline juce::FontOptions font (float height, int style = juce::Font::plain)
    {
        const auto face = (style & juce::Font::bold) != 0 ? typefaces::interSemiBold() : typefaces::interRegular();
        return juce::FontOptions (face).withHeight (height);
    }

    // Inter Bold, for the app name only.
    inline juce::FontOptions heavyFont (float height)
    {
        return juce::FontOptions (typefaces::interBold()).withHeight (height);
    }

    // JetBrains Mono: plain = Regular, bold = Medium.
    inline juce::FontOptions numericFont (float height, int style = juce::Font::plain)
    {
        const auto face = (style & juce::Font::bold) != 0 ? typefaces::monoMedium() : typefaces::monoRegular();
        return juce::FontOptions (face).withHeight (height);
    }

    // Small all-caps section captions ("MASTER", "OSCILLATOR", "BPM").
    inline juce::FontOptions captionFont (float height = 10.0f)
    {
        return font (height, juce::Font::bold).withKerningFactor (0.10f);
    }
}
