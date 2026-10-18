#pragma once

#include <juce_graphics/juce_graphics.h>

// NEBULA design tokens: "Black is the canvas. Neon is the signal."
// Every component takes colours, radii and fonts from here (directly or via
// the LookAndFeel) so the visual direction can be tuned in one place.
namespace beatforge::theme
{
    // --- Surfaces --------------------------------------------------------------
    inline const juce::Colour background    { 0xff070b18 };   // application canvas
    inline const juce::Colour panel         { 0xff0d1426 };   // primary panels
    inline const juce::Colour panelRaised   { 0xff121c32 };   // headers, raised areas
    inline const juce::Colour control       { 0xff18233a };   // buttons, fields, empty steps
    inline const juce::Colour border        { 0xff253451 };
    inline const juce::Colour borderStrong  { 0xff34476b };   // hover / focus outlines

    // --- Text ------------------------------------------------------------------
    inline const juce::Colour textPrimary   { 0xffedf2ff };
    inline const juce::Colour textSecondary { 0xff9baac7 };   // labels, metadata
    inline const juce::Colour textMuted     { 0xff687997 };   // hints, inactive

    // --- Neon accents (reserve for meaningful state) ---------------------------
    inline const juce::Colour violet        { 0xff8b5cff };   // primary accent: active, selected
    inline const juce::Colour brightViolet  { 0xffb14cff };   // playhead
    inline const juce::Colour magenta       { 0xfff044c8 };
    inline const juce::Colour cyan          { 0xff20d9f2 };
    inline const juce::Colour electricBlue  { 0xff3987ff };
    inline const juce::Colour teal          { 0xff12d6b0 };
    inline const juce::Colour amber         { 0xffffad42 };
    inline const juce::Colour green         { 0xff3ddc84 };
    inline const juce::Colour red           { 0xffff5268 };

    // --- Semantic roles --------------------------------------------------------
    inline const juce::Colour accent        = violet;         // toggles that are on, playing
    inline const juce::Colour playhead      = brightViolet;
    inline const juce::Colour ok            = green;          // healthy status
    inline const juce::Colour error         = red;            // errors, clipping, (later) record
    inline const juce::Colour muteOn        = amber;
    inline const juce::Colour soloOn        = cyan;

    // Level meters: cyan body, violet then magenta near the top, red at clip.
    inline const juce::Colour meterLow      = cyan;
    inline const juce::Colour meterMid      = violet;
    inline const juce::Colour meterHigh     = magenta;

    // --- Track colours ---------------------------------------------------------
    // Fixed per musical role so a track looks the same in every view.
    namespace track
    {
        inline const juce::Colour kick      = magenta;
        inline const juce::Colour snare     = amber;
        inline const juce::Colour closedHat = cyan;
        inline const juce::Colour openHat   = teal;
        inline const juce::Colour clap      = violet;
        inline const juce::Colour synth     = electricBlue;   // synth / lead MIDI track
        // Planned roles (pads, vocals, FX) get colours when they exist.

        // Arrangement tracks: the first track of a type gets its role colour,
        // further ones step through a fixed palette (never random).
        inline juce::Colour forTrack (bool isDrums, int indexOfType)
        {
            static const juce::Colour drums[] { magenta, amber, teal, brightViolet };
            static const juce::Colour synths[] { electricBlue, cyan, green, violet };
            const auto i = (size_t) (indexOfType % 4);
            return isDrums ? drums[i] : synths[i];
        }
    }

    // --- Geometry ----------------------------------------------------------------
    constexpr float cornerRadius = 5.0f;    // panels
    constexpr float controlRadius = 4.0f;   // buttons, fields, steps
    constexpr float borderWidth = 1.0f;

    // --- Typography --------------------------------------------------------------
    // Ubuntu Sans ships with Ubuntu; JUCE falls back to its default if missing.
    inline const juce::String sansTypeface { "Ubuntu Sans" };
    inline const juce::String monoTypeface { "Ubuntu Sans Mono" };   // tabular values

    inline juce::FontOptions font (float height, int style = juce::Font::plain)
    {
        return juce::FontOptions (sansTypeface, height, style);
    }

    inline juce::FontOptions numericFont (float height, int style = juce::Font::plain)
    {
        return juce::FontOptions (monoTypeface, height, style);
    }

    // Small all-caps panel/section captions ("DRUMS", "MASTER", "BPM").
    inline juce::FontOptions captionFont (float height = 10.5f)
    {
        return font (height, juce::Font::bold).withKerningFactor (0.08f);
    }
}
