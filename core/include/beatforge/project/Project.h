#pragma once

#include "beatforge/audio/DrumMachine.h"
#include "beatforge/midi/Note.h"
#include "beatforge/time/MusicalTime.h"
#include "beatforge/time/TempoMap.h"

#include <array>
#include <map>
#include <optional>
#include <string>

namespace beatforge::project
{
    // One drum-machine lane as stored in a project.
    struct DrumLane
    {
        std::string name;
        float gainDb = 0.0f;
        bool muted = false;
        bool soloed = false;
        std::array<bool, audio::DrumMachine::numSteps> steps {};
        std::string extra = "{}";   // unknown keys of this lane (JSON object)

        bool operator== (const DrumLane&) const = default;
    };

    // Everything a project file preserves. Plain data: no GUI, no engine.
    // Unknown fields found when loading are kept in the `extra*` JSON strings
    // and written back on save, so data from newer versions is not lost.
    struct Project
    {
        std::string name = "Untitled";
        double tempo = time::defaultTempo;
        time::TimeSignature timeSignature;
        bool metronomeEnabled = false;
        bool loopEnabled = false;
        int loopBars = 4;   // loop from bar 1; 1, 2, 4 or 8

        std::string drumTrackName = "Drums";
        std::array<DrumLane, audio::DrumMachine::numLanes> lanes = defaultLanes();

        std::string extraTopLevel = "{}";   // unknown top-level keys (JSON object)
        std::string extraDrumTrack = "{}";  // unknown keys of the drum track
        std::string extraLanes = "[]";      // lanes we do not understand, kept verbatim

        // Synth track with one looping MIDI clip (absent until created).
        std::string synthTrackName = "Synth";
        std::optional<midi::Clip> synthClip;
        std::string extraSynthTrack = "{}";            // unknown keys of the synth track
        std::string extraClip = "{}";                  // unknown keys of the clip
        std::map<midi::NoteId, std::string> noteExtra; // unknown keys per note
        std::string invalidNotes = "[]";               // unreadable notes, kept verbatim
        std::string extraTracks = "[]";     // tracks of types this version cannot play

        static std::array<DrumLane, audio::DrumMachine::numLanes> defaultLanes();
        static bool isValidLoopBars (int bars) noexcept { return bars == 1 || bars == 2 || bars == 4 || bars == 8; }
        static bool isValidClipBars (int bars) noexcept { return isValidLoopBars (bars); }

        static constexpr int maxNameLength = 64;
        static constexpr int maxLaneNameLength = 32;

        bool operator== (const Project&) const = default;
    };
}
