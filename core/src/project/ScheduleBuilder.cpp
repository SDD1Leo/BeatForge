#include "beatforge/project/ScheduleBuilder.h"

#include <algorithm>

namespace beatforge::project
{
    std::unique_ptr<audio::TrackSchedule> buildSchedule (const Track& track)
    {
        using Kind = audio::TrackSchedule::Kind;
        auto schedule = std::make_unique<audio::TrackSchedule>();
        auto clips = track.clips;
        std::stable_sort (clips.begin(), clips.end(), [] (const Clip& a, const Clip& b) { return a.start < b.start; });

        for (size_t c = 0; c < clips.size(); ++c)
        {
            const auto& clip = clips[c];
            auto end = clip.end();
            if (c + 1 < clips.size())
                end = std::min (end, clips[c + 1].start);   // the next clip takes over
            if (end <= clip.start)
                continue;

            if (track.type == TrackType::drums)
            {
                for (time::Tick t = clip.start, step = 0; t < end; t += patternTicks / stepsPerPattern, ++step)
                {
                    std::uint8_t mask = 0;
                    for (int lane = 0; lane < audio::DrumMachine::numLanes; ++lane)
                        if (clip.pattern[(size_t) lane][(size_t) (step % stepsPerPattern)])
                            mask |= (std::uint8_t) (1u << lane);
                    if (mask != 0)
                        schedule->events.push_back ({ t, Kind::drumHit, mask, 0, 0, 0 });
                }
            }
            else
            {
                for (auto n : clip.notes)
                {
                    if (n.start >= clip.length)
                        continue;   // beyond a shortened clip: kept, silent
                    n = midi::constrained (n, clip.length);
                    const auto start = clip.start + n.start;
                    const auto stop = std::min (clip.start + n.end(), end);
                    if (start >= stop)
                        continue;

                    const auto voice = voiceId (clip.id, n.id);
                    schedule->events.push_back ({ start, Kind::noteOn, 0, n.pitch, n.velocity, voice });
                    schedule->events.push_back ({ stop, Kind::noteOff, 0, n.pitch, 0, voice });
                    schedule->spans.push_back ({ voice, start, stop });
                }
            }
        }

        std::stable_sort (schedule->events.begin(), schedule->events.end(), [] (const audio::TrackSchedule::Event& a, const audio::TrackSchedule::Event& b)
        {
            if (a.tick != b.tick) return a.tick < b.tick;
            return (int) a.kind < (int) b.kind;
        });

        return schedule;
    }
}
