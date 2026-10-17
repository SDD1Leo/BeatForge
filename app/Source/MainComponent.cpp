#include "MainComponent.h"
#include "ui/Theme.h"

#include <beatforge/BuildInfo.h>

namespace beatforge
{
    namespace
    {
        juce::String toJuce (std::string_view s) { return juce::String (s.data(), s.size()); }
    }

    MainComponent::MainComponent()
    {
        setSize (1280, 800);
    }

    void MainComponent::paint (juce::Graphics& g)
    {
        g.fillAll (theme::background);

        // Header: wordmark + version.
        g.setColour (theme::panel);
        g.fillRect (headerArea);
        g.setColour (theme::border);
        g.drawHorizontalLine (headerArea.getBottom() - 1, 0.0f, (float) getWidth());

        auto header = headerArea.reduced (16, 0);
        g.setColour (theme::accent);
        g.fillRoundedRectangle (header.removeFromLeft (10).withSizeKeepingCentre (10, 22).toFloat(), 2.0f);
        header.removeFromLeft (10);
        g.setColour (theme::textPrimary);
        g.setFont (juce::FontOptions (20.0f, juce::Font::bold));
        g.drawText ("BeatForge", header, juce::Justification::centredLeft);
        g.setColour (theme::textMuted);
        g.setFont (juce::FontOptions (13.0f));
        g.drawText ("v" + toJuce (versionString()), header, juce::Justification::centredRight);

        // Body: honest placeholder card — nothing here is interactive yet.
        auto card = bodyArea.withSizeKeepingCentre (juce::jmin (520, bodyArea.getWidth() - 32), 112).toFloat();
        g.setColour (theme::panel);
        g.fillRoundedRectangle (card, theme::cornerRadius);
        g.setColour (theme::border);
        g.drawRoundedRectangle (card, theme::cornerRadius, 1.0f);

        auto text = card.reduced (24.0f, 20.0f);
        g.setColour (theme::textPrimary);
        g.setFont (juce::FontOptions (18.0f, juce::Font::bold));
        g.drawText ("Foundation ready", text.removeFromTop (30.0f), juce::Justification::centredLeft);
        g.setColour (theme::textMuted);
        g.setFont (juce::FontOptions (14.0f));
        g.drawFittedText ("Phase 0: application shell, build system and tests.\n"
                          "Audio output and the transport bar arrive in Phase 1.",
                          text.toNearestInt(), juce::Justification::topLeft, 3);

        // Status bar.
        g.setColour (theme::panel);
        g.fillRect (statusArea);
        g.setColour (theme::border);
        g.drawHorizontalLine (statusArea.getY(), 0.0f, (float) getWidth());

        auto status = statusArea.reduced (16, 0);
        g.setColour (theme::ok);
        g.fillEllipse (status.removeFromLeft (8).withSizeKeepingCentre (8, 8).toFloat());
        status.removeFromLeft (8);
        g.setColour (theme::textMuted);
        g.setFont (juce::FontOptions (12.0f));
        const auto dot = juce::String::fromUTF8 ("  \xc2\xb7  ");
        g.drawText ("Ready" + dot + "JUCE " + toJuce (juceVersionString()) + dot + "Ctrl+Q to quit",
                    status, juce::Justification::centredLeft);
    }

    void MainComponent::resized()
    {
        auto area = getLocalBounds();
        headerArea = area.removeFromTop (48);
        statusArea = area.removeFromBottom (26);
        bodyArea = area;
    }
}
