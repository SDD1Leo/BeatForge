#include "ui/BeatForgeLookAndFeel.h"
#include "ui/Theme.h"

namespace beatforge
{
    BeatForgeLookAndFeel::BeatForgeLookAndFeel()
        : juce::LookAndFeel_V4 (juce::LookAndFeel_V4::ColourScheme {
              theme::background,   // windowBackground
              theme::panel,        // widgetBackground
              theme::panel,        // menuBackground
              theme::border,       // outline
              theme::textPrimary,  // defaultText
              theme::panelRaised,  // defaultFill
              juce::Colours::white,// highlightedText
              theme::accent,       // highlightedFill
              theme::textPrimary   // menuText
          })
    {
        setColour (juce::ResizableWindow::backgroundColourId, theme::background);
        setColour (juce::DocumentWindow::textColourId, theme::textPrimary);
    }
}
