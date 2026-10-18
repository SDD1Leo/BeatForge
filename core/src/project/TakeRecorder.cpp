#include "beatforge/project/TakeRecorder.h"

#include <algorithm>
#include <cmath>

namespace beatforge::project
{
    bool TakeRecorder::begin (TrackId id)
    {
        if (active)
            finish();

        const auto* t = session.findTrack (id);
        if (t == nullptr || t->type != TrackType::instrument)
            return false;

        active = true;
        trackId = id;
        held.fill ({});
        lastTick = 0.0;
        takeClips.clear();
        lastClip = 0;
        notesRecorded = 0;
        session.beginGesture ("Record take");
        return true;
    }

    void TakeRecorder::add (const audio::AudioEngine::RecordedEvent& e)
    {
        if (! active)
            return;

        lastTick = std::max (lastTick, e.tick);

        if (e.pitch == audio::AudioEngine::RecordedEvent::endOfTake)
        {
            for (int pitch = 0; pitch < 128; ++pitch)
                if (held[(size_t) pitch].down)
                {
                    commit (pitch, held[(size_t) pitch].tick, e.tick, held[(size_t) pitch].velocity);
                    held[(size_t) pitch].down = false;
                }
            return finish();
        }

        if (e.pitch < 0 || e.pitch > 127)
            return;

        auto& h = held[(size_t) e.pitch];
        if (e.velocity > 0)
        {
            if (h.down)   // retriggered without a note off: end the first one here
                commit (e.pitch, h.tick, e.tick, h.velocity);
            h = { true, e.tick, e.velocity };
        }
        else if (h.down)
        {
            commit (e.pitch, h.tick, e.tick, h.velocity);
            h.down = false;
        }
    }

    void TakeRecorder::finish()
    {
        if (! active)
            return;

        for (int pitch = 0; pitch < 128; ++pitch)
            if (held[(size_t) pitch].down)
                commit (pitch, held[(size_t) pitch].tick, lastTick, held[(size_t) pitch].velocity);

        held.fill ({});
        active = false;
        session.endGesture();
    }

    //==============================================================================
    void TakeRecorder::commit (int pitch, double on, double off, int velocity)
    {
        const auto& p = session.getProject();
        auto start = (time::Tick) std::llround (on);
        auto end = (time::Tick) std::llround (off);

        // Held across a loop wrap: the note ran to the loop end.
        if (end < start && p.loopEnabled && start >= p.loopStart && start < p.loopEnd)
            end = p.loopEnd;
        end = std::max (end, start + minNoteLength);

        const auto clipId = clipFor (start, end);
        const auto* c = session.findClip (clipId);
        if (c == nullptr)
            return;

        midi::Note n;
        n.pitch = pitch;
        n.velocity = std::clamp (velocity, 1, 127);
        n.start = start - c->start;
        n.length = std::min (end, c->end()) - start;
        if (n.length < 1 || session.addNote (clipId, n) == 0)
            return;

        lastClip = clipId;
        ++notesRecorded;
    }

    ClipId TakeRecorder::clipFor (time::Tick start, time::Tick end)
    {
        const auto& p = session.getProject();
        const auto* t = session.findTrack (trackId);
        if (t == nullptr)
            return 0;

        const auto perBar = p.timeSignature.ticksPerBar();
        auto barsCovering = [&] (time::Tick from, time::Tick to) { return ((to - from + perBar - 1) / perBar) * perBar; };
        const bool inLoop = p.loopEnabled && start >= p.loopStart && start < p.loopEnd;

        // 1. A clip under the note (the later one wins, as in playback).
        for (auto it = t->clips.rbegin(); it != t->clips.rend(); ++it)
            if (it->start <= start && start < it->end())
            {
                // A clip of this take grows to fit the note (outside loops).
                const bool ours = std::find (takeClips.begin(), takeClips.end(), it->id) != takeClips.end();
                if (ours && ! inLoop && end > it->end())
                    session.resizeClip (it->id, std::min (barsCovering (it->start, end), Project::maxClipLength));
                return it->id;
            }

        // 2. Extend this take's clip when nothing else starts in between.
        if (! inLoop && ! takeClips.empty())
            if (const auto* last = session.findClip (takeClips.back()); last != nullptr && last->end() <= start)
            {
                const bool blocked = std::any_of (t->clips.begin(), t->clips.end(),
                                                  [&] (const Clip& c) { return c.start >= last->end() && c.start < end; });
                const auto length = barsCovering (last->start, end);
                if (! blocked && length <= Project::maxClipLength)
                {
                    session.resizeClip (last->id, length);
                    return last->id;
                }
            }

        // 3. A new clip: the loop region, or whole bars from the note's bar.
        const auto clipStart = inLoop ? p.loopStart : (start / perBar) * perBar;
        const auto length = inLoop ? p.loopEnd - p.loopStart : barsCovering (clipStart, end);
        const auto id = session.createClip (trackId, clipStart, std::min (length, Project::maxClipLength));
        if (id != 0)
            takeClips.push_back (id);
        return id;
    }
}
