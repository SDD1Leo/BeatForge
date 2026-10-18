#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <vector>

namespace beatforge
{
    // NEBULA project browser: the projects in the BeatForge folder plus recent
    // ones elsewhere, newest first, with tempo, length, tracks and date; a
    // search box; Open (or double-click / Enter), Browse... for other places.
    // Opening goes through ProjectActions (which asks about unsaved changes).
    class ProjectBrowser final : public juce::Component,
                                 private juce::ListBoxModel
    {
    public:
        ProjectBrowser (juce::File folder, const juce::Array<juce::File>& recent, juce::File current);

        std::function<void (const juce::File&)> onOpen;
        std::function<void()> onBrowse, onCancel;

        void paint (juce::Graphics&) override;
        void resized() override;
        bool keyPressed (const juce::KeyPress&) override;

        int getNumShown() const noexcept { return (int) shown.size(); }

    private:
        struct Entry
        {
            juce::File file;
            juce::String name, details;
            juce::Time modified;
            bool isCurrent = false;
            bool readable = true;
        };

        int getNumRows() override { return (int) shown.size(); }
        void paintListBoxItem (int row, juce::Graphics&, int width, int height, bool selected) override;
        void listBoxItemDoubleClicked (int row, const juce::MouseEvent&) override;
        void selectedRowsChanged (int) override;
        void returnKeyPressed (int row) override;

        void filter();
        void openSelected();

        juce::File folder;
        std::vector<Entry> entries;
        std::vector<size_t> shown;
        juce::TextEditor search;
        juce::ListBox list { "Projects", this };
        juce::TextButton openButton { "Open" }, browseButton { "Browse..." }, cancelButton { "Cancel" };
        juce::Rectangle<int> headerArea;
    };
}
