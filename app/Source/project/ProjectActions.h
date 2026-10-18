#pragma once

#include <beatforge/project/Session.h>

#include <juce_gui_extra/juce_gui_extra.h>

#include <functional>
#include <memory>

namespace beatforge
{
    // File-level project commands (New, Open, Save, Save As, Recent, Rename,
    // Export MIDI)
    // with their dialogs. All project changes still go through the Session;
    // this class only adds files, prompts and error reporting.
    class ProjectActions
    {
    public:
        ProjectActions (project::Session&, juce::PropertiesFile& settings, juce::Component& dialogParent);

        // In headless/smoke mode results are printed instead of shown in dialogs.
        void setReportToConsole (bool shouldReport) noexcept { reportToConsole = shouldReport; }

        void newProject();
        void openWithChooser();
        void openFile (const juce::File&);               // asks to save unsaved changes first
        void save (std::function<void (bool saved)> done = {});
        void saveAs (std::function<void (bool saved)> done = {});
        void renameProject();
        void exportMidiWithChooser();
        bool exportMidiTo (const juce::File&);

        // Runs `proceed` once unsaved changes are saved or discarded; never if
        // the user cancels.
        void confirmDiscardChanges (std::function<void()> proceed);

        void showMenu (juce::Component& anchor);

        juce::File defaultFolder() const;
        static juce::String appVersion();

    private:
        void loadFile (const juce::File&);
        void saveTo (const juce::File&, std::function<void (bool)> done);
        void showError (const juce::String& title, const juce::String& message);
        void showInfo (const juce::String& title, const juce::String& message);
        void rememberRecent (const juce::File&);
        void restoreFocus();   // dialogs leave no component focused; give it back

        project::Session& session;
        juce::PropertiesFile& settings;
        juce::Component& parent;
        juce::RecentlyOpenedFilesList recent;
        std::unique_ptr<juce::FileChooser> chooser;
        bool reportToConsole = false;
    };
}
