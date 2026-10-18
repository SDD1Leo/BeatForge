#include "project/Autosave.h"
#include "project/ProjectActions.h"

#include <beatforge/project/ProjectFile.h>

namespace beatforge
{
    namespace
    {
        constexpr const char* originalKey = "recoveryOriginalFile";
    }

    Autosave::Autosave (project::Session& s, juce::PropertiesFile& st)
        : session (s), settings (st),
          file (st.getFile().getSiblingFile ("recovery").getChildFile ("unsaved-work.beatforge"))
    {
        // Tests can shorten the interval.
        if (const auto env = juce::SystemStats::getEnvironmentVariable ("BEATFORGE_AUTOSAVE_SECONDS", {}); env.getIntValue() > 0)
            intervalSeconds = env.getIntValue();
    }

    Autosave::~Autosave()
    {
        session.removeListener (listenerId);
        discard();
    }

    void Autosave::start()
    {
        listenerId = session.addListener ([this] (project::Session::Change)
        {
            if (! session.isDirty())
            {
                dirtySince = {};
                discard();   // saved, opened or new: nothing to protect
            }
            else if (dirtySince == juce::Time())
            {
                dirtySince = juce::Time::getCurrentTime();
            }
        });
        startTimer (2000);
    }

    void Autosave::timerCallback()
    {
        if (! session.isDirty() || (written && *written == session.getProject()))
            return;
        const auto now = juce::Time::getCurrentTime();
        const auto since = lastWrite > dirtySince ? lastWrite : dirtySince;
        if ((now - since).inSeconds() >= intervalSeconds)
            write();
    }

    void Autosave::write()
    {
        file.getParentDirectory().createDirectory();
        const auto result = project::saveFile (session.getProject(), file.getFullPathName().toStdString(),
                                               ProjectActions::appVersion().toStdString());
        if (! result.ok())
            return;   // try again at the next tick; never bother the user about the safety copy
        written = session.getProject();
        lastWrite = juce::Time::getCurrentTime();
        settings.setValue (originalKey, session.getFile() ? juce::String (session.getFile()->string()) : juce::String());
        settings.saveIfNeeded();
        if (onAutosaved) onAutosaved();
    }

    void Autosave::discard()
    {
        if (file.existsAsFile())
            file.deleteFile();
        written.reset();
        lastWrite = {};
        if (settings.containsKey (originalKey))
        {
            settings.removeValue (originalKey);
            settings.saveIfNeeded();
        }
    }

    bool Autosave::isCurrentAutosaved() const
    {
        return session.isDirty() && written && *written == session.getProject();
    }

    std::optional<Autosave::Recovery> Autosave::pendingRecovery() const
    {
        if (! file.existsAsFile())
            return std::nullopt;
        auto loaded = project::loadFile (file.getFullPathName().toStdString());
        if (! loaded.project)
            return std::nullopt;
        Recovery r { std::move (*loaded.project), std::nullopt, file.getLastModificationTime() };
        const auto original = settings.getValue (originalKey);
        if (original.isNotEmpty())
            r.originalFile = original.toStdString();
        return r;
    }
}
