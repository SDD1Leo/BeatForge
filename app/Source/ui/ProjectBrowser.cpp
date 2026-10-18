#include "ui/ProjectBrowser.h"
#include "ui/Theme.h"

#include <beatforge/project/AudioExport.h>
#include <beatforge/project/ProjectFile.h>

#include <algorithm>

namespace beatforge
{
    namespace
    {
        juce::String when (juce::Time t)
        {
            const auto now = juce::Time::getCurrentTime();
            const auto days = (int) ((now.toMilliseconds() - t.toMilliseconds()) / (1000LL * 60 * 60 * 24));
            if (t.getDayOfYear() == now.getDayOfYear() && t.getYear() == now.getYear())
                return "Today " + t.formatted ("%H:%M");
            if (days < 7)
                return t.formatted ("%a %H:%M");
            return t.formatted ("%d %b %Y");
        }

        juce::String homeRelative (const juce::File& f)
        {
            const auto home = juce::File::getSpecialLocation (juce::File::userHomeDirectory).getFullPathName();
            const auto path = f.getFullPathName();
            return path.startsWith (home) ? "~" + path.substring (home.length()) : path;
        }
    }

    ProjectBrowser::ProjectBrowser (juce::File f, const juce::Array<juce::File>& recent, juce::File current)
        : folder (std::move (f))
    {
        juce::Array<juce::File> files;
        if (folder.isDirectory())
            folder.findChildFiles (files, juce::File::findFiles, true, juce::String ("*") + project::fileExtension);
        for (auto& r : recent)
            if (r.existsAsFile() && ! files.contains (r))
                files.add (r);

        for (auto& file : files)
        {
            if (entries.size() >= 500)
                break;
            Entry e;
            e.file = file;
            e.modified = file.getLastModificationTime();
            e.isCurrent = file == current;
            const auto loaded = project::loadFile (file.getFullPathName().toStdString());
            if (loaded.project)
            {
                const auto& p = *loaded.project;
                e.name = juce::String::fromUTF8 (p.name.c_str());
                auto trimmed = [] (double v) { return juce::String (v, 2).trimCharactersAtEnd ("0").trimCharactersAtEnd ("."); };
                const auto perBar = p.timeSignature.ticksPerBar(), end = project::songEnd (p);
                e.details = trimmed (p.tempo) + " BPM  " + juce::String::fromUTF8 ("\xc2\xb7") + "  "
                            + trimmed ((double) end / (double) perBar) + (end == perBar ? " bar  " : " bars  ")
                            + juce::String::fromUTF8 ("\xc2\xb7") + "  " + juce::String ((int) p.tracks.size()) + (p.tracks.size() == 1 ? " track" : " tracks");
            }
            else
            {
                e.name = file.getFileNameWithoutExtension();
                e.details = "Cannot be opened: " + juce::String::fromUTF8 (loaded.error.c_str());
                e.readable = false;
            }
            entries.push_back (std::move (e));
        }
        std::sort (entries.begin(), entries.end(), [] (const Entry& a, const Entry& b) { return a.modified > b.modified; });

        search.setTextToShowWhenEmpty ("Search projects", theme::textMuted);
        search.onTextChange = [this] { filter(); };
        search.onReturnKey = [this] { openSelected(); };
        search.onEscapeKey = [this] { if (onCancel) onCancel(); };
        addAndMakeVisible (search);

        list.setRowHeight (52);
        list.setColour (juce::ListBox::backgroundColourId, theme::background);
        addAndMakeVisible (list);

        openButton.setColour (juce::TextButton::buttonColourId, theme::violet);
        openButton.onClick = [this] { openSelected(); };
        browseButton.setTooltip ("Open a project from another folder");
        browseButton.onClick = [this] { if (onBrowse) onBrowse(); };
        cancelButton.onClick = [this] { if (onCancel) onCancel(); };
        for (auto* b : { &openButton, &browseButton, &cancelButton })
            addAndMakeVisible (*b);

        filter();
        setSize (640, 520);
    }

    void ProjectBrowser::filter()
    {
        shown.clear();
        const auto q = search.getText().trim();
        for (size_t i = 0; i < entries.size(); ++i)
            if (q.isEmpty() || entries[i].name.containsIgnoreCase (q) || entries[i].file.getFileName().containsIgnoreCase (q))
                shown.push_back (i);
        list.updateContent();
        list.selectRow (shown.empty() ? -1 : 0);
        selectedRowsChanged (list.getSelectedRow());
        repaint();
    }

    void ProjectBrowser::selectedRowsChanged (int row)
    {
        openButton.setEnabled (row >= 0 && row < (int) shown.size() && entries[shown[(size_t) row]].readable);
    }

    void ProjectBrowser::openSelected()
    {
        const auto row = list.getSelectedRow();
        if (row >= 0 && row < (int) shown.size() && entries[shown[(size_t) row]].readable && onOpen)
            onOpen (entries[shown[(size_t) row]].file);
    }

    void ProjectBrowser::listBoxItemDoubleClicked (int, const juce::MouseEvent&) { openSelected(); }
    void ProjectBrowser::returnKeyPressed (int) { openSelected(); }

    bool ProjectBrowser::keyPressed (const juce::KeyPress& key)
    {
        if (key == juce::KeyPress::escapeKey) { if (onCancel) onCancel(); return true; }
        return false;
    }

    void ProjectBrowser::paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool selected)
    {
        if (row < 0 || row >= (int) shown.size())
            return;
        const auto& e = entries[shown[(size_t) row]];
        auto area = juce::Rectangle<int> (0, 0, width, height).reduced (4, 2);
        g.setColour (selected ? theme::panelRaised.brighter (0.08f) : theme::panelRaised);
        g.fillRoundedRectangle (area.toFloat(), theme::controlRadius);
        if (selected)
        {
            g.setColour (theme::accent);
            g.drawRoundedRectangle (area.toFloat().reduced (0.5f), theme::controlRadius, 1.5f);
        }

        area.reduce (14, 6);
        auto top = area.removeFromTop (20);
        g.setColour (theme::textSecondary);
        g.setFont (theme::font (12.0f));
        g.drawText (when (e.modified), top.removeFromRight (120), juce::Justification::centredRight);
        g.setColour (e.readable ? theme::textPrimary : theme::textSecondary);
        g.setFont (theme::font (14.5f, juce::Font::bold));
        g.drawText (e.name + (e.isCurrent ? "   (open now)" : ""), top, juce::Justification::centredLeft, true);

        g.setFont (theme::font (12.0f));
        g.setColour (e.readable ? theme::textSecondary : theme::error);
        g.drawText (e.details + "    " + homeRelative (e.file), area, juce::Justification::centredLeft, true);
    }

    void ProjectBrowser::paint (juce::Graphics& g)
    {
        g.fillAll (theme::panel);
        g.setColour (theme::textPrimary);
        g.setFont (theme::font (17.0f, juce::Font::bold));
        g.drawText ("Open Project", headerArea, juce::Justification::centredLeft);
        g.setColour (theme::textSecondary);
        g.setFont (theme::font (12.0f));
        g.drawText (juce::String ((int) shown.size()) + " of " + juce::String ((int) entries.size()) + " projects in " + homeRelative (folder)
                        + (entries.empty() ? juce::String() : " and recent files"),
                    headerArea, juce::Justification::centredRight);

        if (shown.empty())
        {
            g.setColour (theme::textMuted);
            g.setFont (theme::font (14.0f));
            g.drawFittedText (entries.empty() ? "No projects yet in " + homeRelative (folder) + ".\nSave one with Ctrl+S, or use Browse... to open one from elsewhere."
                                              : juce::String ("No project matches the search."),
                              list.getBounds().reduced (24), juce::Justification::centred, 3);
        }
    }

    void ProjectBrowser::resized()
    {
        auto area = getLocalBounds().reduced (20, 0);
        headerArea = area.removeFromTop (54);
        search.setBounds (area.removeFromTop (32));
        area.removeFromTop (10);
        auto buttons = area.removeFromBottom (58).withSizeKeepingCentre (area.getWidth(), 34);
        list.setBounds (area);
        openButton.setBounds (buttons.removeFromRight (110));
        buttons.removeFromRight (10);
        cancelButton.setBounds (buttons.removeFromRight (100));
        browseButton.setBounds (buttons.removeFromLeft (120));
    }
}
