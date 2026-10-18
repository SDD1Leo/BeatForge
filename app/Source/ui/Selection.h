#pragma once

#include "ui/Theme.h"

#include <beatforge/project/Session.h>

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <vector>

namespace beatforge
{
    // What the editors work on: the selected track and (optionally) clip.
    // UI state only (not saved, not undoable). Repairs itself when the
    // session changes underneath it (deleted clip, undo, opened project).
    class Selection
    {
    public:
        explicit Selection (project::Session& s) : session (s)
        {
            listenerId = session.addListener ([this] (project::Session::Change) { validate(); });
            validate();
        }

        ~Selection() { session.removeListener (listenerId); }

        project::TrackId getTrack() const noexcept { return track; }
        project::ClipId getClip() const noexcept   { return clip; }

        void selectTrack (project::TrackId t)
        {
            if (t == track && session.trackOfClip (clip) == t)
                return;
            track = t;
            if (session.trackOfClip (clip) != t)
                clip = 0;
            changed();
        }

        void selectClip (project::ClipId c)
        {
            const auto t = session.trackOfClip (c);
            if (t == 0 || (c == clip && t == track))
                return;
            clip = c;
            track = t;
            changed();
        }

        // Track colour by type and position among tracks of that type.
        juce::Colour colourOf (project::TrackId id) const;

        int addListener (std::function<void()> l) { listeners.emplace_back (++nextId, std::move (l)); return nextId; }
        void removeListener (int id) { std::erase_if (listeners, [id] (auto& p) { return p.first == id; }); }

    private:
        void validate()
        {
            const auto oldTrack = track, oldClip = clip;
            if (session.findTrack (track) == nullptr)
                track = session.getProject().tracks.empty() ? 0 : session.getProject().tracks.front().id;
            if (session.findClip (clip) == nullptr || session.trackOfClip (clip) != track)
                clip = 0;
            if (clip == 0)
                if (auto* t = session.findTrack (track); t != nullptr && ! t->clips.empty())
                    clip = t->clips.front().id;
            if (oldTrack != track || oldClip != clip)
                changed();
        }

        void changed()
        {
            auto copy = listeners;
            for (auto& [id, l] : copy)
                l();
        }

        project::Session& session;
        int listenerId = 0;
        project::TrackId track = 0;
        project::ClipId clip = 0;
        std::vector<std::pair<int, std::function<void()>>> listeners;
        int nextId = 0;
    };

    inline juce::Colour Selection::colourOf (project::TrackId id) const
    {
        int index = 0;
        for (auto& t : session.getProject().tracks)
        {
            if (t.id == id)
                return theme::track::forTrack (t.type == project::TrackType::drums, index);
            if (session.findTrack (id) != nullptr && t.type == session.findTrack (id)->type)
                ++index;
        }
        return theme::textMuted;
    }
}
