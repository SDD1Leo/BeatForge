#pragma once

#include <juce_graphics/juce_graphics.h>

// Shared palette for the BeatForge UI. Components read colours from here (or
// from the LookAndFeel colour IDs it feeds) instead of hard-coding them.
namespace beatforge::theme
{
    inline const juce::Colour background   { 0xff15171c };
    inline const juce::Colour panel        { 0xff1d2027 };
    inline const juce::Colour panelRaised  { 0xff252933 };
    inline const juce::Colour border       { 0xff2f3440 };
    inline const juce::Colour textPrimary  { 0xffe8eaf0 };
    inline const juce::Colour textMuted    { 0xff8a91a3 };
    inline const juce::Colour accent       { 0xffff7a2f }; // forge orange
    inline const juce::Colour ok           { 0xff4cd38a };
    inline const juce::Colour error        { 0xffff5566 };

    constexpr float cornerRadius = 6.0f;
}
