#include "project/ProjectActions.h"

#include <beatforge/BuildInfo.h>
#include <beatforge/project/ProjectFile.h>

#include <iostream>

namespace beatforge
{
    namespace
    {
        constexpr const char* recentKey = "recentProjects";
        constexpr const char* lastFolderKey = "lastProjectFolder";
        constexpr int recentBase = 1000;   // menu ids for recent files

        // JUCE's own browser: themed by the NEBULA LookAndFeel and consistent
        // everywhere (zenity 4.0.x ignores the suggested save name).
        constexpr bool useNativeDialogs = false;

        juce::File withExtension (juce::File f)
        {
            return f.hasFileExtension (project::fileExtension) ? f : f.withFileExtension (project::fileExtension);
        }

        juce::String toJuce (const std::string& s) { return juce::String::fromUTF8 (s.c_str()); }
    }

    ProjectActions::ProjectActions (project::Session& s, juce::PropertiesFile& p, juce::Component& c)
        : session (s), settings (p), parent (c)
    {
        recent.setMaxNumberOfItems (8);
        recent.restoreFromString (settings.getValue (recentKey));
    }

    juce::String ProjectActions::appVersion()
    {
        const auto v = versionString();
        return juce::String (v.data(), v.size());
    }

    juce::File ProjectActions::defaultFolder() const
    {
        const auto last = juce::File (settings.getValue (lastFolderKey));
        if (last.isDirectory())
            return last;

        return juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("BeatForge");
    }

    //==============================================================================
    void ProjectActions::confirmDiscardChanges (std::function<void()> proceed)
    {
        if (! session.isDirty() || reportToConsole)
            return proceed();

        const auto name = toJuce (session.getProject().name);
        auto options = juce::MessageBoxOptions()
                           .withIconType (juce::MessageBoxIconType::QuestionIcon)
                           .withTitle ("Save changes?")
                           .withMessage ("\"" + name + "\" has unsaved changes. Save them before continuing?")
                           .withButton ("Save")
                           .withButton ("Don't Save")
                           .withButton ("Cancel")
                           .withAssociatedComponent (&parent);

        juce::AlertWindow::showAsync (options, [this, proceed] (int result)
        {
            restoreFocus();
            if (result == 1)        // Save
                save ([proceed] (bool saved) { if (saved) proceed(); });
            else if (result == 2)   // Don't Save
                proceed();
            // 0: Cancel - do nothing
        });
    }

    void ProjectActions::newProject()
    {
        confirmDiscardChanges ([this] { session.newProject(); });
    }

    void ProjectActions::openWithChooser()
    {
        confirmDiscardChanges ([this]
        {
            chooser = std::make_unique<juce::FileChooser> ("Open BeatForge project", defaultFolder(),
                                                           juce::String ("*") + project::fileExtension, useNativeDialogs);
            chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                                  [this] (const juce::FileChooser& fc)
            {
                restoreFocus();
                if (fc.getResult() != juce::File())
                    loadFile (fc.getResult());
            });
        });
    }

    void ProjectActions::openFile (const juce::File& file)
    {
        confirmDiscardChanges ([this, file] { loadFile (file); });
    }

    void ProjectActions::loadFile (const juce::File& file)
    {
        const auto result = project::loadFile (file.getFullPathName().toStdString());

        if (! result.ok())
        {
            showError ("Could not open project", file.getFileName() + "\n\n" + toJuce (result.error));
            return;
        }

        session.openProject (*result.project, file.getFullPathName().toStdString());
        rememberRecent (file);

        if (reportToConsole)
            std::cout << "SMOKE: project \"" << result.project->name << "\" loaded with "
                      << result.warnings.size() << " warning(s)" << std::endl;

        if (! result.warnings.empty())
        {
            juce::String list;
            for (auto& w : result.warnings)
                list << juce::String::fromUTF8 ("\xe2\x80\xa2 ") << toJuce (w) << "\n";

            showInfo ("Opened with corrections",
                      "Some values in " + file.getFileName() + " were missing or out of range and were corrected. "
                      "Nothing changes on disk until you save.\n\n" + list.trimEnd());
        }
    }

    //==============================================================================
    void ProjectActions::save (std::function<void (bool)> done)
    {
        if (const auto& file = session.getFile())
            saveTo (juce::File (juce::String (file->string())), std::move (done));
        else
            saveAs (std::move (done));
    }

    void ProjectActions::saveAs (std::function<void (bool)> done)
    {
        auto folder = defaultFolder();
        folder.createDirectory();

        const auto suggested = folder.getChildFile (juce::File::createLegalFileName (toJuce (session.getProject().name)))
                                   .withFileExtension (project::fileExtension);

        chooser = std::make_unique<juce::FileChooser> ("Save BeatForge project", suggested,
                                                       juce::String ("*") + project::fileExtension, useNativeDialogs);
        chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                                  | juce::FileBrowserComponent::warnAboutOverwriting,
                              [this, done] (const juce::FileChooser& fc)
        {
            restoreFocus();
            if (fc.getResult() == juce::File())
            {
                if (done) done (false);   // cancelled
                return;
            }

            saveTo (withExtension (fc.getResult()), done);
        });
    }

    void ProjectActions::saveTo (const juce::File& file, std::function<void (bool)> done)
    {
        // An untitled project takes its name from the file it is first saved as.
        if (session.getProject().name == project::Project {}.name)
            session.renameProject (file.getFileNameWithoutExtension().substring (0, project::Project::maxNameLength).toStdString());

        const auto result = project::saveFile (session.getProject(), file.getFullPathName().toStdString(),
                                               appVersion().toStdString());

        if (! result.ok())
        {
            showError ("Could not save project", toJuce (result.error));
            if (done) done (false);
            return;
        }

        session.markSaved (file.getFullPathName().toStdString());
        rememberRecent (file);

        if (reportToConsole)
            std::cout << "SMOKE: project saved to " << file.getFullPathName() << std::endl;

        if (done) done (true);
    }

    void ProjectActions::rememberRecent (const juce::File& file)
    {
        if (reportToConsole)
            return;   // smoke/test runs never touch the user's recent files

        recent.addFile (file);
        settings.setValue (recentKey, recent.toString());
        settings.setValue (lastFolderKey, file.getParentDirectory().getFullPathName());
        settings.saveIfNeeded();
    }

    //==============================================================================
    void ProjectActions::renameProject()
    {
        auto* window = new juce::AlertWindow ("Rename project", "Project name:", juce::MessageBoxIconType::NoIcon, &parent);
        window->addTextEditor ("name", toJuce (session.getProject().name));
        window->addButton ("Rename", 1, juce::KeyPress (juce::KeyPress::returnKey));
        window->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));

        window->enterModalState (true, juce::ModalCallbackFunction::create ([this, window] (int result)
        {
            restoreFocus();
            if (result == 1)
            {
                const auto name = window->getTextEditorContents ("name").trim();
                if (! session.renameProject (name.toStdString()))
                    showError ("Name not changed", "A project name needs 1 to 64 characters.");
            }
        }), true);
    }

    void ProjectActions::showMenu (juce::Component& anchor)
    {
        recent.removeNonExistentFiles();

        juce::PopupMenu recentMenu;
        recent.createPopupMenuItems (recentMenu, recentBase, false, true);

        auto item = [] (int id, const juce::String& text, const juce::String& shortcut)
        {
            juce::PopupMenu::Item i (text);
            i.itemID = id;
            i.shortcutKeyDescription = shortcut;
            return i;
        };

        juce::PopupMenu menu;
        menu.addSectionHeader ("Project");
        menu.addItem (item (1, "New", "Ctrl+N"));
        menu.addItem (item (2, "Open...", "Ctrl+O"));
        menu.addSubMenu ("Open Recent", recentMenu, recentMenu.getNumItems() > 0);
        menu.addSeparator();
        menu.addItem (item (3, "Save", "Ctrl+S"));
        menu.addItem (item (4, "Save As...", "Ctrl+Shift+S"));
        menu.addSeparator();
        menu.addItem (item (5, "Rename...", {}));
        menu.addSeparator();
        auto undoItem = item (6, "Undo", "Ctrl+Z");
        undoItem.isEnabled = session.canUndo();
        auto redoItem = item (7, "Redo", "Ctrl+Shift+Z");
        redoItem.isEnabled = session.canRedo();
        menu.addItem (undoItem);
        menu.addItem (redoItem);

        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&anchor).withMinimumWidth (anchor.getWidth()),
                             [this] (int id)
        {
            switch (id)
            {
                case 0: break;
                case 1: newProject(); break;
                case 2: openWithChooser(); break;
                case 3: save(); break;
                case 4: saveAs(); break;
                case 5: renameProject(); break;
                case 6: session.undo(); break;
                case 7: session.redo(); break;
                default:
                    if (id >= recentBase)
                        openFile (recent.getFile (id - recentBase));
            }
        });
    }

    //==============================================================================
    void ProjectActions::showError (const juce::String& title, const juce::String& message)
    {
        if (reportToConsole)
        {
            std::cout << "SMOKE: " << title << ": " << message.replace ("\n", " ") << std::endl;
            return;
        }

        juce::AlertWindow::showAsync (juce::MessageBoxOptions()
                                          .withIconType (juce::MessageBoxIconType::WarningIcon)
                                          .withTitle (title)
                                          .withMessage (message)
                                          .withButton ("OK")
                                          .withAssociatedComponent (&parent),
                                      [this] (int) { restoreFocus(); });
    }

    void ProjectActions::showInfo (const juce::String& title, const juce::String& message)
    {
        if (reportToConsole)
        {
            std::cout << "SMOKE: " << title << ": " << message.replace ("\n", " ") << std::endl;
            return;
        }

        juce::AlertWindow::showAsync (juce::MessageBoxOptions()
                                          .withIconType (juce::MessageBoxIconType::InfoIcon)
                                          .withTitle (title)
                                          .withMessage (message)
                                          .withButton ("OK")
                                          .withAssociatedComponent (&parent),
                                      [this] (int) { restoreFocus(); });
    }

    void ProjectActions::restoreFocus()
    {
        if (parent.isShowing())
            parent.grabKeyboardFocus();
    }
}
