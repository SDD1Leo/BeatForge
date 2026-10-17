#pragma once

#include "beatforge/time/MusicalTime.h"

#include <cstdint>
#include <vector>

namespace beatforge::midi
{
    using NoteId = std::uint32_t;

    struct Note
    {
        NoteId id = 0;             // unique within a clip; assigned by the Session
        int pitch = 60;            // MIDI note number 0..127 (60 = C4)
        int velocity = 100;        // 1..127
        time::Tick start = 0;      // from the clip start
        time::Tick length = 240;   // >= 1

        time::Tick end() const noexcept { return start + length; }
        bool operator== (const Note&) const = default;
    };

    // A looping MIDI clip that starts at bar 1. Plain data.
    struct Clip
    {
        time::Tick length = 4 * 3840;   // 4 bars of 4/4
        std::vector<Note> notes;

        bool operator== (const Clip&) const = default;
    };

    constexpr int minPitch = 0, maxPitch = 127;
    constexpr int minVelocity = 1, maxVelocity = 127;

    // Grid values the editor offers, in ticks (960 PPQ).
    constexpr time::Tick quarter = 960, eighth = 480, sixteenth = 240, thirtySecond = 120;

    bool isValidGrid (time::Tick grid) noexcept;

    // Clamps pitch/velocity into range and keeps the note inside [0, clipLength).
    Note constrained (Note, time::Tick clipLength) noexcept;

    // Moves each note's start to the nearest grid line (ties go later) and keeps
    // its length; notes stay inside the clip. Notes not listed in `ids` are
    // unchanged; an empty `ids` quantizes every note.
    void quantize (std::vector<Note>& notes, time::Tick grid, time::Tick clipLength,
                   const std::vector<NoteId>& ids = {});

    double pitchToFrequency (int pitch) noexcept;   // equal temperament, A4 = 440 Hz
}
