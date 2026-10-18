#include "ui/ShortcutsDialog.h"
#include "ui/Theme.h"

namespace beatforge
{
    namespace
    {
        struct Group { const char* title; std::initializer_list<std::pair<const char*, const char*>> keys; };

        const Group left[] {
            { "PLAYBACK", { { "Space", "Play / Stop" }, { "R  or  Ctrl+R", "Record into the selected synth track" },
                            { "L", "Loop on / off" }, { "M", "Metronome on / off" }, { "K", "Computer keyboard as MIDI keyboard" } } },
            { "PROJECT", { { "Ctrl+N", "New project" }, { "Ctrl+O", "Open project (browser)" }, { "Ctrl+S", "Save" },
                           { "Ctrl+Shift+S", "Save As" }, { "Ctrl+Shift+E", "Export audio (WAV)" }, { "Ctrl+E", "Export MIDI" },
                           { "Ctrl+Z", "Undo" }, { "Ctrl+Shift+Z  or  Ctrl+Y", "Redo" }, { "Ctrl+Q", "Quit" } } },
            { "VIEW", { { "Ctrl+1", "Step Sequencer / Piano Roll" }, { "Ctrl+2", "Instrument" }, { "Ctrl+3", "Mixer" },
                        { "Ctrl+=  /  Ctrl+-", "Zoom the arrangement in / out" }, { "Ctrl+wheel", "Zoom at the pointer" },
                        { "Shift+wheel", "Scroll the arrangement" }, { "F1", "This overview" } } },
        };

        const Group right[] {
            { "ARRANGEMENT", { { "Double-click lane", "Create a clip" }, { "Drag clip / its edge", "Move / resize (snaps to beats)" },
                               { "Ctrl+D", "Duplicate clip" }, { "Ctrl+C / Ctrl+X", "Copy / cut clip" },
                               { "Ctrl+V", "Paste on the selected track at the playhead" }, { "Delete", "Delete clip" },
                               { "Drag track header", "Reorder tracks" } } },
            { "PIANO ROLL", { { "Click / drag", "Draw / select notes" }, { "Delete", "Delete selected notes" },
                              { "Ctrl+A", "Select all" }, { "Up / Down", "Transpose (Shift = octave)" },
                              { "Left / Right", "Nudge by the grid" }, { "Q", "Quantize" } } },
            { "COMPUTER KEYBOARD (K)", { { "Q W E R T Y U I O P [ ]", "White keys from C4" },
                                         { "2 3 5 6 7 9 0 =", "Black keys" }, { "Z / X", "Octave down / up" },
                                         { "C / V", "Velocity down / up" } } },
        };

        void paintColumn (juce::Graphics& g, juce::Rectangle<int> area, const Group* groups, size_t count)
        {
            for (size_t i = 0; i < count; ++i)
            {
                g.setColour (theme::textSecondary);
                g.setFont (theme::captionFont());
                g.drawText (groups[i].title, area.removeFromTop (22), juce::Justification::centredLeft);
                for (auto [key, what] : groups[i].keys)
                {
                    auto row = area.removeFromTop (21);
                    g.setColour (theme::textPrimary);
                    g.setFont (theme::numericFont (12.0f));
                    g.drawText (key, row.removeFromLeft (182), juce::Justification::centredLeft, true);
                    g.setColour (theme::textSecondary);
                    g.setFont (theme::font (12.5f));
                    g.drawText (what, row, juce::Justification::centredLeft, true);
                }
                area.removeFromTop (12);
            }
        }
    }

    ShortcutsDialog::ShortcutsDialog() { setSize (900, 610); }

    void ShortcutsDialog::paint (juce::Graphics& g)
    {
        g.fillAll (theme::panel);
        auto area = getLocalBounds().reduced (24, 16);
        g.setColour (theme::textPrimary);
        g.setFont (theme::font (17.0f, juce::Font::bold));
        g.drawText ("Keyboard Shortcuts", area.removeFromTop (36), juce::Justification::centredLeft);
        area.removeFromTop (6);
        auto leftArea = area.removeFromLeft (area.getWidth() / 2).withTrimmedRight (16);
        paintColumn (g, leftArea, left, std::size (left));
        paintColumn (g, area.withTrimmedLeft (16), right, std::size (right));
    }
}
