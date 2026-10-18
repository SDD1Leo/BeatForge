#pragma once

#include "beatforge/audio/Instruments.h"
#include "beatforge/time/MusicalTime.h"

#include <cstdint>
#include <vector>

namespace beatforge::audio
{
    // Everything one track plays, flattened onto the song timeline (absolute
    // ticks), sorted by tick: note-offs, then drum hits, then note-ons.
    // Built on the message thread (project::buildSchedule); immutable once
    // published to the engine.
    struct TrackSchedule
    {
        enum class Kind : std::uint8_t { noteOff, drumHit, noteOn };

        struct Event
        {
            time::Tick tick = 0;
            Kind kind = Kind::noteOn;
            std::uint8_t laneMask = 0;   // drum hits: which lanes sound
            int pitch = 60, velocity = 100;
            VoiceId voice = 0;
        };

        struct Span   // where each note sounds, for "is this voice still in its note?"
        {
            VoiceId voice;
            time::Tick start, end;
        };

        std::vector<Event> events;
        std::vector<Span> spans;

        bool covers (VoiceId voice, double tick) const noexcept
        {
            for (auto& s : spans)
                if (s.voice == voice)
                    return tick >= (double) s.start && tick < (double) s.end;
            return false;
        }
    };
}
