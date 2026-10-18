#pragma once

#include <beatforge/audio/AudioEngine.h>
#include <beatforge/project/ScheduleBuilder.h>

#include <algorithm>
#include <cmath>
#include <vector>

// Test helpers: put content on engine track slots the same way the Session
// does (Track -> buildSchedule -> slot), and render.
namespace beatforge::test
{
    // A drum track with one clip from bar 1, `bars` long, playing `pattern`.
    inline void playPattern (audio::AudioEngine& e, int slot, const project::DrumPattern& pattern, int bars = 8)
    {
        project::Track t;
        t.id = (project::TrackId) slot + 1;
        t.type = project::TrackType::drums;
        project::Clip c;
        c.id = 1;
        c.start = 0;
        c.length = bars * project::patternTicks;
        c.pattern = pattern;
        t.clips.push_back (c);
        e.setSlotType (slot, audio::SlotType::drums);
        e.setSlotSchedule (slot, project::buildSchedule (t));
    }

    inline project::DrumPattern pattern (std::initializer_list<std::pair<int, std::initializer_list<int>>> lanes)
    {
        project::DrumPattern p {};
        for (auto& [lane, steps] : lanes)
            for (int s : steps)
                p[(size_t) lane][(size_t) s] = true;
        return p;
    }

    // An instrument track with one clip from bar 1 (ids 1..n). With
    // `loop`, the transport loops over the clip, as clips did before the arrangement.
    inline void playNotes (audio::AudioEngine& e, int slot, std::vector<midi::Note> notes, time::Tick clipLength = 3840, bool loop = true)
    {
        project::Track t;
        t.id = (project::TrackId) slot + 1;
        t.type = project::TrackType::instrument;
        project::Clip c;
        c.id = 1;
        c.length = clipLength;
        midi::NoteId id = 1;
        for (auto& n : notes) { if (n.id == 0) n.id = id; ++id; }
        c.notes = notes;
        t.clips.push_back (c);
        e.setSlotType (slot, audio::SlotType::instrument);
        e.setSlotSchedule (slot, project::buildSchedule (t));
        if (loop)
            e.setLoop (true, 0, clipLength);
    }

    inline std::vector<float> render (audio::AudioEngine& e, int numSamples, int blockSize = 512)
    {
        std::vector<float> l ((size_t) blockSize), r ((size_t) blockSize), out;
        float* ch[] { l.data(), r.data() };
        out.reserve ((size_t) numSamples);
        for (int done = 0; done < numSamples; done += blockSize)
        {
            const int n = std::min (blockSize, numSamples - done);
            e.process (ch, 2, n);
            out.insert (out.end(), l.begin(), l.begin() + n);
        }
        return out;
    }

    inline float peakIn (const std::vector<float>& x, size_t from = 0, size_t to = SIZE_MAX)
    {
        float p = 0.0f;
        for (size_t i = from; i < std::min (to, x.size()); ++i) p = std::max (p, std::abs (x[i]));
        return p;
    }
}
